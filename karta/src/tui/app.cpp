#include "karta/tui/app.h"

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

#include <curses.h>

#include "karta/core/auth.h"
#include "karta/core/json.h"
#include "karta/ai/provider.h"
#include "karta/session/session.h"
#include "karta/tui/model-picker.h"
#include "karta/tui/screen.h"

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

std::string name_or(const AgentEvent& event) {
  const std::string n = event.payload["toolName"].as_string();
  return n.empty() ? "tool" : n;
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
    models_all_done_ = true;
    if (models_all_thread_.joinable()) models_all_thread_.join();
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
  std::vector<std::string> models_; // legacy/current-provider list
  std::vector<std::pair<std::string,std::string>> all_models_; // (provider, model) from all connected providers
  bool models_loading_all_ = false;
  std::atomic<bool> models_all_done_{false};
  std::thread models_all_thread_;
  std::mutex models_all_mutex_;
  std::string models_all_error_;
  std::vector<std::pair<std::string,std::string>> models_all_result_;
  bool consume_models_all();
  void begin_all_model_pick();

  // Inline completion (slash commands / model search)
  bool completion_active_ = false;
  enum class CompletionKind { None, Command, Model } completion_kind_ = CompletionKind::None;
  std::vector<std::pair<std::string,std::string>> completion_all_; // value,label/desc
  std::vector<int> completion_filtered_;
  int completion_index_ = 0;
  std::string completion_prefix_;
  void update_completion();
  void render_completion();
  void clear_completion();
  void apply_completion(bool submit);
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
    changed = consume_models_all() || changed;

    int ch = 0;
    const tui::Key key = ui_.poll(&ch);
    if (key != tui::Key::None) {
      const std::string before = ui_.input();
      handle_key(key, ch);
      if (ui_.input() != before || key == tui::Key::Backspace || key == tui::Key::Delete || key == tui::Key::Char) update_completion();
      changed = true;
    }

    const bool active = busy_ || job_running_ || models_loading_all_ || ui_.streaming();
    if (changed || active) {
      ui_.draw();
      if (!changed) std::this_thread::sleep_for(std::chrono::milliseconds(80));
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
  if (key == tui::Key::CtrlL) { ui_.draw(); return; }
  if (key == tui::Key::CtrlD) {
    if (ui_.input().empty() && !busy_ && !job_running_ && phase_ == Phase::Chat) { running_ = false; return; }
  }
  if (completion_active_) {
    if (key == tui::Key::Escape) { clear_completion(); return; }
    if (key == tui::Key::Up) {
      if (!completion_filtered_.empty()) completion_index_ = (completion_index_ <= 0) ? (int)completion_filtered_.size()-1 : completion_index_-1;
      render_completion(); return;
    }
    if (key == tui::Key::Down || key == tui::Key::Tab) {
      if (!completion_filtered_.empty()) completion_index_ = (completion_index_ + 1) % (int)completion_filtered_.size();
      render_completion(); return;
    }
    if (key == tui::Key::Enter) { apply_completion(true); return; }
  }
  switch (key) {
    case tui::Key::Enter: {
      const std::string line = ui_.input();
      if (ui_.multiline()) {
        const std::string text = ui_.input_text();
        ui_.set_input_text("");
        ui_.set_multiline(false);
        clear_completion();
        handle_submit(text);
      } else {
        ui_.set_input("");
        clear_completion();
        handle_submit(line);
      }
      return;
    }
    case tui::Key::CtrlC:
    case tui::Key::Escape:
      if (busy_ && agent_) {
        agent_->interrupt();
        ui_.set_status("cancelling…", true);
      } else if (job_running_ || models_loading_all_) {
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
  if (job_running_ && !models_loading_all_) { say("still fetching — Esc cancels", tui::Style::Dim); return; }
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
    say("  /thinking [0-3|off|low|medium|high]  set thinking level (default 1)", tui::Style::Normal);
    say("  /theme [name]       set color theme (default, dark, light, mono)", tui::Style::Normal);
    say("  /session            show the session file", tui::Style::Normal);
    say("  /clear              clear the transcript", tui::Style::Normal);
    say("  /undo               undo last edit (multi-line)", tui::Style::Normal);
    say("  /redo               redo last edit (multi-line)", tui::Style::Normal);
    say("  /exit               quit (Esc cancels a turn, Ctrl+D at empty prompt)", tui::Style::Normal);
    say("  Ctrl+L              refresh screen", tui::Style::Normal);
    say("  Ctrl+Z              undo (multi-line)", tui::Style::Normal);
    say("  Ctrl+Y              redo (multi-line)", tui::Style::Normal);
    say("  Enter               submit (multi-line: Shift+Enter for new line)", tui::Style::Normal);
    return;
  }
  if (name == "thinking" || name == "think") {
    if (argument.empty()) {
      say("thinking: " + thinking_label(options_.agent.model.thinking), tui::Style::Normal);
      say("  0 off, 1 default/low, 2 medium, 3 high", tui::Style::Dim);
      return;
    }
    options_.agent.model.thinking = normalize_thinking(argument);
    if (agent_) agent_->set_model(options_.agent.model);
    sync_header();
    say("thinking → " + thinking_label(options_.agent.model.thinking), tui::Style::Accent);
    return;
  }
  if (name == "login") {
    begin_login(argument);
    return;
  }
  if (name == "model" || name == "models") {
    // /model <provider/model or id> sets directly (back-compat). /model <number> uses current list.
    // Bare /model opens unified search across all logged-in providers.
    if (!argument.empty()) {
      if (all_digits(argument) && !models_.empty()) { finish_model_pick(argument); return; }
      if (argument.find('/') != std::string::npos) {
        // provider/model
        const size_t sl = argument.find('/');
        std::string prov = argument.substr(0, sl), mod = argument.substr(sl+1);
        if (!prov.empty() && !mod.empty()) { apply_spec(spec_for(prov, mod)); say("model → "+prov+"/"+mod, tui::Style::Accent); return; }
      }
      // treat as search: open unified picker seeded with query
      ui_.set_input("/model " + argument);
      begin_all_model_pick();
      update_completion();
      return;
    }
    begin_all_model_pick();
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
    all_models_.clear();
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
  if (name == "theme") {
    if (argument.empty()) {
      say("Current theme: default", tui::Style::Normal);
      say("Available: default, dark, light, mono", tui::Style::Dim);
      return;
    }
    tui::ThemeColors theme;
    if (argument == "dark") {
      theme.normal = COLOR_WHITE;
      theme.accent = COLOR_CYAN;
      theme.user = COLOR_YELLOW;
      theme.assistant = COLOR_MAGENTA;
      theme.success = COLOR_GREEN;
      theme.error = COLOR_RED;
      theme.dim = -1;
      theme.bold = -1;
      theme.status = -1;
      theme.header_bg = COLOR_BLUE;
      theme.header_fg = COLOR_WHITE;
      theme.input_bg = COLOR_BLACK;
      theme.input_fg = COLOR_WHITE;
      theme.selection_bg = COLOR_CYAN;
      theme.selection_fg = COLOR_BLACK;
    } else if (argument == "light") {
      theme.normal = COLOR_BLACK;
      theme.accent = COLOR_BLUE;
      theme.user = COLOR_MAGENTA;
      theme.assistant = COLOR_CYAN;
      theme.success = COLOR_GREEN;
      theme.error = COLOR_RED;
      theme.dim = -1;
      theme.bold = -1;
      theme.status = -1;
      theme.header_bg = COLOR_WHITE;
      theme.header_fg = COLOR_BLACK;
      theme.input_bg = COLOR_WHITE;
      theme.input_fg = COLOR_BLACK;
      theme.selection_bg = COLOR_BLUE;
      theme.selection_fg = COLOR_WHITE;
    } else if (argument == "mono") {
      theme.normal = -1;
      theme.accent = -1;
      theme.user = -1;
      theme.assistant = -1;
      theme.success = -1;
      theme.error = -1;
      theme.dim = -1;
      theme.bold = -1;
      theme.status = -1;
      theme.header_bg = -1;
      theme.header_fg = -1;
      theme.input_bg = -1;
      theme.input_fg = -1;
      theme.selection_bg = -1;
      theme.selection_fg = -1;
    } else {
      // default - reset to ncurses defaults
    }
    ui_.apply_theme(theme);
    say("theme → " + argument, tui::Style::Accent);
    return;
  }
  if (name == "undo") {
    if (ui_.multiline() && ui_.editor()) {
      ui_.editor()->undo();
      say("undone", tui::Style::Accent);
    } else {
      say("undo only available in multi-line mode", tui::Style::Dim);
    }
    return;
  }
  if (name == "redo") {
    if (ui_.multiline() && ui_.editor()) {
      ui_.editor()->redo();
      say("redone", tui::Style::Accent);
    } else {
      say("redo only available in multi-line mode", tui::Style::Dim);
    }
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
  all_models_.clear();
    say("reused on every launch until you run /logout", tui::Style::Dim);
  } else {
    say("warning: could not save the key to " + auth_file_path() +
            " — it stays active for this session only",
        tui::Style::Error);
  }

  begin_model_pick();
}

void TuiApp::begin_model_pick() {
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
  ui_.set_status(std::string("thinking (") + thinking_label(options_.agent.model.thinking) + ")…", true);
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
    if (event.type == "message_start") {
      const std::string role = event.payload["role"].as_string();
      if (role == "assistant") {
        if (!ui_.streaming()) ui_.stream_begin(tui::Style::Assistant);
        if (!completion_active_) ui_.set_status(std::string("thinking (") + thinking_label(options_.agent.model.thinking) + ")…", true);
      }
    } else if (event.type == "message_update") {
      const std::string delta = event.payload["delta"].as_string();
      if (!delta.empty()) {
        if (!ui_.streaming()) ui_.stream_begin(tui::Style::Assistant);
        ui_.stream_delta(delta);
        // keep rotating indicator just above input while generating
        if (!completion_active_) ui_.set_status("generating…", true);
      }
    } else if (event.type == "message_end") {
      if (options_.session) options_.session->append_message(event.payload);
      const std::string role = event.payload["role"].as_string();
      if (role == "assistant") {
        if (ui_.streaming()) {
          ui_.stream_end();
        } else {
          // Fallback: if streaming deltas were missed, render final text.
          std::string text;
          if (event.payload["content"].is_string()) {
            text = event.payload["content"].as_string();
          } else if (event.payload["content"].is_array()) {
            for (const Json& part : event.payload["content"].items()) {
              if (part["type"].as_string() == "text") text += part["text"].as_string();
            }
          } else if (event.payload["text"].is_string()) {
            text = event.payload["text"].as_string();
          }
          if (!text.empty()) {
            say("• " + std::string(""), tui::Style::Dim);
            ui_.append_text(text, tui::Style::Assistant);
            ui_.append_text("", tui::Style::Normal);
          }
        }
      }
    } else if (event.type == "tool_execution_start") {
      if (ui_.streaming()) ui_.stream_end();
      const std::string name = event.payload["toolName"].as_string();
      const std::string args = event.payload["arguments"].dump();
      say("● " + name, tui::Style::Accent);
      // show a short args preview (wrapped) instead of one truncated line
      const std::string preview = first_line(args, 220);
      if (!preview.empty()) say("  args: " + preview, tui::Style::Dim);
    } else if (event.type == "tool_execution_end") {
      if (ui_.streaming()) ui_.stream_end();
      const bool is_error = event.payload["isError"].as_bool(false);
      const std::string result = event.payload["result"].as_string();
      say(std::string(is_error ? "  ✗ " : "  ✓ ") + name_or(event), is_error ? tui::Style::Error : tui::Style::Success);
      if (!result.empty()) {
        // Render full result as wrapped transcript lines (no "..." truncation).
        // Large outputs remain scrollable (PgUp/PgDn) instead of hidden.
        ui_.append_text(result, is_error ? tui::Style::Error : tui::Style::Normal);
        ui_.append_text("", tui::Style::Normal);
      }
    } else if (event.type == "error") {
      if (ui_.streaming()) ui_.stream_end();
      say("error: " + event.payload["message"].as_string(), tui::Style::Error);
    } else if (event.type == "compaction") {
      if (ui_.streaming()) ui_.stream_end();
      if (options_.session) {
        options_.session->append_compaction(event.payload["summary"].as_string(),
                                            event.payload["tokensBefore"].as_int(0));
      }
      say("[conversation compacted]", tui::Style::Dim);
    } else if (event.type == "compaction_failed") {
      if (ui_.streaming()) ui_.stream_end();
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


namespace {
bool contains_ic(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
    [](char a,char b){ return std::tolower((unsigned char)a)==std::tolower((unsigned char)b); });
  return it != hay.end();
}

// Robust fuzzy matching using Smith-Waterman style algorithm (like fzf/skim)
// Returns score: higher = better match, 0 = no match
int fuzzy_match(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return 100;
  if (hay.empty()) return 0;
  
  std::string h = hay;
  std::string n = needle;
  std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c){ return std::tolower(c); });
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c){ return std::tolower(c); });
  
  const int match_bonus = 10;
  const int gap_penalty = -3;
  const int mismatch_penalty = -1;
  
  // DP matrix for local alignment (Smith-Waterman)
  std::vector<std::vector<int>> dp(n.size() + 1, std::vector<int>(h.size() + 1, 0));
  int max_score = 0;
  
  for (std::size_t i = 1; i <= n.size(); ++i) {
    for (std::size_t j = 1; j <= h.size(); ++j) {
      int match = (n[i-1] == h[j-1]) ? match_bonus : mismatch_penalty;
      int diag = dp[i-1][j-1] + match;
      int up = dp[i-1][j] + gap_penalty;
      int left = dp[i][j-1] + gap_penalty;
      dp[i][j] = std::max({0, diag, up, left});
      max_score = std::max(max_score, dp[i][j]);
    }
  }
  
  if (max_score <= 0) return 0;
  
  // Bonus scoring
  int score = max_score;
  
  // Bonus for prefix match
  if (h.size() >= n.size()) {
    bool is_prefix = true;
    for (std::size_t i = 0; i < n.size(); ++i) {
      if (h[i] != n[i]) { is_prefix = false; break; }
    }
    if (is_prefix) score += 50;
  }
  
  // Bonus for exact substring
  if (h.find(n) != std::string::npos) score += 30;
  
  // Bonus for word boundary matches
  for (std::size_t i = 0; i + n.size() <= h.size(); ++i) {
    if (h.substr(i, n.size()) == n) {
      bool word_boundary = (i == 0 || !std::isalnum(static_cast<unsigned char>(h[i-1]))) &&
                           (i + n.size() == h.size() || !std::isalnum(static_cast<unsigned char>(h[i + n.size()])));
      if (word_boundary) { score += 20; break; }
    }
  }
  
  // Bonus for early first character match
  std::size_t first_pos = h.find(n[0]);
  if (first_pos != std::string::npos && first_pos < 5) score += 10 - static_cast<int>(first_pos) * 2;
  
  return score;
}
}

