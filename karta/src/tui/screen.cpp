#include "tui/screen.h"

#include <curses.h>

#include <cstddef>
#include <string>
#include <vector>

namespace karta::tui {
namespace {

// Counts UTF-8 lead bytes as one column each.
std::size_t display_width(const std::string& text) {
  std::size_t width = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xC0) != 0x80) ++width;
  }
  return width;
}

std::size_t utf8_length(unsigned char lead) {
  if (lead >= 0xF0) return 4;
  if (lead >= 0xE0) return 3;
  if (lead >= 0xC0) return 2;
  return 1;
}

std::vector<std::string> wrap_text(const std::string& text, std::size_t width) {
  if (width < 8) width = 8;
  std::vector<std::string> lines;
  std::string current;
  std::size_t current_width = 0;

  for (std::size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    std::size_t length = utf8_length(lead);
    if (i + length > text.size()) length = text.size() - i;
    const std::string chunk = text.substr(i, length);
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
  lines.push_back(current);
  return lines;
}

int style_attr(Style style, bool colors) {
  switch (style) {
    case Style::Dim:
    case Style::Status:
      return A_DIM;
    case Style::Bold:
      return A_BOLD;
    case Style::Accent:
      return A_BOLD | (colors ? COLOR_PAIR(2) : 0);
    case Style::User:
      return A_BOLD | (colors ? COLOR_PAIR(3) : 0);
    case Style::Success:
      return colors ? COLOR_PAIR(4) : A_BOLD;
    case Style::Error:
      return A_BOLD | (colors ? COLOR_PAIR(5) : 0);
    case Style::Normal:
    default:
      return A_NORMAL;
  }
}

const char* spinner_frame(std::size_t index) {
  static const char* frames[] = {"|", "/", "-", "\\"};
  return frames[index % 4];
}

}  // namespace

Tui::Tui() = default;
Tui::~Tui() { stop(); }

bool Tui::init(const TuiOptions& options) {
  if (running_) return true;
  (void)options;

  initscr();
  if (stdscr == nullptr) return false;

  raw();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
#ifdef NCURSES_VERSION
  set_escdelay(25);
#endif
  curs_set(1);

  if (has_colors()) {
    start_color();
    colors_ = true;
    int background = COLOR_BLACK;
#ifdef NCURSES_VERSION
    if (use_default_colors() == OK) background = -1;
#endif
    init_pair(1, COLOR_WHITE, background);
    init_pair(2, COLOR_CYAN, background);
    init_pair(3, COLOR_YELLOW, background);
    init_pair(4, COLOR_GREEN, background);
    init_pair(5, COLOR_RED, background);
    init_pair(6, COLOR_MAGENTA, background);
  }

  getmaxyx(stdscr, rows_, cols_);
  if (rows_ < 6) rows_ = 6;
  if (cols_ < 20) cols_ = 20;

  running_ = true;
  return true;
}

void Tui::stop() {
  if (!running_) return;
  running_ = false;
  if (stdscr != nullptr) {
    curs_set(1);
    endwin();
  }
}

void Tui::set_header(const SessionInfo& info) {
  header_ = info.title.empty() ? "karta" : info.title;
  if (!info.version.empty()) header_ += " " + info.version;
  if (!info.provider.empty()) {
    header_ += "  ·  " + info.provider;
    // Model ids often already carry the vendor prefix (nvidia/...), so use a
    // separator instead of provider/model to avoid nvidia/nvidia/...
    if (!info.model.empty()) header_ += " · " + info.model;
  }
  if (!info.cwd.empty()) header_ += "  ·  " + info.cwd;
}

void Tui::set_status(const std::string& text, bool busy) {
  status_ = text;
  status_busy_ = busy;
}

void Tui::append_line(const std::string& text, Style style) {
  append_text(text, style);
}

void Tui::append_text(const std::string& text, Style style) {
  if (!running_) return;
  const std::vector<std::string> wrapped = wrap_text(text, text_width());
  for (const std::string& line : wrapped) lines_.push_back({style, line});
  scroll_ = 0;
  if (!streaming_) trim_transcript();
}

