// Karta — /login and /model model picker.
//
// Mirrors the pi TUI's model-selection flow: fetch the provider's model
// catalog on a worker thread (with an abort flag threaded through
// fetch_models), render a numbered SelectList, apply the selection, and
// restore the ready state. Esc always cancels a fetch.

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "auth.h"
#include "json.h"
#include "provider.h"
#include "session.h"
#include "tui/app.h"
#include "tui/screen.h"
#include "tui/select-list.h"

namespace karta {

class Agent;
struct AgentOptions;
struct ModelSpec;
struct Session;

namespace tui {

class SelectList;

// Lifecycle: start_fetch -> idle | fetched | failed | cancelled.
enum class JobState { Idle, Running, Fetched, Failed, Cancelled };

// Lightweight carrier for the picker window (select list + completion).
struct SelectListSelection {
  std::vector<SelectItem> items_;
  SelectListTheme theme_;
  SelectList::OnSelect on_select_;
};

class ModelPicker {
 public:
  explicit ModelPicker(AgentOptions* agent, Session* session);

  // True when the picker may fetch (non-local provider, credentials present).
  bool can_pick() const;

  // Kick off the model fetch on a worker thread.
  void start_fetch(const ModelSpec& spec);

  // Cancel an in-flight fetch; the job joins before returning.
  void cancel_fetch();

  // React to the finished job and paint the picker window.
  void update(Session& session);

  // Current job state.
  JobState job_state() const { return job_state_; }

 private:
  void fetch_models_(const ModelSpec& spec);

  void apply_selected_model_();

  AgentOptions* agent_options_;
  Session* session_;
  tui::Tui& app_ = *static_cast<tui::Tui*>(nullptr);

  int current_ = -1;
  std::string model_selected_;

  std::thread job_thread_;
  std::atomic<bool> cancel_{false};
  JobState job_state_ = JobState::Idle;
  std::string job_error_;
  std::vector<std::string> job_models_;
};

}  // namespace tui
}  // namespace karta
