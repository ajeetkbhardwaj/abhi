// Karta — /login and /model model picker.
//
// Mirrors the pi TUI's model-selection flow: fetch the provider's model
// catalog on a worker thread (with an abort flag threaded through
// fetch_models), render a numbered SelectList, apply the selection, and
// restore the ready state. Esc always cancels a fetch.

#include "tui/model-picker.h"

#include <algorithm>
#include <cstdlib>

#include "agent.h"
#include "auth.h"
#include "json.h"
#include "provider.h"
#include "session.h"
#include "tui/app.h"
#include "tui/screen.h"

namespace karta::tui {

ModelPicker::ModelPicker(AgentOptions* agent_options, Session* session)
    : agent_options_(agent_options),
      session_(session),
      job_state_(JobState::Idle) {}

bool ModelPicker::can_pick() const {
  const std::string provider = agent_options_->model.provider;
  if (provider_is_local(provider)) return false;
  return !agent_options_->model.api_key.empty();
}

void ModelPicker::start_fetch(const ModelSpec& spec) {
  current_ = -1;
  if (job_thread_.joinable()) job_thread_.join();
  cancel_ = false;
  job_state_ = JobState::Running;
  job_thread_ = std::thread([this, spec]() { fetch_models_(spec); });
}

void ModelPicker::cancel_fetch() {
  cancel_ = true;
  job_state_ = JobState::Cancelled;
  if (job_thread_.joinable()) job_thread_.join();
}

void ModelPicker::fetch_models_(const ModelSpec& spec) {
  std::string error;
  job_models_.clear();
  job_models_ = fetch_models(spec, &error, &cancel_);
  if (cancel_) {
    job_state_ = JobState::Cancelled;
    return;
  }
  job_error_ = error;
  job_state_ = (error.empty() && !job_models_.empty()) ? JobState::Fetched
                                                      : JobState::Failed;
}

void ModelPicker::update(Session& session) {
  if (job_state_ == JobState::Running) return;

  if (job_state_ == JobState::Failed) {
    if (job_error_ == "aborted") {
      job_state_ = JobState::Idle;
      return;
    }
    // Flag the parent: the selection must be done with /model <id>.
    if (!job_error_.empty()) {
      app_.append_text("/model fetch failed: " + job_error_, tui::Style::Error);
      return;
    }
  }

  std::vector<SelectItem> items;
  items.reserve(job_models_.size());
  for (const std::string& model : job_models_) {
    items.push_back({model, model, ""});
  }

  SelectListTheme theme;
  SelectList list(items, 7, theme, {}, [this](const SelectItem& item) {
    model_selected_ = item.value;
    if (model_selected_.empty()) return;
    apply_selected_model_();
  });

  // Keep the whole catalog on screen: numbered entries in the visible window,
  // centred on the currently selected model.
  const std::size_t rows = app_.rows();
  const std::size_t room = rows > 12 ? rows - 10 : 10;
  const std::size_t visible = std::min(job_models_.size(), room);
  for (std::size_t i = 0; i < visible; ++i) {
    const bool selected = i == static_cast<std::size_t>(current_);
    std::string line = "  " + std::to_string(i + 1) + ") " + job_models_[i];
    if (selected) line += "  ← current";
    app_.append_text(line, selected ? Style::Success : Style::Normal);
  }
  if (job_models_.size() > visible) {
    app_.append_text("  … " + std::to_string(job_models_.size() - visible) +
                     " more (type /model <id>)",
                     Style::Dim);
  }

  const std::string catalog = job_models_.empty() ? std::string("unknown")
                                                  : job_models_[0];
  app_.append_text("Available models for " + catalog + " →", Style::Bold);
  app_.set_prompt("model #> ");
  app_.set_status("select a model", false);
  app_.append_text("Type a number and press Enter, or type a model id. Esc cancels.", Style::Dim);

  current_ = -1;
  job_state_ = JobState::Idle;
}

void ModelPicker::apply_selected_model_() {
  ModelSpec spec = agent_options_->model;
  spec.model = model_selected_;
  agent_options_->model = spec;
  agent_options_->model.model = spec.model;
  if (session_) session_->append_model_change(spec.provider, spec.model);
  SessionInfo info;
  info.title = "karta";
  info.version = "0.1.0";
  info.provider = spec.provider;
  info.model = spec.model;
  info.cwd = "";
  info.id = "";
  app_.set_header(info);
  app_.append_text("model → " + spec.model, Style::Accent);
  app_.set_status("ready · /login · /model", false);
}

}  // namespace karta::tui
