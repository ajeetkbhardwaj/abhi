// Karta — Clipboard integration (system clipboard copy/paste).
#pragma once

#include <string>

namespace karta::tui {

// Copy text to system clipboard.
// Returns true on success.
bool clipboard_copy(const std::string& text);

// Paste text from system clipboard.
// Returns empty string on failure or if clipboard is empty.
std::string clipboard_paste();

}  // namespace karta::tui