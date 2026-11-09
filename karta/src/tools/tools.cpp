#include "karta/tools/tools.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

#include "karta/core/platform.h"

namespace fs = std::filesystem;

namespace karta {
namespace {

std::string lower_copy(std::string value) {
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

std::string trim_right(std::string value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  return value;
}

std::string resolve_path(const std::string& input, const std::string& cwd) {
  if (input.empty()) return cwd;
  fs::path path(input);
  if (input == "~") return home_directory();
  if (input.rfind("~/", 0) == 0 || input.rfind("~\\", 0) == 0) {
    path = fs::path(home_directory()) / input.substr(2);
  } else if (path.is_relative()) {
    path = fs::path(cwd) / path;
  }
  std::error_code error;
  const fs::path normalized = path.lexically_normal();
  return normalized.string();
}

std::string display_path(const fs::path& path, const std::string& cwd) {
  std::error_code error;
  const fs::path relative = fs::relative(path, cwd, error);
  if (!error && !relative.empty() && relative.native().rfind("..", 0) != 0) {
    return relative.generic_string();
  }
  return path.generic_string();
}

bool read_file(const std::string& path, std::string& out, std::string& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "cannot read file: " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  out = buffer.str();
  return true;
}

bool write_file(const std::string& path, const std::string& content, std::string& error) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "cannot write file: " + path;
    return false;
  }
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!stream) {
    error = "write failed: " + path;
    return false;
  }
  return true;
}

