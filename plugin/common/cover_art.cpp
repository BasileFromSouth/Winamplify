#include "cover_art.h"
#include "http.h"
#include "utf8.h"

#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace spotify {
namespace {

std::wstring g_last_track_dir;

void WriteBytes(const std::wstring& path, const std::string& data) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
  if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
  fclose(f);
}

void DeleteIfExists(const std::wstring& path) { DeleteFileW(path.c_str()); }

void EnsureDir(const std::wstring& dir) { CreateDirectoryW(dir.c_str(), nullptr); }

// Keep folder names filesystem-safe and short.
std::wstring SafeKey(const std::string& key) {
  std::string s = key;
  if (s.rfind("spotify:", 0) == 0) {
    auto p = s.find_last_of(':');
    if (p != std::string::npos) s = s.substr(p + 1);
  }
  if (s.size() > 40) s = s.substr(0, 40);
  for (char& c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
          c == '_'))
      c = '_';
  }
  if (s.empty()) s = "track";
  return Utf8ToWide(s);
}

void WipeDirContents(const std::wstring& dir) {
  WIN32_FIND_DATAW fd{};
  std::wstring pattern = dir + L"\\*";
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.cFileName[0] == L'.' &&
        (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
      continue;
    std::wstring p = dir + L"\\" + fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      WipeDirContents(p);
      RemoveDirectoryW(p.c_str());
    } else {
      DeleteFileW(p.c_str());
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

void PruneOldTrackDirs(const std::wstring& playing_root, const std::wstring& keep) {
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((playing_root + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (fd.cFileName[0] == L'.') continue;
    std::wstring p = playing_root + L"\\" + fd.cFileName;
    if (_wcsicmp(p.c_str(), keep.c_str()) == 0) continue;
    WipeDirContents(p);
    RemoveDirectoryW(p.c_str());
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

}  // namespace

std::wstring CoverDir() {
  wchar_t appdata[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) return {};
  std::wstring dir = std::wstring(appdata) + L"\\Winamp\\Plugins\\spotify";
  EnsureDir(std::wstring(appdata) + L"\\Winamp");
  EnsureDir(std::wstring(appdata) + L"\\Winamp\\Plugins");
  EnsureDir(dir);
  return dir;
}

std::wstring PrepareTrackCover(const std::string& track_key, const std::string& image_url) {
  auto root = CoverDir();
  if (root.empty()) return {};

  // Legacy root covers (first version) — remove so they don't confuse lookups.
  DeleteIfExists(root + L"\\folder.jpg");
  DeleteIfExists(root + L"\\cover.jpg");
  DeleteIfExists(root + L"\\albumart.jpg");

  if (track_key.empty() || image_url.empty()) {
    if (!g_last_track_dir.empty()) {
      WipeDirContents(g_last_track_dir);
      RemoveDirectoryW(g_last_track_dir.c_str());
      g_last_track_dir.clear();
    }
    return {};
  }

  auto r = Http::GetBinary(image_url);
  if (r.status != 200 || r.body.size() < 64) return {};

  std::wstring playing = root + L"\\playing";
  EnsureDir(playing);
  std::wstring track_dir = playing + L"\\" + SafeKey(track_key);
  EnsureDir(track_dir);

  WriteBytes(track_dir + L"\\folder.jpg", r.body);
  WriteBytes(track_dir + L"\\cover.jpg", r.body);
  WriteBytes(track_dir + L"\\albumart.jpg", r.body);

  // Empty dummy "track" file Winamp treats as current — Album Art keys off this path.
  std::wstring vis = track_dir + L"\\vis.lsv";
  WriteBytes(vis, {});

  PruneOldTrackDirs(playing, track_dir);
  g_last_track_dir = track_dir;
  return vis;
}

}  // namespace spotify
