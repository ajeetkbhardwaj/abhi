#include "provider.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include "auth.h"
#include "http.h"

namespace karta {
namespace {

long long now_millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string lower(std::string value) {
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

std::string env_or_empty(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}

struct ProviderDefaults {
  const char* base_url;
  const char* env_key;
  bool anthropic;
};

const std::map<std::string, ProviderDefaults>& providers() {
  static const std::map<std::string, ProviderDefaults> table = {
      {"anthropic", {"https://api.anthropic.com", "ANTHROPIC_API_KEY", true}},
      {"openai", {"https://api.openai.com/v1", "OPENAI_API_KEY", false}},
      {"openrouter", {"https://openrouter.ai/api/v1", "OPENROUTER_API_KEY", false}},
      {"groq", {"https://api.groq.com/openai/v1", "GROQ_API_KEY", false}},
      {"deepseek", {"https://api.deepseek.com/v1", "DEEPSEEK_API_KEY", false}},
      {"xai", {"https://api.x.ai/v1", "XAI_API_KEY", false}},
      {"mistral", {"https://api.mistral.ai/v1", "MISTRAL_API_KEY", false}},
      {"nvidia", {"https://integrate.api.nvidia.com/v1", "NVIDIA_API_KEY", false}},
      {"ollama", {"http://localhost:11434/v1", "", false}},
      {"lmstudio", {"http://localhost:1234/v1", "", false}},
  };
  return table;
}

Json text_block(const std::string& text) {
  Json block = Json::object();
  block.set("type", "text");
  block.set("text", text);
  return block;
}

std::string join_text(const Json& content_array) {
  std::string out;
  for (const Json& block : content_array.items()) {
    if (block["type"].as_string() == "text") out += block["text"].as_string();
  }
  return out;
}

// ---------------------------------------------------------------- OpenAI wire

Json to_openai_messages(const std::vector<Json>& messages) {
  Json out = Json::array();
  for (const Json& message : messages) {
    const std::string role = message["role"].as_string();
    if (role == "user") {
      Json wire = Json::object();
      wire.set("role", "user");
      wire.set("content", message_text(message));
      out.push_back(wire);
    } else if (role == "assistant") {
      Json wire = Json::object();
      wire.set("role", "assistant");
      const std::string text = join_text(message["content"]);
      Json calls = Json::array();
      for (const Json& block : message["content"].items()) {
        if (block["type"].as_string() != "toolCall") continue;
        Json call = Json::object();
        call.set("id", block["id"].as_string());
        call.set("type", "function");
        Json function = Json::object();
        function.set("name", block["name"].as_string());
        function.set("arguments", block["arguments"].is_string() ? block["arguments"].as_string() : block["arguments"].dump());
        call.set("function", function);
        calls.push_back(call);
      }
      if (calls.size() > 0) {
        wire.set("content", text.empty() ? Json() : Json(text));
        wire.set("tool_calls", calls);
      } else {
        wire.set("content", text);
      }
      out.push_back(wire);
    } else if (role == "toolResult") {
      Json wire = Json::object();
      wire.set("role", "tool");
      wire.set("tool_call_id", message["toolCallId"].as_string());
      wire.set("content", join_text(message["content"]));
      out.push_back(wire);
    }
  }
  return out;
}

Json to_openai_tools(const std::vector<Json>& tools) {
  Json out = Json::array();
  for (const Json& tool : tools) {
    Json entry = Json::object();
    entry.set("type", "function");
    Json function = Json::object();
    function.set("name", tool["name"].as_string());
    function.set("description", tool["description"].as_string());
    function.set("parameters", tool["parameters"]);
    entry.set("function", function);
    out.push_back(entry);
  }
  return out;
}

// ------------------------------------------------------------- Anthropic wire

void append_anthropic_message(Json& messages, const std::string& role, const Json& content) {
  if (messages.items().size() > 0) {
    Json& last = messages[messages.items().size() - 1];
    if (last["role"].as_string() == role) {
      Json merged = last["content"];
      for (const Json& block : content.items()) merged.push_back(block);
      last.set("content", merged);
      return;
    }
  }
  Json wire = Json::object();
  wire.set("role", role);
  wire.set("content", content);
  messages.push_back(wire);
}

Json to_anthropic_messages(const std::vector<Json>& messages) {
  Json out = Json::array();
  for (const Json& message : messages) {
    const std::string role = message["role"].as_string();
    if (role == "user") {
      Json blocks = Json::array();
      blocks.push_back(text_block(message_text(message)));
      append_anthropic_message(out, "user", blocks);
    } else if (role == "assistant") {
      Json blocks = Json::array();
      const std::string text = join_text(message["content"]);
      if (!text.empty()) blocks.push_back(text_block(text));
      for (const Json& block : message["content"].items()) {
        if (block["type"].as_string() != "toolCall") continue;
        Json use = Json::object();
        use.set("type", "tool_use");
        use.set("id", block["id"].as_string());
        use.set("name", block["name"].as_string());
        std::string raw = block["arguments"].is_string() ? block["arguments"].as_string() : block["arguments"].dump();
        std::string parse_error;
        Json input = Json::parse(raw.empty() ? "{}" : raw, &parse_error);
        use.set("input", parse_error.empty() ? input : Json::object());
        blocks.push_back(use);
      }
      append_anthropic_message(out, "assistant", blocks);
    } else if (role == "toolResult") {
      Json blocks = Json::array();
      Json result = Json::object();
      result.set("type", "tool_result");
      result.set("tool_use_id", message["toolCallId"].as_string());
      result.set("content", join_text(message["content"]));
      result.set("is_error", message["isError"].as_bool(false));
      blocks.push_back(result);
      append_anthropic_message(out, "user", blocks);
    }
  }
  return out;
}

Json to_anthropic_tools(const std::vector<Json>& tools) {
  Json out = Json::array();
  for (const Json& tool : tools) {
    Json entry = Json::object();
    entry.set("name", tool["name"].as_string());
    entry.set("description", tool["description"].as_string());
    entry.set("input_schema", tool["parameters"]);
    out.push_back(entry);
  }
  return out;
}

// ------------------------------------------------------------------- SSE

struct SseParser {
  std::string buffer;
  std::function<void(const std::string& event, const std::string& data)> on_event;

  void feed(const char* data, std::size_t length) {
    buffer.append(data, length);
    std::size_t start = 0;
    while (true) {
      const std::size_t end = buffer.find("\n\n", start);
      if (end == std::string::npos) break;
      const std::string block = buffer.substr(start, end - start);
      start = end + 2;
      dispatch(block);
    }
    buffer.erase(0, start);
  }

  void dispatch(const std::string& block) {
    std::string event_name = "message";
    std::string data;
    std::istringstream stream(block);
    std::string line;
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("event:", 0) == 0) {
        event_name = line.substr(6);
        while (!event_name.empty() && event_name.front() == ' ') event_name.erase(event_name.begin());
      } else if (line.rfind("data:", 0) == 0) {
        std::string value = line.substr(5);
        if (!value.empty() && value.front() == ' ') value.erase(value.begin());
        if (!data.empty()) data.push_back('\n');
        data += value;
      }
    }
    if (!data.empty() && on_event) on_event(event_name, data);
  }
};

std::string get_api_key_from_env(const std::string& provider) {
  const auto it = providers().find(provider);
  if (it != providers().end() && it->second.env_key[0] != '\0') {
    const std::string value = env_or_empty(it->second.env_key);
    if (!value.empty()) return value;
  }
  // Keys saved by the TUI `/login` command.
  const std::string stored = stored_api_key(provider);
  if (!stored.empty()) return stored;
  // Generic fallbacks, so one key can drive compatible gateways.
  for (const char* name : {"KARTA_API_KEY", "OPENAI_API_KEY", "ANTHROPIC_API_KEY"}) {
    const std::string value = env_or_empty(name);
    if (!value.empty()) return value;
  }
  return "";
}

}  // namespace