bool looks_binary(const std::string& data) {
  const std::size_t limit = std::min<std::size_t>(data.size(), 8192);
  for (std::size_t i = 0; i < limit; ++i) {
    if (data[i] == '\0') return true;
  }
  return false;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (const char c : text) {
    if (c == '\n') {
      if (!current.empty() && current.back() == '\r') current.pop_back();
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') current.pop_back();
    lines.push_back(current);
  }
  return lines;
}

// --------------------------------------------------------------- glob matching

std::string regex_escape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (std::string(".^$+{}()|\\[]*?").find(c) != std::string::npos) out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::string glob_body(const std::string& glob) {
  std::string out;
  for (std::size_t i = 0; i < glob.size(); ++i) {
    const char c = glob[i];
    if (c == '*') {
      if (i + 1 < glob.size() && glob[i + 1] == '*') {
        ++i;
        if (i + 1 < glob.size() && glob[i + 1] == '/') {
          ++i;
          out += "(?:.*/)?";
        } else {
          out += ".*";
        }
      } else {
        out += "[^/]*";
      }
    } else if (c == '?') {
      out += "[^/]";
    } else if (c == '[') {
      const std::size_t end = glob.find(']', i + 1);
      if (end != std::string::npos) {
        out += glob.substr(i, end - i + 1);
        i = end;
      } else {
        out += "\\[";
      }
    } else if (std::string(".^$+{}()|\\").find(c) != std::string::npos) {
      out.push_back('\\');
      out.push_back(c);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool glob_match(const std::string& glob, const std::string& text, bool ignore_case) {
  try {
    const auto flags = std::regex::ECMAScript | (ignore_case ? std::regex::icase : std::regex::ECMAScript);
    const std::regex pattern("^" + glob_body(glob) + "$", flags);
    return std::regex_match(text, pattern);
  } catch (const std::regex_error&) {
    return false;
  }
}

std::string glob_to_regex_string(const std::string& glob, bool anchored, bool dir_only) {
  std::string regex = anchored ? "^" : "(?:^|/)";
  regex += glob_body(glob);
  if (dir_only) regex += "(?:/.*)?";
  regex += "$";
  return regex;
}

// -------------------------------------------------------------- ignore handling

struct IgnoreRules {
  std::vector<std::regex> patterns;
  bool has_negation = false;

  bool ignored(const std::string& relative, bool is_dir) const {
    if (relative.empty()) return false;
    bool result = false;
    for (const auto& pattern : patterns) {
      if (std::regex_search(relative, pattern)) result = true;
    }
    return result;
  }
};

IgnoreRules load_ignore_rules(const fs::path& root) {
  IgnoreRules rules;
  std::string content;
  std::string error;
  if (!read_file((root / ".gitignore").string(), content, error)) return rules;
  std::istringstream stream(content);
  std::string line;
  const bool ignore_case = os_description() == "windows";
  while (std::getline(stream, line)) {
    line = trim_right(line);
    if (line.empty() || line[0] == '#') continue;
    bool negate = false;
    if (line[0] == '!') {
      negate = true;
      line.erase(line.begin());
    }
    if (line.empty()) continue;
    if (negate) {
      rules.has_negation = true;
      continue;  // Karta prunes conservatively; negations only disable pruning.
    }
    bool dir_only = false;
    if (line.back() == '/') {
      dir_only = true;
      line.pop_back();
    }
    if (!line.empty() && line[0] == '/') line.erase(line.begin());
    if (line.empty()) continue;
    const bool anchored = line.find('/') != std::string::npos;
    try {
      rules.patterns.emplace_back(
          glob_to_regex_string(line, anchored, dir_only),
          ignore_case ? (std::regex::ECMAScript | std::regex::icase) : std::regex::ECMAScript);
    } catch (const std::regex_error&) {
      // A malformed ignore line must not break a search.
    }
  }
  return rules;
}

const std::set<std::string>& always_skip_directories() {
  static const std::set<std::string> names = {".git", "node_modules", ".venv", "__pycache__",
                                              ".hg", ".svn", ".cache"};
  return names;
}

using FileVisitor = std::function<void(const fs::directory_entry&)>;

void walk_files(const fs::path& root, const IgnoreRules& rules, const FileVisitor& visitor) {
  std::error_code error;
  fs::recursive_directory_iterator iterator(
      root, fs::directory_options::skip_permission_denied, error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const fs::directory_entry entry = *iterator;
    const std::string relative = fs::relative(entry.path(), root, error).generic_string();
    error.clear();
    const bool is_dir = entry.is_directory(error);
    if (error) {
      error.clear();
      iterator.increment(error);
      continue;
    }
    const std::string name = entry.path().filename().string();
    const bool skip = is_dir && (always_skip_directories().count(name) > 0 ||
                                 (rules.ignored(relative, true) && !rules.has_negation));
    if (skip) {
      iterator.disable_recursion_pending();
    } else if (is_dir) {
      // Descend.
    } else if (rules.ignored(relative, false)) {
      // Ignored file.
    } else {
      visitor(entry);
    }
    iterator.increment(error);
  }
}

// ------------------------------------------------------------------- truncation

struct Truncated {
  std::string text;
  bool truncated = false;
  std::size_t kept_lines = 0;
  std::size_t total_lines = 0;
};

Truncated truncate_head(const std::string& text, std::size_t max_lines, std::size_t max_bytes) {
  Truncated result;
  std::size_t bytes = 0;
  std::size_t lines = 0;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t newline = text.find('\n', offset);
    const std::size_t end = newline == std::string::npos ? text.size() : newline + 1;
    const std::size_t length = end - offset;
    result.total_lines++;
    if (lines + 1 > max_lines || bytes + length > max_bytes) {
      result.truncated = true;
      // Keep counting remaining lines for an accurate notice.
      std::size_t rest = end;
      while (true) {
        const std::size_t next = text.find('\n', rest);
        if (next == std::string::npos) break;
        result.total_lines++;
        rest = next + 1;
      }
      break;
    }
    bytes += length;
    lines++;
    result.text.append(text, offset, length);
    offset = end;
  }
  result.kept_lines = lines;
  if (!result.truncated) {
    result.text = text;
    result.total_lines = lines;
  }
  return result;
}

std::string size_label(std::size_t bytes) {
  std::ostringstream out;
  if (bytes >= 1024 * 1024) {
    out << (bytes / (1024.0 * 1024.0)) << "MB";
  } else if (bytes >= 1024) {
    out << (bytes / 1024.0) << "KB";
  } else {
    out << bytes << "B";
  }
  return out.str();
}

// ---------------------------------------------------------------------- schemas

Json schema_object(const Json& properties, const std::vector<std::string>& required) {
  Json schema = Json::object();
  schema.set("type", "object");
  schema.set("properties", properties);
  Json required_array = Json::array();
  for (const std::string& name : required) required_array.push_back(name);
  schema.set("required", required_array);
  schema.set("additionalProperties", false);
  return schema;
}

Json string_property(const std::string& description) {
  Json property = Json::object();
  property.set("type", "string");
  property.set("description", description);
  return property;
}

Json number_property(const std::string& description) {
  Json property = Json::object();
  property.set("type", "number");
  property.set("description", description);
  return property;
}

Json boolean_property(const std::string& description) {
  Json property = Json::object();
  property.set("type", "boolean");
  property.set("description", description);
  return property;
}

// ------------------------------------------------------------------------ tools

ToolOutcome bash_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string command = args["command"].as_string();
  if (command.empty()) {
    outcome.text = "command is required";
    outcome.is_error = true;
    return outcome;
  }
  int timeout = static_cast<int>(args["timeout"].as_int(0));
  if (timeout < 0) timeout = 0;
  const int kMaxTimeout = 2147483;
  if (timeout > kMaxTimeout) {
    outcome.text = "Invalid timeout: maximum is " + std::to_string(kMaxTimeout) + " seconds";
    outcome.is_error = true;
    return outcome;
  }

  const ProcessResult result = run_shell(command, context.cwd, timeout, kMaxBytes);
  outcome.text = result.output;
  if (result.timed_out) {
    outcome.is_error = true;
    outcome.text += "\n\n[timeout: " + std::to_string(timeout) + "s]";
  }
  if (result.truncated && !result.full_output_path.empty()) {
    outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached; full output: " +
                    result.full_output_path + "]";
  }
  if (result.output.empty() && !result.timed_out) outcome.text = "(no output)";
  if (result.exit_code != 0 && !result.timed_out) {
    outcome.text += "\n\n[exit code: " + std::to_string(result.exit_code) + "]";
    outcome.is_error = true;
  }
  return outcome;
}

ToolOutcome read_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string path = resolve_path(requested, context.cwd);
  std::string data;
  std::string error;
  if (!read_file(path, data, error)) {
    outcome.text = error;
    outcome.is_error = true;
    return outcome;
  }

  const std::string extension = lower_copy(fs::path(path).extension().string());
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".gif" ||
      extension == ".webp" || extension == ".bmp") {
    outcome.text = "[Image attachments are not supported by this port; inspect with bash instead.]";
    outcome.is_error = true;
    return outcome;
  }
  if (looks_binary(data)) {
    outcome.text = "[Binary file; use bash to inspect: " + display_path(path, context.cwd) + "]";
    outcome.is_error = true;
    return outcome;
  }
  if (data.rfind("\xEF\xBB\xBF", 0) == 0) data.erase(0, 3);

  const std::vector<std::string> lines = split_lines(data);
  const std::size_t total = lines.size();
  long long offset = args["offset"].as_int(1);
  if (offset < 1) offset = 1;
  const long long requested_limit = args["limit"].as_int(0);
  const std::size_t start = static_cast<std::size_t>(offset - 1);
  if (start >= total && !(start == 0 && total == 0)) {
    outcome.text = "[offset " + std::to_string(offset) + " is past end of file (" +
                   std::to_string(total) + " lines)]";
    outcome.is_error = true;
    return outcome;
  }
  std::size_t count = total - start;
  if (requested_limit > 0) count = std::min<std::size_t>(count, static_cast<std::size_t>(requested_limit));

  std::string selected;
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) selected.push_back('\n');
    selected += lines[start + i];
  }
  const Truncated truncated = truncate_head(selected, kMaxLines, kMaxBytes);
  outcome.text = truncated.text;
  if (truncated.truncated) {
    const std::size_t shown = start + truncated.kept_lines + 1;
    outcome.text += "\n\n[" + std::to_string(truncated.kept_lines) + " lines shown; " +
                    size_label(kMaxBytes) + " limit. Use offset=" + std::to_string(shown) +
                    " to continue.]";
  } else if (count < total) {
    outcome.text += "\n\n[Showing lines " + std::to_string(offset) + "-" +
                    std::to_string(offset + static_cast<long long>(count) - 1) + " of " +
                    std::to_string(total) + ". Use offset=" +
                    std::to_string(offset + static_cast<long long>(count)) + " to continue.]";
  }
  if (total == 0) outcome.text = "(empty file)";
  return outcome;
}

