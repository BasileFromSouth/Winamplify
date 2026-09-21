#include "http.h"
#include "utf8.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace spotify {

HttpResponse Http::Request(const std::string& method, const std::string& url,
                           const std::string& bearer, const std::string& body,
                           const std::string& content_type, const std::string& accept) {
  HttpResponse r;
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256]{};
  wchar_t path[2048]{};
  wchar_t extra[2048]{};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  std::wstring wurl = Utf8ToWide(url);
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
    r.error = "invalid URL";
    return r;
  }
  std::wstring fullpath(path, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength) fullpath.append(extra, uc.dwExtraInfoLength);

  HINTERNET session = WinHttpOpen(L"Winamplify/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    r.error = "WinHTTP open";
    return r;
  }
  HINTERNET connect =
      WinHttpConnect(session, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
  if (!connect) {
    r.error = "WinHTTP connect";
    WinHttpCloseHandle(session);
    return r;
  }
  DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  std::wstring wmethod = Utf8ToWide(method);
  HINTERNET req = WinHttpOpenRequest(connect, wmethod.c_str(), fullpath.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) {
    r.error = "WinHTTP request";
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return r;
  }

  std::wstring headers = L"Accept: ";
  headers += Utf8ToWide(accept.empty() ? "*/*" : accept);
  headers += L"\r\n";
  if (!content_type.empty() && !body.empty()) {
    headers += L"Content-Type: ";
    headers += Utf8ToWide(content_type);
    headers += L"\r\n";
  }
  if (!bearer.empty()) {
    headers += L"Authorization: Bearer ";
    headers += Utf8ToWide(bearer);
    headers += L"\r\n";
  }

  BOOL ok = WinHttpSendRequest(req, headers.c_str(), (DWORD)-1, body.empty() ? nullptr : (LPVOID)body.data(),
                               (DWORD)body.size(), (DWORD)body.size(), 0);
  if (ok) ok = WinHttpReceiveResponse(req, nullptr);
  if (!ok) {
    r.error = "WinHTTP send/recv";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return r;
  }
  DWORD status = 0;
  DWORD slen = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                      &slen, WINHTTP_NO_HEADER_INDEX);
  r.status = (int)status;

  std::string acc;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
    std::vector<char> buf(avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, buf.data(), avail, &read) || !read) break;
    acc.append(buf.data(), read);
  }
  r.body = std::move(acc);
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return r;
}

HttpResponse Http::GetBinary(const std::string& url) {
  return Request("GET", url, {}, {}, {}, "*/*");
}

}  // namespace spotify
