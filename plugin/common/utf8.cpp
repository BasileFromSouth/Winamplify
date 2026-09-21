#include "utf8.h"

#include <windows.h>
#include <cctype>
#include <cstdio>

namespace spotify {

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string o(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
  return o;
}

std::wstring Utf8ToWide(const std::string& u) {
  if (u.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), nullptr, 0);
  std::wstring o(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), o.data(), n);
  return o;
}

std::string UrlEncode(const std::string& s) {
  std::string o;
  o.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      char buf[8];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      o += buf;
    }
  }
  return o;
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}

}  // namespace spotify
