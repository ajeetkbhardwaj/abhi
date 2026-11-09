#include "karta/tui/screen.h"

#include <curses.h>

#include <clocale>
#include <cwchar>
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
    case Style::Assistant:
      return colors ? COLOR_PAIR(1) : A_NORMAL;
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

  setlocale(LC_ALL, "");
  initscr();
  if (stdscr == nullptr) return false;

  raw();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);
  mouseinterval(0);
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
  if (!lines_.empty() && !lines_.back().text.empty()) lines_.push_back({Style::Normal, ""});
}

void Tui::set_overlay(const std::vector<std::string>& lines, int selected) { overlay_lines_ = lines; overlay_selected_ = selected; }
void Tui::clear_overlay() { overlay_lines_.clear(); overlay_selected_ = -1; }
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
  if (rows_ < 11) return 1;
  return rows_ - 6;  // transcript: rows 2..rows_-5; status rows_-4, rule rows_-3, input rows_-2, rule rows_-1
}


void Tui::scroll_up(std::size_t n) {
  if (n == 0) n = visible_rows() > 3 ? 3 : 1;
  scroll_ += n;
}
void Tui::scroll_down(std::size_t n) {
  if (n == 0) n = visible_rows() > 3 ? 3 : 1;
  scroll_ = scroll_ > n ? scroll_ - n : 0;
}
bool Tui::in_transcript(int row) const {
  // transcript occupies rows 2 .. rows_-5
  if (rows_ < 11) return false;
  int overlay_n = static_cast<int>(overlay_lines_.size()); int max_overlay = (rows_ > 12) ? 8 : 4; if (overlay_n > max_overlay) overlay_n = max_overlay;
  return row >= 2 && row < static_cast<int>(rows_) - 4 - overlay_n;
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

  if (ch == KEY_MOUSE) {
    MEVENT ev;
    if (getmouse(&ev) == OK) {
      const bool in_tx = in_transcript(ev.y);
#if NCURSES_MOUSE_VERSION >= 2
      if (ev.bstate & BUTTON4_PRESSED) { if (in_tx) scroll_up(3); return Key::Mouse; }
      if (ev.bstate & BUTTON5_PRESSED) { if (in_tx) scroll_down(3); return Key::Mouse; }
#endif
#ifdef BUTTON4_PRESSED
      if (ev.bstate & BUTTON4_PRESSED) { if (in_tx) scroll_up(3); return Key::Mouse; }
#endif
#ifdef BUTTON5_PRESSED
      if (ev.bstate & BUTTON5_PRESSED) { if (in_tx) scroll_down(3); return Key::Mouse; }
#endif
      if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_RELEASED)) {
        // Click in transcript: keep transcript scroll active (no input steal).
        // Scroll is handled by wheel/PgUp/PgDn; click is a safe no-op.
        (void)in_tx;
        return Key::Mouse;
      }
      if (ev.bstate & REPORT_MOUSE_POSITION) return Key::Mouse;
    }
    return Key::Mouse;
  }

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
    case 4:  // Ctrl+D
      return Key::CtrlD;
    case 12: // Ctrl+L
      return Key::CtrlL;
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

    // Ctrl+Left / Ctrl+Right (word navigation)
    case 545:  // KEY_SLEFT (Ctrl+Left)
      return Key::CtrlLeft;
    case 560:  // KEY_SRIGHT (Ctrl+Right)
      return Key::CtrlRight;
    case 566:  // KEY_SHOME (Ctrl+Home)
      return Key::Home;
    case 567:  // KEY_SEND (Ctrl+End)
      return Key::End;
    // Ctrl+Up / Ctrl+Down
    case 568:  // KEY_SPREVIOUS (Ctrl+Up)
      return Key::CtrlUp;
    case 569:  // KEY_SNEXT (Ctrl+Down)
      return Key::CtrlDown;
    // Shift+Tab
    case 353:  // KEY_STAB (Shift+Tab)
      return Key::ShiftTab;
    // Ctrl+Backspace / Ctrl+Delete
    case 513:  // Ctrl+Backspace (KEY_DC with Ctrl)
      return Key::CtrlBackspace;
    case 514:  // Ctrl+Delete
      return Key::CtrlDelete;
    // Ctrl+A / Ctrl+E / Ctrl+K / Ctrl+U / Ctrl+W
    case 1:   // Ctrl+A (beginning of line)
      cursor_ = 0;
      return Key::CtrlA;
    case 5:   // Ctrl+E (end of line)
      cursor_ = input_.size();
      return Key::CtrlE;
    case 11:  // Ctrl+K (kill to end)
      input_.erase(cursor_);
      return Key::CtrlK;
    case 21:  // Ctrl+U (kill to beginning)
      input_.erase(0, cursor_);
      cursor_ = 0;
      return Key::CtrlU;
    case 23:  // Ctrl+W (kill word backward)
      if (cursor_ > 0) {
        std::size_t begin = cursor_;
        while (begin > 0 && input_[begin - 1] == ' ') --begin;
        while (begin > 0 && input_[begin - 1] != ' ') --begin;
        input_.erase(begin, cursor_ - begin);
        cursor_ = begin;
      }
      return Key::CtrlW;
    // F keys
    case KEY_F(1): return Key::F1;
    case KEY_F(2): return Key::F2;
    case KEY_F(3): return Key::F3;
    case KEY_F(4): return Key::F4;
    case KEY_F(5): return Key::F5;
    case KEY_F(6): return Key::F6;
    case KEY_F(7): return Key::F7;
    case KEY_F(8): return Key::F8;
    case KEY_F(9): return Key::F9;
    case KEY_F(10): return Key::F10;
    case KEY_F(11): return Key::F11;
    case KEY_F(12): return Key::F12;

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

