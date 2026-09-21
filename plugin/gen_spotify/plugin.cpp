#include "GEN.H"
#include "wa_ipc.h"
#include "winamplify_ipc.h"
#include "cover_art.h"
#include "oauth_loopback.h"
#include "pkce.h"
#include "spotify_api.h"
#include "store.h"
#include "utf8.h"
#include "vis_loopback.h"

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

winampGeneralPurposePlugin plugin{};
WNDPROC g_old_proc = nullptr;
UINT_PTR g_timer = 0;
UINT_PTR g_hook_timer = 0;
HHOOK g_cwp = nullptr;
constexpr UINT kTimerId = 0x53504F54;
constexpr UINT kHookTimer = 0x53504F55;
constexpr UINT kShuffleMsg = WM_APP + 40;
constexpr UINT kVisMsg = WM_APP + 41;
constexpr UINT kUiMsg = WM_APP + 42;
constexpr UINT kRetargetMsg = WM_APP + 43;
HWND g_prefs = nullptr;

std::mutex g_mu;
std::string g_status;
spotify::PlayerState g_player;
std::vector<spotify::Device> g_devices;
bool g_logged = false;
bool g_capture = true;
bool g_vis_loopback = true;
bool g_in_menu = false;
bool g_local_mode = false;
bool g_pending_local = false;
bool g_pass_stop = false;
ULONGLONG g_pending_until = 0;
ULONGLONG g_local_entered_at = 0;
ULONGLONG g_local_stopped_at = 0;
UINT g_spotify_msg = 0;
int g_subclass_tries = 0;
ULONGLONG g_last_cmd_at = 0;
int g_last_cmd = 0;
ULONGLONG g_last_shuffle_at = 0;
int g_last_shuffle_on = -1;
bool g_ignore_shuffle = false;
int g_base_ms = 0;
int g_dur_ms = 0;
bool g_playing = false;
bool g_available = false;
ULONGLONG g_stamp = 0;
wchar_t g_title[512]{};
wchar_t g_title_only[256]{};
wchar_t g_artist[256]{};
wchar_t g_album[256]{};
char g_title_a[512]{};

enum {
  IDC_CLIENT_ID = 101,
  IDC_LOGIN = 102,
  IDC_LOGOUT = 103,
  IDC_STATUS = 104,
  IDC_NOW = 105,
  IDC_DEVICES = 106,
  IDC_TRANSFER = 107,
  IDC_CAPTURE = 108,
  IDC_REFRESH = 109,
  IDC_VIS = 110,
};

spotify::Api MakeApi() {
  auto c = spotify::LoadConfig();
  return spotify::Api(spotify::EffectiveClientId(c));
}

void SetStatus(const std::string& s) {
  g_status = s;
  if (g_prefs) SetDlgItemTextW(g_prefs, IDC_STATUS, spotify::Utf8ToWide(s).c_str());
}

void SyncClockLocked() {
  g_available = g_player.available;
  g_playing = g_player.available && g_player.is_playing;
  g_base_ms = g_player.progress_ms;
  g_dur_ms = g_player.duration_ms;
  g_stamp = GetTickCount64();
  std::string t;
  if (!g_player.artist.empty()) t = g_player.artist + " - ";
  t += g_player.title;
  wcsncpy_s(g_title, spotify::Utf8ToWide(t).c_str(), _TRUNCATE);
  wcsncpy_s(g_title_only, spotify::Utf8ToWide(g_player.title).c_str(), _TRUNCATE);
  wcsncpy_s(g_artist, spotify::Utf8ToWide(g_player.artist).c_str(), _TRUNCATE);
  wcsncpy_s(g_album, spotify::Utf8ToWide(g_player.album).c_str(), _TRUNCATE);
  WideCharToMultiByte(CP_ACP, 0, g_title, -1, g_title_a, 512, nullptr, nullptr);
}

int CurrentPosMs() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_available) return 0;
  int p = g_base_ms;
  if (g_playing) p += (int)(GetTickCount64() - g_stamp);
  if (p < 0) p = 0;
  if (g_dur_ms > 0 && p > g_dur_ms) p = g_dur_ms;
  return p;
}

void PushWinampUi() {
  HWND hwnd = plugin.hwndParent;
  if (!hwnd) return;
  if (GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) {
    PostMessageW(hwnd, kUiMsg, 0, 0);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_title[0]) {
      std::wstring cap = g_title;
      cap += L" - Winamp";
      SetWindowTextW(hwnd, cap.c_str());
    }
  }
  PostMessageW(hwnd, WM_WA_IPC, 0, IPC_UPDTITLE);
  PostMessageW(hwnd, WM_WA_IPC, IPC_CB_MISC_TITLE, IPC_CB_MISC);
  PostMessageW(hwnd, WM_WA_IPC, IPC_CB_MISC_STATUS, IPC_CB_MISC);
  PostMessageW(hwnd, WM_WA_IPC, IPC_CB_MISC_INFO, IPC_CB_MISC);
}

