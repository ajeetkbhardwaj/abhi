// Karta — pi select-list component.
//
// Mirrors `reference/pi/packages/tui/src/components/select-list.ts`:
//  - numbered primary column, label + optional description
//  - bounded visible window centered on the selection
//  - filtering (setFilter), cursor wrap, mouse press/click, keyboard confirm/cancel
//
// Karta uses this for /model and /provider catalogs.

#include "karta/tui/select-list.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "karta/tui/screen.h"

namespace karta::tui {
namespace {
constexpr int kDefaultPrimaryWidth = 32;

std::string normalize_to_single_line(const std::string& text) {
  std::string result;
  bool in_space = true;
  for (char c : text) {
    if (c == '\r' || c == '\n') {
      in_space = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space) result += ' ';
      in_space = true;
    } else {
      result += c;
      in_space = false;
    }
  }
  while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
    result.pop_back();
  return result;
}

std::size_t visible_width(const std::string& text) {
  std::size_t width = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xC0) != 0x80) ++width;
  }
  return width;
}

std::string truncate_to_width(const std::string& text, std::size_t max_width) {
  if (visible_width(text) <= max_width) return text;
  std::size_t kept = 0;
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    const std::size_t length = (lead >= 0xF0) ? 4 : (lead >= 0xE0 ? 3 : (lead >= 0xC0 ? 2 : 1));
    const std::size_t chunk = (i + length > text.size()) ? text.size() - i : length;
    if (kept + chunk > max_width) break;
    kept += chunk;
    i += chunk;
  }
  return text.substr(0, kept) + (max_width > 0 ? "…" : "");
}
}  // namespace

SelectList::SelectList(std::vector<SelectItem> items, int max_visible, const SelectListTheme& theme,
                       const SelectListLayoutOptions& layout,
                       const OnSelect& on_select)
    : items_(std::move(items)),
      filtered_items_(items_),
      max_visible_(max_visible),
      theme_(theme),
      layout_(layout),
      on_select_(on_select) {}

void SelectList::set_filter(const std::string& filter) {
  filtered_items_.clear();
  for (const auto& item : items_) {
    const std::string value = item.value.empty() ? item.label : item.value;
    if (value.size() >= filter.size() &&
        0 == value.compare(0, filter.size(), filter)) {
      filtered_items_.push_back(item);
    }
  }
  selected_index_ = 0;
}

void SelectList::set_selected_index(int index) {
  if (filtered_items_.empty()) return;
  selected_index_ = std::max(0, std::min(index, static_cast<int>(filtered_items_.size()) - 1));
}

void SelectList::invalidate() {}

std::vector<std::string> SelectList::render(std::size_t width) const {
  const int actual_width = static_cast<int>(width);
  std::vector<std::string> lines;

  if (filtered_items_.empty()) {
    lines.push_back(theme_.no_match("  No matching commands"));
    return lines;
  }

  const int primary_column_width = get_primary_column_width(actual_width);

  const auto [start_index, end_index] = get_visible_range();
  for (int i = start_index; i < end_index; ++i) {
    const SelectItem& item = filtered_items_[static_cast<std::size_t>(i)];
    const bool is_selected = i == selected_index_;
    const std::string description =
        !item.description.empty() ? normalize_to_single_line(item.description) : std::string();
    const std::vector<std::string> rendered =
        render_item(item, is_selected, actual_width, description, primary_column_width);
    lines.insert(lines.end(), rendered.begin(), rendered.end());
  }

  if (start_index > 0 || end_index < static_cast<int>(filtered_items_.size())) {
    const std::string scroll_text =
        "  (" + std::to_string(selected_index_ + 1) + "/" +
        std::to_string(filtered_items_.size()) + ")";
    lines.push_back(theme_.scroll_info(truncate_to_width(scroll_text,
                                                         actual_width - 2)));
  }

  return lines;
}

void SelectList::handle_mouse(MouseEvent event) {
  if (filtered_items_.empty()) return;

  if (event.type == MouseEvent::MouseEventType::Wheel && event.wheel_delta != 0) {
    const int delta = event.wheel_delta < 0 ? -1 : 1;
    const int previous_index = selected_index_;
    selected_index_ = std::max(0, std::min(
        static_cast<int>(filtered_items_.size()) - 1, selected_index_ + delta));
    if (selected_index_ != previous_index) {
      if (on_select_) on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
    }
    return;
  }

  if (event.button != MouseEvent::MouseButton::Left || (event.type != MouseEvent::MouseEventType::Press &&
                                                         event.type != MouseEvent::MouseEventType::Click))
    return;

  const auto [start_index, end_index] = get_visible_range();
  const int item_index = start_index + event.y;
  if (item_index < start_index || item_index >= end_index) return;

  if (event.type == MouseEvent::MouseEventType::Press) {
    mouse_pressed_index_ = item_index;
    if (selected_index_ != item_index) {
      selected_index_ = item_index;
      if (on_select_) on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
    }
    return;
  }

  if (event.type == MouseEvent::MouseEventType::Click) {
    const int clicked_index = mouse_pressed_index_.value_or(item_index);
    mouse_pressed_index_ = std::nullopt;
    const bool changed = selected_index_ != clicked_index;
    selected_index_ = clicked_index;
    if (changed && on_select_) on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
  }
}

