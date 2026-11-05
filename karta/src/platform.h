// Karta — small portability layer (processes, paths, console, time).
#pragma once

#include <cstddef>
#include <string>

namespace karta {

struct ProcessResult {
  int exit_code = -1;
  std::string output;            // Head-truncated combined stdout/stderr.
  bool timed_out = false;
  bool truncated = false;
  std::string full_output_path;  // Set when output was truncated.
};

// Runs `command` through the platform shell in `cwd`, capturing stdout+stderr.
// A timeout of 0 means "no timeout".
ProcessResult run_shell(const std::string& command, const std::string& cwd, int timeout_seconds,
                        std::size_t max_bytes);

std::string home_directory();
std::string current_working_directory();
std::string iso8601_now();
long long epoch_millis();
std::string os_description();
void enable_utf8_console();

}  // namespace karta
