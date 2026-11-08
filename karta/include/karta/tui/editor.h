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
  const std::string& text() const { return text_; }
  void set_cursor(int pos);
  int cursor() const { return cursor_; }

  void insert(char c);
  void insert_string(const std::string& s);
  void backspace();
  void delete_char();
  void move_left();
  void move_right();
  void move_home();
  void move_end();
  void clear();

  std::vector<std::string> render(size_t width) const;

  OnSubmit on_submit;
  OnChange on_change;

 private:
  std::string text_;
  int cursor_ = 0;
};

}  // namespace karta::tui
