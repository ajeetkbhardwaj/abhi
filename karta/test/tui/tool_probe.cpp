// Smoke harness: runs one built-in tool directly.
// Build: g++ -std=c++17 -I src test/tool_probe.cpp src/tools.cpp src/platform.cpp src/json.cpp -lcurl -pthread
#include <iostream>

#include "karta/core/json.h"
#include "karta/tools/tools.h"

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: tool_probe <tool> <cwd> <json-args>\n";
    return 2;
  }
  std::string parse_error;
  const karta::Json args = karta::Json::parse(argv[3], &parse_error);
  if (!parse_error.empty()) {
    std::cerr << "bad json: " << parse_error << "\n";
    return 2;
  }
  karta::ToolContext context{argv[2]};
  for (karta::ToolDefinition& tool : karta::builtin_tools()) {
    if (tool.name != argv[1]) continue;
    const karta::ToolOutcome outcome = tool.execute(args, context);
    std::cout << (outcome.is_error ? "ERROR" : "OK") << "\n" << outcome.text << "\n";
    return 0;
  }
  std::cerr << "unknown tool\n";
  return 1;
}