void Tui::stream_begin(Style style) {
  if (!running_) return;
  stream_style_ = style;
  streaming_ = true;
  stream_buffer_.clear();
  stream_start_ = lines_.size();
  lines_.push_back({style, ""});
  scroll_ = 0;
}

void Tui::stream_delta(const std::string& delta) {
  if (!running_) return;
  if (!streaming_) {
    stream_begin(stream_style_);
  } else {
    lines_.resize(stream_start_);
  }
  stream_buffer_ += delta;
  const std::vector<std::string> wrapped = wrap_text(stream_buffer_, text_width());
  for (const std::string& line : wrapped) lines_.push_back({stream_style_, line});
  scroll_ = 0;
}

void Tui::stream_end() {
  if (!streaming_) return;
  streaming_ = false;
  stream_buffer_.clear();
  if (!lines_.empty() && lines_.back().text.empty()) lines_.pop_back();
  if (!lines_.empty() && !lines_.back().text.empty()) {
    lines_.push_back({Style::Normal, ""});
  }
}

void Tui::clear_transcript() {
  lines_.clear();
  streaming_ = false;
  stream_buffer_.clear();
  scroll_ = 0;
}

void Tui::set_prompt(const std::string& label) { prompt_ = label; }

void Tui::set_input(const std::string& value) {
  input_ = value;
  cursor_ = input_.size();
}

void Tui::set_input_hidden(bool hidden) { input_hidden_ = hidden; }

std::size_t Tui::text_width() const {
  return cols_ > 2 ? cols_ - 1 : 1;
}

std::size_t Tui::visible_rows() const {
  if (rows_ < 10) return 1;
  return rows_ - 5;  // transcript occupies rows 2 .. rows_-4
}

void Tui::trim_transcript() {
  constexpr std::size_t kMaxLines = 4000;
  if (lines_.size() <= kMaxLines) return;
  lines_.erase(lines_.begin(), lines_.begin() + 1000);
}

Key Tui::poll(int* out_ch) {
  if (!running_) return Key::None;

  const int ch = wgetch(stdscr);
  if (ch == ERR) return Key::None;

  if (ch == KEY_RESIZE) {
    getmaxyx(stdscr, rows_, cols_);
    if (rows_ < 6) rows_ = 6;
    if (cols_ < 20) cols_ = 20;
    return Key::Resize;
  }

  switch (ch) {
    case '\n':
    case '\r':
    case KEY_ENTER:
      if (!input_.empty()) {
        history_.push_back(input_);
        if (history_.size() > 200) history_.erase(history_.begin());
      }
      history_pos_ = -1;
      history_draft_.clear();
      return Key::Enter;

    case 3:  // Ctrl+C
      return Key::CtrlC;
    case 27:  // Escape
      return Key::Escape;

    case KEY_BACKSPACE:
    case 127:
    case 8: {
      if (cursor_ == 0) return Key::Backspace;
      std::size_t begin = cursor_ - 1;
      while (begin > 0 && (static_cast<unsigned char>(input_[begin]) & 0xC0) == 0x80) --begin;
      input_.erase(begin, cursor_ - begin);
      cursor_ = begin;
      return Key::Backspace;
    }

    case KEY_DC: {
      if (cursor_ >= input_.size()) return Key::Delete;
      std::size_t end = cursor_ + 1;
      while (end < input_.size() && (static_cast<unsigned char>(input_[end]) & 0xC0) == 0x80) ++end;
      input_.erase(cursor_, end - cursor_);
      return Key::Delete;
    }

    case KEY_LEFT: {
      if (cursor_ > 0) {
        --cursor_;
        while (cursor_ > 0 && (static_cast<unsigned char>(input_[cursor_]) & 0xC0) == 0x80) --cursor_;
      }
      return Key::Left;
    }

    case KEY_RIGHT: {
      if (cursor_ < input_.size()) {
        ++cursor_;
        while (cursor_ < input_.size() && (static_cast<unsigned char>(input_[cursor_]) & 0xC0) == 0x80) ++cursor_;
      }
      return Key::Right;
    }

    case KEY_HOME:
      cursor_ = 0;
      return Key::Home;
    case KEY_END:
      cursor_ = input_.size();
      return Key::End;

    case KEY_UP:
      if (history_.empty()) return Key::Up;
      if (history_pos_ < 0) {
        history_draft_ = input_;
        history_pos_ = static_cast<int>(history_.size()) - 1;
      } else if (history_pos_ > 0) {
        --history_pos_;
      }
      input_ = history_[static_cast<std::size_t>(history_pos_)];
      cursor_ = input_.size();
      return Key::Up;

    case KEY_DOWN:
      if (history_pos_ < 0) return Key::Down;
      if (static_cast<std::size_t>(history_pos_) + 1 >= history_.size()) {
        history_pos_ = -1;
        input_ = history_draft_;
      } else {
        ++history_pos_;
        input_ = history_[static_cast<std::size_t>(history_pos_)];
      }
      cursor_ = input_.size();
      return Key::Down;

    case KEY_PPAGE:
      scroll_ += visible_rows() / 2 + 1;
      return Key::PageUp;

    case KEY_NPAGE: {
      const std::size_t step = visible_rows() / 2 + 1;
      scroll_ = scroll_ > step ? scroll_ - step : 0;
      return Key::PageDown;
    }

    case '\t':
      return Key::Tab;

    default:
      break;
  }

  if (ch >= 32 && ch <= 255) {
    const char byte = static_cast<char>(ch);
    input_.insert(input_.begin() + static_cast<std::ptrdiff_t>(cursor_), byte);
    ++cursor_;
    if (out_ch) *out_ch = ch;
    return Key::Char;
  }
  return Key::None;
}