ToolOutcome write_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string path = resolve_path(requested, context.cwd);
  std::error_code error;
  const fs::path parent = fs::path(path).parent_path();
  if (!parent.empty()) fs::create_directories(parent, error);
  const std::string content = args["content"].as_string();
  std::string write_error;
  if (!write_file(path, content, write_error)) {
    outcome.text = write_error;
    outcome.is_error = true;
    return outcome;
  }
  outcome.text = "Wrote " + std::to_string(content.size()) + " bytes to " +
                 display_path(path, context.cwd);
  return outcome;
}

ToolOutcome edit_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  const Json& edits = args["edits"];
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  if (!edits.is_array() || edits.items().empty()) {
    outcome.text = "Edit tool input is invalid. edits must contain at least one replacement.";
    outcome.is_error = true;
    return outcome;
  }

  const std::string path = resolve_path(requested, context.cwd);
  std::string original;
  std::string error;
  if (!read_file(path, original, error)) {
    outcome.text = error;
    outcome.is_error = true;
    return outcome;
  }

  struct Replacement {
    std::size_t position;
    std::size_t length;
    std::string text;
  };
  std::vector<Replacement> replacements;

  for (std::size_t index = 0; index < edits.items().size(); ++index) {
    const Json& edit = edits.items()[index];
    const std::string old_text = edit["oldText"].as_string();
    const std::string new_text = edit["newText"].as_string();
    if (old_text.empty()) {
      outcome.text = "edits[" + std::to_string(index) + "].oldText must not be empty";
      outcome.is_error = true;
      return outcome;
    }
    const std::size_t position = original.find(old_text);
    if (position == std::string::npos) {
      outcome.text = "edits[" + std::to_string(index) + "].oldText was not found in " +
                     display_path(path, context.cwd);
      outcome.is_error = true;
      return outcome;
    }
    if (original.find(old_text, position + 1) != std::string::npos) {
      outcome.text = "edits[" + std::to_string(index) +
                     "].oldText matches more than once; include more context to make it unique";
      outcome.is_error = true;
      return outcome;
    }
    replacements.push_back({position, old_text.size(), new_text});
  }

  std::sort(replacements.begin(), replacements.end(),
            [](const Replacement& a, const Replacement& b) { return a.position < b.position; });
  for (std::size_t i = 1; i < replacements.size(); ++i) {
    const Replacement& previous = replacements[i - 1];
    if (previous.position + previous.length > replacements[i].position) {
      outcome.text = "edits overlap; merge them into one edit";
      outcome.is_error = true;
      return outcome;
    }
  }

  std::string updated = original;
  for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
    updated.replace(it->position, it->length, it->text);
  }
  std::string write_error;
  if (!write_file(path, updated, write_error)) {
    outcome.text = write_error;
    outcome.is_error = true;
    return outcome;
  }
  outcome.text = "Applied " + std::to_string(replacements.size()) + " edit" +
                 (replacements.size() == 1 ? "" : "s") + " to " +
                 display_path(path, context.cwd);
  return outcome;
}