std::vector<std::string> known_providers() {
  std::vector<std::string> names;
  for (const auto& entry : providers()) names.push_back(entry.first);
  return names;
}

bool provider_is_anthropic(const std::string& provider) {
  const auto it = providers().find(provider);
  return it != providers().end() && it->second.anthropic;
}

bool provider_is_local(const std::string& provider) {
  return provider == "ollama" || provider == "lmstudio";
}

std::string provider_env_var(const std::string& provider) {
  const auto it = providers().find(provider);
  return it == providers().end() ? std::string() : std::string(it->second.env_key);
}

std::vector<std::string> fetch_models(const ModelSpec& spec, std::string* error,
                                      const std::atomic<bool>* cancel) {
  std::vector<std::string> models;
  if (spec.base_url.empty()) {
    if (error) *error = "provider has no base URL";
    return models;
  }

  const bool anthropic = provider_is_anthropic(spec.provider);
  std::vector<std::string> headers = {"Accept: application/json"};
  if (anthropic) {
    if (!spec.api_key.empty()) headers.push_back("x-api-key: " + spec.api_key);
    headers.push_back("anthropic-version: 2023-06-01");
  } else if (!spec.api_key.empty()) {
    headers.push_back("Authorization: Bearer " + spec.api_key);
  }

  const std::string url = spec.base_url + (anthropic ? "/v1/models" : "/models");
  const HttpResponse response = http_get(url, headers, cancel);
  if (!response.error.empty()) {
    if (error) *error = response.error;
    return models;
  }
  if (response.status < 200 || response.status >= 300) {
    const Json parsed = Json::parse(response.body);
    std::string message = parsed["error"]["message"].as_string();
    if (message.empty()) message = parsed["message"].as_string();
    if (error) *error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
    return models;
  }

  const Json parsed = Json::parse(response.body);
  auto collect = [&models](const Json& list) {
    for (const Json& item : list.items()) {
      std::string id = item["id"].as_string();
      if (id.empty()) id = item["name"].as_string();
      if (!id.empty()) models.push_back(id);
    }
  };
  collect(parsed["data"]);   // OpenAI / Anthropic / NVIDIA shape
  collect(parsed["models"]); // Ollama shape

  std::sort(models.begin(), models.end());
  models.erase(std::unique(models.begin(), models.end()), models.end());
  return models;
}

