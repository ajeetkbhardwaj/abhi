// Karta — command line surface (port of coding-agent/src/cli).
#pragma once

#include <string>
#include <vector>

namespace karta {

struct CliOptions {
  std::string provider;
  std::string model;
  std::string api_key;
  std::string system_prompt;
  std::vector<std::string> append_system_prompt;
  std::string thinking = "low"; // 0 off, 1 default/low, 2 medium, 3 high
  std::string mode = "text";  // text | json | rpc

  std::string session_path;
  std::string session_id;
  std::string session_dir;
  std::string session_name;
  std::string fork_path;

  std::vector<std::string> tool_allow;
  std::vector<std::string> tool_exclude;
  std::vector<std::string> prompts;

  // MCP servers: "name=command" (args may follow after spaces in the same value, or use --mcp-arg).
  struct McpServerSpec {
    std::string name;
    std::string command;
    std::vector<std::string> args;
  };
  std::vector<McpServerSpec> mcp_servers;

  bool provider_set = false;
  bool model_set = false;
  bool print = false;
  bool continue_session = false;
  bool no_session = false;
  bool no_tools = false;
  bool no_builtin_tools = false;
  bool help = false;
  bool version = false;
  bool list_models = false;
  bool verbose = false;
  bool tui = false;

  int max_turns = 0;
  long long compact_threshold = 100000;
};

CliOptions parse_args(int argc, char** argv);
void print_help();
int run_cli(const CliOptions& options);

}  // namespace karta