ToolOutcome grep_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested_pattern = args["pattern"].as_string();
  if (requested_pattern.empty()) {
    outcome.text = "pattern is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string search_path = resolve_path(args["path"].as_string(), context.cwd);
  const std::string glob = args["glob"].as_string();
  const bool ignore_case = args["ignoreCase"].as_bool(false);
  const bool literal = args["literal"].as_bool(false);
  long long context_lines = args["context"].as_int(0);
  if (context_lines < 0) context_lines = 0;
  long long limit = args["limit"].as_int(100);
  if (limit < 1) limit = 1;

  std::regex pattern;
  try {
    auto flags = std::regex::ECMAScript | (ignore_case ? std::regex::icase : std::regex::ECMAScript);
    pattern = std::regex(literal ? regex_escape(requested_pattern) : requested_pattern, flags);
  } catch (const std::regex_error&) {
    outcome.text = "invalid regex: " + requested_pattern;
    outcome.is_error = true;
    return outcome;
  }

  const IgnoreRules rules = load_ignore_rules(search_path);
  std::vector<std::pair<std::string, std::string>> files;  // display name, absolute path
  std::error_code error;
  if (fs::is_regular_file(search_path, error)) {
    files.emplace_back(display_path(search_path, context.cwd), search_path);
  } else if (fs::is_directory(search_path, error)) {
    walk_files(search_path, rules, [&](const fs::directory_entry& entry) {
      const std::string relative = fs::relative(entry.path(), search_path).generic_string();
      if (!glob.empty() && !glob_match(glob, relative, ignore_case) &&
          !glob_match(glob, entry.path().filename().string(), ignore_case)) {
        return;
      }
      files.emplace_back(display_path(entry.path(), context.cwd), entry.path().string());
    });
  } else {
    outcome.text = "path not found: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  std::sort(files.begin(), files.end());

  std::ostringstream output;
  long long matches = 0;
  bool limit_hit = false;
  long long printed_line = -1;
  std::string last_file;

  for (const auto& file : files) {
    std::string content;
    std::string read_error;
    if (!read_file(file.second, content, read_error) || looks_binary(content)) continue;
    const std::vector<std::string> lines = split_lines(content);
    for (std::size_t index = 0; index < lines.size(); ++index) {
      if (!std::regex_search(lines[index], pattern)) continue;
      if (matches >= limit) {
        limit_hit = true;
        break;
      }
      matches++;
      const long long first = std::max<long long>(0, static_cast<long long>(index) - context_lines);
      const long long last = std::min<long long>(
          static_cast<long long>(lines.size()) - 1, static_cast<long long>(index) + context_lines);
      for (long long line = first; line <= last; ++line) {
        const bool is_match = line == static_cast<long long>(index);
        if (file.first != last_file) {
          last_file = file.first;
          printed_line = -1;
        }
        if (line <= printed_line && is_match) continue;
        if (line <= printed_line) continue;
        std::string text = lines[static_cast<std::size_t>(line)];
        if (text.size() > 500) text = text.substr(0, 500) + "…";
        output << file.first << (is_match ? ":" : "-") << (line + 1) << (is_match ? ":" : "-") << text
               << "\n";
        printed_line = line;
      }
    }
    if (limit_hit) break;
  }

  if (matches == 0) {
    outcome.text = "No matches found";
    return outcome;
  }
  outcome.text = output.str();
  while (!outcome.text.empty() && outcome.text.back() == '\n') outcome.text.pop_back();
  const Truncated truncated = truncate_head(outcome.text, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) + " matches limit reached. Use limit=" +
                    std::to_string(limit * 2) + " for more, or refine pattern]";
  }
  if (truncated.truncated) {
    outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  }
  return outcome;
}

