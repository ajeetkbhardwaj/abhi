#include "tui.h"

#include <curses.h>
#include <panel.h>
#include <signal.h>
#include <unistd.h>

#include <string>
#include <iostream>

namespace karta::tui {

// Internal NCURSES window wrapper for RAII management
struct Tui::NcursesWindow {
  WINDOW* win = nullptr;
  NcursesWindow() = default;
  explicit NcursesWindow(WINDOW* w) : win(w) {}
  ~NcursesWindow() { if (win) delwin(win); }
  void reset(WINDOW* w) { if (win) delwin(win); win = w; }
};

// Static handler to forward ncurses events to a Tui instance
// We keep a global weak reference; the Tui instance outlives the event loop.
static thread_local Tui* tui_instance = nullptr;

static void handle_sigwinch(int) {
  // Signal received; next pump_input or render will query new dimensions.
  (void)tui_instance;
}

Tui::Tui() {
  // Register the thread-local instance if not already set
  if (!tui_instance) {
    tui_instance = this;
  }
}

Tui::~Tui() {
  stop();
}

bool Tui::init(const TuiOptions& opts) {
  // Guard against double-init
  if (running_) return true;

  // Install signal handler for resize
  signal(SIGWINCH, handle_sigwinch);

  // Initialize ncurses
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);  // non-blocking input
  timeout(0);             // poll with 0ms delay
  curs_set(cursor_visible_ ? 1 : 0);
  start_color();
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);

  // Get initial dimensions
  getmaxyx(stdscr, rows_, cols_);

  // Enforce minimum size
  rows_ = std::max(rows_, opts.min_rows);
  cols_ = std::max(cols_, opts.min_cols);

  // Create the main window
  win_.reset(newwin(rows_, cols_, 0, 0));
  keypad(win_->win, TRUE);
  scrollok(win_->win, TRUE);

  running_ = true;
  return true;
}

void Tui::stop() {
  if (!running_) return;
  running_ = false;

  // Clean up ncurses
  if (win_) {
    delwin(win_->win);
    win_.reset();
  }
  endwin();
  cursor_visible_ = true;
}

bool Tui::pump_input(InputEvent& event) {
  if (!running_) return false;

  int ch = wgetch(win_->win);
  if (ch == ERR) return false;

  // Handle mouse events
  if (ch == MOUSE_EVENT) {
    MEVENT m;
    if (getmouse(&m) == OK) {
      event.type = InputEvent::Type::Mouse;
      event.x = m.x;
      event.y = m.y;
      event.shift = (m.bstate & BSHIFT) != 0;
      event.ctrl = (m.bstate & BCTRL) != 0;
      event.alt = (m.bstate & BALT) != 0;

      // Map button
      switch (m.bstate & BUTTON_RELEASE_MASK) {
        case BUTTON1_RELEASED:
          event.key = "mouse_1"; break;
        case BUTTON1_CLICKED:
          event.key = "mouse_1_click"; break;
        case BUTTON2_RELEASED:
          event.key = "mouse_2"; break;
        case BUTTON2_CLICKED:
          event.key = "mouse_2_click"; break;
        case BUTTON3_RELEASED:
          event.key = "mouse_3"; break;
        case BUTTON3_CLICKED:
          event.key = "mouse_3_click"; break;
        case BUTTON4_RELEASED:
          event.key = "mouse_scroll_up"; break;
        case BUTTON4_CLICKED:
          event.key = "mouse_scroll_up_click"; break;
        case BUTTON5_RELEASED:
          event.key = "mouse_scroll_down"; break;
        case BUTTON5_CLICKED:
          event.key = "mouse_scroll_down_click"; break;
        default:
          event.key = "mouse_other"; break;
      }
      return true;
    }
    return false;
  }

  // Handle keyboard events
  event.type = InputEvent::Type::Key;
  switch (ch) {
    case KEY_F(1):  event.key = "f1"; break;
    case KEY_F(2):  event.key = "f2"; break;
    case KEY_F(3):  event.key = "f3"; break;
    case KEY_F(4):  event.key = "f4"; break;
    case KEY_F(5):  event.key = "f5"; break;
    case KEY_F(6):  event.key = "f6"; break;
    case KEY_F(7):  event.key = "f7"; break;
    case KEY_F(8):  event.key = "f8"; break;
    case KEY_F(9):  event.key = "f9"; break;
    case KEY_F(10): event.key = "f10"; break;
    case KEY_F(11): event.key = "f11"; break;
    case KEY_F(12): event.key = "f12"; break;
    case KEY_ENTER: event.key = "enter"; break;
    case KEY_BACKSPACE: event.key = "backspace"; break;
    case KEY_DC:    event.key = "delete"; break;
    case KEY_INS:   event.key = "insert"; break;
    case KEY_LEFT:  event.key = "left"; break;
    case KEY_RIGHT: event.key = "right"; break;
    case KEY_UP:    event.key = "up"; break;
    case KEY_DOWN:  event.key = "down"; break;
    case KEY_HOME:  event.key = "home"; break;
    case KEY_END:   event.key = "end"; break;
    case KEY_PPAGE: event.key = "page_up"; break;
    case KEY_NPAGE: event.key = "page_down"; break;
    case KEY_MINUS: event.key = "-"; break;
    case KEY_PLUS:  event.key = "+"; break;
    case KEY_BACKSPACE2: event.key = "backspace2"; break;  // some terminals
    default:
      // Regular printable character
      if (ch >= 32 && ch <= 126) {
        event.key = std::string(1, static_cast<char>(ch));
      } else {
        // Special escape sequences handled elsewhere; just record the code
        event.key = std::to_string(ch);
      }
      break;
  }
  return true;
}