static void put_line(int row, const std::string& text, std::size_t cols) {
  mvhline(row, 0, ' ', static_cast<int>(cols));
  std::mbstate_t state{};
  const char* src = text.c_str();
  size_t len = std::mbsrtowcs(nullptr, &src, 0, &state);
  if (len == static_cast<size_t>(-1)) {
    mvaddnstr(row, 0, text.c_str(), static_cast<int>(cols) - 1);
    return;
  }
  state = std::mbstate_t{};
  src = text.c_str();
  std::wstring ws(len, L'\0');
  std::mbsrtowcs(ws.data(), &src, len, &state);
  // Truncate by display width
  std::wstring out;
  int width = 0;
  int maxw = static_cast<int>(cols) - 1;
  for (wchar_t wc : ws) {
    int w = wcwidth(wc);
    if (w < 0) w = 0;
    if (width + w > maxw) break;
    out.push_back(wc);
    width += w;
  }
  mvaddnwstr(row, 0, out.c_str(), static_cast<int>(out.size()));
}

void Tui::draw() {
  if (!running_) return;

  erase();

  attron(A_REVERSE | A_BOLD);
  put_line(0, " " + header_, cols_);
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

  // reserve space for overlay above status
  int overlay_n = static_cast<int>(overlay_lines_.size());
  int max_overlay = (rows_ > 12) ? 8 : 4;
  if (overlay_n > max_overlay) overlay_n = max_overlay;
  std::size_t vis = visible;
  if (vis > static_cast<std::size_t>(overlay_n)) vis -= static_cast<std::size_t>(overlay_n);

  std::size_t st = 0;
  if (total > vis) {
    const std::size_t max_scroll = total - vis;
    if (scroll_ > max_scroll) scroll_ = max_scroll;
    st = max_scroll - scroll_;
  }
  for (std::size_t i = 0; i < vis; ++i) {
    if (st + i >= total) break;
    const Line& line = lines_[st + i];
    const int attr = style_attr(line.style, colors_);
    if (attr != A_NORMAL) attron(attr);
    put_line(static_cast<int>(2 + i), line.text, cols_);
    if (attr != A_NORMAL) attroff(attr);
  }
  // overlay box
  int oy = static_cast<int>(2 + vis);
  for (int i = 0; i < overlay_n; ++i) {
    bool sel = (i == overlay_selected_);
    if (sel) attron(A_REVERSE | A_BOLD);
    else attron(A_DIM);
    std::string ol = (sel?" > ":"   ") + overlay_lines_[static_cast<size_t>(i)];
    put_line(oy + i, ol, cols_);
    if (sel) attroff(A_REVERSE | A_BOLD); else attroff(A_DIM);
  }

  std::string status = status_;
  if (status_busy_) status = std::string(spinner_frame(frame_++)) + " " + status;

  attron(A_DIM);
  put_line(static_cast<int>(rows_) - 4, status, cols_);
  mvhline(static_cast<int>(rows_) - 3, 0, ACS_HLINE, static_cast<int>(cols_));
  attroff(A_DIM);

  const std::string shown = input_hidden_ ? std::string(input_.size(), '*') : input_;
  put_line(static_cast<int>(rows_) - 2, prompt_ + shown, cols_);

  attron(A_DIM);
  mvhline(static_cast<int>(rows_) - 1, 0, ACS_HLINE, static_cast<int>(cols_));
  attroff(A_DIM);

  std::size_t cursor_col = display_width(prompt_) + display_width(input_.substr(0, cursor_));
  if (cursor_col >= cols_) cursor_col = cols_ - 1;
  move(static_cast<int>(rows_) - 2, static_cast<int>(cursor_col));

  refresh();
}

}  // namespace karta::tui