ToolOutcome glob_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string pattern = args["pattern"].as_string();
  if (pattern.empty()) {
    outcome.text = "pattern is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string search_path = resolve_path(args["path"].as_string(), context.cwd);
  long long limit = args["limit"].as_int(1000);
  if (limit < 1) limit = 1;

  std::error_code error;
  if (!fs::is_directory(search_path, error)) {
    outcome.text = "not a directory: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  const IgnoreRules rules = load_ignore_rules(search_path);
  const bool ignore_case = os_description() == "windows";

  std::vector<std::string> results;
  walk_files(search_path, rules, [&](const fs::directory_entry& entry) {
    const std::string relative = fs::relative(entry.path(), search_path).generic_string();
    if (glob_match(pattern, relative, ignore_case)) results.push_back(relative);
  });
  std::sort(results.begin(), results.end());

  const bool limit_hit = static_cast<long long>(results.size()) > limit;
  if (limit_hit) results.resize(static_cast<std::size_t>(limit));

  if (results.empty()) {
    outcome.text = "No files found";
    return outcome;
  }
  std::string joined;
  for (const std::string& result : results) joined += result + "\n";
  while (!joined.empty() && joined.back() == '\n') joined.pop_back();
  const Truncated truncated = truncate_head(joined, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) + " results limit reached. Use limit=" +
                    std::to_string(limit * 2) + " for more, or refine pattern]";
  }
  if (truncated.truncated) outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  return outcome;
}