void SelectList::handle_input(const std::string& data) {
  if (filtered_items_.empty()) return;

  // Up/down: wrap around the filtered list.
  if (data == "\x1b[A" || data == "\x1bOA") {  // up arrow
    selected_index_ = selected_index_ <= 0 ? static_cast<int>(filtered_items_.size()) - 1
                                           : selected_index_ - 1;
    if (on_select_) on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
    return;
  }
  if (data == "\x1b[B" || data == "\x1bOB") {  // down arrow
    selected_index_ = selected_index_ >= static_cast<int>(filtered_items_.size()) - 1 ? 0
                                                                                      : selected_index_ + 1;
    if (on_select_) on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
    return;
  }

  // Digit keys jump to a numbered position (1-based).
  if (data.size() == 1 && data[0] >= '1' && data[0] <= '9') {
    const int page_index = data[0] - '1';
    if (page_index >= 0 && page_index < static_cast<int>(filtered_items_.size()) &&
        page_index != selected_index_) {
      selected_index_ = page_index;
      if (on_select_) on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
    }
    return;
  }

  // Enter confirms the selection.
  if (data == "\n" || data == "\r") {
    if (!filtered_items_.empty() && on_select_) {
      on_select_(filtered_items_[static_cast<std::size_t>(selected_index_)]);
    }
    return;
  }

  // Escape cancels.
  if (data == "\x1b") {
    if (on_cancel_) on_cancel_();
    return;
  }
}

std::pair<int, int> SelectList::get_visible_range() const {
  if (filtered_items_.empty()) return {0, 0};
  const int center = selected_index_;
  const int half = max_visible_ / 2;
  int start_index = center - half;
  if (start_index < 0) start_index = 0;
  const int end_index = start_index + max_visible_;
  if (end_index > static_cast<int>(filtered_items_.size())) {
    start_index = filtered_items_.size() - max_visible_;
    if (start_index < 0) start_index = 0;
  }
  return {start_index, std::min(end_index, static_cast<int>(filtered_items_.size()))};
}

int SelectList::get_primary_column_width(int width) const {
  const int requested_min = layout_.min_primary_column_width.value_or(kDefaultPrimaryWidth);
  const int requested_max = layout_.max_primary_column_width.value_or(kDefaultPrimaryWidth);
  const int min = std::max(1, std::min(requested_min, requested_max));
  const int max = std::max(1, std::max(requested_min, requested_max));

  int widest = 0;
  for (const auto& item : filtered_items_) {
    widest = std::max(widest, static_cast<int>(visible_width(get_display_value(item))));
  }
  widest += 2;  // primary column gap

  const int column_width = std::max(min, std::min(widest, max));
  return column_width;
}

std::string SelectList::get_display_value(const SelectItem& item) const {
  return item.label.empty() ? item.value : item.label;
}

std::string SelectList::truncate_primary(const SelectItem& item, bool is_selected, int max_width,
                                         int effective_primary_column_width) const {
  const std::string value = get_display_value(item);
  if (layout_.truncate_primary) {
    SelectListTruncatePrimaryContext ctx{value, max_width, effective_primary_column_width, item, is_selected};
    return (*layout_.truncate_primary)(ctx);
  }
  return truncate_to_width(value, static_cast<std::size_t>(max_width));
}

std::vector<std::string> SelectList::render_item(const SelectItem& item, bool is_selected,
                                    int width, const std::string& description,
                                    int primary_column_width) const {
  const std::string prefix = is_selected ? "→ " : "  ";
  const int prefix_width = visible_width(prefix);

  if (!description.empty() && width > 40) {
    const int effective_primary_column_width =
        std::max(1, std::min(primary_column_width, width - prefix_width - 4));
    const int max_primary_width = std::max(1, effective_primary_column_width - 2);
    const std::string truncated_value = truncate_primary(item, is_selected, max_primary_width,
                                                         effective_primary_column_width);
    const int truncated_value_width = visible_width(truncated_value);
    const std::string spacing =
        std::string(std::max(1, effective_primary_column_width - truncated_value_width), ' ');
    const int description_start = prefix_width + truncated_value_width + spacing.size();
    const int remaining_width = width - description_start - 2;

    if (remaining_width > 10) {
      const std::string truncated_desc =
          truncate_to_width(description, static_cast<std::size_t>(remaining_width));
      if (is_selected) {
        return {theme_.selected_text(prefix + truncated_value + spacing + truncated_desc)};
      }
      return {prefix + truncated_value + theme_.description(spacing + truncated_desc)};
    }
  }

  const int max_width = width - prefix_width - 2;
  const std::string truncated_value = truncate_primary(item, is_selected, max_width, max_width);
  if (is_selected)
    return {theme_.selected_text(prefix + truncated_value)};
  return {prefix + truncated_value};
}

}  // namespace karta::tui