bool ReadablePtr(const void* p) {
  return p && (ULONG_PTR)p >= 0x10000;
}

bool IsWinampLocalSourceW(const wchar_t* f) {
  if (!ReadablePtr(f) || !f[0]) return false;
  if (_wcsnicmp(f, L"http:", 5) == 0 || _wcsnicmp(f, L"https:", 6) == 0) return false;
  if (_wcsnicmp(f, L"spotify:", 8) == 0) return false;
  if (_wcsnicmp(f, L"winamplify:", 11) == 0 || _wcsnicmp(f, L"wamplify:", 9) == 0 ||
      _wcsnicmp(f, L"loosamp:", 8) == 0)
    return false;
  if (wcsstr(f, L".lsv")) return false;
  return true;
}

bool IsWinampLocalSourceA(const char* f) {
  if (!ReadablePtr(f) || !f[0]) return false;
  if (_strnicmp(f, "http:", 5) == 0 || _strnicmp(f, "https:", 6) == 0) return false;
  if (_strnicmp(f, "spotify:", 8) == 0) return false;
  if (_strnicmp(f, "winamplify:", 11) == 0 || _strnicmp(f, "wamplify:", 9) == 0 ||
      _strnicmp(f, "loosamp:", 8) == 0)
    return false;
  if (strstr(f, ".lsv")) return false;
  return true;
}

bool UseSpotifyTitleW(const wchar_t* f) {
  if (!ReadablePtr(f) || !f[0]) return true;
  if (VisIsOurFileW(f)) return true;
  return !IsWinampLocalSourceW(f);
}

bool UseSpotifyTitleA(const char* f) {
  if (!ReadablePtr(f) || !f[0]) return true;
  if (VisIsOurFileA(f)) return true;
  return !IsWinampLocalSourceA(f);
}

bool PlaylistIsLocal() {
  HWND hwnd = plugin.hwndParent;
  if (!hwnd) return false;
  int pos = (int)SendMessageW(hwnd, WM_WA_IPC, 0, IPC_GETLISTPOS);
  if (pos < 0) return false;
  auto* fw = (wchar_t*)SendMessageW(hwnd, WM_WA_IPC, pos, IPC_GETPLAYLISTFILEW);
  if (IsWinampLocalSourceW(fw)) return true;
  auto* fa = (char*)SendMessageW(hwnd, WM_WA_IPC, pos, IPC_GETPLAYLISTFILE);
  return IsWinampLocalSourceA(fa);
}

void PauseSpotify() {
  std::thread([] {
    auto api = MakeApi();
    std::string dev;
    bool playing = false;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      dev = g_player.device_id;
      playing = g_player.available && g_player.is_playing;
    }
    if (!playing) return;
    if (dev.empty()) dev = spotify::LoadConfig().device_id;
    std::string err;
    if (!api.Pause(dev, &err) && !dev.empty()) {
      err.clear();
      api.Pause({}, &err);
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_player.is_playing = false;
    SyncClockLocked();
  }).detach();
}

void MarkWantLocal(DWORD hold_ms = 8000) {
  g_pending_local = true;
  g_pending_until = GetTickCount64() + hold_ms;
}

bool PendingLocal() { return g_pending_local && GetTickCount64() < g_pending_until; }

void EnterLocalMode() {
  g_pending_local = false;
  g_pending_until = 0;
  g_local_stopped_at = 0;
  g_local_entered_at = GetTickCount64();
  if (g_local_mode) return;
  g_local_mode = true;
  VisSetActive(false);
  PauseSpotify();
}

void LeaveLocalMode(bool stop_winamp) {
  g_local_mode = false;
  g_pending_local = false;
  g_pending_until = 0;
  g_local_stopped_at = 0;
  if (stop_winamp && plugin.hwndParent) {
    g_pass_stop = true;
    PostMessageW(plugin.hwndParent, WM_COMMAND, WINAMP_BUTTON4, 0);
  }
}

void MaybeExitLocalMode() {
  if (!g_local_mode || !plugin.hwndParent) return;
  if (GetTickCount64() - g_local_entered_at < 4000) return;
  int st = (int)SendMessageW(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING);
  if (st == 0) {
    if (!g_local_stopped_at) g_local_stopped_at = GetTickCount64();
    else if (GetTickCount64() - g_local_stopped_at > 2000) g_local_mode = false;
  } else {
    g_local_stopped_at = 0;
  }
}

