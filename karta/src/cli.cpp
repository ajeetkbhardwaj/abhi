#include "cli.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "agent.h"
#include "json.h"
#include "platform.h"
#include "prompt.h"
#include "provider.h"
#include "session.h"
#include "tools.h"

namespace karta {
namespace {

constexpr const char* kVersion = "0.1.0";

std::atomic<bool>* g_interrupt_target = nullptr;

void handle_sigint(int) {
  if (g_interrupt_target) g_interrupt_target->store(true);
}

bool matches_tool_pattern(const std::string& pattern, const std::string& name) {
  if (pattern == "*") return true;
  if (!pattern.empty() && pattern.back() == '*') {
    return name.rfind(pattern.substr(0, pattern.size() - 1), 0) == 0;
  }
  return pattern == name;
}

bool tool_allowed(const std::string& name, const CliOptions& options) {
  if (options.no_tools) return false;
  if (!options.tool_allow.empty()) {
    bool allowed = false;
    for (const std::string& pattern : options.tool_allow) {
      if (matches_tool_pattern(pattern, name)) allowed = true;
    }
    if (!allowed) return false;
  }
  for (const std::string& pattern : options.tool_exclude) {
    if (matches_tool_pattern(pattern, name)) return false;
  }
  return true;
}

std::string trim(const std::string& value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return "";
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string first_line(const std::string& text, std::size_t limit) {
  const std::size_t newline = text.find('\n');
  std::string line = newline == std::string::npos ? text : text.substr(0, newline);
  if (line.size() > limit) line = line.substr(0, limit) + "…";
  return line;
}

class Printer {
 public:
  Printer(const CliOptions& options, bool interactive)
      : options_(options), interactive_(interactive) {}

  void event(const AgentEvent& event) {
    if (options_.mode == "text") {
      text_event(event);
    } else {
      json_event(event);
    }
  }

  void banner(const std::string& model, const std::string& cwd) {
    if (options_.mode != "text" || !interactive_) return;
    std::cout << "karta " << kVersion << " · " << model << " · " << cwd << "\n";
    std::cout << "Ctrl+C cancels a turn · /exit quits · /session shows the session file\n";
  }

 private:
  void text_event(const AgentEvent& event) {
    if (event.type == "message_update") {
      std::cout << event.payload["delta"].as_string() << std::flush;
    } else if (event.type == "tool_execution_start" && interactive_) {
      std::cout << "\n\n● " << event.payload["toolName"].as_string() << " "
                << first_line(event.payload["arguments"].dump(), 120) << "\n";
      std::cout.flush();
    } else if (event.type == "tool_execution_end" && interactive_) {
      const std::string result = event.payload["result"].as_string();
      std::cout << "  " << (event.payload["isError"].as_bool(false) ? "✗ " : "✓ ")
                << first_line(result, 160) << "\n";
      std::cout.flush();
    } else if (event.type == "error") {
      std::cerr << "\nerror: " << event.payload["message"].as_string() << "\n";
    } else if (event.type == "compaction") {
      if (interactive_) std::cerr << "\n[compacted conversation: " << event.payload["tokensBefore"].as_int(0)
                                  << " tokens]\n";
    } else if (event.type == "compaction_failed") {
      std::cerr << "\n[compaction failed: " << event.payload["message"].as_string() << "]\n";
    }
  }

  void json_event(const AgentEvent& event) {
    Json out = Json::object();
    out.set("type", event.type);
    if (event.type == "message_start" || event.type == "message_end") {
      out.set("message", event.payload);
    } else if (event.type == "message_update") {
      out.set("delta", event.payload["delta"]);
    } else if (event.type == "turn_end") {
      out.set("message", event.payload["message"]);
      out.set("toolResults", event.payload["toolResults"]);
    } else if (event.type == "error" || event.type == "compaction_failed") {
      out.set("message", event.payload["message"]);
    } else if (event.payload.is_object()) {
      for (const auto& field : event.payload.fields()) out.set(field.first, field.second);
    }
    std::cout << out.dump() << "\n";
    std::cout.flush();
  }

  const CliOptions& options_;
  bool interactive_;
};

std::string version_string() {
  std::ostringstream out;
  out << "karta " << kVersion;
  return out.str();
}

}  // namespace

void print_help() {
  std::cout <<
      "karta " << kVersion << " — a coding agent (C++ port of pi)\n\n"
      "Usage: karta [options] [prompt]\n\n"
      "Options:\n"
      "  --provider <name>              Provider: anthropic, openai, openrouter, groq,\n"
      "                                 deepseek, xai, mistral, nvidia, ollama, lmstudio\n"
      "  --model <pattern>              Model id, or \"provider/model\"\n"
      "  --api-key <key>                API key (defaults to the provider's env var)\n"
      "  --system-prompt <text>         Replace the default system prompt\n"
      "  --append-system-prompt <text>  Append to the system prompt (repeatable)\n"
      "  --mode <mode>                  Output: text (default), json, or rpc\n"
      "  -p, --print                    Non-interactive: run the prompt and exit\n"
      "  -c, --continue                 Continue the most recent session for this directory\n"
      "  --session <path>               Open a session by file path\n"
      "  --session-id <id>              Use or create a session with this id\n"
      "  --fork <path>                  Fork a session into this directory\n"
      "  --session-dir <dir>            Session storage directory\n"
      "  --no-session                   Do not persist the session\n"
      "  -n, --name <name>              Session display name\n"
      "  -t, --tools <list>             Comma-separated tool allowlist (supports *)\n"
      "  -xt, --exclude-tools <list>    Comma-separated tool denylist\n"
      "  -nt, --no-tools                Disable all tools\n"
      "  -nbt, --no-builtin-tools       Disable built-in tools\n"
      "  --thinking <level>             off, minimal, low, medium, high, xhigh, max\n"
      "  --max-turns <n>                Stop after n model turns (0 = unlimited)\n"
      "  --compact-threshold <tokens>   Auto-compact above this estimate (0 = off)\n"
      "  --list-models                  Show providers and default models\n"
      "  --verbose                      Print session and model details\n"
      "  -v, --version                  Print the version\n"
      "  -h, --help                     Print this help\n\n"
      "Environment:\n"
      "  ANTHROPIC_API_KEY, OPENAI_API_KEY, OPENROUTER_API_KEY, GROQ_API_KEY,\n"
      "  DEEPSEEK_API_KEY, XAI_API_KEY, MISTRAL_API_KEY, or KARTA_API_KEY\n"
      "  KARTA_BASE_URL, KARTA_MODEL, KARTA_MAX_TOKENS, KARTA_THINKING\n";
}

CliOptions parse_args(int argc, char** argv) {
  CliOptions options;
  auto need_value = [&](int& index, const std::string& flag) -> std::string {
    if (index + 1 >= argc) {
      std::cerr << "error: " << flag << " requires a value\n";
      std::exit(2);
    }
    return argv[++index];
  };
  auto split_list = [](const std::string& value) {
    std::vector<std::string> items;
    std::string current;
    std::istringstream stream(value);
    while (std::getline(stream, current, ',')) {
      const std::string item = trim(current);
      if (!item.empty()) items.push_back(item);
    }
    return items;
  };

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else if (arg == "--version" || arg == "-v") {
      options.version = true;
    } else if (arg == "--list-models") {
      options.list_models = true;
    } else if (arg == "--mode") {
      options.mode = need_value(i, arg);
      if (options.mode != "text" && options.mode != "json" && options.mode != "rpc") {
        std::cerr << "error: --mode requires text, json, or rpc\n";
        std::exit(2);
      }
    } else if (arg == "--provider") {
      options.provider = need_value(i, arg);
      options.provider_set = true;
    } else if (arg == "--model") {
      options.model = need_value(i, arg);
      options.model_set = true;
    } else if (arg == "--api-key") {
      options.api_key = need_value(i, arg);
    } else if (arg == "--system-prompt") {
      options.system_prompt = need_value(i, arg);
    } else if (arg == "--append-system-prompt") {
      options.append_system_prompt.push_back(need_value(i, arg));
    } else if (arg == "--thinking") {
      options.thinking = need_value(i, arg);
    } else if (arg == "--print" || arg == "-p") {
      options.print = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') options.prompts.push_back(argv[++i]);
    } else if (arg == "--continue" || arg == "-c") {
      options.continue_session = true;
    } else if (arg == "--session") {
      options.session_path = need_value(i, arg);
    } else if (arg == "--session-id") {
      options.session_id = need_value(i, arg);
    } else if (arg == "--session-dir") {
      options.session_dir = need_value(i, arg);
    } else if (arg == "--fork") {
      options.fork_path = need_value(i, arg);
    } else if (arg == "--no-session") {
      options.no_session = true;
    } else if (arg == "--name" || arg == "-n") {
      options.session_name = need_value(i, arg);
    } else if (arg == "--tools" || arg == "-t") {
      for (const std::string& tool : split_list(need_value(i, arg))) options.tool_allow.push_back(tool);
    } else if (arg == "--exclude-tools" || arg == "-xt") {
      for (const std::string& tool : split_list(need_value(i, arg))) options.tool_exclude.push_back(tool);
    } else if (arg == "--no-tools" || arg == "-nt") {
      options.no_tools = true;
    } else if (arg == "--no-builtin-tools" || arg == "-nbt") {
      options.no_builtin_tools = true;
    } else if (arg == "--max-turns") {
      options.max_turns = std::atoi(need_value(i, arg).c_str());
    } else if (arg == "--compact-threshold") {
      options.compact_threshold = std::atoll(need_value(i, arg).c_str());
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
      std::cerr << "error: unknown option " << arg << "\n";
      std::exit(2);
    } else {
      options.prompts.push_back(arg);
    }
  }
  return options;
}

int run_cli(const CliOptions& options) {
  if (options.help) {
    print_help();
    return 0;
  }
  if (options.version) {
    std::cout << version_string() << "\n";
    return 0;
  }
  if (options.list_models) {
    std::cout << "Providers (use --provider with --model):\n";
    for (const std::string& provider : known_providers()) {
      const ModelSpec spec = resolve_model_spec(provider, "", "");
      std::cout << "  " << provider << " (default: " << spec.model << ")\n";
    }
    return 0;
  }

  const std::string cwd = current_working_directory();

  std::vector<ToolDefinition> tools;
  if (!options.no_tools && !options.no_builtin_tools) {
    for (ToolDefinition& tool : builtin_tools()) {
      if (tool_allowed(tool.name, options)) tools.push_back(std::move(tool));
    }
  }

  ModelSpec spec = resolve_model_spec(options.provider, options.model, options.api_key);
  spec.thinking = options.thinking;
  spec.max_tokens = 8192;

  const std::string directory = Session::directory_for_cwd(cwd, options.session_dir);

  std::unique_ptr<Session> session;
  if (!options.no_session) {
    std::string error;
    if (!options.session_path.empty()) {
      session = Session::load(options.session_path, &error);
      if (!session) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
    } else if (options.continue_session) {
      const std::string latest = Session::find_latest(cwd, directory);
      if (!latest.empty()) session = Session::load(latest, &error);
      if (!session) session = Session::create(cwd, directory, options.session_name, &error);
    } else if (!options.fork_path.empty()) {
      auto source = Session::load(options.fork_path, &error);
      if (!source) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
      session = Session::create(cwd, directory, options.session_name, &error, "", source->path());
      if (session) {
        for (const Json& message : source->record().messages) session->append_message(message);
      }
    } else if (!options.session_id.empty()) {
      const std::string existing = Session::find_by_id(options.session_id, directory);
      if (!existing.empty()) {
        session = Session::load(existing, &error);
      } else {
        session = Session::create(cwd, directory, options.session_name, &error, options.session_id);
      }
    } else {
      session = Session::create(cwd, directory, options.session_name, &error);
    }
    if (!session) {
      std::cerr << "warning: session disabled: " << error << "\n";
    }
  }

  // Resume the model recorded in the session unless the user chose one.
  if (session && !options.model_set && !session->record().model.empty()) {
    const std::string model = session->record().model;
    const std::string provider =
        options.provider_set ? options.provider : session->record().provider;
    spec = resolve_model_spec(provider, model, options.api_key);
    spec.thinking = options.thinking;
  }
  if (session && (session->record().provider != spec.provider || session->record().model != spec.model)) {
    session->append_model_change(spec.provider, spec.model);
  }

  std::string system_prompt =
      build_system_prompt(tools, cwd, options.system_prompt, options.append_system_prompt);

  const bool interactive = !options.print && options.prompts.empty() && options.mode == "text";
  Printer printer(options, interactive);
  const std::string model_label = spec.model.rfind(spec.provider + "/", 0) == 0
                                      ? spec.model
                                      : spec.provider + "/" + spec.model;
  printer.banner(model_label, cwd);
  if (options.verbose) {
    std::cerr << "karta: model=" << spec.provider << "/" << spec.model
              << " tools=" << tools.size() << " session="
              << (session ? session->path() : std::string("(ephemeral)")) << "\n";
  }
  if (spec.api_key.empty() && spec.provider != "ollama" && spec.provider != "lmstudio") {
    std::cerr << "warning: no API key found for " << spec.provider
              << "; set the provider's env var or pass --api-key\n";
  }

  AgentOptions agent_options;
  agent_options.model = spec;
  agent_options.system_prompt = std::move(system_prompt);
  agent_options.tools = tools;
  agent_options.cwd = cwd;
  agent_options.max_turns = options.max_turns;
  agent_options.compact_threshold = options.compact_threshold;

  Agent agent(
      agent_options, [&](const AgentEvent& event) {
        if (event.type == "message_end") {
          if (session) session->append_message(event.payload);
        } else if (event.type == "compaction") {
          if (session) {
            session->append_compaction(event.payload["summary"].as_string(),
                                       event.payload["tokensBefore"].as_int(0));
          }
        }
        printer.event(event);
      });

  if (session) {
    agent.set_messages(session->record().messages);
  }

  g_interrupt_target = agent.interrupt_flag();
  std::signal(SIGINT, handle_sigint);

  int status = 0;

  if (options.mode == "rpc") {
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::string> prompts;
    bool eof = false;

    std::thread reader([&]() {
      std::string line;
      while (std::getline(std::cin, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        std::string parse_error;
        const Json command = Json::parse(trimmed, &parse_error);
        if (!parse_error.empty()) continue;
        const std::string type = command["type"].as_string();
        const std::string text = command["text"].as_string();
        if (type == "prompt") {
          std::lock_guard<std::mutex> lock(mutex);
          prompts.push_back(text);
          condition.notify_one();
        } else if (type == "steer") {
          agent.queue_steering(text);
        } else if (type == "followup" || type == "follow_up") {
          agent.queue_followup(text);
        } else if (type == "abort" || type == "interrupt") {
          agent.interrupt();
        }
      }
      std::lock_guard<std::mutex> lock(mutex);
      eof = true;
      condition.notify_one();
    });

    while (true) {
      std::string prompt;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return !prompts.empty() || eof; });
        if (prompts.empty() && eof) break;
        prompt = prompts.front();
        prompts.pop_front();
      }
      agent.reset_interrupt();
      agent.prompt(prompt);
    }
    reader.join();
  } else if (!options.prompts.empty()) {
    for (const std::string& prompt : options.prompts) {
      agent.reset_interrupt();
      agent.prompt(prompt);
      if (options.mode == "text") std::cout << "\n";
    }
  } else {
    std::string line;
    while (true) {
      std::cout << "\n› " << std::flush;
      if (!std::getline(std::cin, line)) break;
      const std::string trimmed = trim(line);
      if (trimmed.empty()) continue;
      if (trimmed == "/exit" || trimmed == "/quit") break;
      if (trimmed == "/help") {
        std::cout << "/exit quit · /session show session file · /model show model\n";
        continue;
      }
      if (trimmed == "/session") {
        std::cout << (session ? session->path() : std::string("(ephemeral session)")) << "\n";
        continue;
      }
      if (trimmed == "/model") {
        std::cout << spec.provider << "/" << spec.model << "\n";
        continue;
      }
      agent.reset_interrupt();
      agent.prompt(trimmed);
      if (agent.interrupted()) std::cerr << "\n[interrupted]\n";
    }
  }

  g_interrupt_target = nullptr;
  return status;
}

}  // namespace karta
