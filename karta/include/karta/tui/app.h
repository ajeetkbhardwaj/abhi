// Karta — interactive terminal application (chat loop, /login, /model).
#pragma once

#include <string>

#include "karta/agent/agent.h"

namespace karta {

class Session;

struct TuiAppOptions {
  AgentOptions agent;
  Session* session = nullptr;  // optional, non-owning
};

// Runs the terminal UI until the user exits. Returns a process exit code.
int run_tui_app(TuiAppOptions options);

}  // namespace karta