std::tuple<size_t, size_t, size_t, size_t> Tui::render(const std::vector<std::string>& lines) {
  if (!running_) return {0, 0, 0, 0};

  // Clear window
  wclear(win_->win);

  // Determine how many lines to display (respecting height)
  size_t display_rows = 0;
  size_t max_width = 0;
  for (const auto& line : lines) {
    // Truncate or pad to column width
    size_t w = line.size();
    if (w > cols_ - 2) w = cols_ - 2;  // leave margin
    max_width = std::max(max_width, w);

    // Try to write the line
    int start_y = static_cast<int>(display_rows);
    if (start_y >= static_cast<int>(rows_ - 1)) break;  // no more room

    // Write the line (with attribute reset at end)
    mvwhline(win_->win, start_y, 1, ' ', cols_ - 2);  // clear line area
    mvwaddstr(win_->win, start_y, 1, line.c_str());

    display_rows++;
  }

  // Fill remaining rows with empty lines
  while (display_rows < rows_ - 1) {
    mvwaddstr(win_->win, display_rows, 1, std::string(cols_ - 2, ' ').c_str());
    display_rows++;
  }

  // Refresh the panel/window
  wrefresh(win_->win);

  // Return the full updated rectangle
  return {1, 1, cols_ - 2, display_rows};
}

void Tui::set_session(const SessionInfo& info) {
  // Could update a status bar or header with session info
  // For now, store conceptually; render will incorporate if needed
  (void)info;
}

void Tui::write(const std::string& text) {
  if (!running_) return;
  // Direct write: append text at current cursor position
  // Simple implementation: just add to last line or status line
  mvprintw(rows_ - 1, 1, "%s%s", text.c_str(), getiskey() ? "" : "");
  refresh();
}

void Tui::clear() {
  if (!running_) return;
  werase(win_->win);
  wrefresh(win_->win);
}

void Tui::cursor_goto(size_t row, size_t col) {
  if (!running_) return;
  // Clamp to window bounds
  row = std::min(row, rows_ - 1);
  col = std::min(col, cols_ - 1);
  move(row, col);
}

