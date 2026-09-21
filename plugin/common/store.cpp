#include "store.h"
#include "utf8.h"

#if __has_include("client_id.h")
#include "client_id.h"
#else
#define SPOTIFY_CLIENT_ID_DEFAULT ""
#endif

#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#include <sstream>

namespace spotify {
namespace {

std::string ReadAll(const std::wstring& path) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return {};
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

void WriteAll(const std::wstring& path, const std::string& s) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
  fwrite(s.data(), 1, s.size(), f);
  fclose(f);
}

std::string IniGet(const std::string& text, const std::string& key) {
  std::string line, prefix = key + "=";
  std::istringstream in(text);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
  }
  return {};
}

}  // namespace

Paths GetPaths() {
  Paths p;
  wchar_t appdata[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
    return p;
  }
  p.dir = std::wstring(appdata) + L"\\Winamp\\Plugins\\spotify";
  CreateDirectoryW((std::wstring(appdata) + L"\\Winamp").c_str(), nullptr);
  CreateDirectoryW((std::wstring(appdata) + L"\\Winamp\\Plugins").c_str(), nullptr);
  CreateDirectoryW(p.dir.c_str(), nullptr);
  p.config = p.dir + L"\\config.ini";
  p.tokens = p.dir + L"\\tokens.ini";
  return p;
}

Config LoadConfig() {
  Config c;
  c.client_id = SPOTIFY_CLIENT_ID_DEFAULT;
  auto t = ReadAll(GetPaths().config);
  auto id = IniGet(t, "client_id");
  if (!id.empty()) c.client_id = id;
  auto dev = IniGet(t, "device_id");
  if (!dev.empty()) c.device_id = dev;
  auto cap = IniGet(t, "capture_buttons");
  if (!cap.empty()) c.capture_buttons = cap != "0";
  auto vis = IniGet(t, "vis_loopback");
  if (!vis.empty()) c.vis_loopback = vis != "0";
  return c;
}

void SaveConfig(const Config& c) {
  std::string s = "client_id=" + c.client_id + "\ndevice_id=" + c.device_id +
                  "\ncapture_buttons=" + (c.capture_buttons ? "1" : "0") +
                  "\nvis_loopback=" + (c.vis_loopback ? "1" : "0") + "\n";
  WriteAll(GetPaths().config, s);
}

Tokens LoadTokens() {
  Tokens t;
  auto raw = ReadAll(GetPaths().tokens);
  t.access = IniGet(raw, "access");
  t.refresh = IniGet(raw, "refresh");
  auto ex = IniGet(raw, "expires_at");
  if (!ex.empty()) t.expires_at = _atoi64(ex.c_str());
  return t;
}

void SaveTokens(const Tokens& t) {
  std::string s = "access=" + t.access + "\nrefresh=" + t.refresh +
                  "\nexpires_at=" + std::to_string(t.expires_at) + "\n";
  WriteAll(GetPaths().tokens, s);
}

void ClearTokens() { DeleteFileW(GetPaths().tokens.c_str()); }

std::string EffectiveClientId(const Config& c) {
  if (!c.client_id.empty()) return c.client_id;
  return SPOTIFY_CLIENT_ID_DEFAULT;
}

}  // namespace spotify
