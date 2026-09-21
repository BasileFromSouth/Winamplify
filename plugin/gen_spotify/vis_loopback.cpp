#include "vis_loopback.h"
#include "in2.h"
#include "wa_ipc.h"

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <cstring>
#include <string>

namespace {
HWND g_wa = nullptr;
WNDPROC g_orig = nullptr;
bool g_started = false;
bool g_starting = false;
wchar_t g_vis_path[MAX_PATH]{};
wchar_t g_saved_pl[2048]{};
bool g_have_saved = false;
UINT_PTR g_pump = 0;
constexpr UINT kVisMsg = WM_APP + 41;
constexpr UINT kRetargetMsg = WM_APP + 43;
constexpr UINT_PTR kPumpId = 0x4C535650;

void Log(const char* line) {
  wchar_t dir[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) return;
  std::wstring path = std::wstring(dir) + L"\\Winamp\\Plugins\\spotify\\vis.log";
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
  fwrite(line, 1, strlen(line), f);
  fputc('\n', f);
  fclose(f);
}

bool OnUiThread() {
  return g_wa && GetWindowThreadProcessId(g_wa, nullptr) == GetCurrentThreadId();
}

HMODULE InDll() {
  HMODULE h = GetModuleHandleW(L"in_winamplify.dll");
  if (!h) h = GetModuleHandleW(L"in_wamplify.dll");
  return h;
}

In_Module* InMod() {
  HMODULE h = InDll();
  if (!h) return nullptr;
  auto get = (In_Module * (*)()) GetProcAddress(h, "winampGetInModule2");
  if (!get) return nullptr;
  return get();
}

using PumpFn = void (*)();
PumpFn PumpPtr() {
  HMODULE h = InDll();
  if (!h) return nullptr;
  auto p = (PumpFn)GetProcAddress(h, "winamplify_vis_pump");
  if (!p) p = (PumpFn)GetProcAddress(h, "wamplify_vis_pump");
  return p;
}

VOID CALLBACK OnPump(HWND, UINT, UINT_PTR, DWORD) {
  auto p = PumpPtr();
  if (p) p();
}

void EnsureVisFile() {
  if (g_vis_path[0]) return;
  wchar_t dir[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) return;
  std::wstring folder = std::wstring(dir) + L"\\Winamp\\Plugins\\spotify";
  CreateDirectoryW((std::wstring(dir) + L"\\Winamp").c_str(), nullptr);
  CreateDirectoryW((std::wstring(dir) + L"\\Winamp\\Plugins").c_str(), nullptr);
  CreateDirectoryW(folder.c_str(), nullptr);
  lstrcpynW(g_vis_path, (folder + L"\\vis.lsv").c_str(), MAX_PATH);
  HANDLE h = CreateFileW(g_vis_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void RestorePlaylistFile() {
  if (!g_wa || !g_have_saved || !g_saved_pl[0]) return;
  SendMessageW(g_wa, WM_WA_IPC, (WPARAM)g_saved_pl, IPC_CHANGECURRENTFILEW);
  g_have_saved = false;
  g_saved_pl[0] = 0;
  Log("gen: playlist restored");
}

void SavePlaylistFile() {
  g_have_saved = false;
  g_saved_pl[0] = 0;
  if (!g_wa) return;
  int pos = (int)SendMessageW(g_wa, WM_WA_IPC, 0, IPC_GETLISTPOS);
  if (pos < 0) return;
  auto* cur = (const wchar_t*)SendMessageW(g_wa, WM_WA_IPC, pos, IPC_GETPLAYLISTFILEW);
  if (!cur || !cur[0]) return;
  if (VisIsOurFileW(cur)) return;
  lstrcpynW(g_saved_pl, cur, 2048);
  g_have_saved = true;
}

void StartPump() {
  if (!g_wa || g_pump) return;
  g_pump = SetTimer(g_wa, kPumpId, 16, OnPump);
}

void StopPump() {
  if (g_wa && g_pump) {
    KillTimer(g_wa, kPumpId);
    g_pump = 0;
  }
}

void ApplyCurrentFile(const wchar_t* path) {
  if (!g_wa || !path || !path[0]) return;
  SendMessageW(g_wa, WM_WA_IPC, (WPARAM)path, IPC_CHANGECURRENTFILEW);
  int pos = (int)SendMessageW(g_wa, WM_WA_IPC, 0, IPC_GETLISTPOS);
  auto* cur = (const wchar_t*)SendMessageW(g_wa, WM_WA_IPC, pos, IPC_GETPLAYLISTFILEW);
  if (VisIsOurFileW(cur)) return;
  char vis_a[MAX_PATH]{};
  WideCharToMultiByte(CP_ACP, 0, path, -1, vis_a, MAX_PATH, nullptr, nullptr);
  SendMessageW(g_wa, WM_WA_IPC, (WPARAM)vis_a, IPC_CHANGECURRENTFILE);
}

void FeedPlay() {
  EnsureVisFile();
  SavePlaylistFile();
  if (g_vis_path[0] && g_orig && g_wa) {
    ApplyCurrentFile(g_vis_path);
    int pos = (int)SendMessageW(g_wa, WM_WA_IPC, 0, IPC_GETLISTPOS);
    auto* cur = (const wchar_t*)SendMessageW(g_wa, WM_WA_IPC, pos, IPC_GETPLAYLISTFILEW);
    if (VisIsOurFileW(cur)) {
      g_starting = true;
      Log("gen: STARTPLAY vis.lsv (temporary slot, not enqueued)");
      CallWindowProcW(g_orig, g_wa, WM_WA_IPC, 0, IPC_STARTPLAY);
      g_starting = false;
    } else {
      Log("gen: CHANGECURRENTFILE ignored, skip local STARTPLAY");
      RestorePlaylistFile();
    }
  }
  auto* m = InMod();
  if (m && m->Play) {
    Log("gen: Play in_winamplify");
    m->Play(L"winamplify://vis");
  } else {
    Log("gen: in_winamplify missing");
  }
  g_started = true;
  StartPump();
}

void DoRetarget() {
  if (!g_wa || !g_vis_path[0]) return;
  ApplyCurrentFile(g_vis_path);
  PostMessageW(g_wa, WM_WA_IPC, 0, IPC_UPDTITLE);
  PostMessageW(g_wa, WM_WA_IPC, IPC_CB_MISC_TITLE, IPC_CB_MISC);
  PostMessageW(g_wa, WM_WA_IPC, IPC_CB_MISC_INFO, IPC_CB_MISC);
  Log("gen: retarget album-art file");
}

void FeedStop() {
  StopPump();
  auto* m = InMod();
  if (m && m->Stop) m->Stop();
  RestorePlaylistFile();
  g_started = false;
  g_starting = false;
}
}  // namespace

void VisInit(HWND winamp) {
  g_wa = winamp;
  EnsureVisFile();
  Log("gen: VisInit");
}

void VisBindProc(WNDPROC orig) { g_orig = orig; }

bool VisIsActive() { return g_started; }

bool VisIsStarting() { return g_starting; }

bool VisIsOurFileA(const char* f) {
  return f && (strstr(f, ".lsv") || strstr(f, "winamplify:") || strstr(f, "wamplify:") ||
               strstr(f, "loosamp:"));
}

bool VisIsOurFileW(const wchar_t* f) {
  return f && (wcsstr(f, L".lsv") || wcsstr(f, L"winamplify:") || wcsstr(f, L"wamplify:") ||
               wcsstr(f, L"loosamp:"));
}

void VisSetActive(bool active) {
  if (!g_wa) return;
  if (!OnUiThread()) {
    PostMessageW(g_wa, kVisMsg, active ? 1 : 0, 0);
    return;
  }
  if (!active) {
    if (g_started) Log("gen: vis off");
    FeedStop();
    return;
  }
  if (g_started) return;
  FeedPlay();
}

void VisRetargetFile(const wchar_t* vis_lsv_path) {
  if (!g_wa) return;
  if (vis_lsv_path && vis_lsv_path[0]) lstrcpynW(g_vis_path, vis_lsv_path, MAX_PATH);
  if (!g_vis_path[0]) return;
  if (!OnUiThread()) {
    PostMessageW(g_wa, kRetargetMsg, 0, 0);
    return;
  }
  DoRetarget();
}

void VisShutdown() { FeedStop(); }