void RefreshNowPlayingLabel() {
  if (!g_prefs) return;
  std::lock_guard<std::mutex> lock(g_mu);
  std::string t;
  if (!g_player.available) {
    t = "(nothing playing — open Spotify Desktop and start a track)";
  } else {
    t = (g_player.is_playing ? "Playing: " : "Paused: ");
    if (!g_player.artist.empty()) t += g_player.artist + " — ";
    t += g_player.title;
    if (!g_player.device_name.empty()) t += "  [" + g_player.device_name + "]";
  }
  SetDlgItemTextW(g_prefs, IDC_NOW, spotify::Utf8ToWide(t).c_str());
}

void FillDevices() {
  if (!g_prefs) return;
  HWND combo = GetDlgItem(g_prefs, IDC_DEVICES);
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  auto cfg = spotify::LoadConfig();
  int sel = 0;
  for (int i = 0; i < (int)g_devices.size(); i++) {
    std::string label = g_devices[i].name + (g_devices[i].is_active ? " (active)" : "");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)spotify::Utf8ToWide(label).c_str());
    if (g_devices[i].id == cfg.device_id || (cfg.device_id.empty() && g_devices[i].is_active))
      sel = i;
  }
  if (!g_devices.empty()) SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

void Poll() {
  auto api = MakeApi();
  g_logged = api.LoggedIn();
  g_capture = spotify::LoadConfig().capture_buttons;
  g_vis_loopback = spotify::LoadConfig().vis_loopback;
  if (!g_logged) {
    VisSetActive(false);
    return;
  }
  std::string err;
  auto st = api.GetPlayer(&err);
  std::string prev_track;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    prev_track = g_player.track_uri;
    g_player = st;
    SyncClockLocked();
  }
  if (st.track_uri != prev_track) {
    auto key = !st.track_uri.empty() ? st.track_uri : st.album_art_url;
    auto vis = spotify::PrepareTrackCover(key, st.album_art_url);
    if (!vis.empty()) VisRetargetFile(vis.c_str());
    PushWinampUi();
  }
  if (!st.device_id.empty()) {
    auto cfg = spotify::LoadConfig();
    std::string n = st.device_name;
    for (char& c : n)
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    bool web = n.find("web player") != std::string::npos || n.find("chrome") != std::string::npos ||
               n.find("firefox") != std::string::npos || n.find("edge") != std::string::npos;
    if (!web && cfg.device_id != st.device_id) {
      cfg.device_id = st.device_id;
      spotify::SaveConfig(cfg);
    }
  }
  if (!err.empty() && g_prefs) SetStatus(err);
  RefreshNowPlayingLabel();
  if (g_local_mode) {
    VisSetActive(false);
    MaybeExitLocalMode();
    return;
  }
  if (st.available) PushWinampUi();
  static ULONGLONG vis_off_since = 0;
  bool want_vis = g_vis_loopback && st.available && st.is_playing;
  if (want_vis) {
    vis_off_since = 0;
    VisSetActive(true);
  } else {
    if (!vis_off_since) vis_off_since = GetTickCount64();
    if (GetTickCount64() - vis_off_since > 2500) VisSetActive(false);
  }
}

std::string AuthUrl(const std::string& client_id, const std::string& challenge, const std::string& state) {
  return std::string("https://accounts.spotify.com/authorize?response_type=code&client_id=") +
         spotify::UrlEncode(client_id) + "&redirect_uri=" + spotify::UrlEncode(spotify::kRedirectUri) +
         "&scope=" + spotify::UrlEncode(spotify::kScopes) +
         "&code_challenge_method=S256&code_challenge=" + spotify::UrlEncode(challenge) +
         "&state=" + spotify::UrlEncode(state);
}

void DoLogin(HWND hwnd) {
  wchar_t buf[128]{};
  GetDlgItemTextW(hwnd, IDC_CLIENT_ID, buf, 128);
  auto cfg = spotify::LoadConfig();
  cfg.client_id = spotify::WideToUtf8(buf);
  spotify::SaveConfig(cfg);
  if (cfg.client_id.empty()) {
    SetStatus("Paste your Spotify Dashboard Client ID first.");
    return;
  }
  SetStatus("Browser opened — authorize the app...");
  auto pkce = spotify::MakePkce();
  auto state = spotify::RandomUrlToken(16);
  auto url = AuthUrl(cfg.client_id, pkce.challenge, state);
  std::thread([pkce, url, cfg]() {
    std::string err;
    auto code = spotify::RunOAuthLoopback(url, &err);
    if (code.empty()) {
      SetStatus(err.empty() ? "login cancelled" : err);
      return;
    }
    spotify::Api api(cfg.client_id);
    if (!api.ExchangeCode(code, pkce.verifier, &err)) {
      SetStatus(err);
      return;
    }
    g_logged = true;
    SetStatus("Signed in. Open Spotify Desktop if needed.");
    g_devices = api.GetDevices(&err);
    if (g_prefs) {
      FillDevices();
      Poll();
    }
  }).detach();
}

