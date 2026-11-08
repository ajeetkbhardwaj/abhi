#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace karta {
namespace {

const Json kNullSingleton;

struct Parser {
  const std::string& text;
  std::size_t pos = 0;
  std::string error;

  explicit Parser(const std::string& input) : text(input) {}

  void skip_ws() {
    while (pos < text.size()) {
      const char c = text[pos];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos;
      } else {
        break;
      }
    }
  }

  bool fail(const std::string& message) {
    if (error.empty()) {
      std::ostringstream out;
      out << message << " at byte " << pos;
      error = out.str();
    }
    return false;
  }

  bool parse_value(Json& out) {
    skip_ws();
    if (pos >= text.size()) return fail("unexpected end of input");
    const char c = text[pos];
    switch (c) {
      case '{':
        return parse_object(out);
      case '[':
        return parse_array(out);
      case '"': {
        std::string value;
        if (!parse_string(value)) return false;
        out = Json(std::move(value));
        return true;
      }
      case 't':
        return literal("true", Json(true), out);
      case 'f':
        return literal("false", Json(false), out);
      case 'n':
        return literal("null", Json(), out);
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
        return fail("unexpected character");
    }
  }

  bool literal(const char* word, Json value, Json& out) {
    const std::size_t length = std::char_traits<char>::length(word);
    if (text.compare(pos, length, word) != 0) return fail("invalid literal");
    pos += length;
    out = std::move(value);
    return true;
  }

  bool parse_number(Json& out) {
    const std::size_t start = pos;
    if (pos < text.size() && text[pos] == '-') ++pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    if (pos < text.size() && text[pos] == '.') {
      ++pos;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
      ++pos;
      if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    }
    const std::string slice = text.substr(start, pos - start);
    char* end = nullptr;
    const double value = std::strtod(slice.c_str(), &end);
    if (end == slice.c_str() || *end != '\0') return fail("invalid number");
    out = Json(value);
    return true;
  }

  static void append_utf8(std::string& out, unsigned int code) {
    if (code <= 0x7F) {
      out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  bool parse_hex4(unsigned int& value) {
    if (pos + 4 > text.size()) return fail("truncated unicode escape");
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text[pos++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<unsigned int>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<unsigned int>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<unsigned int>(c - 'A' + 10);
      } else {
        return fail("invalid unicode escape");
      }
    }
    return true;
  }

  bool parse_string(std::string& out) {
    if (pos >= text.size() || text[pos] != '"') return fail("expected string");
    ++pos;
    out.clear();
    while (pos < text.size()) {
      const char c = text[pos++];
      if (c == '"') return true;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos >= text.size()) return fail("truncated escape");
      const char esc = text[pos++];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned int code = 0;
          if (!parse_hex4(code)) return false;
          if (code >= 0xD800 && code <= 0xDBFF && pos + 6 <= text.size() && text[pos] == '\\' &&
              text[pos + 1] == 'u') {
            pos += 2;
            unsigned int low = 0;
            if (!parse_hex4(low)) return false;
            if (low >= 0xDC00 && low <= 0xDFFF) {
              code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            }
          }
          append_utf8(out, code);
          break;
        }
        default:
          return fail("invalid escape");
      }
    }
    return fail("unterminated string");
  }

  bool parse_array(Json& out) {
    ++pos;  // '['
    out = Json::array();
    skip_ws();
    if (pos < text.size() && text[pos] == ']') {
      ++pos;
      return true;
    }
    while (true) {
      Json value;
      if (!parse_value(value)) return false;
      out.push_back(std::move(value));
      skip_ws();
      if (pos >= text.size()) return fail("unterminated array");
      const char c = text[pos++];
      if (c == ']') return true;
      if (c != ',') return fail("expected ',' or ']'");
    }
  }

  bool parse_object(Json& out) {
    ++pos;  // '{'
    out = Json::object();
    skip_ws();
    if (pos < text.size() && text[pos] == '}') {
      ++pos;
      return true;
    }
    while (true) {
      skip_ws();
      std::string key;
      if (!parse_string(key)) return false;
      skip_ws();
      if (pos >= text.size() || text[pos] != ':') return fail("expected ':'");
      ++pos;
      Json value;
      if (!parse_value(value)) return false;
      out.set(std::move(key), std::move(value));
      skip_ws();
      if (pos >= text.size()) return fail("unterminated object");
      const char c = text[pos++];
      if (c == '}') return true;
      if (c != ',') return fail("expected ',' or '}'");
    }
  }
};

