#include "karta/tui/scroll-view.h"
#include <algorithm>

namespace karta::tui {

ScrollView::ScrollView() {}

void ScrollView::set_content(const std::vector<std::string>& lines) {
  content_ = lines;
  if (following_end_ && viewport_height_ > 0) {
    if (content_.size() > viewport_height_) {
      scroll_top_ = content_.size() - viewport_height_;
    } else {
      scroll_top_ = 0;
    }
  }
}

void ScrollView::scroll_by(int delta) {
  if (delta == 0) return;
  following_end_ = false;
  if (delta > 0) {
    scroll_top_ += static_cast<size_t>(delta);
  } else {
    if (scroll_top_ >= static_cast<size_t>(-delta)) {
      scroll_top_ -= static_cast<size_t>(-delta);
    } else {
      scroll_top_ = 0;
    }
  }
  size_t max_scroll = 0;
  if (content_.size() > viewport_height_) {
    max_scroll = content_.size() - viewport_height_;
  }
  if (scroll_top_ > max_scroll) scroll_top_ = max_scroll;
}

void ScrollView::scroll_to_top() {
  scroll_top_ = 0;
  following_end_ = false;
}

void ScrollView::scroll_to_bottom() {
  if (viewport_height_ > 0 && content_.size() > viewport_height_) {
    scroll_top_ = content_.size() - viewport_height_;
  } else {
    scroll_top_ = 0;
  }
  following_end_ = true;
}

std::vector<std::string> ScrollView::render(size_t width, size_t height) const {
  (void)width;
  std::vector<std::string> result;
  if (height == 0) return result;
  size_t max_scroll = 0;
  if (content_.size() > height) {
    max_scroll = content_.size() - height;
  }
  size_t start = scroll_top_;
  if (start > max_scroll) start = max_scroll;
  for (size_t i = 0; i < height && start + i < content_.size(); ++i) {
    result.push_back(content_[start + i]);
  }
  return result;
}

}  // namespace karta::tui
