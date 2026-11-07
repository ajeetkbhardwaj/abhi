// Karta — Text component (mirrors pi packages/tui/src/components/text.ts).
#pragma once

#include <string>
#include <vector>

#include "tui/screen.h"

namespace karta::tui {

class Text {
 public:
  Text(const std::string& text = "", Style style = Style::Normal);
  void set_text(const std::string& text);
  const std::string& text() const { return text_; }
  void set_style(Style style) { style_ = style; }
  std::vector<std::string> render(size_t width) const;
  void invalidate() {}

 private:
  std::string text_;
  Style style_;
};

}  // namespace karta::tui
