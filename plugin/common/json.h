#pragma once
#include <string>
#include <vector>

namespace spotify {

struct JsonValue {
  enum Type { kNull, kBool, kNumber, kString, kObject, kArray };
  Type type = kNull;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<std::pair<std::string, JsonValue>> obj;
  std::vector<JsonValue> arr;

  const JsonValue* Get(const char* key) const;
  std::string Str(const char* key, const char* fallback = "") const;
  double Num(const char* key, double fallback = 0) const;
  bool Bool(const char* key, bool fallback = false) const;
  const JsonValue* Path(const char* dotted) const;
};

bool ParseJson(const std::string& text, JsonValue* out, std::string* err = nullptr);

}  // namespace spotify
