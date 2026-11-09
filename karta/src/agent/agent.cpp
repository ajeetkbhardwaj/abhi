#include "karta/agent/agent.h"

#include <algorithm>
#include <future>
#include <sstream>

namespace karta {
namespace {

std::string block_text(const Json& message) {
  std::string text;
  for (const Json& block : message["content"].items()) {
    if (block["type"].as_string() == "text") text += block["text"].as_string();
  }
  return text;
}

std::vector<Json> tool_calls_of(const Json& message) {
  std::vector<Json> calls;
  for (const Json& block : message["content"].items()) {
    if (block["type"].as_string() == "toolCall") calls.push_back(block);
  }
  return calls;
}

Json message_list(const std::vector<Json>& messages) {
  Json out = Json::array();
  for (const Json& message : messages) out.push_back(message);
  return out;
}

}  // namespace

Agent::Agent(AgentOptions options, EventSink sink)
    : options_(std::move(options)), sink_(std::move(sink)) {}

void Agent::emit(const std::string& type, Json payload) {
  if (!sink_) return;
  AgentEvent event;
  event.type = type;
  event.payload = std::move(payload);
  sink_(event);
}

std::string Agent::model_label() const {
  return options_.model.provider + "/" + options_.model.model;
}

void Agent::queue_steering(std::string text) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  steering_queue_.push_back(std::move(text));
}

void Agent::queue_followup(std::string text) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  followup_queue_.push_back(std::move(text));
}

std::vector<Json> Agent::drain_steering() {
  std::vector<std::string> queued;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queued.swap(steering_queue_);
  }
  std::vector<Json> messages;
  for (std::string& text : queued) messages.push_back(make_user_message(text));
  return messages;
}

std::vector<Json> Agent::drain_followups() {
  std::vector<std::string> queued;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queued.swap(followup_queue_);
  }
  std::vector<Json> messages;
  for (std::string& text : queued) messages.push_back(make_user_message(text));
  return messages;
}

std::vector<Json> Agent::prompt(const std::string& text, bool emit_agent_start) {
  std::vector<Json> produced;
  Json message = make_user_message(text);
  messages_.push_back(message);
  produced.push_back(message);

  if (emit_agent_start) emit("agent_start", Json::object());
  emit("turn_start", Json::object());
  emit("message_start", message);
  emit("message_end", message);
  run_loop(produced);
  
  // Final agent_end with all messages (matching pi format)
  Json agent_end_payload = Json::object();
  agent_end_payload.set("messages", message_list(messages_));
  emit("agent_end", agent_end_payload);
  return produced;
}