void TuiApp::clear_completion() {
  completion_active_ = false;
  completion_kind_ = CompletionKind::None;
  completion_filtered_.clear();
  completion_index_ = 0;
  ui_.clear_overlay();
  ui_.set_status(kReadyStatus, false);
}

void TuiApp::update_completion() {
  if (phase_ != Phase::Chat || busy_ || job_running_) { clear_completion(); return; }
  const std::string in = ui_.input();
  completion_all_.clear();
  completion_filtered_.clear();
  completion_kind_ = CompletionKind::None;

  if (in.size() >= 1 && in[0] == '/' && in.find(' ') == std::string::npos) {
    completion_kind_ = CompletionKind::Command;
    completion_prefix_ = lower(in.substr(1));
    const std::vector<std::pair<std::string,std::string>> cmds = {
      {"login","connect provider/key"},{"logout","forget saved key"},{"model","pick or set model"},
      {"provider","show/switch provider"},{"thinking","0 off,1 default,2 medium,3 high"},
      {"session","show session file"},{"clear","clear transcript"},{"help","show commands"},{"exit","quit"}};
    struct ScoredItem { int score; int idx; };
    std::vector<ScoredItem> scored;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
      int score = fuzzy_match(cmds[i].first, completion_prefix_);
      if (score > 0) scored.push_back({score, static_cast<int>(i)});
    }
    std::sort(scored.begin(), scored.end(),
      [](const ScoredItem& a, const ScoredItem& b) { return a.score > b.score; });
    for (const auto& s : scored) {
      completion_filtered_.push_back(static_cast<int>(completion_all_.size()));
      completion_all_.push_back(cmds[static_cast<std::size_t>(s.idx)]);
    }
  } else if ((in.rfind("/model ",0)==0) || (in.rfind("/models ",0)==0) || lower(trim(in))=="/model" || lower(trim(in))=="/models") {
    const size_t sp = in.find(' ');
    completion_prefix_ = (sp==std::string::npos)?"":in.substr(sp+1);
    if (all_models_.empty() && !models_.empty()) {
      // seed from current provider list
      for (auto& m: models_) all_models_.push_back({options_.agent.model.provider,m});
    }
    if (all_models_.empty() && !models_loading_all_) { begin_all_model_pick(); return; }
    if (!all_models_.empty()) {
      completion_kind_ = CompletionKind::Model;
      struct ScoredItem { int score; int idx; };
      std::vector<ScoredItem> scored;
      for (std::size_t i = 0; i < all_models_.size(); ++i) {
        const auto& pm = all_models_[i];
        std::string show = pm.first + "/" + pm.second;
        int score = fuzzy_match(show, completion_prefix_);
        if (score > 0 || contains_ic(pm.second, completion_prefix_)) {
          scored.push_back({score, static_cast<int>(i)});
        }
      }
      // Sort by score descending
      std::sort(scored.begin(), scored.end(),
        [](const ScoredItem& a, const ScoredItem& b) { return a.score > b.score; });
      for (const auto& s : scored) {
        const auto& pm = all_models_[static_cast<std::size_t>(s.idx)];
        completion_filtered_.push_back(static_cast<int>(completion_all_.size()));
        completion_all_.push_back({pm.first, pm.second});
      }
    }
  }

  completion_active_ = !completion_filtered_.empty();
  if (!completion_active_) { if (completion_kind_==CompletionKind::None) { ui_.clear_overlay(); ui_.set_status(kReadyStatus, false);} return; }
  if (completion_index_ >= (int)completion_filtered_.size()) completion_index_ = 0;
  render_completion();
}