void DoLogout() {
  MakeApi().Logout();
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_player = {};
    SyncClockLocked();
  }
  g_logged = false;
  g_devices.clear();
  VisSetActive(false);
  SetStatus("Signed out.");
  RefreshNowPlayingLabel();
  FillDevices();
}

INT_PTR CALLBACK PrefsProc(HWND h, UINT m, WPARAM w, LPARAM) {
  switch (m) {
    case WM_INITDIALOG: {
      g_prefs = h;
      auto cfg = spotify::LoadConfig();
      SetDlgItemTextW(h, IDC_CLIENT_ID, spotify::Utf8ToWide(cfg.client_id).c_str());
      CheckDlgButton(h, IDC_CAPTURE, cfg.capture_buttons ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(h, IDC_VIS, cfg.vis_loopback ? BST_CHECKED : BST_UNCHECKED);
      SetStatus(MakeApi().LoggedIn() ? "Session saved." : "Not signed in yet.");
      if (MakeApi().LoggedIn()) {
        std::thread([] {
          std::string err;
          g_devices = MakeApi().GetDevices(&err);
          if (g_prefs) {
            FillDevices();
            Poll();
          }
        }).detach();
      }
      RefreshNowPlayingLabel();
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case IDC_LOGIN:
          DoLogin(h);
          return TRUE;
        case IDC_LOGOUT:
          DoLogout();
          return TRUE;
        case IDC_REFRESH: {
          std::thread([] {
            std::string err;
            g_devices = MakeApi().GetDevices(&err);
            if (!err.empty()) SetStatus(err);
            FillDevices();
            Poll();
          }).detach();
          return TRUE;
        }
        case IDC_TRANSFER: {
          HWND combo = GetDlgItem(h, IDC_DEVICES);
          int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
          if (i < 0 || i >= (int)g_devices.size()) {
            SetStatus("Pick a device (Spotify Desktop must be open).");
            return TRUE;
          }
          auto cfg = spotify::LoadConfig();
          cfg.device_id = g_devices[i].id;
          wchar_t buf[128]{};
          GetDlgItemTextW(h, IDC_CLIENT_ID, buf, 128);
          cfg.client_id = spotify::WideToUtf8(buf);
          cfg.capture_buttons = IsDlgButtonChecked(h, IDC_CAPTURE) == BST_CHECKED;
          cfg.vis_loopback = IsDlgButtonChecked(h, IDC_VIS) == BST_CHECKED;
          spotify::SaveConfig(cfg);
          g_capture = cfg.capture_buttons;
          g_vis_loopback = cfg.vis_loopback;
          std::string err;
          if (!MakeApi().Transfer(cfg.device_id, true, &err))
            SetStatus(err);
          else
            SetStatus("Playback transferred to " + g_devices[i].name);
          return TRUE;
        }
        case IDC_CAPTURE:
        case IDC_VIS: {
          auto cfg = spotify::LoadConfig();
          cfg.capture_buttons = IsDlgButtonChecked(h, IDC_CAPTURE) == BST_CHECKED;
          cfg.vis_loopback = IsDlgButtonChecked(h, IDC_VIS) == BST_CHECKED;
          wchar_t buf[128]{};
          GetDlgItemTextW(h, IDC_CLIENT_ID, buf, 128);
          cfg.client_id = spotify::WideToUtf8(buf);
          spotify::SaveConfig(cfg);
          g_capture = cfg.capture_buttons;
          g_vis_loopback = cfg.vis_loopback;
          VisSetActive(g_vis_loopback && g_logged && g_playing);
          return TRUE;
        }
        case IDOK:
        case IDCANCEL: {
          auto cfg = spotify::LoadConfig();
          wchar_t buf[128]{};
          GetDlgItemTextW(h, IDC_CLIENT_ID, buf, 128);
          cfg.client_id = spotify::WideToUtf8(buf);
          cfg.capture_buttons = IsDlgButtonChecked(h, IDC_CAPTURE) == BST_CHECKED;
          cfg.vis_loopback = IsDlgButtonChecked(h, IDC_VIS) == BST_CHECKED;
          int i = (int)SendMessageW(GetDlgItem(h, IDC_DEVICES), CB_GETCURSEL, 0, 0);
          if (i >= 0 && i < (int)g_devices.size()) cfg.device_id = g_devices[i].id;
          spotify::SaveConfig(cfg);
          g_capture = cfg.capture_buttons;
          g_vis_loopback = cfg.vis_loopback;
          VisSetActive(g_vis_loopback && g_logged && g_playing);
          g_prefs = nullptr;
          EndDialog(h, 0);
          return TRUE;
        }
      }
      break;
    case WM_CLOSE:
      g_prefs = nullptr;
      EndDialog(h, 0);
      return TRUE;
  }
  return FALSE;
}

int CmdFromButton(int id) {
  switch (id) {
    case WINAMP_BUTTON1:
    case WINAMP_BUTTON1_SHIFT:
    case WINAMP_BUTTON1_CTRL:
      return 1;
    case WINAMP_BUTTON2:
    case WINAMP_BUTTON2_SHIFT:
    case WINAMP_BUTTON2_CTRL:
    case WINAMP_BUTTON3:
    case WINAMP_BUTTON3_SHIFT:
    case WINAMP_BUTTON3_CTRL: {
      std::lock_guard<std::mutex> lock(g_mu);
      return (g_player.available && g_player.is_playing) ? 3 : 2;
    }
    case WINAMP_BUTTON4:
    case WINAMP_BUTTON4_SHIFT:
    case WINAMP_BUTTON4_CTRL:
      return 4;
    case WINAMP_BUTTON5:
    case WINAMP_BUTTON5_SHIFT:
    case WINAMP_BUTTON5_CTRL:
      return 5;
    default:
      return 0;
  }
}

bool RunPlayerCmd(spotify::Api& api, int which, const std::string& dev, std::string* err) {
  switch (which) {
    case 1:
      return api.Previous(dev, err);
    case 2:
      return api.Play(dev, err);
    case 3:
    case 4:
      return api.Pause(dev, err);
    case 5:
      return api.Next(dev, err);
    default:
      return false;
  }
}

void SpotifyCmd(int which) {
  std::thread([which] {
    auto api = MakeApi();
    std::string dev;
    bool playing = false;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      dev = g_player.device_id;
      playing = g_player.available && g_player.is_playing;
    }
    if (dev.empty()) dev = spotify::LoadConfig().device_id;

    std::string err;
    bool ok = RunPlayerCmd(api, which, dev, &err);
    if (!ok && !dev.empty()) {
      err.clear();
      ok = RunPlayerCmd(api, which, {}, &err);
    }
    if (!ok) {
      auto ds = api.GetDevices(&err);
      std::string pick;
      for (auto& d : ds) {
        if (d.is_active) {
          pick = d.id;
          break;
        }
      }
      if (pick.empty() && !ds.empty()) pick = ds[0].id;
      if (!pick.empty()) {
        api.Transfer(pick, which == 2 || (which != 3 && which != 4 && !playing), &err);
        err.clear();
        ok = RunPlayerCmd(api, which, pick, &err);
      }
    }
    if (!ok && !err.empty()) SetStatus(err);
    else {
      std::lock_guard<std::mutex> lock(g_mu);
      if (which == 2) g_player.is_playing = true;
      if (which == 3 || which == 4) g_player.is_playing = false;
      g_player.available = true;
      SyncClockLocked();
    }
    Poll();
  }).detach();
}