void Agent::run_loop(std::vector<Json>& produced) {
  std::vector<Json> pending = drain_steering();
  bool explicit_continuation = false;

  while (true) {
    bool has_more_tool_calls = true;

    // Inner loop: process tool calls and steering messages.
    while (has_more_tool_calls || !pending.empty()) {
      if (options_.max_turns > 0 && turn_count_ >= options_.max_turns) {
        Json error = Json::object();
        error.set("message", "Maximum turn count reached (" + std::to_string(options_.max_turns) + ")");
        emit("error", error);
        emit("agent_end", Json::object());
        return;
      }

      for (const Json& message : pending) {
        messages_.push_back(message);
        produced.push_back(message);
        emit("message_start", message);
        emit("message_end", message);
      }
      pending.clear();

      emit("turn_start", Json::object());

      LlmRequest request;
      request.system = options_.system_prompt;
      request.messages = messages_;
      request.tools.clear();
      for (const ToolDefinition& tool : options_.tools) request.tools.push_back(tool_declaration(tool));

      Json skeleton = Json::object();
      skeleton.set("role", "assistant");
      skeleton.set("content", Json::array());
      emit("message_start", skeleton);

      const AssistantTurn turn = stream_completion(
          options_.model, request,
          [&](const std::string& delta) {
            Json update = Json::object();
            update.set("delta", delta);
            // Include assistantMessageEvent for pi compatibility
            Json event = Json::object();
            event.set("type", "text_delta");
            event.set("text", delta);
            update.set("assistantMessageEvent", event);
            emit("message_update", update);
          },
          &interrupted_);

      const Json assistant = make_assistant_message(turn);
      messages_.push_back(assistant);
      produced.push_back(assistant);
      emit("message_end", assistant);
      turn_count_++;

      if (turn.stop_reason == "error" || turn.stop_reason == "aborted") {
        Json payload = Json::object();
        payload.set("message", assistant);
        payload.set("toolResults", Json::array());
        emit("turn_end", payload);
        if (!turn.error.empty() && turn.stop_reason == "error") {
          Json error = Json::object();
          error.set("message", turn.error);
          emit("error", error);
        }
        emit("agent_end", Json::object());
        return;
      }

      std::vector<Json> tool_results;
      const std::vector<Json> calls = tool_calls_of(assistant);
      has_more_tool_calls = false;
      bool terminate = false;
      if (!calls.empty()) {
        ToolBatch batch = turn.stop_reason == "length" ? fail_truncated_tool_calls(assistant)
                                                       : execute_tool_calls(assistant);
        tool_results = batch.messages;
        terminate = batch.terminate;
        has_more_tool_calls = !terminate;
        for (const Json& result : tool_results) {
          messages_.push_back(result);
          produced.push_back(result);
          emit("message_start", result);
          emit("message_end", result);
        }
      }

      Json turn_end = Json::object();
      turn_end.set("message", assistant);
      turn_end.set("toolResults", message_list(tool_results));
      emit("turn_end", turn_end);

      if (terminate) {
        Json agent_end_payload = Json::object();
        agent_end_payload.set("messages", message_list(messages_));
        emit("agent_end", agent_end_payload);
        return;
      }

      maybe_compact();

      pending = drain_steering();
      if (has_more_tool_calls || !pending.empty()) explicit_continuation = false;
    }

    // Agent would stop here. Check for follow-up messages.
    std::vector<Json> followups = drain_followups();
    if (!followups.empty()) {
      explicit_continuation = false;
      pending = std::move(followups);
      continue;
    }

    // Fulfil an explicit continue decision with one context-only turn.
    if (explicit_continuation) {
      explicit_continuation = false;
      continue;
    }
    break;
  }

  Json agent_end_payload = Json::object();
  agent_end_payload.set("messages", message_list(messages_));
  emit("agent_end", agent_end_payload);
}

