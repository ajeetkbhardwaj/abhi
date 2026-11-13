// Karta — Markdown renderer (mirrors pi packages/tui/src/components/markdown.ts).
#pragma once

#include <string>
#include <vector>
#include <utility>

namespace karta::tui {

struct MarkdownRender {
  enum class NodeType { Text, Bold, Italic, Code, Heading, Link, ListItem, Break };
  NodeType type;
  std::string content;
  std::string href = "";  // for Link nodes
};

std::vector<MarkdownRender> render_markdown(const std::string& text, std::size_t width);

}  // namespace karta::tui