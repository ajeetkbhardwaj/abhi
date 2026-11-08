// Karta — system prompt assembly (port of core/system-prompt.ts).
#pragma once

#include <string>
#include <vector>

#include "karta/tools/tools.h"

namespace karta {

std::string build_system_prompt(const std::vector<ToolDefinition>& tools, const std::string& cwd,
                                const std::string& custom_prompt,
                                const std::vector<std::string>& appended);

// Project instruction files (AGENTS.md, AGENTS.override.md, CLAUDE.md) from cwd upwards.
std::string load_project_instructions(const std::string& cwd);

}  // namespace karta