Agent::ToolBatch Agent::execute_tool_calls(const Json& assistant_message) {
  ToolBatch batch;
  ToolContext context{options_.cwd};

  // Collect all tool calls first
  struct ToolCallInfo {
    std::string id;
    std::string name;
    Json arguments;
    std::string parse_error;
  };

  std::vector<ToolCallInfo> calls;
  for (const Json& call : tool_calls_of(assistant_message)) {
    ToolCallInfo info;
    info.id = call["id"].as_string();
    info.name = call["name"].as_string();
    if (call["arguments"].is_string()) {
      const std::string raw = call["arguments"].as_string();
      if (!raw.empty()) info.arguments = Json::parse(raw, &info.parse_error);
    } else if (call["arguments"].is_object()) {
      info.arguments = call["arguments"];
    }
    calls.push_back(std::move(info));
  }

  // Determine which calls can run in parallel (non-overlapping file writes)
  // For simplicity, we'll run read-only tools in parallel, and write tools sequentially
  // A more sophisticated approach would analyze oldText/newText paths
  auto is_read_only_tool = [](const std::string& name) {
    return name == "read" || name == "grep" || name == "find" || name == "ls" || name == "glob" || name == "bash";
  };

  // Execute read-only tools in parallel
  using ParallelResult = std::pair<size_t, ToolOutcome>;
  std::vector<std::future<ParallelResult>> parallel_results;
  std::vector<size_t> parallel_indices;
  std::vector<ToolOutcome> outcomes(calls.size());

  for (size_t i = 0; i < calls.size(); ++i) {
    const auto& info = calls[i];
    
    if (is_read_only_tool(info.name) && info.parse_error.empty()) {
      parallel_indices.push_back(i);
    }
  }

  // Launch parallel read-only tools
  for (size_t idx : parallel_indices) {
    parallel_results.emplace_back(std::async(std::launch::async, [this, &calls, idx, &context]() {
      const auto& info = calls[idx];
      // before_tool_call hook
      if (options_.before_tool_call) {
        options_.before_tool_call(info.name, info.arguments);
      }
      
      const ToolDefinition* tool = find_tool(options_.tools, info.name);
      if (!tool) {
        ToolOutcome outcome;
        outcome.text = "Unknown tool: " + info.name;
        outcome.is_error = true;
        return ParallelResult(idx, outcome);
      }
      try {
        ToolOutcome outcome = tool->execute(info.arguments, context);
        return ParallelResult(idx, outcome);
      } catch (const std::exception& error) {
        ToolOutcome outcome;
        outcome.text = std::string("Tool failed: ") + error.what();
        outcome.is_error = true;
        return ParallelResult(idx, outcome);
      }
    }));
  }

  // Wait for parallel results and emit events
  for (auto& fut : parallel_results) {
    auto [idx, outcome] = fut.get();
    const auto& info = calls[idx];
    
    Json start = Json::object();
    start.set("toolCallId", info.id);
    start.set("toolName", info.name);
    start.set("arguments", info.arguments);
    emit("tool_execution_start", start);

    const Json message = make_tool_result_message(info.id, info.name, outcome.text, outcome.is_error);
    batch.messages.push_back(message);

    // after_tool_call hook
    if (options_.after_tool_call) {
      Json result = Json::object();
      result.set("content", message["content"]);
      result.set("isError", outcome.is_error);
      options_.after_tool_call(info.name, info.arguments, result);
    }

    Json update = Json::object();
    update.set("toolCallId", info.id);
    update.set("toolName", info.name);
    update.set("arguments", info.arguments);
    update.set("partialResult", outcome.text);
    emit("tool_execution_update", update);

    Json end = Json::object();
    end.set("toolCallId", info.id);
    end.set("toolName", info.name);
    end.set("isError", outcome.is_error);
    end.set("result", outcome.text);
    emit("tool_execution_end", end);

    if (outcome.terminate) {
      batch.terminate = true;
    }
  }

  // Execute remaining tools sequentially (write tools and any that had parse errors)
  for (size_t i = 0; i < calls.size(); ++i) {
    // Skip already executed parallel tools
    bool was_parallel = false;
    for (size_t pidx : parallel_indices) {
      if (pidx == i) { was_parallel = true; break; }
    }
    if (was_parallel) continue;

    const auto& info = calls[i];

    Json start = Json::object();
    start.set("toolCallId", info.id);
    start.set("toolName", info.name);
    start.set("arguments", info.arguments);
    emit("tool_execution_start", start);

    // before_tool_call hook
    if (options_.before_tool_call) {
      options_.before_tool_call(info.name, info.arguments);
    }

    ToolOutcome outcome;
    if (!info.parse_error.empty()) {
      outcome.text = "Invalid tool arguments: " + info.parse_error;
      outcome.is_error = true;
    } else {
      const ToolDefinition* tool = find_tool(options_.tools, info.name);
      if (!tool) {
        outcome.text = "Unknown tool: " + info.name;
        outcome.is_error = true;
      } else {
        try {
          outcome = tool->execute(info.arguments, context);
        } catch (const std::exception& error) {
          outcome.text = std::string("Tool failed: ") + error.what();
          outcome.is_error = true;
        }
      }
    }

    const Json message = make_tool_result_message(info.id, info.name, outcome.text, outcome.is_error);
    batch.messages.push_back(message);

    // after_tool_call hook
    if (options_.after_tool_call) {
      Json result = Json::object();
      result.set("content", message["content"]);
      result.set("isError", outcome.is_error);
      options_.after_tool_call(info.name, info.arguments, result);
    }

    Json update = Json::object();
    update.set("toolCallId", info.id);
    update.set("toolName", info.name);
    update.set("arguments", info.arguments);
    update.set("partialResult", outcome.text);
    emit("tool_execution_update", update);

    Json end = Json::object();
    end.set("toolCallId", info.id);
    end.set("toolName", info.name);
    end.set("isError", outcome.is_error);
    end.set("result", outcome.text);
    emit("tool_execution_end", end);

    if (outcome.terminate) {
      batch.terminate = true;
      break;
    }
  }
  return batch;
}

