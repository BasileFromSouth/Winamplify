#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace spotify {

struct Paths {
  std::wstring dir;
  std::wstring config;
  std::wstring tokens;
};

Paths GetPaths();

struct Config {
  std::string client_id;
  std::string device_id;
  bool capture_buttons = true;
  bool vis_loopback = true;
};

struct Tokens {
  std::string access;
  std::string refresh;
  std::int64_t expires_at = 0;
};

Config LoadConfig();
void SaveConfig(const Config& c);
Tokens LoadTokens();
void SaveTokens(const Tokens& t);
void ClearTokens();
std::string EffectiveClientId(const Config& c);

}  // namespace spotify
