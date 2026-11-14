// Minimal JSON value, parser and serializer. Enough for the command envelope,
// status object and bridge protocol. Portable C++17, no dependencies.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace airo {

class Json {
 public:
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool b) : type_(Type::Bool), bool_(b) {}
  Json(int v) : type_(Type::Number), num_(v) {}
  Json(unsigned v) : type_(Type::Number), num_(v) {}
  Json(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(unsigned long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(unsigned long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(double v) : type_(Type::Number), num_(v) {}
  Json(const char* s) : type_(Type::String), str_(s) {}
  Json(std::string s) : type_(Type::String), str_(std::move(s)) {}
  Json(Array a) : type_(Type::Array), arr_(std::move(a)) {}
  Json(Object o) : type_(Type::Object), obj_(std::move(o)) {}
  template <typename T>
  Json(const std::optional<T>& o) {
    if (o) *this = Json(*o);
  }

  static Json object() { return Json(Object{}); }
  static Json array() { return Json(Array{}); }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool def = false) const { return isBool() ? bool_ : def; }
  double asNumber(double def = 0) const { return isNumber() ? num_ : def; }
  const std::string& asString() const;
  const Array& asArray() const;
  const Object& asObject() const;
  Array& mutArray();

  /// Object member access; returns a null Json if missing or not an object.
  const Json& operator[](const std::string& key) const;
  bool has(const std::string& key) const;
  Json& set(const std::string& key, Json v);
  Json& push(Json v);

  std::string dump() const;
  static std::optional<Json> parse(const std::string& text);

 private:
  void dumpTo(std::string& out) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  Array arr_;
  Object obj_;
};

}  // namespace airo
