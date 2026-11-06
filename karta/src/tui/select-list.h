// Karta — pi select-list component.
//
// Mirrors `reference/pi/packages/tui/src/components/select-list.ts`:
//  numbered primary column + optional description, bounded visible window
//  centered on the selection, filtering, mouse and keyboard navigation.
#pragma once

#include <optional>
#include <string>
#include <vector>
#include <functional>

#include "tui/screen.h"

namespace karta::tui {

struct SelectItem {
  std::string value;       // stored value / id
  std::string label;       // displayed primary text
  std::string description; // optional secondary line
};

struct SelectListTruncatePrimaryContext {
  std::string text;
  int max_width;
  int column_width;
  SelectItem item;
  bool is_selected;
};

struct SelectListLayoutOptions {
  std::optional<int> min_primary_column_width = std::nullopt;
  std::optional<int> max_primary_column_width = std::nullopt;
  std::optional<std::function<std::string(SelectListTruncatePrimaryContext)>>
      truncate_primary = std::nullopt;
};

struct SelectListTheme {
  std::function<std::string(const std::string&)> no_match = [](auto text) { return text; };
  std::function<std::string(const std::string&)> selected_text =
      [](auto text) { return text; };
  std::function<std::string(const std::string&)> description =
      [](auto text) { return text; };
  std::function<std::string(const std::string&)> scroll_info =
      [](auto text) { return text; };
};

// Event over mouse events to keep the component portable outside ncurses.
struct MouseEvent {
  enum class MouseEventType { Press, Release, Move, Drag, Click, Wheel };
  enum class MouseButton { Left, Middle, Right, None };
  MouseEventType type;
  MouseButton button = MouseButton::None;
  int x = 0;    // local to the component
  int y = 0;    // local to the component
  int width = 0;
  int height = 0;
  int wheel_delta = 0;
};

class SelectList {
 public:
  using OnSelect = std::function<void(const SelectItem&)>;
  using OnCancel = std::function<void()>;

  SelectList(std::vector<SelectItem> items, int max_visible, const SelectListTheme& theme,
             const SelectListLayoutOptions& layout = SelectListLayoutOptions{},
             const OnSelect& on_select = OnSelect());

  void set_on_select(const OnSelect& on_select) { on_select_ = on_select; }

  void set_filter(const std::string& filter);
  void set_selected_index(int index);
  void invalidate();

  // Renders the visible window as text lines at `width` columns.
  std::vector<std::string> render(std::size_t width) const;

  // Mouse events (only left button press/click/wheel are handled).
  void handle_mouse(MouseEvent event);

  // Keyboard events forwarded from the keybindings manager.
  void handle_input(const std::string& data);

  void set_on_select(OnSelect on_select) { on_select_ = std::move(on_select); }
  void set_on_cancel(OnCancel on_cancel) { on_cancel_ = std::move(on_cancel); }

  const std::vector<SelectItem>& items() const { return items_; }
  const std::vector<SelectItem>& filtered_items() const { return filtered_items_; }
  int selected_index() const { return selected_index_; }

 private:
  std::vector<std::string> render_item(const SelectItem& item, bool is_selected,
                                       int width, const std::string& description,
                                       int primary_column_width) const;
  std::string get_display_value(const SelectItem& item) const;
  std::string truncate_primary(const SelectItem& item, bool is_selected, int max_width,
                               int column_width) const;
  std::pair<int, int> get_visible_range() const;
  int get_primary_column_width(int width) const;

  std::vector<SelectItem> items_;
  std::vector<SelectItem> filtered_items_;
  int selected_index_ = 0;
  int mouse_pressed_index_ = -1;
  int max_visible_ = 5;
  SelectListTheme theme_;
  SelectListLayoutOptions layout_;

  OnSelect on_select_;
  OnCancel on_cancel_;
};

}  // namespace karta::tui
