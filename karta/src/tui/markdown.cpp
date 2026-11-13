// Karta — Markdown renderer implementation.

#include "karta/tui/markdown.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace karta::tui {

static std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

static std::vector<MarkdownRender> render_inline(const std::string& text) {
  std::vector<MarkdownRender> renders;
  std::string remaining = text;
  std::size_t pos = 0;

  while (pos < remaining.size()) {
    // Check for **bold**
    if (pos + 1 < remaining.size() && remaining[pos] == '*' && remaining[pos + 1] == '*') {
      std::size_t end = remaining.find("**", pos + 2);
      if (end != std::string::npos) {
        renders.push_back({MarkdownRender::NodeType::Bold, remaining.substr(pos + 2, end - pos - 2)});
        pos = end + 2;
        continue;
      }
    }

    // Check for *italic* (but not **)
    if (pos + 1 < remaining.size() && remaining[pos] == '*' && remaining[pos + 1] != '*') {
      std::size_t end = remaining.find("*", pos + 1);
      if (end != std::string::npos) {
        renders.push_back({MarkdownRender::NodeType::Italic, remaining.substr(pos + 1, end - pos - 1)});
        pos = end + 1;
        continue;
      }
    }

    // Check for `code`
    if (remaining[pos] == '`') {
      std::size_t end = remaining.find('`', pos + 1);
      if (end != std::string::npos) {
        renders.push_back({MarkdownRender::NodeType::Code, remaining.substr(pos + 1, end - pos - 1)});
        pos = end + 1;
        continue;
      }
    }

    // Check for [link](url)
    if (remaining[pos] == '[') {
      std::size_t bracket_end = remaining.find(']', pos + 1);
      if (bracket_end != std::string::npos) {
        std::size_t paren = remaining.find('(', bracket_end + 1);
        if (paren != std::string::npos) {
          std::size_t close_paren = remaining.find(')', paren + 1);
          if (close_paren != std::string::npos) {
            std::string label = remaining.substr(pos + 1, bracket_end - pos - 1);
            std::string url = remaining.substr(paren + 1, close_paren - paren - 1);
            renders.push_back({MarkdownRender::NodeType::Link, label, url});
            pos = close_paren + 1;
            continue;
          }
        }
      }
    }

    // Regular text - consume until next markdown marker or newline
    std::size_t next_mark = remaining.size();
    std::size_t next_newline = remaining.find('\n', pos);
    if (next_newline != std::string::npos) next_mark = next_newline;
    
    // Find next markdown marker
    for (std::size_t p = pos; p < next_mark; ++p) {
      char c = remaining[p];
      // Check for ** (bold)
      if (p + 1 < next_mark && remaining[p] == '*' && remaining[p + 1] == '*') {
        next_mark = p;
        break;
      }
      // Check for * (italic) - not **
      if (p + 1 < next_mark && remaining[p] == '*' && remaining[p + 1] != '*') {
        next_mark = p;
        break;
      }
      // Check for ` (code)
      if (remaining[p] == '`') {
        next_mark = p;
        break;
      }
      // Check for [ (link)
      if (remaining[p] == '[') {
        next_mark = p;
        break;
      }
    }
    
    if (next_mark > pos) {
      renders.push_back({MarkdownRender::NodeType::Text, remaining.substr(pos, next_mark - pos)});
      pos = next_mark;
    } else {
      // No more plain text, loop will check for markdown markers
    }
  }

  return renders;
}

std::vector<MarkdownRender> render_markdown(const std::string& text, std::size_t width) {
  (void)width;  // width used for future wrapping
  std::vector<MarkdownRender> result;

  // Split into lines first
  auto lines = split_lines(text);

  for (const auto& line : lines) {
    auto inline_renders = render_inline(line);
    result.insert(result.end(), inline_renders.begin(), inline_renders.end());

    // Add a break marker between lines (except after the last one)
    if (&line != &lines.back()) {
      result.push_back({MarkdownRender::NodeType::Break, ""});
    }
  }

  return result;
}

}  // namespace karta::tui