void FireCmd(int cmd) {
  if (!cmd || !g_logged || !g_capture || g_in_menu) return;
  if (g_local_mode || PendingLocal() || g_pass_stop) return;
  auto now = GetTickCount64();
  if (cmd == g_last_cmd && now - g_last_cmd_at < 200) return;
  g_last_cmd = cmd;
  g_last_cmd_at = now;
  SpotifyCmd(cmd);
}

void ApplySpotifyShuffle(bool on) {
  if (!g_logged || !g_capture || g_local_mode || g_in_menu) return;
  auto now = GetTickCount64();
  if (g_last_shuffle_on == (int)on && now - g_last_shuffle_at < 400) return;
  g_last_shuffle_on = on ? 1 : 0;
  g_last_shuffle_at = now;
  std::thread([on] {
    auto api = MakeApi();
    std::string dev;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      dev = g_player.device_id;
    }
    if (dev.empty()) dev = spotify::LoadConfig().device_id;
    std::string err;
    bool ok = api.SetShuffle(on, dev, &err);
    if (!ok && !dev.empty()) {
      err.clear();
      ok = api.SetShuffle(on, {}, &err);
    }
    if (!ok && !err.empty()) SetStatus(err);
    else {
      std::lock_guard<std::mutex> lock(g_mu);
      g_player.shuffle = on ? "on" : "off";
    }
  }).detach();
}

void PushShuffleFromWinamp(HWND hwnd) {
  if (!g_old_proc || !hwnd) return;
  int on = (int)CallWindowProcW(g_old_proc, hwnd, WM_WA_IPC, 0, IPC_GET_SHUFFLE);
  ApplySpotifyShuffle(on != 0);
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool HookTitlesW(WPARAM wParam) {
  auto* ht = (waHookTitleStructW*)wParam;
  if (!ReadablePtr(ht) || !ReadablePtr(ht->title) || g_in_menu) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_available || !g_title[0]) return false;
  size_t n = 0;
  while (g_title[n] && n < 127) {
    ht->title[n] = g_title[n];
    n++;
  }
  ht->title[n] = 0;
  ht->length = g_dur_ms / 1000;
  return true;
}