ModelSpec resolve_model_spec(const std::string& provider_arg, const std::string& model_arg,
                             const std::string& api_key_arg) {
  ModelSpec spec;
  spec.provider = provider_arg;
  spec.model = model_arg;

  // "provider/model" selects the provider unless one was given explicitly.
  const std::size_t slash = spec.model.find('/');
  if (spec.provider.empty() && slash != std::string::npos) {
    spec.provider = spec.model.substr(0, slash);
    spec.model = spec.model.substr(slash + 1);
  }
  if (spec.provider.empty()) {
    const std::string env_model = env_or_empty("KARTA_MODEL");
    if (!env_model.empty()) {
      const std::size_t pos = env_model.find('/');
      if (pos != std::string::npos) {
        spec.provider = env_model.substr(0, pos);
        spec.model = env_model.substr(pos + 1);
      } else {
        spec.provider = "openai";
        spec.model = env_model;
      }
    }
  }
  if (spec.provider.empty()) spec.provider = "anthropic";
  if (spec.model.empty()) {
    spec.model = spec.provider == "anthropic" ? "claude-sonnet-4-5"
                  : spec.provider == "openai"  ? "gpt-4o-mini"
                  : spec.provider == "nvidia"  ? "nvidia/nemotron-3.5-lightning-30b-a3b"
                  : spec.provider == "ollama"  ? "llama3.1"
                                               : "default";
  }

  const auto it = providers().find(spec.provider);
  if (it != providers().end()) {
    spec.base_url = env_or_empty("KARTA_BASE_URL");
    if (spec.base_url.empty()) spec.base_url = it->second.base_url;
    spec.api_key = api_key_arg.empty() ? get_api_key_from_env(spec.provider) : api_key_arg;
  } else {
    // Unknown provider id: treat it as an OpenAI-compatible endpoint.
    spec.base_url = env_or_empty("KARTA_BASE_URL");
    spec.api_key = api_key_arg.empty() ? get_api_key_from_env(spec.provider) : api_key_arg;
  }

  while (!spec.base_url.empty() && spec.base_url.back() == '/') spec.base_url.pop_back();
  const std::string env_max = env_or_empty("KARTA_MAX_TOKENS");
  if (!env_max.empty()) spec.max_tokens = std::atoi(env_max.c_str()) > 0 ? std::atoi(env_max.c_str()) : spec.max_tokens;
  const std::string env_thinking = env_or_empty("KARTA_THINKING");
  if (spec.thinking == "off" && !env_thinking.empty()) spec.thinking = env_thinking;
  return spec;
}