void Tui::set_cursor_visibility(bool visible) {
  cursor_visible_ = visible;
  if (visible) {
    curs_set(1);
  } else {
    curs_set(0);
  }
}

bool Tui::read_event(InputEvent& event, bool blocking) {
  if (!running_) return false;

  // Set blocking mode
  if (blocking) {
    timeout(-1);  // blocking wait
  } else {
    timeout(0);   // non-blocking poll
  }

  int ch = wgetch(win_->win);
  if (ch == ERR) {
    timeout(0);  // restore non-blocking
    return false;
  }

  // Process as in pump_input
  event.type = InputEvent::Type::Key;
  switch (ch) {
    case KEY_F(1):  event.key = "f1"; break;
    case KEY_F(2):  event.key = "f2"; break;
    case KEY_F(3):  event.key = "f3"; break;
    case KEY_F(4):  event.key = "f4"; break;
    case KEY_F(5):  event.key = "f5"; break;
    case KEY_F(6):  event.key = "f6"; break;
    case KEY_F(7):  event.key = "f7"; break;
    case KEY_F(8):  event.key = "f8"; break;
    case KEY_F(9):  event.key = "f9"; break;
    case KEY_F(10): event.key = "f10"; break;
    case KEY_F(11): event.key = "f11"; break;
    case KEY_F(12): event.key = "f12"; break;
    case KEY_ENTER: event.key = "enter"; break;
    case KEY_BACKSPACE: event.key = "backspace"; break;
    case KEY_DC:    event.key = "delete"; break;
    case KEY_INS:   event.key = "insert"; break;
    case KEY_LEFT:  event.key = "left"; break;
    case KEY_RIGHT: event.key = "right"; break;
    case KEY_UP:    event.key = "up"; break;
    case KEY_DOWN:  event.key = "down"; break;
    case KEY_HOME:  event.key = "home"; break;
    case KEY_END:   event.key = "end"; break;
    case KEY_PPAGE: event.key = "page_up"; break;
    case KEY_NPAGE: event.key = "page_down"; break;
    case KEY_MINUS: event.key = "-"; break;
    case KEY_PLUS:  event.key = "+"; break;
    default:
      if (ch >= 32 && ch <= 126) {
        event.key = std::string(1, static_cast<char>(ch));
      } else {
        event.key = std::to_string(ch);
      }
      break;
  }

  // Handle mouse separately if mouse event
  if (ch == MOUSE_EVENT) {
    MEVENT m;
    if (getmouse(&m) == OK) {
      event.type = InputEvent::Type::Mouse;
      event.x = m.x;
      event.y = m.y;
      event.shift = (m.bstate & BSHIFT) != 0;
      event.ctrl = (m.bstate & BCTRL) != 0;
      event.alt = (m.bstate & BALT) != 0;
      switch (m.bstate & BUTTON_RELEASE_MASK) {
        case BUTTON1_RELEASED: event.key = "mouse_1"; break;
        case BUTTON1_CLICKED:  event.key = "mouse_1_click"; break;
        case BUTTON2_RELEASED: event.key = "mouse_2"; break;
        case BUTTON2_CLICKED:  event.key = "mouse_2_click"; break;
        case BUTTON3_RELEASED: event.key = "mouse_3"; break;
        case BUTTON3_CLICKED:  event.key = "mouse_3_click"; break;
        case BUTTON4_RELEASED: event.key = "mouse_scroll_up"; break;
        case BUTTON4_CLICKED:  event.key = "mouse_scroll_up_click"; break;
        case BUTTON5_RELEASED: event.key = "mouse_scroll_down"; break;
        case BUTTON5_CLICKED:  event.key = "mouse_scroll_down_click"; break;
        default: event.key = "mouse_other"; break;
      }
    }
    return (event.type == InputEvent::Type::Mouse) || (event.type == InputEvent::Type::Key);
  }

  timeout(0);  // restore non-blocking
  return true;
}

}  // namespace karta::tui