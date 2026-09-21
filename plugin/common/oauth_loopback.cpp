#include "oauth_loopback.h"
#include "utf8.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace spotify {
namespace {

std::string QueryParam(const std::string& path, const char* key) {
  std::string k = std::string(key) + "=";
  auto q = path.find('?');
  if (q == std::string::npos) return {};
  auto p = path.find(k, q);
  if (p == std::string::npos) return {};
  p += k.size();
  auto e = path.find_first_of("& ", p);
  auto raw = path.substr(p, e == std::string::npos ? std::string::npos : e - p);
  std::string o;
  for (size_t i = 0; i < raw.size(); i++) {
    if (raw[i] == '%' && i + 2 < raw.size()) {
      unsigned int uv = 0;
      sscanf_s(raw.c_str() + i + 1, "%02x", &uv);
      o += (char)uv;
      i += 2;
    } else if (raw[i] == '+') {
      o += ' ';
    } else {
      o += raw[i];
    }
  }
  return o;
}

}  // namespace

std::string RunOAuthLoopback(const std::string& authorize_url, std::string* error, int timeout_ms) {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    if (error) *error = "WSAStartup";
    return {};
  }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    if (error) *error = "socket";
    WSACleanup();
    return {};
  }
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(43147);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 1) != 0) {
    if (error) *error = "bind/listen 127.0.0.1:43147 (port deja pris ?)";
    closesocket(s);
    WSACleanup();
    return {};
  }

  std::wstring wurl = Utf8ToWide(authorize_url);
  ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

  DWORD timeout = (DWORD)timeout_ms;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
  SOCKET c = accept(s, nullptr, nullptr);
  std::string code;
  if (c == INVALID_SOCKET) {
    if (error) *error = "login timeout (no callback)";
  } else {
    char buf[4096]{};
    int n = recv(c, buf, sizeof(buf) - 1, 0);
    std::string req(buf, n > 0 ? n : 0);
    auto line_end = req.find("\r\n");
    std::string line = req.substr(0, line_end);
    code = QueryParam(line, "code");
    auto errp = QueryParam(line, "error");
    const char* html =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
        R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Winamplify</title>
<style>
  html, body { margin: 0; height: 100%; }
  body {
    display: flex;
    align-items: center;
    justify-content: center;
    background: #121214;
    color: #e8e8ea;
    font-family: "Segoe UI", system-ui, sans-serif;
  }
  .card { text-align: center; padding: 2rem; }
  .brand {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 12px;
    margin: 0 0 12px;
  }
  .brand h1 { margin: 0; font-size: 1.75rem; font-weight: 650; }
  p { margin: 0; color: #a8a8b0; line-height: 1.45; }
  svg { flex-shrink: 0; display: block; }
</style>
</head>
<body>
  <div class="card">
    <div class="brand">
      <svg width="36" height="36" viewBox="0 0 32 32" aria-hidden="true">
        <rect width="32" height="32" rx="7" fill="#1a1a1d"/>
        <path fill="#ffcc22" d="M18.2 3.2 7.4 16.6h7.1L11.2 28.8l13.2-15.2h-7.2L18.2 3.2z"/>
      </svg>
      <h1>Winamplify</h1>
    </div>
    <p>[[MSG]]</p>
  </div>
</body>
</html>)";
    std::string page = html;
    const char* msg = !code.empty()
                          ? "You can close this tab and return to Winamp."
                          : "Sign-in did not complete. You can close this tab and try again in Winamp.";
    auto pos = page.find("[[MSG]]");
    if (pos != std::string::npos) page.replace(pos, 7, msg);
    send(c, page.c_str(), (int)page.size(), 0);
    closesocket(c);
    if (code.empty() && error) *error = errp.empty() ? "no OAuth code" : errp;
  }
  closesocket(s);
  WSACleanup();
  return code;
}

}  // namespace spotify
