#include "airo/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace airo {

namespace {
const std::string kEmptyString;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;
const Json kNull;

class Parser {
 public:
  explicit Parser(const std::string& s) : s_(s) {}

  std::optional<Json> parseDocument() {
    auto v = value(0);
    if (!v) return std::nullopt;
    ws();
    if (i_ != s_.size()) return std::nullopt;
    return v;
  }

 private:
  void ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
  }
  bool lit(const char* w) {
    size_t n = 0;
    while (w[n]) ++n;
    if (s_.compare(i_, n, w) != 0) return false;
    i_ += n;
    return true;
  }

  std::optional<Json> value(int depth) {
    if (depth > 32) return std::nullopt;
    ws();
    if (i_ >= s_.size()) return std::nullopt;
    const char c = s_[i_];
    if (c == '{') return object(depth);
    if (c == '[') return array(depth);
    if (c == '"') {
      auto str = string();
      if (!str) return std::nullopt;
      return Json(*str);
    }
    if (lit("true")) return Json(true);
    if (lit("false")) return Json(false);
    if (lit("null")) return Json();
    return number();
  }

  std::optional<Json> number() {
    const size_t start = i_;
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
    bool digits = false;
    while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' ||
                              s_[i_] == 'E' || ((s_[i_] == '-' || s_[i_] == '+') && (s_[i_ - 1] == 'e' || s_[i_ - 1] == 'E')))) {
      digits = digits || (s_[i_] >= '0' && s_[i_] <= '9');
      ++i_;
    }
    if (!digits) return std::nullopt;
    const std::string tok = s_.substr(start, i_ - start);
    char* end = nullptr;
    const double v = std::strtod(tok.c_str(), &end);
    if (end != tok.c_str() + tok.size() || !std::isfinite(v)) return std::nullopt;
    return Json(v);
  }

  static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  std::optional<uint32_t> hex4() {
    if (i_ + 4 > s_.size()) return std::nullopt;
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      const char h = s_[i_++];
      v <<= 4;
      if (h >= '0' && h <= '9') v |= static_cast<uint32_t>(h - '0');
      else if (h >= 'a' && h <= 'f') v |= static_cast<uint32_t>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') v |= static_cast<uint32_t>(h - 'A' + 10);
      else return std::nullopt;
    }
    return v;
  }

  std::optional<std::string> string() {
    ++i_;  // opening quote
    std::string out;
    while (i_ < s_.size()) {
      const char c = s_[i_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) return std::nullopt;
      const char e = s_[i_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          auto cp = hex4();
          if (!cp) return std::nullopt;
          if (*cp >= 0xD800 && *cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
            i_ += 2;
            auto lo = hex4();
            if (!lo || *lo < 0xDC00 || *lo > 0xDFFF) return std::nullopt;
            *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*lo - 0xDC00);
          }
          appendUtf8(out, *cp);
          break;
        }
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<Json> array(int depth) {
    ++i_;
    Json::Array a;
    ws();
    if (i_ < s_.size() && s_[i_] == ']') {
      ++i_;
      return Json(std::move(a));
    }
    while (true) {
      auto v = value(depth + 1);
      if (!v) return std::nullopt;
      a.push_back(std::move(*v));
      ws();
      if (i_ >= s_.size()) return std::nullopt;
      if (s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (s_[i_] == ']') {
        ++i_;
        return Json(std::move(a));
      }
      return std::nullopt;
    }
  }

  std::optional<Json> object(int depth) {
    ++i_;
    Json::Object o;
    ws();
    if (i_ < s_.size() && s_[i_] == '}') {
      ++i_;
      return Json(std::move(o));
    }
    while (true) {
      ws();
      if (i_ >= s_.size() || s_[i_] != '"') return std::nullopt;
      auto k = string();
      if (!k) return std::nullopt;
      ws();
      if (i_ >= s_.size() || s_[i_] != ':') return std::nullopt;
      ++i_;
      auto v = value(depth + 1);
      if (!v) return std::nullopt;
      o[*k] = std::move(*v);
      ws();
      if (i_ >= s_.size()) return std::nullopt;
      if (s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (s_[i_] == '}') {
        ++i_;
        return Json(std::move(o));
      }
      return std::nullopt;
    }
  }

  const std::string& s_;
  size_t i_ = 0;
};

void escape(std::string& out, const std::string& s) {
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}
}  // namespace

const std::string& Json::asString() const { return isString() ? str_ : kEmptyString; }
const Json::Array& Json::asArray() const { return isArray() ? arr_ : kEmptyArray; }
const Json::Object& Json::asObject() const { return isObject() ? obj_ : kEmptyObject; }
Json::Array& Json::mutArray() {
  if (!isArray()) *this = Json(Array{});
  return arr_;
}

const Json& Json::operator[](const std::string& key) const {
  if (!isObject()) return kNull;
  auto it = obj_.find(key);
  return it == obj_.end() ? kNull : it->second;
}

bool Json::has(const std::string& key) const { return isObject() && obj_.count(key) > 0; }

Json& Json::set(const std::string& key, Json v) {
  if (!isObject()) *this = Json(Object{});
  obj_[key] = std::move(v);
  return *this;
}

Json& Json::push(Json v) {
  mutArray().push_back(std::move(v));
  return *this;
}

std::string Json::dump() const {
  std::string out;
  dumpTo(out);
  return out;
}

void Json::dumpTo(std::string& out) const {
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
      char buf[32];
      if (std::floor(num_) == num_ && std::fabs(num_) < 9e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(num_));
      else
        std::snprintf(buf, sizeof buf, "%.10g", num_);
      out += buf;
      break;
    }
    case Type::String: escape(out, str_); break;
    case Type::Array: {
      out += '[';
      bool first = true;
      for (const Json& v : arr_) {
        if (!first) out += ',';
        first = false;
        v.dumpTo(out);
      }
      out += ']';
      break;
    }
    case Type::Object: {
      out += '{';
      bool first = true;
      for (const auto& [k, v] : obj_) {
        if (!first) out += ',';
        first = false;
        escape(out, k);
        out += ':';
        v.dumpTo(out);
      }
      out += '}';
      break;
    }
  }
}

std::optional<Json> Json::parse(const std::string& text) { return Parser(text).parseDocument(); }

}  // namespace airo