bool HookTitlesA(WPARAM wParam) {
  auto* ht = (waHookTitleStruct*)wParam;
  if (!ReadablePtr(ht) || !ReadablePtr(ht->title) || g_in_menu) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_available || !g_title_a[0]) return false;
  size_t n = 0;
  while (g_title_a[n] && n < 127) {
    ht->title[n] = g_title_a[n];
    n++;
  }
  ht->title[n] = 0;
  ht->length = g_dur_ms / 1000;
  return true;
}

bool FillMetaW(WPARAM wParam) {
  auto* s = (extendedFileInfoStructW*)wParam;
  if (!ReadablePtr(s) || !s->metadata || !s->ret || s->retlen == 0 || s->retlen > 4096) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_available || !g_title[0]) return false;
  const wchar_t* src = nullptr;
  wchar_t lenbuf[32]{};
  if (_wcsicmp(s->metadata, L"title") == 0) src = g_title_only[0] ? g_title_only : g_title;
  else if (_wcsicmp(s->metadata, L"artist") == 0) src = g_artist;
  else if (_wcsicmp(s->metadata, L"album") == 0) src = g_album;
  else if (_wcsicmp(s->metadata, L"length") == 0) {
    _snwprintf_s(lenbuf, _TRUNCATE, L"%d", g_dur_ms / 1000);
    src = lenbuf;
  } else
    return false;
  wcsncpy_s(s->ret, s->retlen, src ? src : L"", _TRUNCATE);
  return true;
}

bool FillMetaA(WPARAM wParam) {
  auto* s = (extendedFileInfoStruct*)wParam;
  if (!ReadablePtr(s) || !s->metadata || !s->ret || s->retlen == 0 || s->retlen > 4096) return false;
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_available || !g_title_a[0]) return false;
  char tmp[512]{};
  const char* src = nullptr;
  if (_stricmp(s->metadata, "title") == 0) {
    WideCharToMultiByte(CP_ACP, 0, g_title_only[0] ? g_title_only : g_title, -1, tmp, 512, nullptr,
                        nullptr);
    src = tmp;
  } else if (_stricmp(s->metadata, "artist") == 0) {
    WideCharToMultiByte(CP_ACP, 0, g_artist, -1, tmp, 512, nullptr, nullptr);
    src = tmp;
  } else if (_stricmp(s->metadata, "album") == 0) {
    WideCharToMultiByte(CP_ACP, 0, g_album, -1, tmp, 512, nullptr, nullptr);
    src = tmp;
  } else if (_stricmp(s->metadata, "length") == 0) {
    sprintf_s(tmp, "%d", g_dur_ms / 1000);
    src = tmp;
  } else
    return false;
  strncpy_s(s->ret, s->retlen, src ? src : "", _TRUNCATE);
  return true;
}