ToolOutcome find_tool_impl(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string pattern = args["pattern"].as_string();
  if (pattern.empty()) {
    outcome.text = "pattern is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string search_path = resolve_path(args["path"].as_string(), context.cwd);
  long long limit = args["limit"].as_int(1000);
  if (limit < 1) limit = 1;

  std::error_code error;
  if (!fs::is_directory(search_path, error)) {
    outcome.text = "not a directory: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  const IgnoreRules rules = load_ignore_rules(search_path);
  const bool ignore_case = os_description() == "windows";
  const bool pattern_has_slash = pattern.find('/') != std::string::npos;

  std::vector<std::string> results;
  walk_files(search_path, rules, [&](const fs::directory_entry& entry) {
    const std::string relative = fs::relative(entry.path(), search_path).generic_string();
    const std::string candidate = pattern_has_slash ? relative : entry.path().filename().string();
    if (glob_match(pattern, candidate, ignore_case)) results.push_back(relative);
  });
  std::sort(results.begin(), results.end());

  const bool limit_hit = static_cast<long long>(results.size()) > limit;
  if (limit_hit) results.resize(static_cast<std::size_t>(limit));

  if (results.empty()) {
    outcome.text = "No files found";
    return outcome;
  }
  std::string joined;
  for (const std::string& result : results) joined += result + "\n";
  while (!joined.empty() && joined.back() == '\n') joined.pop_back();
  const Truncated truncated = truncate_head(joined, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) + " results limit reached. Use limit=" +
                    std::to_string(limit * 2) + " for more, or refine pattern]";
  }
  if (truncated.truncated) outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  return outcome;
}

ToolOutcome ls_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string path = resolve_path(args["path"].as_string(), context.cwd);
  long long limit = args["limit"].as_int(500);
  if (limit < 1) limit = 1;

  std::error_code error;
  if (!fs::is_directory(path, error)) {
    outcome.text = "not a directory: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  std::vector<std::pair<std::string, bool>> entries;
  fs::directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
  const fs::directory_iterator end;
  while (!error && iterator != end) {
    const bool is_dir = iterator->is_directory(error);
    error.clear();
    std::string name = iterator->path().filename().string();
    if (is_dir) name.push_back('/');
    entries.emplace_back(name, is_dir);
    iterator.increment(error);
  }
  std::sort(entries.begin(), entries.end());

  const bool limit_hit = static_cast<long long>(entries.size()) > limit;
  if (limit_hit) entries.resize(static_cast<std::size_t>(limit));
  if (entries.empty()) {
    outcome.text = "(empty directory)";
    return outcome;
  }
  std::string joined;
  for (const auto& entry : entries) joined += entry.first + "\n";
  while (!joined.empty() && joined.back() == '\n') joined.pop_back();
  const Truncated truncated = truncate_head(joined, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) + " entries limit reached. Use limit=" +
                    std::to_string(limit * 2) + " for more]";
  }
  if (truncated.truncated) outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  return outcome;
}

#ifdef _WIN32
ToolOutcome powershell_tool(const Json& args, ToolContext& context) {
  Json wrapped = args;
  wrapped.set("command", "powershell.exe -NoProfile -NonInteractive -Command \"" +
                             args["command"].as_string() + "\"");
  return bash_tool(wrapped, context);
}
#endif

}  // namespace

