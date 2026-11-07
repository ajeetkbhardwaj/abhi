#include "tui/editor.h"
#include <algorithm>

namespace karta::tui {

Editor::Editor() {}

void Editor::set_text(const std::string& text) {
  text_ = text;
  cursor_ = static_cast<int>(text_.size());
  if (on_change) on_change(text_);
}

void Editor::set_cursor(int pos) {
  if (pos < 0) pos = 0;
  if (pos > static_cast<int>(text_.size())) pos = static_cast<int>(text_.size());
  cursor_ = pos;
}

void Editor::insert(char c) {
  text_.insert(text_.begin() + cursor_, c);
  ++cursor_;
  if (on_change) on_change(text_);
}

void Editor::insert_string(const std::string& s) {
  text_.insert(cursor_, s);
  cursor_ += static_cast<int>(s.size());
  if (on_change) on_change(text_);
}

void Editor::backspace() {
  if (cursor_ <= 0) return;
  --cursor_;
  text_.erase(text_.begin() + cursor_);
  if (on_change) on_change(text_);
}

void Editor::delete_char() {
  if (cursor_ >= static_cast<int>(text_.size())) return;
  text_.erase(text_.begin() + cursor_);
  if (on_change) on_change(text_);
}

void Editor::move_left() {
  if (cursor_ > 0) --cursor_;
}

void Editor::move_right() {
  if (cursor_ < static_cast<int>(text_.size())) ++cursor_;
}

void Editor::move_home() { cursor_ = 0; }
void Editor::move_end() { cursor_ = static_cast<int>(text_.size()); }
void Editor::clear() {
  text_.clear();
  cursor_ = 0;
  if (on_change) on_change(text_);
}

std::vector<std::string> Editor::render(size_t width) const {
  (void)width;
  std::vector<std::string> lines;
  lines.push_back(text_);
  return lines;
}

}  // namespace karta::tui