void dump_string(const std::string& value, std::string& out) {
  out.push_back('"');
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          out += buffer;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
}

void dump_value(const Json& value, std::string& out) {
  switch (value.type()) {
    case Json::Type::Null:
      out += "null";
      break;
    case Json::Type::Bool:
      out += value.as_bool() ? "true" : "false";
      break;
    case Json::Type::Number: {
      const double number = value.as_double();
      if (std::isfinite(number) && std::floor(number) == number && std::fabs(number) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
        out += buffer;
      } else if (!std::isfinite(number)) {
        out += "null";
      } else {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.17g", number);
        out += buffer;
      }
      break;
    }
    case Json::Type::String:
      dump_string(value.as_string(), out);
      break;
    case Json::Type::Array: {
      out.push_back('[');
      bool first = true;
      for (const Json& item : value.items()) {
        if (!first) out.push_back(',');
        first = false;
        dump_value(item, out);
      }
      out.push_back(']');
      break;
    }
    case Json::Type::Object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, item] : value.fields()) {
        if (!first) out.push_back(',');
        first = false;
        dump_string(key, out);
        out.push_back(':');
        dump_value(item, out);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

Json::Json() = default;
Json::Json(std::nullptr_t) {}
Json::Json(bool value) : type_(Type::Bool), bool_(value) {}
Json::Json(int value) : type_(Type::Number), number_(value) {}
Json::Json(long value) : type_(Type::Number), number_(static_cast<double>(value)) {}
Json::Json(long long value) : type_(Type::Number), number_(static_cast<double>(value)) {}
Json::Json(double value) : type_(Type::Number), number_(value) {}
Json::Json(const char* value) : type_(Type::String), string_(value ? value : "") {}
Json::Json(std::string value) : type_(Type::String), string_(std::move(value)) {}

Json Json::array() {
  Json value;
  value.type_ = Type::Array;
  return value;
}

Json Json::object() {
  Json value;
  value.type_ = Type::Object;
  return value;
}

bool Json::as_bool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }

double Json::as_double(double fallback) const { return type_ == Type::Number ? number_ : fallback; }

long long Json::as_int(long long fallback) const {
  return type_ == Type::Number ? static_cast<long long>(number_) : fallback;
}

const std::string& Json::as_string() const { return type_ == Type::String ? string_ : null_value().string_; }

bool Json::contains(const std::string& key) const {
  return type_ == Type::Object && object_.find(key) != object_.end();
}

const Json& Json::operator[](const std::string& key) const {
  if (type_ != Type::Object) return null_value();
  const auto it = object_.find(key);
  return it == object_.end() ? null_value() : it->second;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    object_.clear();
  }
  return object_[key];
}

const Json& Json::operator[](std::size_t index) const {
  if (type_ != Type::Array || index >= array_.size()) return null_value();
  return array_[index];
}

Json& Json::operator[](std::size_t index) { return array_[index]; }

void Json::push_back(Json value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    array_.clear();
  }
  array_.push_back(std::move(value));
}

void Json::set(std::string key, Json value) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    object_.clear();
  }
  object_[std::move(key)] = std::move(value);
}

std::size_t Json::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  if (type_ == Type::String) return string_.size();
  return 0;
}

std::string Json::dump() const {
  std::string out;
  out.reserve(256);
  dump_value(*this, out);
  return out;
}

Json Json::parse(const std::string& text, std::string* error) {
  Parser parser(text);
  Json value;
  if (!parser.parse_value(value)) {
    if (error) *error = parser.error;
    return Json();
  }
  parser.skip_ws();
  if (parser.pos != text.size()) {
    if (error) *error = "trailing characters after JSON value";
    return Json();
  }
  return value;
}

const Json& Json::null_value() { return kNullSingleton; }

std::string json_escape(const std::string& value) {
  std::string out;
  dump_string(value, out);
  return out;
}

}  // namespace karta