void EnsureSubclass() {
  if (g_in_menu || g_subclass_tries > 20) return;
  HWND main = plugin.hwndParent;
  if (!main) return;
  LONG_PTR cur = GetWindowLongPtrW(main, GWLP_WNDPROC);
  if (cur != (LONG_PTR)SubclassProc) {
    g_old_proc = (WNDPROC)SetWindowLongPtrW(main, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
    VisBindProc(g_old_proc);
    g_subclass_tries++;
  }
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_ENTERMENULOOP || msg == WM_INITMENU || msg == WM_INITMENUPOPUP) {
    g_in_menu = true;
    return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
  }
  if (msg == WM_EXITMENULOOP || msg == WM_UNINITMENUPOPUP) {
    g_in_menu = false;
    return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
  }
  if (g_in_menu) return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);

  if (g_spotify_msg && msg == g_spotify_msg) {
    LeaveLocalMode(false);
    return 0;
  }
  if (msg == kShuffleMsg) {
    PushShuffleFromWinamp(hwnd);
    return 0;
  }
  if (msg == kVisMsg) {
    VisSetActive(wParam != 0);
    return 0;
  }
  if (msg == kRetargetMsg) {
    VisRetargetFile(nullptr);
    return 0;
  }
  if (msg == kUiMsg) {
    PushWinampUi();
    return 0;
  }

  if (msg == WM_DROPFILES && !VisIsActive()) MarkWantLocal();
  if (msg == WM_COPYDATA) {
    auto* cds = (COPYDATASTRUCT*)lParam;
    if (cds && cds->lpData && cds->cbData > 0 &&
        (cds->dwData == IPC_PLAYFILE || cds->dwData == IPC_PLAYFILEW)) {
      bool ours = cds->dwData == IPC_PLAYFILEW ? VisIsOurFileW((const wchar_t*)cds->lpData)
                                              : VisIsOurFileA((const char*)cds->lpData);
      if (!ours && !VisIsActive()) MarkWantLocal();
    }
  }
  if (msg == WM_COMMAND) {
    int id = LOWORD(wParam);
    if (id == WINAMP_FILE_PLAY || id == WINAMP_FILE_LOC || id == WINAMP_FILE_DIR)
      MarkWantLocal(30 * 60 * 1000);
  }
  if (msg == WM_WA_IPC) {
    if (lParam == IPC_SETPLAYLISTPOS) {
      /* auto-advance also uses this; only user open/drop marks local */
    }
    if (lParam == IPC_PLAYFILE && wParam > 0xFFFF) {
      if (IsWinampLocalSourceA((const char*)wParam)) MarkWantLocal();
    }
    if (lParam == IPC_PLAYFILEW && wParam > 0xFFFF) {
      if (IsWinampLocalSourceW((const wchar_t*)wParam)) MarkWantLocal();
    }
    if (lParam == IPC_PLAYING_FILEW) {
      auto* f = (const wchar_t*)wParam;
      if (!VisIsOurFileW(f) && IsWinampLocalSourceW(f) && (PendingLocal() || g_local_mode))
        EnterLocalMode();
    }
    if (lParam == IPC_PLAYING_FILE) {
      auto* f = (const char*)wParam;
      if (!VisIsOurFileA(f) && IsWinampLocalSourceA(f) && (PendingLocal() || g_local_mode))
        EnterLocalMode();
    }
  }

  if (msg == WM_TIMER && wParam == kTimerId) {
    std::thread([] { Poll(); }).detach();
    return 0;
  }
  if (msg == WM_TIMER && wParam == kHookTimer) {
    if (!g_in_menu) {
      if (!GetCapture()) EnsureSubclass();
      if (g_logged && g_available && !g_local_mode && !GetCapture() && !VisIsActive())
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  if (g_logged && g_available && !g_local_mode && msg == WM_WA_IPC) {
    if (lParam == IPC_ISPLAYING) {
      if (VisIsActive()) return 1;
      std::lock_guard<std::mutex> lock(g_mu);
      return g_playing ? 1 : 3;
    }
    if (lParam == IPC_GETOUTPUTTIME) {
      int pos = CurrentPosMs();
      std::lock_guard<std::mutex> lock(g_mu);
      if (wParam == 0) return pos;
      if (wParam == 1) return g_dur_ms / 1000;
      if (wParam == 2) return g_dur_ms;
    }
    if (lParam == IPC_HOOK_TITLESW && HookTitlesW(wParam)) return TRUE;
    if (lParam == IPC_HOOK_TITLES && HookTitlesA(wParam)) return TRUE;
    if ((lParam == IPC_GET_EXTENDED_FILE_INFOW || lParam == IPC_GET_EXTENDED_FILE_INFOW_HOOKABLE) &&
        FillMetaW(wParam))
      return TRUE;
    if ((lParam == IPC_GET_EXTENDED_FILE_INFO || lParam == IPC_GET_EXTENDED_FILE_INFO_HOOKABLE) &&
        FillMetaA(wParam))
      return TRUE;
    if (lParam == IPC_GET_PLAYING_TITLE) {
      std::lock_guard<std::mutex> lock(g_mu);
      return (LRESULT)g_title;
    }
  }

  if (g_logged && g_capture) {
    if (msg == WM_COMMAND) {
      int id = LOWORD(wParam);
      if (id == WINAMP_FILE_SHUFFLE && !g_local_mode && !PendingLocal()) {
        LRESULT r = CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
        PushShuffleFromWinamp(hwnd);
        return r;
      }
      int cmd = CmdFromButton(id);
      if (cmd) {
        if (g_pass_stop && cmd == 4) {
          g_pass_stop = false;
          return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
        }
        if (g_local_mode || PendingLocal()) {
          if (cmd == 4 && g_local_mode) {
            LeaveLocalMode(false);
            return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
          }
          EnterLocalMode();
          return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
        }
        FireCmd(cmd);
        return 0;
      }
    }
    if (msg == WM_WA_IPC) {
      if (lParam == IPC_STARTPLAY) {
        if (VisIsStarting()) {
          return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
        }
        if (g_local_mode || PendingLocal() || PlaylistIsLocal()) {
          EnterLocalMode();
          return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
        }
        int cmd = CmdFromButton(WINAMP_BUTTON2);
        FireCmd(cmd);
        return 0;
      }
      if (!g_local_mode && !PendingLocal() && lParam == IPC_SET_SHUFFLE) {
        LRESULT r = CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
        if (!g_ignore_shuffle) ApplySpotifyShuffle(wParam != 0);
        return r;
      }
      if (!g_local_mode && !PendingLocal() && lParam == IPC_SETVOLUME && wParam != (WPARAM)-666) {
        int vol255 = (int)wParam;
        int pct = (vol255 * 100) / 255;
        std::thread([pct] {
          std::string err;
          std::string dev;
          {
            std::lock_guard<std::mutex> lock(g_mu);
            dev = g_player.device_id;
          }
          MakeApi().SetVolume(pct, dev, &err);
        }).detach();
        return 0;
      }
      if (!g_local_mode && !PendingLocal() && lParam == IPC_JUMPTOTIME) {
        int ms = (int)wParam;
        std::thread([ms] {
          std::string err;
          std::string dev;
          {
            std::lock_guard<std::mutex> lock(g_mu);
            dev = g_player.device_id;
          }
          MakeApi().Seek(ms, dev, &err);
          std::lock_guard<std::mutex> lock(g_mu);
          g_base_ms = ms;
          g_player.progress_ms = ms;
          g_stamp = GetTickCount64();
        }).detach();
        return 0;
      }
    }
  }
  return CallWindowProcW(g_old_proc, hwnd, msg, wParam, lParam);
}

bool InSpotifyMlView(HWND hwnd) {
  for (HWND h = hwnd; h; h = GetParent(h)) {
    wchar_t cls[64]{};
    GetClassNameW(h, cls, 64);
    if (lstrcmpW(cls, L"WinamplifySpotifyView") == 0) return true;
  }
  return false;
}

LRESULT CALLBACK CwpHook(int code, WPARAM wParam, LPARAM lParam) {
  if (code >= 0 && g_logged && g_capture && !g_in_menu && !g_local_mode && !g_pass_stop) {
    auto* c = (CWPSTRUCT*)lParam;
    if (c) {
      if ((c->message == WM_LBUTTONDBLCLK || c->message == WM_NCLBUTTONDBLCLK) &&
          !InSpotifyMlView(c->hwnd))
        MarkWantLocal();
      if (c->message == WM_COMMAND && !PendingLocal()) {
        int id = LOWORD(c->wParam);
        if (id == WINAMP_FILE_SHUFFLE) {
          if (plugin.hwndParent) PostMessageW(plugin.hwndParent, kShuffleMsg, 0, 0);
        } else {
          int cmd = CmdFromButton(id);
          if (cmd) FireCmd(cmd);
        }
      }
    }
  }
  return CallNextHookEx(g_cwp, code, wParam, lParam);
}

int init() {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  auto cfg = spotify::LoadConfig();
  g_capture = cfg.capture_buttons;
  g_vis_loopback = cfg.vis_loopback;
  g_logged = MakeApi().LoggedIn();
  g_spotify_msg = RegisterWindowMessageW(WINAMPLIFY_SPOTIFY_MODE_MSG);
  VisInit(plugin.hwndParent);
  g_old_proc = (WNDPROC)SetWindowLongPtrW(plugin.hwndParent, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
  VisBindProc(g_old_proc);
  g_cwp = SetWindowsHookExW(WH_CALLWNDPROC, CwpHook, nullptr,
                            GetWindowThreadProcessId(plugin.hwndParent, nullptr));
  g_timer = SetTimer(plugin.hwndParent, kTimerId, 1800, nullptr);
  g_hook_timer = SetTimer(plugin.hwndParent, kHookTimer, 250, nullptr);
  if (g_logged) std::thread([] { Poll(); }).detach();
  return 0;
}

void config() {
  DialogBoxParamW(plugin.hDllInstance, MAKEINTRESOURCEW(200), plugin.hwndParent, PrefsProc, 0);
}

void quit() {
  VisShutdown();
  if (g_cwp) {
    UnhookWindowsHookEx(g_cwp);
    g_cwp = nullptr;
  }
  if (g_timer) KillTimer(plugin.hwndParent, kTimerId);
  if (g_hook_timer) KillTimer(plugin.hwndParent, kHookTimer);
  if (g_old_proc) SetWindowLongPtrW(plugin.hwndParent, GWLP_WNDPROC, (LONG_PTR)g_old_proc);
}

char desc[] = "Spotify Connect (Winamplify)";

}  // namespace

extern "C" __declspec(dllexport) winampGeneralPurposePlugin* winampGetGeneralPurposePlugin() {
  plugin.version = GPPHDR_VER;
  plugin.description = desc;
  plugin.init = init;
  plugin.config = config;
  plugin.quit = quit;
  return &plugin;
}