void TuiApp::render_completion() {
  if (!completion_active_ || completion_filtered_.empty()) { ui_.clear_overlay(); return; }
  std::vector<std::string> lines;
  int total = (int)completion_filtered_.size();
  int maxn = 8; int start = 0;
  if (completion_index_ >= maxn) start = completion_index_ - maxn + 1;
  int end = std::min(total, start + maxn);
  for (int i=start;i<end;i++) {
    const auto& it = completion_all_[completion_filtered_[i]];
    std::string label = (completion_kind_==CompletionKind::Command) ? ("/"+it.first+"  — "+it.second) : (it.first + "/" + it.second);
    if (label.size() > (size_t)(ui_.cols()>8?ui_.cols()-8:40)) label = label.substr(0, ui_.cols()>11?ui_.cols()-11:37) + "…";
    lines.push_back(label);
  }
  if (total > maxn) lines.insert(lines.begin(), "… " + std::to_string(completion_index_+1) + "/" + std::to_string(total) + (completion_kind_==CompletionKind::Model?" (type to search)":""));
  ui_.set_overlay(lines, completion_index_ - start + ((total>maxn)?1:0));
  std::string hint = (completion_kind_==CompletionKind::Command?"commands":"models") + std::string(": ↑↓/Tab navigate · Enter select · Esc close");
  ui_.set_status(hint, false);
}

