#pragma once

#include <string>
#include <vector>
#include <memory>

namespace karta::tui {

/// State of the TUI session
struct SessionInfo {
  std::string id;
  std::string cwd;
  std::string model;
  std::string provider;
  size_t turn_count = 0;
  bool is_running = false;
};

/// Render result from the TUI
struct RenderResult {
  std::vector<std::string> lines;  ///< Rendered lines
  size_t cursor_row = 0;           ///< Cursor row position
  size_t cursor_col = 0;           ///< Cursor column position
};

/// Input event from the terminal
struct InputEvent {
  enum class Type { Key, Mouse, Resize };
  Type type = Type::Key;
  std::string key;                ///< Keyboard character or special key name
  int x = 0, y = 0;               ///< Mouse coordinates (if mouse event)
  bool shift = false, ctrl = false, alt = false;
};

/// TUI configuration options
struct TuiOptions {
  bool fullscreen = false;
  bool cursor_visible = true;
  std::string title = "Karta";
  size_t min_rows = 24;
  size_t min_cols = 80;
};

/// Main TUI class providing a terminal user interface for Karta
class Tui {
 public:
  Tui();
  ~Tui();

  /// Initialize the TUI with given options
  /// Returns true on success
  bool init(const TuiOptions& opts = TuiOptions{});

  /// Clean up and restore the terminal
  void stop();

  /// Check if TUI is running
  bool is_running() const { return running_; }

  /// Process one input event; returns true if handled
  bool pump_input(InputEvent& event);

  /// Render the current state
  /// Returns the rectangle that was updated (row, col, width, height)
  std::tuple<size_t, size_t, size_t, size_t> render(const std::vector<std::string>& lines);

  /// Set the session information to display
  void set_session(const SessionInfo& info);

  /// Get the current terminal dimensions
  size_t rows() const { return rows_; }
  size_t cols() const { return cols_; }

  /// Write a string directly to the terminal (bypassing render cycle)
  void write(const std::string& text);

  /// Clear the screen
  void clear();

  /// Move cursor to position
  void cursor_goto(size_t row, size_t col);

  /// Show/hardware cursor
  void set_cursor_visibility(bool visible);

  /// Read a single input event (blocking or non-blocking)
  /// @param blocking if true, waits for input; if false, checks and returns immediately
  bool read_event(InputEvent& event, bool blocking = false);

 private:
  bool running_ = false;
  bool cursor_visible_ = true;
  size_t rows_ = 24;
  size_t cols_ = 80;
  /// NCURSES WINDOW handle (managed internally)
  // Note: We use a raw pointer with manual management for maximum compatibility;
  // ncurses WINDOW* is the standard handle type.
  struct NcursesWindow;
  std::unique_ptr<NcursesWindow> win_;
};
}  // namespace karta::tui