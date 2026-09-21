#pragma once
#include <string>

namespace spotify {

std::string WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& u);
std::string UrlEncode(const std::string& s);
std::string JsonEscape(const std::string& s);

}  // namespace spotify
