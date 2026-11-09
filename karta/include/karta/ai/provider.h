// Karta — provider abstraction and streaming LLM transport.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "karta/core/json.h"

namespace karta {

struct ToolCall {
  std::string id;              // provider-assigned call id
  std::string name;            // tool name
  std::string arguments;       // raw JSON object text
  std::string idempotency_key; // client-generated key for deduplication
};

struct Usage {
  long long input_tokens = 0;
  long long output_tokens = 0;
  long long cache_read = 0;
  long long cache_write = 0;
};

struct AssistantTurn {
  std::string text;
  std::vector<ToolCall> tool_calls;
  Usage usage;
  std::string stop_reason = "stop";  // stop | length | toolUse | error | aborted
  std::string error;
};

std::string normalize_thinking(const std::string& level);
std::string thinking_label(const std::string& level);

struct ModelSpec {
  std::string provider = "anthropic";
  std::string model = "claude-sonnet-4-5";
  std::string base_url;
  std::string api_key;
  int max_tokens = 8192;
  std::string thinking = "low"; // 0 off, 1 default/low, 2 medium, 3 high
};

struct LlmRequest {
  std::string system;
  std::vector<Json> messages;  // canonical AgentMessage objects
  std::vector<Json> tools;     // OpenAI-style function declarations
};

using TextCallback = std::function<void(const std::string&)>;

ModelSpec resolve_model_spec(const std::string& provider, const std::string& model,
                             const std::string& api_key);

AssistantTurn stream_completion(const ModelSpec& spec, const LlmRequest& request,
                                const TextCallback& on_text, const std::atomic<bool>* cancel);

std::vector<std::string> known_providers();
bool provider_is_anthropic(const std::string& provider);
bool provider_is_local(const std::string& provider);
std::string provider_env_var(const std::string& provider);

// GETs the provider's model catalog (used by the TUI `/model` picker).
// On failure returns an empty list and fills `error`. Setting `cancel` aborts
// the request and reports "aborted", so quitting never blocks on the network.
std::vector<std::string> fetch_models(const ModelSpec& spec, std::string* error,
                                      const std::atomic<bool>* cancel = nullptr);

// Canonical AgentMessage helpers, shared by the agent loop and session replay.
Json make_user_message(const std::string& text);
Json make_user_message_with_images(const std::string& text, const std::vector<std::pair<std::string, std::string>>& images);
Json make_assistant_message(const AssistantTurn& turn);
Json make_tool_result_message(const std::string& call_id, const std::string& tool_name,
                              const std::string& text, bool is_error);
Json make_tool_result_message_extended(const std::string& call_id,
                                       const std::string& tool_name,
                                       const Json& content,
                                       bool is_error,
                                       const Json* details = nullptr,
                                       const Json* usage = nullptr,
                                       long long duration_ms = -1);
std::string message_text(const Json& message);

}  // namespace karta
