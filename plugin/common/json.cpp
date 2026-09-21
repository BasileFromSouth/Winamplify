#include "json.h"

#include <cctype>
#include <cmath>

namespace spotify {
namespace {

struct Parser {
  const std::string& t;
  size_t i = 0;
  std::string err;

  void Skip() {
    while (i < t.size() && (unsigned char)t[i] <= ' ') i++;
  }

  bool Peek(char c) {
    Skip();
    return i < t.size() && t[i] == c;
  }

  bool Eat(char c) {
    Skip();
    if (i < t.size() && t[i] == c) {
      i++;
      return true;
    }
    return false;
  }

  JsonValue ParseValue() {
    Skip();
    if (i >= t.size()) {
      err = "eof";
      return {};
    }
    char c = t[i];
    if (c == '{') return ParseObject();
    if (c == '[') return ParseArray();
    if (c == '"') return ParseString();
    if (c == 't' || c == 'f') return ParseBool();
    if (c == 'n') return ParseNull();
    return ParseNumber();
  }

  JsonValue ParseNull() {
    if (t.compare(i, 4, "null") == 0) {
      i += 4;
      return {};
    }
    err = "bad null";
    return {};
  }

  JsonValue ParseBool() {
    JsonValue v;
    v.type = JsonValue::kBool;
    if (t.compare(i, 4, "true") == 0) {
      i += 4;
      v.b = true;
      return v;
    }
    if (t.compare(i, 5, "false") == 0) {
      i += 5;
      v.b = false;
      return v;
    }
    err = "bad bool";
    return {};
  }

  JsonValue ParseNumber() {
    JsonValue v;
    v.type = JsonValue::kNumber;
    size_t start = i;
    if (t[i] == '-') i++;
    while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
    if (i < t.size() && t[i] == '.') {
      i++;
      while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
    }
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
      i++;
      if (i < t.size() && (t[i] == '+' || t[i] == '-')) i++;
      while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
    }
    try {
      v.n = std::stod(t.substr(start, i - start));
    } catch (...) {
      err = "bad number";
    }
    return v;
  }

  JsonValue ParseString() {
    JsonValue v;
    v.type = JsonValue::kString;
    if (!Eat('"')) {
      err = "string";
      return v;
    }
    std::string o;
    while (i < t.size()) {
      char c = t[i++];
      if (c == '"') break;
      if (c != '\\') {
        o += c;
        continue;
      }
      if (i >= t.size()) break;
      char e = t[i++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          o += e;
          break;
        case 'b':
          o += '\b';
          break;
        case 'f':
          o += '\f';
          break;
        case 'n':
          o += '\n';
          break;
        case 'r':
          o += '\r';
          break;
        case 't':
          o += '\t';
          break;
        case 'u': {
          if (i + 4 > t.size()) break;
          unsigned cp = 0;
          for (int k = 0; k < 4; k++) {
            char h = t[i++];
            cp <<= 4;
            if (h >= '0' && h <= '9')
              cp |= h - '0';
            else if (h >= 'a' && h <= 'f')
              cp |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F')
              cp |= h - 'A' + 10;
          }
          if (cp < 0x80)
            o += (char)cp;
          else if (cp < 0x800) {
            o += (char)(0xC0 | (cp >> 6));
            o += (char)(0x80 | (cp & 0x3F));
          } else {
            o += (char)(0xE0 | (cp >> 12));
            o += (char)(0x80 | ((cp >> 6) & 0x3F));
            o += (char)(0x80 | (cp & 0x3F));
          }
          break;
        }
        default:
          o += e;
      }
    }
    v.s = std::move(o);
    return v;
  }

  JsonValue ParseArray() {
    JsonValue v;
    v.type = JsonValue::kArray;
    Eat('[');
    Skip();
    if (Eat(']')) return v;
    for (;;) {
      v.arr.push_back(ParseValue());
      if (Eat(']')) break;
      if (!Eat(',')) {
        err = "array comma";
        break;
      }
    }
    return v;
  }

  JsonValue ParseObject() {
    JsonValue v;
    v.type = JsonValue::kObject;
    Eat('{');
    Skip();
    if (Eat('}')) return v;
    for (;;) {
      JsonValue k = ParseString();
      if (!Eat(':')) {
        err = "colon";
        break;
      }
      v.obj.emplace_back(k.s, ParseValue());
      if (Eat('}')) break;
      if (!Eat(',')) {
        err = "obj comma";
        break;
      }
    }
    return v;
  }
};

}  // namespace

const JsonValue* JsonValue::Get(const char* key) const {
  if (type != kObject) return nullptr;
  for (auto& kv : obj) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

std::string JsonValue::Str(const char* key, const char* fallback) const {
  auto* v = Get(key);
  if (!v) return fallback;
  if (v->type == kString) return v->s;
  if (v->type == kNumber) return std::to_string((long long)v->n);
  if (v->type == kBool) return v->b ? "true" : "false";
  return fallback;
}

double JsonValue::Num(const char* key, double fallback) const {
  auto* v = Get(key);
  if (!v) return fallback;
  if (v->type == kNumber) return v->n;
  if (v->type == kString) {
    try {
      return std::stod(v->s);
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

bool JsonValue::Bool(const char* key, bool fallback) const {
  auto* v = Get(key);
  if (!v) return fallback;
  if (v->type == kBool) return v->b;
  return fallback;
}

const JsonValue* JsonValue::Path(const char* dotted) const {
  const JsonValue* cur = this;
  std::string key;
  for (const char* p = dotted;; p++) {
    if (*p && *p != '.') {
      key += *p;
      continue;
    }
    cur = cur->Get(key.c_str());
    if (!cur) return nullptr;
    key.clear();
    if (!*p) return cur;
  }
}

bool ParseJson(const std::string& text, JsonValue* out, std::string* err) {
  Parser p{text};
  *out = p.ParseValue();
  if (!p.err.empty()) {
    if (err) *err = p.err;
    return false;
  }
  return true;
}

}  // namespace spotify