void TuiApp::apply_completion(bool submit) {
  if (!completion_active_ || completion_filtered_.empty()) return;
  const auto& item = completion_all_[completion_filtered_[completion_index_]];
  if (completion_kind_ == CompletionKind::Command) {
    ui_.set_input("/" + item.first + (item.first=="exit"||item.first=="help"||item.first=="clear"||item.first=="session"?"":" "));
  } else {
    // item.first=provider, item.second=model
    std::string prov=item.first, mod=item.second;
    ui_.set_input("/model " + prov + "/" + mod);
    if (submit) {
      ui_.set_input(""); clear_completion();
      apply_spec(spec_for(prov, mod)); say("model → "+prov+"/"+mod, tui::Style::Accent); ui_.set_status(kReadyStatus,false); return;
    }
  }
  if (submit) {
    const std::string line = ui_.input();
    ui_.set_input("");
    clear_completion();
    handle_submit(line);
  } else {
    update_completion();
  }
}



void TuiApp::begin_all_model_pick() {
  if (!all_models_.empty()) {
    clear_completion();
    ui_.set_prompt("model> ");
    ui_.set_input("/model ");
    update_completion();
    ui_.set_status("search models (all logged-in providers) · ↑↓/Tab · Enter · Esc", false);
    return;
  }
  if (models_loading_all_ || job_running_) { say("already loading models — wait or Esc cancels", tui::Style::Dim); return; }
  std::vector<std::string> provs = known_providers();
  std::map<std::string,bool> seen; std::vector<std::string> targets;
  auto addp=[&](const std::string& p){ std::string lp=lower(p); if(lp.empty()||seen[lp]) return; seen[lp]=true; targets.push_back(lp); };
  for (auto& kv : load_auth()) addp(kv.first);
  for (auto& p : provs) { if (provider_is_local(p) || !stored_api_key(p).empty()) { addp(p); } else { ModelSpec s=resolve_model_spec(p,"",""); if(!s.api_key.empty()) addp(p); } }
  addp(options_.agent.model.provider);
  if (targets.empty()) { say("no providers connected — use /login first", tui::Style::Error); return; }
  models_loading_all_ = true; models_all_done_ = false;
  { std::lock_guard<std::mutex> lk(models_all_mutex_); models_all_error_.clear(); models_all_result_.clear(); }
  ui_.set_status("loading models from " + std::to_string(targets.size()) + " provider(s)… (Esc cancels)", true);
  if (models_all_thread_.joinable()) models_all_thread_.join();
  job_cancel_ = false;
  models_all_thread_ = std::thread([this, targets]() {
    std::vector<std::pair<std::string,std::string>> out; std::string first_err;
    for (auto& p : targets) {
      if (job_cancel_.load()) break;
      ModelSpec spec = resolve_model_spec(p, "", "");
      std::string err; auto ms = fetch_models(spec, &err, &job_cancel_);
      if (!err.empty() && first_err.empty()) first_err = p + ": " + err;
      for (auto& m : ms) out.push_back({p,m});
    }
    std::sort(out.begin(), out.end()); out.erase(std::unique(out.begin(), out.end()), out.end());
    { std::lock_guard<std::mutex> lk(models_all_mutex_); models_all_result_ = std::move(out); models_all_error_ = first_err; }
    models_all_done_ = true;
  });
}

bool TuiApp::consume_models_all() {
  if (!models_all_done_.load()) return false;
  models_all_done_ = false; models_loading_all_ = false;
  if (models_all_thread_.joinable()) models_all_thread_.join();
  std::vector<std::pair<std::string,std::string>> res; std::string err;
  { std::lock_guard<std::mutex> lk(models_all_mutex_); res = models_all_result_; err = models_all_error_; }
  all_models_ = std::move(res);
  if (all_models_.empty()) {
    ui_.set_status(kReadyStatus, false);
    say("no models loaded" + (err.empty()?"":(" — "+err)), tui::Style::Error);
    say("use /model <provider/model> to set directly, or /login", tui::Style::Dim);
    return true;
  }
  say("Loaded " + std::to_string(all_models_.size()) + " models (all logged-in providers). Type to search, ↑↓/Tab, Enter.", tui::Style::Success);
  ui_.set_input("/model ");
  update_completion();
  return true;
}

}  // namespace karta
