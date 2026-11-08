#include "karta/agent/prompt.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "karta/core/platform.h"

namespace fs = std::filesystem;

namespace karta {
namespace {

constexpr std::size_t kMaxInstructionBytes = 16 * 1024;
constexpr int kMaxInstructionFiles = 3;

std::string today() {
  const std::string stamp = iso8601_now();
  return stamp.substr(0, 10);
}

std::string tool_index(const std::vector<ToolDefinition>& tools) {
  std::string out;
  for (const ToolDefinition& tool : tools) {
    std::string first_line = tool.description;
    const std::size_t newline = first_line.find('\n');
    if (newline != std::string::npos) first_line.resize(newline);
    out += "- " + tool.name + ": " + first_line + "\n";
  }
  if (out.empty()) out = "- (no tools enabled)\n";
  return out;
}

std::string read_capped(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return "";
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  std::string content = buffer.str();
  if (content.size() > kMaxInstructionBytes) {
    content.resize(kMaxInstructionBytes);
    content += "\n\n[truncated by karta]";
  }
  return content;
}

}  // namespace

std::string load_project_instructions(const std::string& cwd) {
  std::string combined;
  int found = 0;
  std::error_code error;
  fs::path directory = fs::absolute(cwd, error);

  for (int depth = 0; depth < 5 && !directory.empty() && found < kMaxInstructionFiles; ++depth) {
    for (const char* name : {"AGENTS.override.md", "AGENTS.md", "CLAUDE.md"}) {
      const fs::path candidate = directory / name;
      if (!fs::is_regular_file(candidate, error)) {
        error.clear();
        continue;
      }
      const std::string content = read_capped(candidate);
      if (content.empty()) continue;
      combined += "<project_instructions path=\"" + candidate.generic_string() + "\">\n" + content +
                  "\n</project_instructions>\n\n";
      ++found;
      break;
    }
    const fs::path parent = directory.parent_path();
    if (parent == directory) break;
    directory = parent;
  }
  return combined;
}

std::string build_system_prompt(const std::vector<ToolDefinition>& tools, const std::string& cwd,
                                const std::string& custom_prompt,
                                const std::vector<std::string>& appended) {
  std::string prompt;
  const std::string instructions = load_project_instructions(cwd);

  if (!custom_prompt.empty()) {
    prompt = custom_prompt;
    if (!prompt.empty() && prompt.back() != '\n') prompt.push_back('\n');
  } else {
    prompt +=
        "You are karta, a coding agent running in the user's terminal. You help with software "
        "engineering: reading and changing code, running commands, debugging, and explaining how "
        "things work.\n\n";
    prompt +=
        "Be direct and concise. Do the work instead of describing it. Prefer the dedicated tools "
        "over shell equivalents: read before editing, use edit for targeted changes, and verify "
        "changes by running the project's own checks.\n\n";
    prompt += "Working directory: " + cwd + "\n";
    prompt += "Platform: " + os_description() + "\n";
    prompt += "Date: " + today() + "\n\n";
    prompt += "<tools>\n" + tool_index(tools) + "</tools>\n\n";
    prompt +=
        "<guidelines>\n"
        "- Read a file before changing it, and match the surrounding style.\n"
        "- Keep edits minimal and scoped to the request; no unrequested refactors.\n"
        "- Use grep/find/ls to locate code instead of guessing paths.\n"
        "- Never invent command output; run the command.\n"
        "- Report failures honestly, including checks you could not run.\n"
        "- Do not commit, push, or deploy unless the user asks.\n"
        "</guidelines>\n";
  }

  if (!instructions.empty()) prompt += "\n" + instructions;
  for (const std::string& extra : appended) {
    if (extra.empty()) continue;
    prompt += "\n" + extra;
    if (prompt.back() != '\n') prompt.push_back('\n');
  }
  return prompt;
}

}  // namespace karta
