#include "karta/tui/screen.h"
#include "karta/tui/markdown.h"
#include "karta/tui/clipboard.h"

#include <curses.h>

#include <clocale>
#include <cwchar>
#include <cstddef>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>

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
  theme_ = options.theme;
  editor_ = std::make_unique<Editor>();

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

    // Use theme colors if set, otherwise use defaults
    auto get_color = [&](int theme_val, int default_val) {
      return theme_val >= 0 ? theme_val : default_val;
    };

    init_pair(1, get_color(theme_.normal, COLOR_WHITE), background);
    init_pair(2, get_color(theme_.accent, COLOR_CYAN), background);
    init_pair(3, get_color(theme_.user, COLOR_YELLOW), background);
    init_pair(4, get_color(theme_.success, COLOR_GREEN), background);
    init_pair(5, get_color(theme_.error, COLOR_RED), background);
    init_pair(6, get_color(theme_.assistant, COLOR_MAGENTA), background);
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

  bool has_markdown = text.find("**") != std::string::npos ||
                      text.find("*") != std::string::npos ||
                      text.find("`") != std::string::npos ||
                      text.find("[") != std::string::npos ||
                      text.find("```") != std::string::npos;

  if (has_markdown) {
    append_markdown(text, style);
  } else {
    const std::vector<std::string> wrapped = wrap_text(text, text_width());
    for (const std::string& line : wrapped) lines_.push_back({style, line});
  }
  scroll_ = 0;
  if (!streaming_) trim_transcript();
}

