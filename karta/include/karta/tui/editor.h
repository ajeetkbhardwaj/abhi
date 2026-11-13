// Karta — Editor component (mirrors pi packages/tui/src/components/editor.ts structure).
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <deque>
#include <chrono>

namespace karta::tui {

class Editor {
 public:
  using OnSubmit = std::function<void(const std::string&)>;
  using OnChange = std::function<void(const std::string&)>;

  Editor();
  void set_text(const std::string& text);
  std::string text() const;
  void set_cursor(int line, int col);
  int cursor_line() const { return cursor_line_; }
  int cursor_col() const { return cursor_col_; }
  int cursor_pos() const { return cursor_pos(); } // Total position

  void insert(char c);
  void insert_string(const std::string& s);
  void backspace();
  void delete_char();
  void move_left();
  void move_right();
  void move_up();
  void move_down();
  void move_home();
  void move_end();
  void new_line(); // Enter key - new line
  void clear();

  // Kill-ring: kill and yank text
  std::string kill_ring() const;
  void set_kill_ring(const std::string& text);
  int kill_ring_index() const;
  void kill_ring_next();
  void kill_ring_prev();

  // Undo stack
  void undo();
  void redo();
  int undo_history_size() const;
  int redo_history_size() const;
  // End an editing group (call after a sequence of inserts)
  void end_edit_group();

  std::vector<std::string> render(size_t width) const;
  // Calculate screen cursor position (row, col) for given width (accounts for wrapping)
  std::pair<int, int> cursor_screen_pos(size_t width) const;

  OnSubmit on_submit;
  OnChange on_change;

 private:
  void set_text_without_undo(const std::string& text);
  void maybe_save_undo();

 private:
  std::vector<std::string> lines_;
  int cursor_line_ = 0;
  int cursor_col_ = 0;
  std::deque<std::string> undo_stack_;
  std::deque<std::string> redo_stack_;
  std::string kill_ring_text_;
  int kill_ring_idx_ = 0;
  
  // Edit grouping
  bool in_edit_group_ = false;
  std::chrono::steady_clock::time_point last_edit_time_;
  static constexpr std::chrono::milliseconds kEditGroupTimeout{300};
  void update_cursor_pos();
};

}  // namespace karta::tui