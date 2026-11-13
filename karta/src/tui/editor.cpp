#include "karta/tui/editor.h"

#include <algorithm>
#include <chrono>

namespace karta::tui {

Editor::Editor() {}

int Editor::cursor_pos() const {
  int pos = 0;
  for (int i = 0; i < cursor_line_ && i < static_cast<int>(lines_.size()); ++i) {
    pos += static_cast<int>(lines_[i].size()) + 1; // +1 for newline
  }
  pos += cursor_col_;
  return pos;
}
void Editor::set_kill_ring(const std::string& text) { kill_ring_text_ = text; }
int Editor::kill_ring_index() const { return kill_ring_idx_; }
void Editor::kill_ring_next() { if (kill_ring_idx_ < static_cast<int>(kill_ring_text_.size())) kill_ring_idx_++; }
void Editor::kill_ring_prev() { if (kill_ring_idx_ > 0) kill_ring_idx_--; }

void Editor::undo() {
  if (undo_stack_.empty()) return;
  end_edit_group();
  redo_stack_.push_back(text());
  std::string prev = undo_stack_.back();
  undo_stack_.pop_back();
  set_text_without_undo(prev);
}

void Editor::redo() {
  if (redo_stack_.empty()) return;
  end_edit_group();
  undo_stack_.push_back(text());
  std::string next = redo_stack_.back();
  redo_stack_.pop_back();
  set_text_without_undo(next);
}

void Editor::end_edit_group() {
  if (in_edit_group_) {
    in_edit_group_ = false;
    // Save the final state of this edit group
    if (undo_stack_.empty() || undo_stack_.back() != text()) {
      undo_stack_.push_back(text());
      redo_stack_.clear();
    }
  }
}

int Editor::undo_history_size() const { return static_cast<int>(undo_stack_.size()); }
int Editor::redo_history_size() const { return static_cast<int>(redo_stack_.size()); }

void Editor::set_text_without_undo(const std::string& text) {
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

void Editor::set_text(const std::string& text) {
  undo_stack_.push_back(this->text());
  redo_stack_.clear();
  set_text_without_undo(text);
}

std::string Editor::text() const {
  std::string result;
  for (size_t i = 0; i < lines_.size(); ++i) {
    result += lines_[i];
    if (i + 1 < lines_.size()) result += '\n';
  }
  return result;
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

void Editor::maybe_save_undo() {
  if (!in_edit_group_) {
    // Start new edit group
    in_edit_group_ = true;
    undo_stack_.push_back(text());
    redo_stack_.clear();
  }
  last_edit_time_ = std::chrono::steady_clock::now();
}

void Editor::update_cursor_pos() {
  if (on_change) on_change(text());
}

void Editor::insert(char c) {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    maybe_save_undo();
    lines_[cursor_line_].insert(lines_[cursor_line_].begin() + cursor_col_, c);
    ++cursor_col_;
    update_cursor_pos();
  }
}

void Editor::insert_string(const std::string& s) {
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    maybe_save_undo();
    lines_[cursor_line_].insert(cursor_col_, s);
    cursor_col_ += static_cast<int>(s.size());
    update_cursor_pos();
  }
}

void Editor::backspace() {
  end_edit_group();
  undo_stack_.push_back(text());
  redo_stack_.clear();

  if (cursor_col_ > 0) {
    lines_[cursor_line_].erase(cursor_col_ - 1, 1);
    --cursor_col_;
    update_cursor_pos();
  } else if (cursor_line_ > 0) {
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
  end_edit_group();
  undo_stack_.push_back(text());
  redo_stack_.clear();

  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size()) && cursor_col_ < static_cast<int>(lines_[cursor_line_].size())) {
    lines_[cursor_line_].erase(cursor_col_, 1);
    update_cursor_pos();
  } else if (cursor_line_ + 1 < static_cast<int>(lines_.size())) {
    lines_[cursor_line_] += lines_[cursor_line_ + 1];
    lines_.erase(lines_.begin() + cursor_line_ + 1);
    update_cursor_pos();
  }
}

void Editor::move_left() {
  end_edit_group();
  if (cursor_col_ > 0) {
    --cursor_col_;
  } else if (cursor_line_ > 0) {
    --cursor_line_;
    cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
  }
  update_cursor_pos();
}

void Editor::move_right() {
  end_edit_group();
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size()) && cursor_col_ < static_cast<int>(lines_[cursor_line_].size())) {
    ++cursor_col_;
  } else if (cursor_line_ + 1 < static_cast<int>(lines_.size())) {
    ++cursor_line_;
    cursor_col_ = 0;
  }
  update_cursor_pos();
}

void Editor::move_up() {
  end_edit_group();
  if (cursor_line_ > 0) {
    --cursor_line_;
    if (cursor_col_ > static_cast<int>(lines_[cursor_line_].size())) {
      cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
    }
  }
  update_cursor_pos();
}

void Editor::move_down() {
  end_edit_group();
  if (cursor_line_ + 1 < static_cast<int>(lines_.size())) {
    ++cursor_line_;
    if (cursor_col_ > static_cast<int>(lines_[cursor_line_].size())) {
      cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
    }
  }
  update_cursor_pos();
}

void Editor::move_home() {
  end_edit_group();
  cursor_col_ = 0;
  update_cursor_pos();
}

void Editor::move_end() {
  end_edit_group();
  if (cursor_line_ >= 0 && cursor_line_ < static_cast<int>(lines_.size())) {
    cursor_col_ = static_cast<int>(lines_[cursor_line_].size());
    update_cursor_pos();
  }
}

void Editor::new_line() {
  end_edit_group();
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
  end_edit_group();
  undo_stack_.push_back(text());
  redo_stack_.clear();

  lines_.clear();
  lines_.push_back("");
  cursor_line_ = 0;
  cursor_col_ = 0;
  update_cursor_pos();
}

std::vector<std::string> Editor::render(size_t width) const {
  std::vector<std::string> result;
  for (const auto& line : lines_) {
    if (width > 0 && line.size() >= width) {
      // Wrap long lines
      for (size_t i = 0; i < line.size(); i += width) {
        result.push_back(line.substr(i, width));
      }
    } else {
      result.push_back(line);
    }
  }
  if (result.empty()) result.push_back("");
  return result;
}

// Calculate screen cursor position (row, col) for given width
std::pair<int, int> Editor::cursor_screen_pos(size_t width) const {
  int screen_row = 0;
  for (int i = 0; i < cursor_line_; ++i) {
    const std::string& line = lines_[i];
    int wrapped_lines = std::max(1, static_cast<int>((line.size() + width - 1) / width));
    screen_row += wrapped_lines;
  }
  int screen_col = cursor_col_ % width;
  return {screen_row, screen_col};
}

}  // namespace karta::tui