void Tui::append_markdown(const std::string& text, Style base_style) {
  auto renders = render_markdown(text, text_width());
  
  std::vector<std::pair<Style, std::string>> styled_segments;
  
  for (const auto& r : renders) {
    if (r.type == MarkdownRender::NodeType::Break) {
      // Force line break
      for (const auto& seg : styled_segments) {
        auto wrapped = wrap_text(seg.second, text_width());
        for (const auto& w : wrapped) lines_.push_back({seg.first, w});
      }
      lines_.push_back({base_style, ""});
      styled_segments.clear();
      continue;
    }
    
    Style seg_style = base_style;
    if (r.type == MarkdownRender::NodeType::Bold) {
      seg_style = Style::Bold;
    } else if (r.type == MarkdownRender::NodeType::Italic) {
      seg_style = Style::Bold;  // Use Bold for italic (ncurses limited)
    } else if (r.type == MarkdownRender::NodeType::Code) {
      seg_style = Style::Dim;   // Use Dim for code
    } else if (r.type == MarkdownRender::NodeType::Link) {
      seg_style = Style::Accent; // Use Accent for links
    }
    
    styled_segments.emplace_back(seg_style, r.content);
  }
  
  // Flush remaining segments
  for (const auto& seg : styled_segments) {
    auto wrapped = wrap_text(seg.second, text_width());
    for (const auto& w : wrapped) lines_.push_back({seg.first, w});
  }
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

void Tui::set_multiline(bool enabled) {
  multiline_ = enabled;
  if (enabled && editor_) {
    editor_->set_text(input_);
    editor_->set_cursor(static_cast<int>(editor_->text().size()), 0);
  }
}

std::string Tui::input_text() const {
  if (multiline_ && editor_) {
    return editor_->text();
  }
  return input_;
}

void Tui::set_input_text(const std::string& text) {
  if (multiline_ && editor_) {
    editor_->set_text(text);
  } else {
    input_ = text;
    cursor_ = input_.size();
  }
}

void Tui::apply_theme(const ThemeColors& theme) {
  theme_ = theme;
  if (!running_ || !colors_) return;
  
  int background = COLOR_BLACK;
#ifdef NCURSES_VERSION
  if (use_default_colors() == OK) background = -1;
#endif

  auto get_color = [&](int theme_val, int default_val) {
    return theme_val >= 0 ? theme_val : default_val;
  };

  init_pair(1, get_color(theme_.normal, COLOR_WHITE), background);
  init_pair(2, get_color(theme_.accent, COLOR_CYAN), background);
  init_pair(3, get_color(theme_.user, COLOR_YELLOW), background);
  init_pair(4, get_color(theme_.success, COLOR_GREEN), background);
  init_pair(5, get_color(theme_.error, COLOR_RED), background);
  init_pair(6, get_color(theme_.assistant, COLOR_MAGENTA), background);
  
  // Redraw immediately
  draw();
}

void Tui::save_theme(const std::string& path) const {
  std::string config_path = path.empty() ? (getenv("HOME") ? std::string(getenv("HOME")) + "/.karta/theme.json" : "theme.json") : path;
  
  // Create directory if needed
  std::string dir = config_path.substr(0, config_path.find_last_of('/'));
  #ifdef _WIN32
  _mkdir(dir.c_str());
  #else
  mkdir(dir.c_str(), 0700);
  #endif
  
  std::ofstream f(config_path);
  if (!f) return;
  
  f << "{\n";
  f << "  \"normal\": " << theme_.normal << ",\n";
  f << "  \"dim\": " << theme_.dim << ",\n";
  f << "  \"bold\": " << theme_.bold << ",\n";
  f << "  \"accent\": " << theme_.accent << ",\n";
  f << "  \"user\": " << theme_.user << ",\n";
  f << "  \"assistant\": " << theme_.assistant << ",\n";
  f << "  \"success\": " << theme_.success << ",\n";
  f << "  \"error\": " << theme_.error << ",\n";
  f << "  \"status\": " << theme_.status << ",\n";
  f << "  \"header_bg\": " << theme_.header_bg << ",\n";
  f << "  \"header_fg\": " << theme_.header_fg << ",\n";
  f << "  \"input_bg\": " << theme_.input_bg << ",\n";
  f << "  \"input_fg\": " << theme_.input_fg << ",\n";
  f << "  \"selection_bg\": " << theme_.selection_bg << ",\n";
  f << "  \"selection_fg\": " << theme_.selection_fg << "\n";
  f << "}\n";
}

void Tui::load_theme(const std::string& path) {
  std::string config_path = path.empty() ? (getenv("HOME") ? std::string(getenv("HOME")) + "/.karta/theme.json" : "theme.json") : path;
  
  std::ifstream f(config_path);
  if (!f) return;
  
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  
  // Simple JSON parsing for our known keys
  auto extract = [&](const std::string& key, int& out) {
    std::string search = "\"" + key + "\"";
    size_t pos = content.find(search);
    if (pos != std::string::npos) {
      pos = content.find(':', pos);
      if (pos != std::string::npos) {
        pos = content.find_first_not_of(" \t\n", pos + 1);
        if (pos != std::string::npos) {
          out = std::stoi(content.substr(pos));
        }
      }
    }
  };
  
  extract("normal", theme_.normal);
  extract("dim", theme_.dim);
  extract("bold", theme_.bold);
  extract("accent", theme_.accent);
  extract("user", theme_.user);
  extract("assistant", theme_.assistant);
  extract("success", theme_.success);
  extract("error", theme_.error);
  extract("status", theme_.status);
  extract("header_bg", theme_.header_bg);
  extract("header_fg", theme_.header_fg);
  extract("input_bg", theme_.input_bg);
  extract("input_fg", theme_.input_fg);
  extract("selection_bg", theme_.selection_bg);
  extract("selection_fg", theme_.selection_fg);
}

void Tui::copy_selection_to_clipboard() {
  if (!mouse_selecting_) return;
  
  const std::size_t visible = visible_rows();
  const std::size_t total = lines_.size();
  if (total == 0) return;
  
  std::size_t max_scroll = 0;
  if (total > visible) max_scroll = total - visible;
  
  // Convert screen row to line index in lines_
  // Transcript starts at screen row 2
  // Visible lines start at index: max_scroll - scroll_
  auto row_to_line_idx = [&](int row) -> std::size_t {
    if (row < 2) return 0;
    std::size_t vis_idx = static_cast<std::size_t>(row - 2);
    if (vis_idx >= visible) vis_idx = visible - 1;
    std::size_t start_idx = (max_scroll > scroll_) ? max_scroll - scroll_ : 0;
    std::size_t line_idx = start_idx + vis_idx;
    return (line_idx < total) ? line_idx : total - 1;
  };
  
  std::size_t line_start = row_to_line_idx(mouse_sel_start_);
  std::size_t line_end = row_to_line_idx(mouse_sel_end_);
  
  if (line_start > line_end) std::swap(line_start, line_end);
  
  std::string selected;
  for (std::size_t i = line_start; i <= line_end; ++i) {
    selected += lines_[i].text;
    if (i != line_end) selected += "\n";
  }
  
  if (!selected.empty()) {
    clipboard_copy(selected);
  }
}

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
      const bool in_overlay = overlay_selected_ >= 0 && ev.y >= 2 && ev.y < static_cast<int>(rows_) - 4 && static_cast<size_t>(ev.y - 2) < overlay_lines_.size();

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

      // Left click/drag: text selection in transcript
      // Note: BUTTON1_DRAG not available in all ncurses; use BUTTON1_PRESSED + REPORT_MOUSE_POSITION
      if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_RELEASED)) {
        if (in_tx) {
          if (ev.bstate & BUTTON1_PRESSED) {
            mouse_sel_start_ = ev.y;
            mouse_sel_end_ = ev.y;
            mouse_selecting_ = true;
          } else if (mouse_selecting_ && (ev.bstate & REPORT_MOUSE_POSITION) && (ev.bstate & BUTTON1_PRESSED)) {
            // Drag while button 1 pressed
            mouse_sel_end_ = ev.y;
          } else if ((ev.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED)) && mouse_selecting_) {
            mouse_sel_end_ = ev.y;
            copy_selection_to_clipboard();
            mouse_selecting_ = false;
          }
          return Key::Mouse;
        } else if (in_overlay) {
          overlay_selected_ = static_cast<int>(ev.y - 2);
          return Key::Mouse;
        }
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

  // Multi-line mode: delegate to Editor
  if (multiline_ && editor_) {
    // Enter = submit, Ctrl+Enter = new line (Shift+Enter not reliable across terminals)
    if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
      // Check for Ctrl+Enter (some terminals send different codes)
      // Most terminals: Enter = 10/13, Ctrl+Enter = same
      // We'll use a simple approach: if we can detect modifier
      int c = wgetch(stdscr);
      if (c == ERR) {
        // Plain Enter = submit
        return Key::Enter;
      }
      // If we got another char, put it back
      ungetch(c);
      // For now: Enter = submit
      return Key::Enter;
    }
    // Ctrl+Enter = new line (ASCII 10 with ctrl = 10, same as Enter)
    // Use Alt+Enter for new line instead
    if (ch == 27) {  // ESC
      int next = wgetch(stdscr);
      if (next == '\n' || next == '\r' || next == KEY_ENTER) {
        // Alt+Enter = new line
        editor_->new_line();
        return Key::Char;
      }
      ungetch(next);
    }

    switch (ch) {
      case KEY_BACKSPACE:
      case 127:
      case 8: {
        editor_->backspace();
        return Key::Backspace;
      }
      case KEY_DC: {
        editor_->delete_char();
        return Key::Delete;
      }
      case KEY_LEFT: {
        editor_->move_left();
        return Key::Left;
      }
      case KEY_RIGHT: {
        editor_->move_right();
        return Key::Right;
      }
      case KEY_UP: {
        editor_->move_up();
        return Key::Up;
      }
      case KEY_DOWN: {
        editor_->move_down();
        return Key::Down;
      }
      case KEY_HOME: {
        editor_->move_home();
        return Key::Home;
      }
      case KEY_END: {
        editor_->move_end();
        return Key::End;
      }
      case KEY_PPAGE: {
        // PageUp in multi-line: could scroll transcript
        scroll_ += visible_rows() / 2 + 1;
        return Key::PageUp;
      }
      case KEY_NPAGE: {
        const std::size_t step = visible_rows() / 2 + 1;
        scroll_ = scroll_ > step ? scroll_ - step : 0;
        return Key::PageDown;
      }
      case '\t': {
        editor_->insert('\t');
        return Key::Tab;
      }
      // Undo: Ctrl+Z
      case 26: {
        editor_->undo();
        return Key::CtrlZ;
      }
      // Redo: Ctrl+Y or Ctrl+Shift+Z (ASCII 26 is Ctrl+Z, Ctrl+Shift+Z would be different)
      case 25: {  // Ctrl+Y
        editor_->redo();
        return Key::CtrlY;
      }
      // Ctrl+Shift+Z for redo (some terminals)
      case 90: {  // 'Z' with shift (uppercase)
        editor_->redo();
        return Key::CtrlY;
      }
      default:
        break;
    }

    if (ch >= 32 && ch <= 255) {
      editor_->insert(static_cast<char>(ch));
      if (out_ch) *out_ch = ch;
      return Key::Char;
    }
    return Key::None;
  }

  // Single-line mode (original behavior)
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

  if (multiline_ && editor_) {
    // Multi-line input rendering
    auto rendered = editor_->render(cols_ - 2);
    int input_start_row = static_cast<int>(rows_) - 2 - static_cast<int>(rendered.size());
    for (std::size_t i = 0; i < rendered.size(); ++i) {
      put_line(input_start_row + static_cast<int>(i), prompt_ + rendered[i], cols_);
    }
    mvhline(static_cast<int>(rows_) - 1, 0, ACS_HLINE, static_cast<int>(cols_));
    
    // Position cursor (accounting for line wrapping)
    auto [screen_row, screen_col] = editor_->cursor_screen_pos(cols_ - 2);
    int cursor_row = input_start_row + screen_row;
    if (cursor_row >= static_cast<int>(rows_) - 1) cursor_row = static_cast<int>(rows_) - 2;
    move(cursor_row, static_cast<int>(display_width(prompt_) + screen_col));
  } else {
    // Single-line input (original behavior)
    const std::string shown = input_hidden_ ? std::string(input_.size(), '*') : input_;
    put_line(static_cast<int>(rows_) - 2, prompt_ + shown, cols_);

    attron(A_DIM);
    mvhline(static_cast<int>(rows_) - 1, 0, ACS_HLINE, static_cast<int>(cols_));
    attroff(A_DIM);

    std::size_t cursor_col = display_width(prompt_) + display_width(input_.substr(0, cursor_));
    if (cursor_col >= cols_) cursor_col = cols_ - 1;
    move(static_cast<int>(rows_) - 2, static_cast<int>(cursor_col));
  }

  refresh();
}

}  // namespace karta::tui
