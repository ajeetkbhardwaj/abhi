// Karta — ncurses terminal layer (transcript, status, single-line editor).
//
// The TUI is deliberately small: it owns the screen, a scrollback transcript,
// a status line and one input line with history. All agent and provider logic
// lives above it in tui_app.cpp.
#pragma once

#include <string>
#include <utility>
#include <vector>

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
};

struct SessionInfo {
  std::string title = "karta";
  std::string version;
  std::string provider;
  std::string model;
  std::string cwd;
  std::string id;
};

struct TuiOptions {
  size_t min_rows = 10;
  size_t min_cols = 40;
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
  std::vector<std::string> overlay_lines_;
  int overlay_selected_ = -1;
};

}  // namespace karta::tui
