// Karta — Editor component (mirrors pi packages/tui/src/components/editor.ts structure).
#pragma once

#include <string>
#include <vector>
#include <functional>

#include "karta/tui/screen.h"

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

  std::vector<std::string> render(size_t width) const;

  OnSubmit on_submit;
  OnChange on_change;

 private:
  std::vector<std::string> lines_;
  int cursor_line_ = 0;
  int cursor_col_ = 0;
  void update_cursor_pos();
};

}  // namespace karta::tui
