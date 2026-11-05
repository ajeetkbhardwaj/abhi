// Karta — dependency-free JSON for the agent core.
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace karta {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json();
  Json(std::nullptr_t);
  Json(bool value);
  Json(int value);
  Json(long value);
  Json(long long value);
  Json(double value);
  Json(const char* value);
  Json(std::string value);

  static Json array();
  static Json object();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const;
  double as_double(double fallback = 0.0) const;
  long long as_int(long long fallback = 0) const;
  const std::string& as_string() const;

  const std::vector<Json>& items() const { return array_; }
  std::vector<Json>& items() { return array_; }
  const std::map<std::string, Json>& fields() const { return object_; }

  bool contains(const std::string& key) const;
  const Json& operator[](const std::string& key) const;
  Json& operator[](const std::string& key);
  const Json& operator[](std::size_t index) const;
  Json& operator[](std::size_t index);

  void push_back(Json value);
  void set(std::string key, Json value);
  std::size_t size() const;

  std::string dump() const;
  static Json parse(const std::string& text, std::string* error = nullptr);

  // Missing-key sentinel, shared by the const accessors.
  static const Json& null_value();

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::map<std::string, Json> object_;
};

std::string json_escape(const std::string& value);

}  // namespace karta
