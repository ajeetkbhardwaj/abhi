// Karta — ncurses terminal layer (transcript, status, single-line editor).
//
// The TUI is deliberately small: it owns the screen, a scrollback transcript,
// a status line and one input line with history. All agent and provider logic
// lives above it in tui_app.cpp.
#pragma once

#include <string>
#include <utility>
#include <vector>
#include <memory>

#include "karta/tui/editor.h"

namespace karta::tui {

enum class Style {
  Normal,
  Dim,
  Bold,
  Accent,
  User,
  Assistant,
  Success,
  Error,
  Status,
};

enum class Key {
  None,
  Char,
  Enter,
  Backspace,
  Delete,
  Left,
  Right,
  Up,
  Down,
  Home,
  End,
  PageUp,
  PageDown,
  Escape,
  CtrlC,
  CtrlD,
  CtrlL,
  Tab,
  Mouse,
  Resize,
  // Extended keybindings for pi compatibility
  CtrlLeft,
  CtrlRight,
  CtrlUp,
  CtrlDown,
  ShiftTab,
  CtrlBackspace,
  CtrlDelete,
  CtrlA,
  CtrlE,
  CtrlK,
  CtrlU,
  CtrlW,
  AltLeft,
  AltRight,
  AltUp,
  AltDown,
  AltBackspace,
  AltDelete,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  // Clipboard
  Copy,
  Paste,
  // Editor
  CtrlZ,
  CtrlY,
};

struct SessionInfo {
  std::string title = "karta";
  std::string version;
  std::string provider;
  std::string model;
  std::string cwd;
  std::string id;
};

struct ThemeColors {
  int normal = -1;
  int dim = -1;
  int bold = -1;
  int accent = -1;
  int user = -1;
  int assistant = -1;
  int success = -1;
  int error = -1;
  int status = -1;
  int header_bg = -1;
  int header_fg = -1;
  int input_bg = -1;
  int input_fg = -1;
  int selection_bg = -1;
  int selection_fg = -1;
};

struct TuiOptions {
  size_t min_rows = 10;
  size_t min_cols = 40;
  ThemeColors theme;
};

class Tui {
 public:
  Tui();
  ~Tui();

  Tui(const Tui&) = delete;
  Tui& operator=(const Tui&) = delete;

  bool init(const TuiOptions& options = TuiOptions{});
  void stop();
  bool running() const { return running_; }

  size_t rows() const { return rows_; }
  size_t cols() const { return cols_; }

  void set_header(const SessionInfo& info);
  void set_status(const std::string& text, bool busy = false);

  void append_line(const std::string& text, Style style = Style::Normal);
  void append_text(const std::string& text, Style style = Style::Normal);

  // Markdown rendering
  void append_markdown(const std::string& text, Style base_style);

  // Streaming assistant text: begin, feed deltas, end.
  void stream_begin(Style style = Style::Normal);
  void stream_delta(const std::string& delta);
  void stream_end();
  bool streaming() const { return streaming_; }

  void clear_transcript();
  void set_overlay(const std::vector<std::string>& lines, int selected=-1);
  void clear_overlay();

  void set_prompt(const std::string& label);
  const std::string& input() const { return input_; }
  void set_input(const std::string& value);
  void set_input_hidden(bool hidden);

  // Multi-line input support
  void set_multiline(bool enabled);
  bool multiline() const { return multiline_; }
  std::string input_text() const;  // Returns full text including newlines
  void set_input_text(const std::string& text);
  Editor* editor() const { return editor_.get(); }

  // Theme support
  void apply_theme(const ThemeColors& theme);
  void save_theme(const std::string& path = "") const;
  void load_theme(const std::string& path = "");

  // Non-blocking. Handles line editing (characters, backspace, arrows, history).
  Key poll(int* ch = nullptr);

  void draw();

 private:
  struct Line {
    Style style = Style::Normal;
    std::string text;
  };

  size_t text_width() const;
  size_t visible_rows() const;
  void trim_transcript();
  void scroll_up(std::size_t n=0);
  void scroll_down(std::size_t n=0);
  bool in_transcript(int row) const;

  bool running_ = false;
  size_t rows_ = 24;
  size_t cols_ = 80;

  std::vector<Line> lines_;
  size_t scroll_ = 0;  // lines scrolled up from the bottom
  size_t frame_ = 0;   // spinner animation

  bool streaming_ = false;
  std::string stream_buffer_;
  size_t stream_start_ = 0;
  Style stream_style_ = Style::Normal;

  std::string header_;
  std::string status_;
  bool status_busy_ = false;

  std::string prompt_ = "> ";
  std::string input_;
  size_t cursor_ = 0;
  bool input_hidden_ = false;

  std::vector<std::string> history_;
  int history_pos_ = -1;
  std::string history_draft_;

  bool colors_ = false;
  ThemeColors theme_;
  std::vector<std::string> overlay_lines_;
  int overlay_selected_ = -1;

  // Multi-line input
  std::unique_ptr<Editor> editor_;
  bool multiline_ = false;

  // Mouse selection for auto-copy
  bool mouse_selecting_ = false;
  int mouse_sel_start_ = 0;
  int mouse_sel_end_ = 0;
  void copy_selection_to_clipboard();
};

}  // namespace karta::tui