std::string message_text(const Json& message) {
  const std::string role = message["role"].as_string();
  if (role == "toolResult") return join_text(message["content"]);
  return join_text(message["content"]);
}

// ------------------------------------------------------------------- OpenAI

namespace {

AssistantTurn stream_openai(const ModelSpec& spec, const LlmRequest& request,
                            const TextCallback& on_text, const std::atomic<bool>* cancel,
                            std::string& raw_body) {
  AssistantTurn turn;

  Json body = Json::object();
  body.set("model", spec.model);
  body.set("stream", true);
  body.set("max_tokens", spec.max_tokens);
  body.set("messages", to_openai_messages(request.messages));
  if (!request.tools.empty()) {
    body.set("tools", to_openai_tools(request.tools));
    body.set("tool_choice", "auto");
  }
  const std::string provider = lower(spec.provider);
  if (provider == "openai" || provider == "openrouter" || provider == "groq" || provider == "deepseek") {
    Json options = Json::object();
    options.set("include_usage", true);
    body.set("stream_options", options);
  }
  if (spec.thinking != "off" && !spec.thinking.empty() && provider == "openai") {
    body.set("reasoning_effort", spec.thinking == "xhigh" || spec.thinking == "max" ? "high" : spec.thinking);
  }

  std::vector<std::string> headers = {"Content-Type: application/json",
                                      "Accept: text/event-stream"};
  if (!spec.api_key.empty()) headers.push_back("Authorization: Bearer " + spec.api_key);

  std::map<int, ToolCall> partial_calls;
  bool saw_done = false;
  bool stream_failed = false;
  std::string stream_error;

  SseParser parser;
  parser.on_event = [&](const std::string&, const std::string& data) {
    if (data == "[DONE]") {
      saw_done = true;
      return;
    }
    std::string parse_error;
    const Json chunk = Json::parse(data, &parse_error);
    if (!parse_error.empty()) return;
    if (chunk.contains("error")) {
      stream_failed = true;
      stream_error = chunk["error"]["message"].as_string();
      return;
    }
    if (chunk["usage"].is_object()) {
      turn.usage.input_tokens = chunk["usage"]["prompt_tokens"].as_int(0);
      turn.usage.output_tokens = chunk["usage"]["completion_tokens"].as_int(0);
      turn.usage.cache_read = chunk["usage"]["prompt_tokens_details"]["cached_tokens"].as_int(0);
    }
    const Json& choice = chunk["choices"][0];
    if (!choice.is_object()) return;
    const Json& delta = choice["delta"];
    if (delta["content"].is_string()) {
      const std::string piece = delta["content"].as_string();
      if (!piece.empty()) {
        turn.text += piece;
        if (on_text) on_text(piece);
      }
    }
    for (const Json& call : delta["tool_calls"].items()) {
      const int index = static_cast<int>(call["index"].as_int(0));
      ToolCall& slot = partial_calls[index];
      if (call["id"].is_string()) slot.id = call["id"].as_string();
      if (call["function"]["name"].is_string()) slot.name = call["function"]["name"].as_string();
      if (call["function"]["arguments"].is_string()) slot.arguments += call["function"]["arguments"].as_string();
    }
    if (choice["finish_reason"].is_string()) {
      const std::string reason = choice["finish_reason"].as_string();
      if (reason == "tool_calls") turn.stop_reason = "toolUse";
      else if (reason == "length") turn.stop_reason = "length";
      else turn.stop_reason = "stop";
    }
  };

  const std::string url = spec.base_url + "/chat/completions";
  const HttpResponse response = http_post_stream(
      url, headers, body.dump(),
      [&](const char* data, std::size_t length) {
        raw_body.append(data, length);
        if (raw_body.size() > 4 * 1024 * 1024) raw_body.erase(0, raw_body.size() - 4 * 1024 * 1024);
        parser.feed(data, length);
        return true;
      },
      cancel);

  if (!response.error.empty()) {
    turn.stop_reason = response.error == "aborted" ? "aborted" : "error";
    turn.error = response.error;
    return turn;
  }
  if (response.status < 200 || response.status >= 300) {
    turn.stop_reason = "error";
    Json parsed = Json::parse(raw_body);
    const std::string message = parsed["error"]["message"].as_string();
    turn.error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
    return turn;
  }
  if (stream_failed) {
    turn.stop_reason = "error";
    turn.error = stream_error;
    return turn;
  }
  if (!saw_done && turn.text.empty() && partial_calls.empty()) {
    turn.stop_reason = "error";
    turn.error = "empty response stream";
    return turn;
  }

  for (auto& entry : partial_calls) {
    ToolCall call = entry.second;
    if (call.name.empty()) continue;
    if (call.id.empty()) call.id = "call_" + std::to_string(entry.first);
    if (call.arguments.empty()) call.arguments = "{}";
    turn.tool_calls.push_back(call);
  }
  if (!turn.tool_calls.empty() && turn.stop_reason != "length") turn.stop_reason = "toolUse";
  return turn;
}

// ----------------------------------------------------------------- Anthropic

AssistantTurn stream_anthropic(const ModelSpec& spec, const LlmRequest& request,
                               const TextCallback& on_text, const std::atomic<bool>* cancel,
                               std::string& raw_body) {
  AssistantTurn turn;
  turn.stop_reason = "stop";

  Json body = Json::object();
  body.set("model", spec.model);
  body.set("max_tokens", spec.max_tokens);
  body.set("stream", true);
  if (!request.system.empty()) body.set("system", request.system);
  body.set("messages", to_anthropic_messages(request.messages));
  if (!request.tools.empty()) body.set("tools", to_anthropic_tools(request.tools));

  std::vector<std::string> headers = {"Content-Type: application/json",
                                      "Accept: text/event-stream",
                                      "anthropic-version: 2023-06-01"};
  if (!spec.api_key.empty()) headers.push_back("x-api-key: " + spec.api_key);

  std::map<int, ToolCall> blocks;
  bool stream_failed = false;
  std::string stream_error;

  SseParser parser;
  parser.on_event = [&](const std::string&, const std::string& data) {
    std::string parse_error;
    const Json event = Json::parse(data, &parse_error);
    if (!parse_error.empty()) return;
    const std::string type = event["type"].as_string();
    if (type == "message_start") {
      turn.usage.input_tokens = event["message"]["usage"]["input_tokens"].as_int(0);
      turn.usage.cache_read = event["message"]["usage"]["cache_read_input_tokens"].as_int(0);
      turn.usage.cache_write = event["message"]["usage"]["cache_creation_input_tokens"].as_int(0);
    } else if (type == "content_block_start") {
      const int index = static_cast<int>(event["index"].as_int(0));
      const Json& block = event["content_block"];
      if (block["type"].as_string() == "tool_use") {
        ToolCall call;
        call.id = block["id"].as_string();
        call.name = block["name"].as_string();
        blocks[index] = call;
      }
    } else if (type == "content_block_delta") {
      const int index = static_cast<int>(event["index"].as_int(0));
      const Json& delta = event["delta"];
      if (delta["type"].as_string() == "text_delta") {
        const std::string piece = delta["text"].as_string();
        if (!piece.empty()) {
          turn.text += piece;
          if (on_text) on_text(piece);
        }
      } else if (delta["type"].as_string() == "input_json_delta") {
        blocks[index].arguments += delta["partial_json"].as_string();
      }
    } else if (type == "message_delta") {
      const std::string reason = event["delta"]["stop_reason"].as_string();
      if (reason == "tool_use") turn.stop_reason = "toolUse";
      else if (reason == "max_tokens") turn.stop_reason = "length";
      else if (reason == "end_turn" || reason == "stop_sequence") turn.stop_reason = "stop";
      else if (!reason.empty()) turn.stop_reason = reason;
      if (event["usage"].is_object()) {
        turn.usage.output_tokens = event["usage"]["output_tokens"].as_int(turn.usage.output_tokens);
      }
    } else if (type == "error") {
      stream_failed = true;
      stream_error = event["error"]["message"].as_string();
    }
  };

  const std::string url = spec.base_url + "/v1/messages";
  const HttpResponse response = http_post_stream(
      url, headers, body.dump(),
      [&](const char* data, std::size_t length) {
        raw_body.append(data, length);
        if (raw_body.size() > 4 * 1024 * 1024) raw_body.erase(0, raw_body.size() - 4 * 1024 * 1024);
        parser.feed(data, length);
        return true;
      },
      cancel);

  if (!response.error.empty()) {
    turn.stop_reason = response.error == "aborted" ? "aborted" : "error";
    turn.error = response.error;
    return turn;
  }
  if (response.status < 200 || response.status >= 300) {
    turn.stop_reason = "error";
    Json parsed = Json::parse(raw_body);
    const std::string message = parsed["error"]["message"].as_string();
    turn.error = message.empty() ? ("HTTP " + std::to_string(response.status)) : message;
    return turn;
  }
  if (stream_failed) {
    turn.stop_reason = "error";
    turn.error = stream_error;
    return turn;
  }

  for (auto& entry : blocks) {
    ToolCall call = entry.second;
    if (call.name.empty()) continue;
    if (call.arguments.empty()) call.arguments = "{}";
    turn.tool_calls.push_back(call);
  }
  if (!turn.tool_calls.empty() && turn.stop_reason == "stop") turn.stop_reason = "toolUse";
  return turn;
}

}  // namespace

