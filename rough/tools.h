// Karta — built-in toolset (port of packages/coding-agent/src/core/tools).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "json.h"

namespace karta {

struct ToolContext {
  std::string cwd;
};

struct ToolOutcome {
  std::string text;
  bool is_error = false;
  bool terminate = false;
};

struct ToolDefinition {
  std::string name;
  std::string description;
  Json parameters;  // JSON Schema object
  std::function<ToolOutcome(const Json&, ToolContext&)> execute;
};

std::vector<ToolDefinition> builtin_tools();
const ToolDefinition* find_tool(const std::vector<ToolDefinition>& tools, const std::string& name);

// OpenAI-style function declaration consumed by the provider layer.
Json tool_declaration(const ToolDefinition& tool);

// Shared limits, matching pi's truncate.ts constants.
constexpr std::size_t kMaxLines = 2000;
constexpr std::size_t kMaxBytes = 50 * 1024;

}  // namespace karta