std::vector<ToolDefinition> builtin_tools() {
  std::vector<ToolDefinition> tools;

  {
    Json properties = Json::object();
    properties.set("command", string_property("Shell command to execute"));
    properties.set("timeout", number_property("Timeout in seconds (optional, no default timeout)"));
    ToolDefinition tool;
    tool.name = "bash";
    tool.description =
        "Execute a shell command in the working directory. Returns combined stdout and stderr, "
        "truncated to 2000 lines or 50KB (whichever is hit first). Use for builds, tests, git, "
        "and any command not covered by the dedicated tools.";
    tool.parameters = schema_object(properties, {"command"});
    tool.execute = bash_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to read (relative or absolute)"));
    properties.set("offset", number_property("Line number to start reading from (1-indexed)"));
    properties.set("limit", number_property("Maximum number of lines to read"));
    ToolDefinition tool;
    tool.name = "read";
    tool.description =
        "Read the contents of a text file. Output is truncated to 2000 lines or 50KB (whichever "
        "is hit first). Use offset/limit for large files and continue with offset until complete.";
    tool.parameters = schema_object(properties, {"path"});
    tool.execute = read_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to write (relative or absolute)"));
    properties.set("content", string_property("Content to write to the file"));
    ToolDefinition tool;
    tool.name = "write";
    tool.description =
        "Write content to a file, creating parent directories as needed. Overwrites the whole "
        "file; prefer edit for targeted changes.";
    tool.parameters = schema_object(properties, {"path", "content"});
    tool.execute = write_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json edit_properties = Json::object();
    edit_properties.set("oldText", string_property("Exact text to replace; must be unique."));
    edit_properties.set("newText", string_property("Replacement text for this targeted edit."));
    Json edit_schema = Json::object();
    edit_schema.set("type", "object");
    edit_schema.set("properties", edit_properties);
    Json edit_required = Json::array();
    edit_required.push_back("oldText");
    edit_required.push_back("newText");
    edit_schema.set("required", edit_required);

    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to edit (relative or absolute)"));
    Json edits = Json::object();
    edits.set("type", "array");
    edits.set("description", "One or more targeted replacements.");
    edits.set("items", edit_schema);
    properties.set("edits", edits);

    ToolDefinition tool;
    tool.name = "edit";
    tool.description =
        "Edit a single file using exact text replacement. Every edits[].oldText must match a "
        "unique, non-overlapping region of the original file. Merge nearby changes into one edit.";
    tool.parameters = schema_object(properties, {"path", "edits"});
    tool.execute = edit_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("pattern", string_property("Search pattern (regex or literal string)"));
    properties.set("path", string_property("Directory or file to search (default: current directory)"));
    properties.set("glob", string_property("Filter files by glob pattern, e.g. '*.ts'"));
    properties.set("ignoreCase", boolean_property("Case-insensitive search (default: false)"));
    properties.set("literal", boolean_property("Treat pattern as a literal string (default: false)"));
    properties.set("context", number_property("Lines to show before and after each match (default: 0)"));
    properties.set("limit", number_property("Maximum number of matches to return (default: 100)"));

    ToolDefinition tool;
    tool.name = "grep";
    tool.description =
        "Search file contents for a pattern. Returns matching lines with file paths and line "
        "numbers. Skips .git, node_modules and .gitignore-matched paths. Output is truncated to "
        "100 matches or 50KB.";
    tool.parameters = schema_object(properties, {"pattern"});
    tool.execute = grep_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("pattern", string_property("Glob pattern to match files, e.g. '*.ts' or 'src/**/*.json'"));
    properties.set("path", string_property("Directory to search in (default: current directory)"));
    properties.set("limit", number_property("Maximum number of results (default: 1000)"));
    ToolDefinition tool;
    tool.name = "glob";
    tool.description =
        "Find files matching a glob pattern. Returns matching file paths relative to the search "
        "directory. Skips .git, node_modules and .gitignore-matched paths. Unlike 'find', this "
        "matches the full relative path against the pattern.";
    tool.parameters = schema_object(properties, {"pattern"});
    tool.execute = glob_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("pattern", string_property("Glob pattern to match files, e.g. '*.ts' or 'src/**/*.json'"));
    properties.set("path", string_property("Directory to search in (default: current directory)"));
    properties.set("limit", number_property("Maximum number of results (default: 1000)"));
    ToolDefinition tool;
    tool.name = "find";
    tool.description =
        "Search for files by glob pattern. Returns matching file paths relative to the search "
        "directory. Skips .git, node_modules and .gitignore-matched paths.";
    tool.parameters = schema_object(properties, {"pattern"});
    tool.execute = find_tool_impl;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("path", string_property("Directory to list (default: current directory)"));
    properties.set("limit", number_property("Maximum number of entries to return (default: 500)"));
    ToolDefinition tool;
    tool.name = "ls";
    tool.description =
        "List directory contents, sorted alphabetically, with '/' for directories. Includes "
        "dotfiles. Output is truncated to 500 entries.";
    tool.parameters = schema_object(properties, {"path"});
    tool.execute = ls_tool;
    tools.push_back(std::move(tool));
  }

#ifdef _WIN32
  {
    Json properties = Json::object();
    properties.set("command", string_property("PowerShell command to execute"));
    properties.set("timeout", number_property("Timeout in seconds (optional)"));
    ToolDefinition tool;
    tool.name = "powershell";
    tool.description = "Execute a PowerShell command on Windows. Returns combined output.";
    tool.parameters = schema_object(properties, {"command"});
    tool.execute = powershell_tool;
    tools.push_back(std::move(tool));
  }
#endif

  return tools;
}

const ToolDefinition* find_tool(const std::vector<ToolDefinition>& tools, const std::string& name) {
  for (const ToolDefinition& tool : tools) {
    if (tool.name == name) return &tool;
  }
  return nullptr;
}

Json tool_declaration(const ToolDefinition& tool) {
  Json declaration = Json::object();
  declaration.set("name", tool.name);
  declaration.set("description", tool.description);
  declaration.set("parameters", tool.parameters);
  return declaration;
}

}  // namespace karta
