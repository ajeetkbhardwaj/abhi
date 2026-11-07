// Karta — ScrollView component (mirrors pi packages/tui/src/components/scroll-view.ts).
#pragma once

#include <vector>
#include <string>

namespace karta::tui {

class ScrollView {
 public:
  ScrollView();
  void set_content(const std::vector<std::string>& lines);
  void scroll_by(int delta);
  void scroll_to_top();
  void scroll_to_bottom();
  std::vector<std::string> render(size_t width, size_t height) const;

  size_t scroll_top() const { return scroll_top_; }
  bool is_following_end() const { return following_end_; }

 private:
  std::vector<std::string> content_;
  size_t scroll_top_ = 0;
  size_t viewport_height_ = 0;
  bool following_end_ = true;
};

}  // namespace karta::tui
