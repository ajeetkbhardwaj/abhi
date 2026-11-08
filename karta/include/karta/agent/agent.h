// Karta — agent loop (port of packages/agent/src/agent-loop.ts).
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "karta/core/json.h"
#include "karta/ai/provider.h"
#include "karta/tools/tools.h"

namespace karta {

struct AgentOptions {
  ModelSpec model;
  std::string system_prompt;
  std::vector<ToolDefinition> tools;
  std::string cwd;
  int max_turns = 0;             // 0 = unlimited
  long long compact_threshold = 0;  // estimated tokens; 0 disables auto-compaction
};

struct AgentEvent {
  std::string type;
  Json payload;
};

class Agent {
 public:
  using EventSink = std::function<void(const AgentEvent&)>;

  Agent(AgentOptions options, EventSink sink);

  // Runs one prompt to completion and returns the messages produced this turn.
  std::vector<Json> prompt(const std::string& text, bool emit_agent_start = true);

  const std::vector<Json>& messages() const { return messages_; }
  void set_messages(std::vector<Json> messages) { messages_ = std::move(messages); }
  void set_system_prompt(std::string prompt) { options_.system_prompt = std::move(prompt); }
  void set_model(ModelSpec model) { options_.model = std::move(model); }
  const AgentOptions& options() const { return options_; }

  void interrupt() { interrupted_.store(true); }
  bool interrupted() const { return interrupted_.load(); }
  void reset_interrupt() { interrupted_.store(false); }
  std::atomic<bool>* interrupt_flag() { return &interrupted_; }

  // Queues used while the loop is running (steering) and after it would stop (follow-ups).
  void queue_steering(std::string text);
  void queue_followup(std::string text);
  std::string model_label() const;

 private:
  void run_loop(std::vector<Json>& produced);
  void emit(const std::string& type, Json payload);
  std::vector<Json> drain_steering();
  std::vector<Json> drain_followups();
  long long estimate_tokens() const;
  void maybe_compact();

  struct ToolBatch {
    std::vector<Json> messages;
    bool terminate = false;
  };
  ToolBatch execute_tool_calls(const Json& assistant_message);
  ToolBatch fail_truncated_tool_calls(const Json& assistant_message);

  AgentOptions options_;
  EventSink sink_;
  std::vector<Json> messages_;
  std::mutex queue_mutex_;
  std::vector<std::string> steering_queue_;
  std::vector<std::string> followup_queue_;
  std::atomic<bool> interrupted_{false};
  bool compaction_failed_ = false;
  int turn_count_ = 0;
};

}  // namespace karta