AssistantTurn stream_completion(const ModelSpec& spec, const LlmRequest& request,
                                const TextCallback& on_text, const std::atomic<bool>* cancel) {
  std::string raw_body;
  if (provider_is_anthropic(spec.provider)) {
    return stream_anthropic(spec, request, on_text, cancel, raw_body);
  }
  return stream_openai(spec, request, on_text, cancel, raw_body);
}

// ------------------------------------------------------------ Agent messages

Json make_user_message(const std::string& text) {
  Json message = Json::object();
  message.set("role", "user");
  Json content = Json::array();
  content.push_back(text_block(text));
  message.set("content", content);
  message.set("timestamp", now_millis());
  return message;
}

Json make_assistant_message(const AssistantTurn& turn) {
  Json message = Json::object();
  message.set("role", "assistant");
  Json content = Json::array();
  if (!turn.text.empty()) content.push_back(text_block(turn.text));
  for (const ToolCall& call : turn.tool_calls) {
    Json block = Json::object();
    block.set("type", "toolCall");
    block.set("id", call.id);
    block.set("name", call.name);
    block.set("arguments", call.arguments);
    content.push_back(block);
  }
  message.set("content", content);
  message.set("stopReason", turn.stop_reason);
  Json usage = Json::object();
  usage.set("input", turn.usage.input_tokens);
  usage.set("output", turn.usage.output_tokens);
  usage.set("cacheRead", turn.usage.cache_read);
  usage.set("cacheWrite", turn.usage.cache_write);
  message.set("usage", usage);
  message.set("timestamp", now_millis());
  if (!turn.error.empty()) message.set("errorMessage", turn.error);
  return message;
}

Json make_tool_result_message(const std::string& call_id, const std::string& tool_name,
                              const std::string& text, bool is_error) {
  Json message = Json::object();
  message.set("role", "toolResult");
  message.set("toolCallId", call_id);
  message.set("toolName", tool_name);
  Json content = Json::array();
  content.push_back(text_block(text));
  message.set("content", content);
  message.set("isError", is_error);
  message.set("timestamp", now_millis());
  return message;
}

}  // namespace karta