Agent::ToolBatch Agent::fail_truncated_tool_calls(const Json& assistant_message) {
  ToolBatch batch;
  for (const Json& call : tool_calls_of(assistant_message)) {
    const std::string id = call["id"].as_string();
    const std::string name = call["name"].as_string();
    const std::string text =
        "Tool call not executed: the response hit the model's output limit, so its arguments may "
        "be truncated. Re-issue the call with a smaller response.";
    batch.messages.push_back(make_tool_result_message(id, name, text, true));

    Json arguments = Json::object();
    std::string parse_error;
    if (call["arguments"].is_string()) {
      const std::string raw = call["arguments"].as_string();
      if (!raw.empty()) arguments = Json::parse(raw, &parse_error);
    } else if (call["arguments"].is_object()) {
      arguments = call["arguments"];
    }

    // Emit tool_execution_update with partialResult for pi compatibility
    Json update = Json::object();
    update.set("toolCallId", id);
    update.set("toolName", name);
    update.set("arguments", arguments);
    update.set("partialResult", text);
    emit("tool_execution_update", update);

    Json end = Json::object();
    end.set("toolCallId", id);
    end.set("toolName", name);
    end.set("isError", true);
    end.set("result", text);
    emit("tool_execution_end", end);
  }
  return batch;
}

long long Agent::estimate_tokens() const {
  std::size_t characters = options_.system_prompt.size();
  for (const Json& message : messages_) {
    characters += block_text(message).size();
    for (const Json& block : message["content"].items()) {
      if (block["type"].as_string() == "toolCall") characters += block["arguments"].as_string().size();
    }
  }
  return static_cast<long long>(characters / 4);
}

void Agent::maybe_compact() {
  if (options_.compact_threshold <= 0 || compaction_failed_) return;
  const long long tokens_before = estimate_tokens();
  if (tokens_before < options_.compact_threshold) return;

  std::string transcript;
  for (const Json& message : messages_) {
    std::string role = message["role"].as_string();
    if (role == "toolResult") role = "tool";
    transcript += role + ": " + block_text(message);
    for (const Json& block : message["content"].items()) {
      if (block["type"].as_string() == "toolCall") {
        transcript += "\n[tool call] " + block["name"].as_string() + " " +
                      block["arguments"].as_string().substr(0, 400);
      }
    }
    transcript += "\n\n";
  }

  LlmRequest request;
  request.system =
      "You compact conversation history for a coding agent. Preserve the user's goal, decisions, "
      "files and symbols touched, commands run, errors and their fixes, and anything still "
      "unfinished. Be specific and terse. Reply with the summary only.";
  request.messages.push_back(
      make_user_message("Summarize this transcript so work can continue without it:\n\n" + transcript));

  const AssistantTurn turn =
      stream_completion(options_.model, request, nullptr, &interrupted_);
  if (turn.stop_reason == "error" || turn.stop_reason == "aborted" || turn.text.empty()) {
    compaction_failed_ = true;
    Json failure = Json::object();
    failure.set("message", turn.error.empty() ? "compaction produced no summary" : turn.error);
    emit("compaction_failed", failure);
    return;
  }

  compaction_failed_ = false;
  Json event = Json::object();
  event.set("summary", turn.text);
  event.set("tokensBefore", tokens_before);
  emit("compaction", event);

  messages_.clear();
  messages_.push_back(make_user_message(
      "[Context summary: the earlier conversation was compacted]\n\n" + turn.text));
}

}  // namespace karta