void Tui::draw() {
  if (!running_) return;

  erase();

  attron(A_REVERSE | A_BOLD);
  mvhline(0, 0, ' ', static_cast<int>(cols_));
  const std::string header = " " + header_;
  mvaddnstr(0, 0, header.c_str(), static_cast<int>(cols_) - 1);
  attroff(A_REVERSE | A_BOLD);

  attron(A_DIM);
  mvhline(1, 0, ACS_HLINE, static_cast<int>(cols_));
  attroff(A_DIM);

  const std::size_t visible = visible_rows();
  const std::size_t total = lines_.size();
  std::size_t start = 0;
  if (total > visible) {
    const std::size_t max_scroll = total - visible;
    if (scroll_ > max_scroll) scroll_ = max_scroll;
    start = max_scroll - scroll_;
  }

  for (std::size_t i = 0; i < visible; ++i) {
    if (start + i >= total) break;
    const Line& line = lines_[start + i];
    const int attr = style_attr(line.style, colors_);
    if (attr != A_NORMAL) attron(attr);
    mvaddnstr(static_cast<int>(2 + i), 0, line.text.c_str(), static_cast<int>(cols_) - 1);
    if (attr != A_NORMAL) attroff(attr);
  }

  std::string status = status_;
  if (status_busy_) status = std::string(spinner_frame(frame_++)) + " " + status;

  attron(A_DIM);
  mvhline(static_cast<int>(rows_) - 3, 0, ' ', static_cast<int>(cols_));
  mvaddnstr(static_cast<int>(rows_) - 3, 0, status.c_str(), static_cast<int>(cols_) - 1);
  mvhline(static_cast<int>(rows_) - 2, 0, ACS_HLINE, static_cast<int>(cols_));
  attroff(A_DIM);

  const std::string shown = input_hidden_ ? std::string(input_.size(), '*') : input_;
  const std::string edit_line = prompt_ + shown;
  mvhline(static_cast<int>(rows_) - 1, 0, ' ', static_cast<int>(cols_));
  mvaddnstr(static_cast<int>(rows_) - 1, 0, edit_line.c_str(), static_cast<int>(cols_) - 1);

  std::size_t cursor_col = display_width(prompt_) + display_width(input_.substr(0, cursor_));
  if (cursor_col >= cols_) cursor_col = cols_ - 1;
  move(static_cast<int>(rows_) - 1, static_cast<int>(cursor_col));

  refresh();
}

}  // namespace karta::tui
