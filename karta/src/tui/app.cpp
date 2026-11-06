#include "tui/app.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "auth.h"
#include "json.h"
#include "provider.h"
#include "session.h"
#include "tui/model-picker.h"
#include "tui/screen.h"

namespace karta {
namespace {

constexpr const char* kVersion = "0.1.0";
constexpr const char* kReadyStatus = "ready · /login · /model";

std::string trim(const std::string& value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return "";
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string lower(std::string value) {
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

bool all_digits(const std::string& value) {
  if (value.empty()) return false;
  for (char c : value) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

std::string first_line(const std::string& text, std::size_t limit) {
  const std::size_t newline = text.find('\n');
  std::string line = newline == std::string::npos ? text : text.substr(0, newline);
  if (line.size() > limit) line = line.substr(0, limit) + "…";
  return line;
}

std::string join(const std::vector<std::string>& items, const std::string& separator) {
  std::string out;
  for (const std::string& item : items) {
    if (!out.empty()) out += separator;
    out += item;
  }
  return out;
}

}  // namespace

class TuiApp {
 public:
  explicit TuiApp(TuiAppOptions options) : options_(std::move(options)), model_picker_(nullptr, nullptr) {}

  ~TuiApp() {
    if (agent_) agent_->interrupt();
    job_cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (job_thread_.joinable()) job_thread_.join();
    model_picker_.cancel_fetch();
    ui_.stop();
  }

  int run();

 private:
  enum class Phase { Chat, LoginProvider, LoginKey, ModelPick };
  enum class JobKind { None, FetchModels };

  void sync_header();
  void reset_prompt();
  void say(const std::string& text, tui::Style style);

  void handle_key(tui::Key key, int ch);
  void handle_submit(const std::string& line);
  void handle_command(const std::string& line);

  void start_prompt(const std::string& text);
  bool consume_events();
  void stop_worker();

  void start_job(JobKind kind, const ModelSpec& spec);
  bool consume_job();

  void begin_login(const std::string& provider);
  void finish_login_provider(const std::string& name);
  void finish_login_key(const std::string& key);

  void begin_model_pick();
  void show_models();
  void finish_model_pick(const std::string& selection);

  void apply_spec(const ModelSpec& spec);
  void apply_model(const std::string& model);
  ModelSpec spec_for(const std::string& provider, const std::string& model) const;

  TuiAppOptions options_;
  tui::Tui ui_;
  std::unique_ptr<Agent> agent_;
  tui::ModelPicker model_picker_;

  bool running_ = true;
  bool busy_ = false;
  Phase phase_ = Phase::Chat;

  std::thread worker_;
  std::atomic<bool> worker_done_{false};
  std::mutex queue_mutex_;
  std::deque<AgentEvent> queue_;

  JobKind job_kind_ = JobKind::None;
  std::thread job_thread_;
  std::atomic<bool> job_cancel_{false};
  std::atomic<bool> job_done_{false};
  std::atomic<bool> job_running_{false};
  std::mutex job_mutex_;
  std::string job_error_;
  std::vector<std::string> job_models_;

  std::string pending_provider_;
  std::vector<std::string> models_;
};

int TuiApp::run() {
  if (!ui_.init()) {
    std::cerr << "error: failed to initialize the terminal UI "
                 "(run outside a pipe, or use --mode text -p)\n";
    return 1;
  }

  agent_ = std::make_unique<Agent>(options_.agent, [this](const AgentEvent& event) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push_back(event);
  });
  if (options_.session) agent_->set_messages(options_.session->record().messages);

  sync_header();
  say("karta " + std::string(kVersion) + " — terminal UI", tui::Style::Accent);
  say("/login connect a provider · /model choose a model · /help commands · /exit quit",
      tui::Style::Dim);

  const ModelSpec& current = options_.agent.model;
  if (!provider_is_local(current.provider) && current.api_key.empty()) {
    say("Not connected. Type /login to add an API key for a provider.", tui::Style::Dim);
  } else {
    say("Connected to " + current.provider + " · model " + current.model, tui::Style::Dim);
  }
  ui_.set_status(kReadyStatus, false);

  while (running_) {
    bool changed = consume_events();
    changed = consume_job() || changed;

    int ch = 0;
    const tui::Key key = ui_.poll(&ch);
    if (key != tui::Key::None) {
      handle_key(key, ch);
      changed = true;
    }

    if (changed) {
      ui_.draw();
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  stop_worker();
  job_cancel_ = true;
  if (job_thread_.joinable()) job_thread_.join();
  ui_.stop();
  return 0;
}

void TuiApp::say(const std::string& text, tui::Style style) {
  ui_.append_text(text, style);
}

void TuiApp::sync_header() {
  tui::SessionInfo info;
  info.title = "karta";
  info.version = kVersion;
  info.cwd = options_.agent.cwd;
  info.provider = options_.agent.model.provider;
  info.model = options_.agent.model.model;
  if (options_.session) info.id = options_.session->id();
  ui_.set_header(info);
}

void TuiApp::reset_prompt() {
  phase_ = Phase::Chat;
  ui_.set_prompt("> ");
  ui_.set_input("");
  ui_.set_input_hidden(false);
  ui_.set_status(kReadyStatus, false);
}

void TuiApp::handle_key(tui::Key key, int /*ch*/) {
  switch (key) {
    case tui::Key::Enter: {
      const std::string line = ui_.input();
      ui_.set_input("");
      handle_submit(line);
      return;
    }
    case tui::Key::CtrlC:
    case tui::Key::Escape:
      if (busy_ && agent_) {
        agent_->interrupt();
        ui_.set_status("cancelling…", true);
      } else if (job_running_) {
        // A model fetch in flight: abort the HTTP transfer.
        job_cancel_ = true;
        ui_.set_status("cancelling…", true);
      } else if (phase_ != Phase::Chat) {
        reset_prompt();
        say("cancelled", tui::Style::Dim);
      } else if (key == tui::Key::CtrlC) {
        running_ = false;
      }
      return;
    default:
      return;
  }
}

void TuiApp::handle_submit(const std::string& line) {
  if (line.empty()) return;

  // Commands are recognized in every phase, so /exit always quits and a
  // mistyped slash can never be stored as a provider, key, or model id.
  if (line[0] == '/') {
    handle_command(line);
    return;
  }

  if (phase_ == Phase::LoginProvider) {
    finish_login_provider(line);
    return;
  }
  if (phase_ == Phase::LoginKey) {
    finish_login_key(line);
    return;
  }
  if (phase_ == Phase::ModelPick) {
    finish_model_pick(line);
    return;
  }
  if (job_running_) {
    say("still fetching models — Esc cancels", tui::Style::Dim);
    return;
  }
  start_prompt(line);
}

void TuiApp::handle_command(const std::string& line) {
  std::string body = line.substr(1);
  std::string name = body;
  std::string argument;
  const std::size_t space = body.find_first_of(" \t");
  if (space != std::string::npos) {
    name = body.substr(0, space);
    argument = trim(body.substr(space + 1));
  }
  name = lower(name);

  if (name == "exit" || name == "quit") {
    running_ = false;
    return;
  }
  if (name == "help") {
    say("Commands", tui::Style::Bold);
    say("  /login [provider]   connect a provider with an API key", tui::Style::Normal);
    say("  /logout [provider]  forget the saved key for a provider", tui::Style::Normal);
    say("  /model [id]         list and pick a model (or set one directly)", tui::Style::Normal);
    say("  /provider [name]    show or switch the provider", tui::Style::Normal);
    say("  /session            show the session file", tui::Style::Normal);
    say("  /clear              clear the transcript", tui::Style::Normal);
    say("  /exit               quit (Esc cancels a running turn)", tui::Style::Normal);
    return;
  }
  if (name == "login") {
    begin_login(argument);
    return;
  }
  if (name == "model" || name == "models") {
    if (!argument.empty()) {
      const ModelSpec& current = options_.agent.model;
      if (!provider_is_local(current.provider) && current.api_key.empty()) {
        say("not logged in to " + current.provider + " — use /login first", tui::Style::Error);
        return;
      }
      if (all_digits(argument)) {
        if (models_.empty()) {
          say("run /model first to load the model list", tui::Style::Dim);
          return;
        }
        finish_model_pick(argument);
        return;
      }
      apply_model(argument);
      return;
    }
    begin_model_pick();
    return;
  }
  if (name == "provider") {
    if (argument.empty()) {
      say("provider: " + options_.agent.model.provider, tui::Style::Normal);
    } else {
      begin_login(argument);
    }
    return;
  }
  if (name == "logout") {
    const std::string provider =
        argument.empty() ? options_.agent.model.provider : lower(trim(argument));
    if (provider_is_local(provider)) {
      say(provider + " does not need credentials", tui::Style::Dim);
      return;
    }
    const bool removed = remove_auth_key(provider);
    if (provider == options_.agent.model.provider) {
      ModelSpec spec = resolve_model_spec(provider, options_.agent.model.model, "");
      apply_spec(spec);
      if (!spec.api_key.empty()) {
        say("an environment variable still provides a key for " + provider, tui::Style::Dim);
      }
    }
    say(removed ? "logged out of " + provider + " — saved key removed"
                : "no saved key for " + provider,
        removed ? tui::Style::Success : tui::Style::Dim);
    return;
  }
  if (name == "session") {
    say("session: " + (options_.session ? options_.session->path() : std::string("(ephemeral)")),
        tui::Style::Normal);
    return;
  }
  if (name == "clear") {
    ui_.clear_transcript();
    return;
  }
  say("unknown command /" + name + " — /help for the list", tui::Style::Error);
}

ModelSpec TuiApp::spec_for(const std::string& provider, const std::string& model) const {
  return resolve_model_spec(provider, model, "");
}

void TuiApp::begin_login(const std::string& providerArg) {
  const std::string provider = trim(providerArg);
  if (!provider.empty()) {
    finish_login_provider(provider);
    return;
  }
  phase_ = Phase::LoginProvider;
  ui_.set_prompt("provider> ");
  ui_.set_input("");
  ui_.set_input_hidden(false);
  ui_.set_status("choose a provider", false);
  say("Known providers: " + join(known_providers(), ", "), tui::Style::Dim);
  say("Type a provider name and press Enter, or Esc to cancel.", tui::Style::Dim);
}

void TuiApp::finish_login_provider(const std::string& raw) {
  const std::string provider = lower(trim(raw));
  reset_prompt();
  if (provider.empty()) {
    say("login cancelled", tui::Style::Dim);
    return;
  }

  const std::vector<std::string> known = known_providers();
  if (std::find(known.begin(), known.end(), provider) == known.end()) {
    say("unknown provider: " + provider, tui::Style::Error);
    say("known providers: " + join(known, ", "), tui::Style::Dim);
    return;
  }

  pending_provider_ = provider;
  const ModelSpec spec = spec_for(provider, "");

  if (provider_is_local(provider)) {
    apply_spec(spec);
    say("connected to local provider " + provider + " (no API key needed)", tui::Style::Success);
    begin_model_pick();
    return;
  }

  if (!spec.api_key.empty()) {
    apply_spec(spec);
    say("using stored credentials for " + provider, tui::Style::Success);
    begin_model_pick();
    return;
  }

  const std::string env = provider_env_var(provider);
  phase_ = Phase::LoginKey;
  ui_.set_prompt("api key> ");
  ui_.set_input("");
  ui_.set_input_hidden(true);
  ui_.set_status("enter the API key for " + provider, false);
  say("Paste the " + (env.empty() ? std::string("API") : env) + " key for " + provider +
          " and press Enter. Esc cancels.",
      tui::Style::Dim);
}

void TuiApp::finish_login_key(const std::string& raw) {
  const std::string key = trim(raw);
  const std::string provider = pending_provider_;
  reset_prompt();
  if (key.empty()) {
    say("login cancelled", tui::Style::Dim);
    return;
  }

  // Store first: the key is the user's, we never spend it on a test request.
  ModelSpec spec = spec_for(provider, "");
  spec.api_key = key;
  apply_spec(spec);

  if (save_auth_key(provider, key)) {
    say("saved " + provider + " key to " + auth_file_path(), tui::Style::Success);
    say("reused on every launch until you run /logout", tui::Style::Dim);
  } else {
    say("warning: could not save the key to " + auth_file_path() +
            " — it stays active for this session only",
        tui::Style::Error);
  }

  begin_model_pick();
}

void TuiApp::begin_model_pick() {
  if (job_running_) {
    say("a request is already running", tui::Style::Dim);
    return;
  }
  const ModelSpec& current = options_.agent.model;
  if (!provider_is_local(current.provider) && current.api_key.empty()) {
    say("not logged in to " + current.provider + " — use /login first", tui::Style::Error);
    return;
  }
  reset_prompt();
  ui_.set_status("fetching models…", true);
  start_job(JobKind::FetchModels, current);
}

void TuiApp::show_models() {
  if (models_.empty()) {
    say("no models returned — type /model <id> to set one manually", tui::Style::Dim);
    reset_prompt();
    return;
  }

  const ModelSpec& current = options_.agent.model;
  say("Available models for " + current.provider + " (" + std::to_string(models_.size()) + ")",
      tui::Style::Bold);

  // Keep the whole list on screen above the prompt, like pi's select-list
  // window, so every number shown is a number the user can type.
  const std::size_t room = ui_.rows() > 12 ? ui_.rows() - 10 : 10;
  const std::size_t limit = std::min(models_.size(), room);
  for (std::size_t i = 0; i < limit; ++i) {
    const bool selected = models_[i] == current.model;
    std::string line = "  " + std::to_string(i + 1) + ") " + models_[i];
    if (selected) line += "   ← current";
    say(line, selected ? tui::Style::Success : tui::Style::Normal);
  }
  if (models_.size() > limit) {
    say("  … " + std::to_string(models_.size() - limit) + " more (type /model <id>)",
        tui::Style::Dim);
  }

  phase_ = Phase::ModelPick;
  ui_.set_prompt("model #> ");
  ui_.set_input("");
  ui_.set_status("select a model", false);
  say("Type a number and press Enter, or type a model id. Esc cancels.", tui::Style::Dim);
}

void TuiApp::finish_model_pick(const std::string& raw) {
  const std::string selection = trim(raw);
  const std::string provider = options_.agent.model.provider;
  const std::string existing_key = options_.agent.model.api_key;
  reset_prompt();
  if (selection.empty()) {
    say("model selection cancelled", tui::Style::Dim);
    return;
  }

  if (all_digits(selection)) {
    const long index = std::strtol(selection.c_str(), nullptr, 10);
    if (index < 1 || static_cast<std::size_t>(index) > models_.size()) {
      say("no model at position " + selection, tui::Style::Error);
      return;
    }
    apply_model(models_[static_cast<std::size_t>(index - 1)]);
    return;
  }

  ModelSpec spec = options_.agent.model;
  spec.provider = provider;
  spec.api_key = existing_key;
  spec.model = selection;
  apply_spec(spec);
}

void TuiApp::apply_spec(const ModelSpec& spec) {
  options_.agent.model = spec;
  if (agent_) agent_->set_model(spec);
  if (options_.session) options_.session->append_model_change(spec.provider, spec.model);
  sync_header();
}

void TuiApp::apply_model(const std::string& model) {
  if (model.empty()) return;
  ModelSpec spec = options_.agent.model;
  spec.model = model;
  apply_spec(spec);
  say("model → " + spec.model, tui::Style::Accent);
  ui_.set_status(kReadyStatus, false);
}

void TuiApp::start_prompt(const std::string& text) {
  if (busy_) {
    say("a turn is already running — press Esc to cancel", tui::Style::Dim);
    return;
  }
  if (!agent_) return;

  say("› " + text, tui::Style::User);
  ui_.set_status("thinking…", true);
  busy_ = true;
  worker_done_ = false;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
  }
  if (worker_.joinable()) worker_.join();

  worker_ = std::thread([this, text]() {
    agent_->reset_interrupt();
    agent_->prompt(text);
    worker_done_ = true;
  });
}

bool TuiApp::consume_events() {
  bool changed = false;
  std::deque<AgentEvent> events;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    events.swap(queue_);
  }

  for (const AgentEvent& event : events) {
    changed = true;
    if (event.type == "message_update") {
      const std::string delta = event.payload["delta"].as_string();
      if (!delta.empty()) ui_.stream_delta(delta);
    } else if (event.type == "message_end") {
      if (ui_.streaming()) ui_.stream_end();
      if (options_.session) options_.session->append_message(event.payload);
    } else if (event.type == "tool_execution_start") {
      if (ui_.streaming()) ui_.stream_end();
      const std::string name = event.payload["toolName"].as_string();
      const std::string args = event.payload["arguments"].dump();
      say("● " + name + " " + first_line(args, 100), tui::Style::Accent);
    } else if (event.type == "tool_execution_end") {
      const bool is_error = event.payload["isError"].as_bool(false);
      const std::string result = event.payload["result"].as_string();
      say(std::string(is_error ? "  ✗ " : "  ✓ ") + first_line(result, 120),
          is_error ? tui::Style::Error : tui::Style::Success);
    } else if (event.type == "error") {
      say("error: " + event.payload["message"].as_string(), tui::Style::Error);
    } else if (event.type == "compaction") {
      if (options_.session) {
        options_.session->append_compaction(event.payload["summary"].as_string(),
                                            event.payload["tokensBefore"].as_int(0));
      }
      say("[conversation compacted]", tui::Style::Dim);
    } else if (event.type == "compaction_failed") {
      say("[compaction failed: " + event.payload["message"].as_string() + "]", tui::Style::Dim);
    }
  }

  if (worker_done_.load()) {
    if (worker_.joinable()) worker_.join();
    worker_done_ = false;
    busy_ = false;
    if (ui_.streaming()) ui_.stream_end();
    ui_.set_status(kReadyStatus, false);
    changed = true;
  }
  return changed;
}

void TuiApp::stop_worker() {
  if (agent_) agent_->interrupt();
  if (worker_.joinable()) worker_.join();
  worker_done_ = false;
  busy_ = false;
}

void TuiApp::start_job(JobKind kind, const ModelSpec& spec) {
  if (job_thread_.joinable()) job_thread_.join();
  job_kind_ = kind;
  job_cancel_ = false;
  job_done_ = false;
  job_running_ = true;
  {
    std::lock_guard<std::mutex> lock(job_mutex_);
    job_error_.clear();
    job_models_.clear();
  }

  job_thread_ = std::thread([this, spec]() {
    std::string error;
    std::vector<std::string> models = fetch_models(spec, &error, &job_cancel_);
    {
      std::lock_guard<std::mutex> lock(job_mutex_);
      job_error_ = error;
      job_models_ = std::move(models);
    }
    job_done_ = true;
    job_running_ = false;
  });
}

bool TuiApp::consume_job() {
  if (!job_done_.load()) return false;
  job_done_ = false;
  if (job_thread_.joinable()) job_thread_.join();
  job_running_ = false;

  std::string error;
  std::vector<std::string> models;
  {
    std::lock_guard<std::mutex> lock(job_mutex_);
    error = job_error_;
    models = job_models_;
  }

  const JobKind kind = job_kind_;
  job_kind_ = JobKind::None;

  if (kind == JobKind::FetchModels) {
    if (!error.empty()) {
      ui_.set_status(kReadyStatus, false);
      if (error == "aborted") {
        say("model fetch cancelled", tui::Style::Dim);
        return true;
      }
      say("could not fetch models: " + error, tui::Style::Error);
      say("the key is saved — retry /model, or use /model <id> to set one directly",
          tui::Style::Dim);
      return true;
    }
    if (models.empty()) {
      ui_.set_status(kReadyStatus, false);
      say("no models returned — type /model <id>", tui::Style::Dim);
      return true;
    }
    models_ = models;
    show_models();
    return true;
  }

  return false;
}

int run_tui_app(TuiAppOptions options) {
  TuiApp app(std::move(options));
  return app.run();
}

}  // namespace karta
