// Karta — Clipboard integration implementation.

#include "karta/tui/clipboard.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace karta::tui {

static bool has_display() {
  return std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
}

static std::string exec_cmd(const std::string& cmd) {
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) return result;
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
    result += buffer;
  }
  return result;
}

static bool run_silent(const std::string& cmd) {
  return system((cmd + " 2>/dev/null").c_str()) == 0;
}

bool clipboard_copy(const std::string& text) {
  if (!has_display()) return false;
  
  // Try xclip first (Linux X11)
  if (run_silent("which xclip >/dev/null 2>&1")) {
    std::string cmd = "printf %s " + std::string("'") + text + std::string("'") + " | xclip -selection clipboard 2>/dev/null";
    return run_silent(cmd);
  }
  // Try xsel (Linux X11 alternative)
  if (run_silent("which xsel >/dev/null 2>&1")) {
    std::string cmd = "printf %s " + std::string("'") + text + std::string("'") + " | xsel --clipboard --input 2>/dev/null";
    return run_silent(cmd);
  }
  // Try pbcopy (macOS)
  if (run_silent("which pbcopy >/dev/null 2>&1")) {
    std::string cmd = "printf %s " + std::string("'") + text + std::string("'") + " | pbcopy 2>/dev/null";
    return run_silent(cmd);
  }
  // Try clip.exe (Windows)
  if (run_silent("where clip >/dev/null 2>&1")) {
    std::string cmd = "printf %s " + std::string("'") + text + std::string("'") + " | clip 2>/dev/null";
    return run_silent(cmd);
  }
  // Try wl-copy (Wayland)
  if (run_silent("which wl-copy >/dev/null 2>&1")) {
    std::string cmd = "printf %s " + std::string("'") + text + std::string("'") + " | wl-copy 2>/dev/null";
    return run_silent(cmd);
  }
  return false;
}

std::string clipboard_paste() {
  if (!has_display()) return "";
  
  // Try xclip first (Linux X11)
  if (run_silent("which xclip >/dev/null 2>&1")) {
    return exec_cmd("xclip -selection clipboard -o 2>/dev/null");
  }
  // Try xsel (Linux X11 alternative)
  if (run_silent("which xsel >/dev/null 2>&1")) {
    return exec_cmd("xsel --clipboard --output 2>/dev/null");
  }
  // Try pbpaste (macOS)
  if (run_silent("which pbpaste >/dev/null 2>&1")) {
    return exec_cmd("pbpaste 2>/dev/null");
  }
  // Try clip.exe (Windows) - not easily readable
  // Try wl-paste (Wayland)
  if (run_silent("which wl-paste >/dev/null 2>&1")) {
    return exec_cmd("wl-paste 2>/dev/null");
  }
  return "";
}

}  // namespace karta::tui