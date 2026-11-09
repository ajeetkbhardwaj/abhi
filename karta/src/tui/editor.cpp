#include "karta/tui/editor.h"
#include <algorithm>

namespace karta::tui {

Editor::Editor() {}

void Editor::set_text(const std::string& text) {
  lines_.clear();
  size_t start = 0;
  while (true) {
    size_t pos = text.find('\n', start);
    if (pos == std::string::npos) {
      lines_.push_back(text.substr(start));
      break;
    }
    lines_.push_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  if (lines_.empty()) lines_.push_back("");
  cursor_line_ = static_cast<int>(lines_.size()) - 1;
  cursor_col_ = static_cast<int>(lines_.back().size());
  if (on_change) on_change(this->text());
}

void Editor::set_cursor(int line, int col) {
  if (line < 0) line = 0;
  if (line >= static_cast<int>(lines_.size())) line = static_cast<int>(lines_.size()) - 1;
  cursor_line_ = line;
  if (col < 0) col = 0;
  if (col > static_cast<int>(lines_[line].size())) col = static_cast<int>(lines_[line].size());
  cursor_col_ = col;
  update_cursor_pos();
}

void Editor::update_cursor_pos() {
  if (on_change) on_change(text());
}

std::string Editor::text() const {
  std::string result;
  for (size_t i = 0; i < lines_.size(); ++i) {
    result += lines_[i];
    if (i + 1 < lines_.size()) result += '\n';
  }
  return result;
}

void Editor::insert(char c) {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    lines_[cursor_line_].insert(lines_[cursor_line_].begin() + cursor_col_, c);
    ++cursor_col_;
    update_cursor_pos();
  }
}

void Editor::insert_string(const std::string& s) {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    lines_[cursor_line_].insert(cursor_col_, s);
    cursor_col_ += static_cast<int>(s.size());
    update_cursor_pos();
  }
}

void Editor::backspace() {
  if (cursor_col_ > 0) {
    lines_[cursor_line_].erase(cursor_col_ - 1, 1);
    --cursor_col_;
    update_cursor_pos();
  } else if (cursor_line_ > 0) {
    // Join with previous line
    std::string prev = lines_[cursor_line_ - 1];
    std::string curr = lines_[cursor_line_];
    cursor_col_ = static_cast<int>(prev.size());
    lines_[cursor_line_ - 1] = prev + curr;
    lines_.erase(lines_.begin() + cursor_line_);
    --cursor_line_;
    update_cursor_pos();
  }
}

void Editor::delete_char() {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size()) && cursor_col_ < static_cast<int>(lines_[cursor_line_].size())) {
    lines_[cursor_line_].erase(cursor_col_, 1);
    update_cursor_pos();
  } else if (cursor_line_ + 1 < static_cast<int>(lines_.size())) {
    // Join with next line
    lines_[cursor_line_] += lines_[cursor_line_ + 1];
    lines_.erase(lines_.begin() + cursor_line_ + 1);
    update_cursor_pos();
  }
}

void Editor::move_left() {
  if (cursor_col_ > 0) {
    --cursor_col_;
  } else if (cursor_line_ > 0) {
    --cursor_line_;
    cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
  }
  update_cursor_pos();
}

void Editor::move_right() {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size()) && cursor_col_ < static_cast<int>(lines_[cursor_line_].size())) {
    ++cursor_col_;
  } else if (cursor_line_ + 1 < static_cast<int>(lines_.size())) {
    ++cursor_line_;
    cursor_col_ = 0;
  }
  update_cursor_pos();
}

void Editor::move_up() {
  if (cursor_line_ > 0) {
    --cursor_line_;
    if (cursor_col_ > static_cast<int>(lines_[cursor_line_].size())) {
      cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
    }
    update_cursor_pos();
  }
}

void Editor::move_down() {
  if (cursor_line_ + 1 < static_cast<int>(lines_.size())) {
    ++cursor_line_;
    if (cursor_col_ > static_cast<int>(lines_[cursor_line_].size())) {
      cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
    }
    update_cursor_pos();
  }
}

void Editor::move_home() {
  cursor_col_ = 0;
  update_cursor_pos();
}

void Editor::move_end() {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
    update_cursor_pos();
  }
}

void Editor::new_line() {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    std::string curr = lines_[cursor_line_];
    std::string left = curr.substr(0, cursor_col_);
    std::string right = curr.substr(cursor_col_);
    lines_[cursor_line_] = left;
    lines_.insert(lines_.begin() + cursor_line_ + 1, right);
    ++cursor_line_;
    cursor_col_ = 0;
    update_cursor_pos();
  }
}

void Editor::clear() {
  lines_.clear();
  lines_.push_back("");
  cursor_line_ = 0;
  cursor_col_ = 0;
  update_cursor_pos();
}

std::vector<std::string> Editor::render(size_t width) const {
  std::vector<std::string> result;
  for (const auto& line : lines_) {
    result.push_back(line);
  }
  if (result.empty()) result.push_back("");
  return result;
}

}  // namespace karta::tui