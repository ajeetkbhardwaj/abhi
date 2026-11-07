#include "tui/text.h"
#include <algorithm>

namespace karta::tui {
namespace {

std::vector<std::string> wrap_text_local(const std::string& text, size_t width) {
  if (width == 0) width = 1;
  std::vector<std::string> lines;
  std::string current;
  size_t current_width = 0;
  for (size_t i = 0; i < text.size();) {
    unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t length = 1;
    if (lead >= 0xF0) length = 4;
    else if (lead >= 0xE0) length = 3;
    else if (lead >= 0xC0) length = 2;
    if (i + length > text.size()) length = text.size() - i;
    std::string chunk = text.substr(i, length);
    i += length;
    if (chunk == "\n") {
      lines.push_back(current);
      current.clear();
      current_width = 0;
      continue;
    }
    if (current_width + 1 > width) {
      lines.push_back(current);
      current.clear();
      current_width = 0;
    }
    current += chunk;
    ++current_width;
  }
  if (current.empty() && (text.empty() || text.back() == '\n')) {
    // keep empty line representation consistent
  }
  lines.push_back(current);
  return lines;
}

}

Text::Text(const std::string& text, Style style) : text_(text), style_(style) {}

void Text::set_text(const std::string& text) { text_ = text; }

std::vector<std::string> Text::render(size_t width) const {
  if (text_.empty()) return {""};
  return wrap_text_local(text_, width);
}

}  // namespace karta::tui
