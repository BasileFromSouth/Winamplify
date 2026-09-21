#include "ml.h"
#include "wa_ipc.h"
#include "spotify_api.h"
#include "store.h"
#include "utf8.h"
#include "winamplify_ipc.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

winampMediaLibraryPlugin plugin{};
int g_root_id = 0;
int g_search_id = 0;
int g_liked_id = 0;
HWND g_active_host = nullptr;
constexpr UINT kReskinMsg = WM_APP + 2;
constexpr UINT_PTR kSkinSubclassId = 0x53504F54; // 'SPOT'

enum Kind { kRoot, kSearch, kLiked, kPlaylist };

enum {
  IDC_SEARCH_LABEL = 1001,
  IDC_SEARCH_EDIT = 1002,
  IDC_SEARCH_CLEAR = 1003,
  IDT_SEARCH = 1,
};

const int kBarH = 28;

struct Node {
  Kind kind = kRoot;
  std::string id;
  std::string uri;
  std::string name;
};

struct View {
  Kind kind = kRoot;
  HWND host = nullptr;
  HWND lv = nullptr;
  HWND label = nullptr;
  HWND edit = nullptr;
  HWND clear = nullptr;
  INT_PTR skin = 0;
  WNDPROC old_edit = nullptr;
  WNDPROC old_lv = nullptr;
  HBRUSH br_wnd = nullptr;
  HBRUSH br_item = nullptr;
  unsigned gen = 0;
  std::string last_q;
  std::vector<spotify::Track> tracks;
  std::string node_uri;
  std::string node_id;
};

std::unordered_map<int, Node> g_nodes;
std::vector<std::string> g_title_store;

void MlLog(const std::string& line);

spotify::Api MakeApi() {
  auto c = spotify::LoadConfig();
  return spotify::Api(spotify::EffectiveClientId(c));
}

int AddTree(int parent, const char* title, bool children) {
  mlAddTreeItemStruct it{};
  it.parent_id = parent;
  it.title = const_cast<char*>(title);
  it.has_children = children ? 1 : 0;
  it.this_id = 0;
  SendMessage(plugin.hwndLibraryParent, WM_ML_IPC, (WPARAM)&it, ML_IPC_ADDTREEITEM);
  return it.this_id;
}

void EnsureChildren() {
  if (g_search_id) return;
  g_search_id = AddTree(g_root_id, "Search", false);
  g_nodes[g_search_id] = {kSearch, {}, {}, "Search"};
  g_liked_id = AddTree(g_root_id, "Liked Songs", false);
  g_nodes[g_liked_id] = {kLiked, {}, "spotify:user:me:collection", "Liked Songs"};

  std::string err;
  auto lists = MakeApi().GetPlaylists(&err);
  for (auto& p : lists) {
    g_title_store.push_back(p.name);
    int id = AddTree(g_root_id, g_title_store.back().c_str(), false);
    g_nodes[id] = {kPlaylist, p.id, p.uri.empty() ? ("spotify:playlist:" + p.id) : p.uri, p.name};
  }
}

LRESULT MlIpc(WPARAM w, LPARAM ipc) {
  if (!plugin.hwndLibraryParent) return 0;
  return SendMessage(plugin.hwndLibraryParent, WM_ML_IPC, w, ipc);
}

COLORREF WaColor(int idx, COLORREF fallback) {
  using Fn = int (*)(int);
  auto fn = (Fn)MlIpc(1, ML_IPC_SKIN_WADLG_GETFUNC);
  if (!fn) return fallback;
  return (COLORREF)fn(idx);
}

HFONT WaFont() {
  return (HFONT)MlIpc(66, ML_IPC_SKIN_WADLG_GETFUNC);
}

void ApplyFont(HWND w) {
  HFONT f = WaFont();
  if (f && w) SendMessageW(w, WM_SETFONT, (WPARAM)f, TRUE);
}

void RecreateBrushes(View* v) {
  if (v->br_wnd) DeleteObject(v->br_wnd);
  if (v->br_item) DeleteObject(v->br_item);
  v->br_wnd = CreateSolidBrush(WaColor(WADLG_WNDBG, RGB(0, 0, 80)));
  v->br_item = CreateSolidBrush(WaColor(WADLG_ITEMBG, RGB(0, 0, 80)));
}

void SkinList(View* v) {
  if (!v || !v->lv) return;
  COLORREF bg = WaColor(WADLG_ITEMBG, RGB(0, 0, 80));
  COLORREF fg = WaColor(WADLG_ITEMFG, RGB(255, 255, 255));
  ListView_SetBkColor(v->lv, bg);
  ListView_SetTextBkColor(v->lv, bg);
  ListView_SetTextColor(v->lv, fg);
  HWND hdr = ListView_GetHeader(v->lv);
  if (hdr) {
    SetWindowTheme(hdr, L"", L"");
    SetWindowTheme(v->lv, L"", L"");
  }
}

void AttachMlSkin(View* v) {
  if (!v || !v->lv) return;
  if (v->skin) {
    MlIpc((WPARAM)v->skin, ML_IPC_UNSKIN_LISTVIEW);
    v->skin = 0;
  }
  v->skin = MlIpc((WPARAM)v->lv, ML_IPC_SKIN_LISTVIEW);
}

void ReskinView(View* v) {
  if (!v || !v->host) return;
  RecreateBrushes(v);
  ApplyFont(v->label);
  ApplyFont(v->edit);
  ApplyFont(v->clear);
  ApplyFont(v->lv);
  SkinList(v);
  AttachMlSkin(v);
  if (v->skin) MlIpc((WPARAM)v->skin, ML_IPC_LISTVIEW_UPDATE);
  InvalidateRect(v->host, nullptr, TRUE);
  if (v->lv) InvalidateRect(v->lv, nullptr, TRUE);
  if (v->label) InvalidateRect(v->label, nullptr, TRUE);
  if (v->edit) InvalidateRect(v->edit, nullptr, TRUE);
  if (v->clear) InvalidateRect(v->clear, nullptr, TRUE);
  UpdateWindow(v->host);
}

void InitColumns(HWND lv) {
  LVCOLUMNW c{};
  c.mask = LVCF_TEXT | LVCF_WIDTH;
  c.cx = 220;
  wchar_t t1[] = L"Title";
  c.pszText = t1;
  ListView_InsertColumn(lv, 0, &c);
  c.cx = 180;
  wchar_t t2[] = L"Artist";
  c.pszText = t2;
  ListView_InsertColumn(lv, 1, &c);
  c.cx = 180;
  wchar_t t3[] = L"Album";
  c.pszText = t3;
  ListView_InsertColumn(lv, 2, &c);
  c.cx = 70;
  wchar_t t4[] = L"Length";
  c.pszText = t4;
  ListView_InsertColumn(lv, 3, &c);
}

std::wstring DurationText(int ms) {
  if (ms <= 0) return L"";
  int s = ms / 1000;
  wchar_t buf[32];
  swprintf_s(buf, L"%d:%02d", s / 60, s % 60);
  return buf;
}

void FillTracks(View* v, const std::vector<spotify::Track>& tracks) {
  if (!v || !v->lv) return;
  v->tracks = tracks;
  ListView_DeleteAllItems(v->lv);
  for (int i = 0; i < (int)tracks.size(); i++) {
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = i;
    auto title = spotify::Utf8ToWide(tracks[i].name);
    wchar_t empty[] = L"";
    it.pszText = title.empty() ? empty : title.data();
    ListView_InsertItem(v->lv, &it);
    auto artist = spotify::Utf8ToWide(tracks[i].artist);
    ListView_SetItemText(v->lv, i, 1, artist.empty() ? empty : artist.data());
    auto album = spotify::Utf8ToWide(tracks[i].album);
    ListView_SetItemText(v->lv, i, 2, album.empty() ? empty : album.data());
    auto dur = DurationText(tracks[i].duration_ms);
    ListView_SetItemText(v->lv, i, 3, dur.empty() ? empty : dur.data());
  }
  if (v->skin) MlIpc((WPARAM)v->skin, ML_IPC_LISTVIEW_UPDATE);
}

View* ViewFrom(HWND h) { return (View*)GetWindowLongPtrW(h, GWLP_USERDATA); }

void LayoutView(View* v) {
  if (!v || !v->host) return;
  RECT r;
  GetClientRect(v->host, &r);
  int top = 0;
  if (v->kind == kSearch) {
    top = kBarH;
    if (v->label) MoveWindow(v->label, 8, 6, 58, 16, TRUE);
    int clear_w = 92;
    if (v->edit) MoveWindow(v->edit, 70, 3, r.right - 70 - clear_w - 12, 22, TRUE);
    if (v->clear) MoveWindow(v->clear, r.right - clear_w - 6, 3, clear_w, 22, TRUE);
  }
  if (v->lv) MoveWindow(v->lv, 0, top, r.right, r.bottom - top, TRUE);
}

bool PlayerStarted(spotify::Api& api) {
  std::string err;
  auto st = api.GetPlayer(&err);
  return st.is_playing || !st.title.empty();
}

std::vector<std::string> CandidateDevices(spotify::Api& api) {
  std::string err;
  auto st = api.GetPlayer(&err);
  auto ds = api.GetDevices(&err);
  std::vector<std::string> out;
  auto add = [&](const std::string& id) {
    if (id.empty()) return;
    for (auto& x : out)
      if (x == id) return;
    out.push_back(id);
  };
  if (!st.device_id.empty() && !st.commands_restricted) add(st.device_id);
  for (auto& d : ds) {
    if (d.is_active && d.id != st.device_id) add(d.id);
  }
  for (auto& d : ds) add(d.id);
  auto cfg = spotify::LoadConfig();
  add(cfg.device_id);
  add({});
  return out;
}

bool PlayWithRetry(spotify::Api& api, const std::function<bool(const std::string&, std::string*)>& fn,
                   std::string* out_err = nullptr) {
  std::string err;
  auto st = api.GetPlayer(&err);
  if (st.commands_restricted) MlLog("player restricted on " + st.device_name + " — skip transfer");
  for (auto& dev : CandidateDevices(api)) {
    if (!fn(dev, &err)) continue;
    Sleep(400);
    if (PlayerStarted(api)) {
      if (out_err) out_err->clear();
      return true;
    }
    MlLog(std::string("play 2xx but idle, device=") + (dev.empty() ? "default" : dev));
  }
  if (out_err) *out_err = err.empty() ? "no playable Spotify device" : err;
  return false;
}

void MlLog(const std::string& line) {
  wchar_t dir[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) return;
  std::wstring folder = std::wstring(dir) + L"\\Winamp\\Plugins\\spotify";
  CreateDirectoryW((std::wstring(dir) + L"\\Winamp").c_str(), nullptr);
  CreateDirectoryW((std::wstring(dir) + L"\\Winamp\\Plugins").c_str(), nullptr);
  CreateDirectoryW(folder.c_str(), nullptr);
  FILE* f = nullptr;
  _wfopen_s(&f, (folder + L"\\ml.log").c_str(), L"a");
  if (!f) return;
  fprintf(f, "%s\n", line.c_str());
  fclose(f);
}

void NotifySpotifyPlayback() {
  static UINT msg = RegisterWindowMessageW(WINAMPLIFY_SPOTIFY_MODE_MSG);
  if (plugin.hwndWinampParent) PostMessageW(plugin.hwndWinampParent, msg, 1, 0);
}

void PlayPlaylist(const std::string& uri_or_id) {
  if (uri_or_id.empty()) return;
  auto api = MakeApi();
  std::string uri = uri_or_id;
  if (uri.rfind("spotify:", 0) != 0) uri = "spotify:playlist:" + uri;
  std::string err;
  bool ok = PlayWithRetry(api, [&](const std::string& dev, std::string* e) {
    return api.PlayContext(uri, 0, dev, e);
  }, &err);
  MlLog(std::string(ok ? "play playlist ok " : "play playlist fail ") + uri + " " + err);
  if (ok) NotifySpotifyPlayback();
}

void PlayContextUri(const std::string& uri) {
  if (uri.empty()) return;
  auto api = MakeApi();
  std::string err;
  bool ok = PlayWithRetry(api, [&](const std::string& dev, std::string* e) {
    return api.PlayContext(uri, 0, dev, e);
  }, &err);
  MlLog(std::string(ok ? "play album ok " : "play album fail ") + uri + " " + err);
  if (ok) NotifySpotifyPlayback();
}

void PlayUris(const std::vector<std::string>& uris) {
  if (uris.empty()) return;
  auto api = MakeApi();
  std::string err;
  bool ok = PlayWithRetry(
      api, [&](const std::string& dev, std::string* e) { return api.PlayUris(uris, dev, e); }, &err);
  auto st = api.GetPlayer(&err);
  MlLog(std::string(ok ? "play ok " : "play failed ") + uris[0] + " playing=" +
        (st.is_playing ? "1" : "0") + " title=" + st.title + " device=" + st.device_name +
        (ok ? "" : (" err=" + err)));
  if (ok) NotifySpotifyPlayback();
}

void PlayLiked() {
  NotifySpotifyPlayback();
  auto api = MakeApi();
  std::string err;
  auto tracks = api.GetLikedTracks(&err);
  std::vector<std::string> uris;
  for (int i = 0; i < (int)tracks.size() && i < 50; i++) uris.push_back(tracks[i].uri);
  PlayUris(uris);
}

void PlayRow(const std::vector<spotify::Track>& tracks, int index) {
  if (index < 0 || index >= (int)tracks.size()) return;
  const auto& tr = tracks[index];
  MlLog(std::string("play row i=") + std::to_string(index) + " uri=" + tr.uri);
  if (tr.uri.rfind("spotify:playlist:", 0) == 0) {
    PlayPlaylist(tr.uri);
    return;
  }
  if (tr.uri.rfind("spotify:album:", 0) == 0) {
    PlayContextUri(tr.uri);
    return;
  }
  if (tr.uri.rfind("spotify:user:me:collection", 0) == 0) {
    PlayLiked();
    return;
  }
  if (tr.uri.rfind("spotify:track:", 0) == 0) {
    if (!tr.album_uri.empty()) {
      auto api = MakeApi();
      std::string err;
      MlLog("search play context " + tr.album_uri + " offset " + tr.uri);
      bool ok = PlayWithRetry(api, [&](const std::string& dev, std::string* e) {
        return api.PlayContextAt(tr.album_uri, tr.uri, dev, e);
      }, &err);
      auto st = api.GetPlayer(&err);
      MlLog(std::string(ok ? "search play ok " : "search play fail ") + tr.uri +
            " playing=" + (st.is_playing ? "1" : "0") + " title=" + st.title +
            " device=" + st.device_name + (ok ? "" : (" err=" + err)));
      if (ok) NotifySpotifyPlayback();
      return;
    }
    PlayUris({tr.uri});
    return;
  }
  MlLog(std::string("play skipped, uri=") + tr.uri);
}

void ActivateIndex(View* v, int i) {
  if (!v) {
    MlLog("activate no view");
    return;
  }
  if (i < 0 && v->lv) i = ListView_GetNextItem(v->lv, -1, LVNI_SELECTED);
  std::string uri = (i >= 0 && i < (int)v->tracks.size()) ? v->tracks[i].uri : "";
  MlLog(std::string("activate i=") + std::to_string(i) + " n=" + std::to_string(v->tracks.size()) +
        " uri=" + uri);
  if (i < 0 || i >= (int)v->tracks.size()) return;
  auto tracks = v->tracks;
  std::thread([tracks, i] { PlayRow(tracks, i); }).detach();
}

void RunSearch(View* v) {
  if (!v || !v->edit) return;
  wchar_t buf[512]{};
  GetWindowTextW(v->edit, buf, 512);
  std::string q = spotify::WideToUtf8(buf);
  while (!q.empty() && (q.front() == ' ' || q.front() == '\t')) q.erase(q.begin());
  while (!q.empty() && (q.back() == ' ' || q.back() == '\t')) q.pop_back();
  if (q == v->last_q) return;
  v->last_q = q;
  unsigned gen = ++v->gen;
  HWND host = v->host;
  if (q.empty()) {
    FillTracks(v, {});
    return;
  }
  std::thread([host, gen, q] {
    std::string err;
    auto api = MakeApi();
    std::vector<spotify::Track> tracks;
    if (!api.LoggedIn()) {
      spotify::Track t;
      t.name = "Sign in first (Prefs > Spotify Connect)";
      tracks.push_back(t);
    } else {
      tracks = api.Search(q, &err);
      if (tracks.empty()) {
        spotify::Track t;
        t.name = err.empty() ? "No results" : err;
        tracks.push_back(t);
      }
    }
    if (!IsWindow(host)) return;
    auto* dst = ViewFrom(host);
    if (!dst || dst->gen != gen) return;
    dst->tracks = std::move(tracks);
    PostMessageW(host, WM_APP + 1, 0, 0);
  }).detach();
}

void LoadNode(View* v, const Node& node) {
  unsigned gen = ++v->gen;
  HWND host = v->host;
  std::thread([host, gen, node] {
    std::string err;
    auto api = MakeApi();
    std::vector<spotify::Track> tracks;
    if (!api.LoggedIn()) {
      spotify::Track t;
      t.name = "Sign in first (Prefs > Spotify Connect)";
      tracks.push_back(t);
    } else if (node.kind == kSearch) {
      spotify::Track t;
      t.name = "Type in Search and press Enter";
      tracks.push_back(t);
    } else if (node.kind == kLiked) {
      tracks = api.GetLikedTracks(&err);
      if (tracks.empty()) {
        spotify::Track t;
        t.name = "Liked Songs";
        t.artist = "double-click = play on Spotify";
        t.uri = "spotify:user:me:collection";
        tracks.push_back(t);
      }
    } else if (node.kind == kPlaylist) {
      tracks = api.GetPlaylistTracks(node.id, &err);
      if (tracks.empty()) {
        spotify::Track t;
        t.name = node.name;
        t.artist = "double-click = play on Spotify";
        t.uri = node.uri.empty() ? ("spotify:playlist:" + node.id) : node.uri;
        tracks.push_back(t);
      }
    } else {
      auto lists = api.GetPlaylists(&err);
      for (auto& p : lists) {
        spotify::Track t;
        t.name = p.name;
        t.artist = "Playlist";
        t.album = std::to_string(p.tracks) + " tracks";
        t.uri = p.uri.empty() ? ("spotify:playlist:" + p.id) : p.uri;
        tracks.push_back(t);
      }
    }
    if (!IsWindow(host)) return;
    auto* dst = ViewFrom(host);
    if (!dst || dst->gen != gen) return;
    dst->tracks = std::move(tracks);
    PostMessageW(host, WM_APP + 1, 0, 0);
  }).detach();
}

LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  View* v = ViewFrom(GetParent(h));
  if (v && m == WM_LBUTTONDBLCLK) {
    LVHITTESTINFO ht{};
    ht.pt.x = GET_X_LPARAM(l);
    ht.pt.y = GET_Y_LPARAM(l);
    int i = ListView_HitTest(h, &ht);
    if (i < 0) i = ListView_GetNextItem(h, -1, LVNI_SELECTED);
    ActivateIndex(v, i);
  }
  if (v && m == WM_KEYDOWN && w == VK_RETURN) {
    ActivateIndex(v, -1);
    return 0;
  }
  if (v && v->old_lv) return CallWindowProcW(v->old_lv, h, m, w, l);
  return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  View* v = ViewFrom(GetParent(h));
  if (m == WM_KEYDOWN && w == VK_RETURN && v) {
    KillTimer(v->host, IDT_SEARCH);
    v->last_q.clear();
    RunSearch(v);
    return 0;
  }
  if (v && v->old_edit) return CallWindowProcW(v->old_edit, h, m, w, l);
  return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN || m == WM_DRAWITEM ||
      m == WM_ERASEBKGND) {
    using HandleFn = int (*)(HWND, UINT, WPARAM, LPARAM);
    auto handle = (HandleFn)MlIpc(2, ML_IPC_SKIN_WADLG_GETFUNC);
    if (handle) {
      int r = handle(h, m, w, l);
      if (r) return r;
    }
  }

  View* v = ViewFrom(h);
  if (m == WM_ERASEBKGND) {
    RECT r;
    GetClientRect(h, &r);
    HBRUSH br = v && v->br_wnd ? v->br_wnd : (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect((HDC)w, &r, br);
    return 1;
  }
  if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN) {
    HDC dc = (HDC)w;
    COLORREF bg = WaColor(m == WM_CTLCOLOREDIT ? WADLG_ITEMBG : WADLG_WNDBG, RGB(0, 0, 80));
    COLORREF fg = WaColor(m == WM_CTLCOLOREDIT ? WADLG_ITEMFG : WADLG_WNDFG, RGB(255, 255, 255));
    SetBkColor(dc, bg);
    SetTextColor(dc, fg);
    if (v) return (LRESULT)(m == WM_CTLCOLOREDIT ? v->br_item : v->br_wnd);
  }
  if (m == WM_APP + 1 && v) {
    FillTracks(v, v->tracks);
    return 0;
  }
  if (m == kReskinMsg && v) {
    ReskinView(v);
    return 0;
  }
  if (m == WM_SIZE && v) {
    LayoutView(v);
    return 0;
  }
  if (m == WM_TIMER && w == IDT_SEARCH && v) {
    KillTimer(h, IDT_SEARCH);
    RunSearch(v);
    return 0;
  }
  if (m == WM_COMMAND && v && v->kind == kSearch) {
    int id = LOWORD(w);
    int code = HIWORD(w);
    if (id == IDC_SEARCH_CLEAR) {
      SetWindowTextW(v->edit, L"");
      v->last_q.clear();
      FillTracks(v, {});
      return 0;
    }
    if (id == IDC_SEARCH_EDIT && code == EN_CHANGE) {
      SetTimer(h, IDT_SEARCH, 450, nullptr);
      return 0;
    }
  }
  if ((m == WM_NOTIFY || m == WM_NOTIFY + 0x2000) && v && v->lv) {
    auto* n = (NMHDR*)l;
    if (n && n->hwndFrom == v->lv &&
        (n->code == NM_DBLCLK || n->code == NM_RETURN)) {
      auto* ia = (NMITEMACTIVATE*)l;
      int i = ia ? ia->iItem : -1;
      ActivateIndex(v, i);
    }
  }
  if (m == WM_DESTROY && v) {
    KillTimer(h, IDT_SEARCH);
    if (g_active_host == h) g_active_host = nullptr;
    if (v->lv && v->old_lv) SetWindowLongPtrW(v->lv, GWLP_WNDPROC, (LONG_PTR)v->old_lv);
    if (v->edit && v->old_edit) SetWindowLongPtrW(v->edit, GWLP_WNDPROC, (LONG_PTR)v->old_edit);
    if (v->skin) MlIpc((WPARAM)v->skin, ML_IPC_UNSKIN_LISTVIEW);
    if (v->br_wnd) DeleteObject(v->br_wnd);
    if (v->br_item) DeleteObject(v->br_item);
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    delete v;
  }
  return DefWindowProcW(h, m, w, l);
}

const wchar_t* kViewClass = L"WinamplifySpotifyView";

HWND CreateView(HWND parent, int tree_id) {
  static bool reg = false;
  if (!reg) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ViewProc;
    wc.hInstance = plugin.hDllInstance;
    wc.lpszClassName = kViewClass;
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    reg = true;
  }
  auto it = g_nodes.find(tree_id);
  Node node = it != g_nodes.end() ? it->second : Node{};

  RECT pr;
  GetClientRect(parent, &pr);
  HWND host = CreateWindowExW(0, kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0,
                              pr.right, pr.bottom, parent, nullptr, plugin.hDllInstance, nullptr);
  auto* v = new View{};
  v->host = host;
  v->kind = node.kind;
  v->node_uri = node.uri;
  v->node_id = node.id;
  SetWindowLongPtrW(host, GWLP_USERDATA, (LONG_PTR)v);
  RecreateBrushes(v);

  if (node.kind == kSearch) {
    v->label = CreateWindowExW(0, L"STATIC", L"Search:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 8, 6,
                               58, 16, host, (HMENU)IDC_SEARCH_LABEL, plugin.hDllInstance, nullptr);
    v->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 70, 3, 200, 22, host,
                              (HMENU)IDC_SEARCH_EDIT, plugin.hDllInstance, nullptr);
    v->clear = CreateWindowExW(0, L"BUTTON", L"Clear Search", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 280,
                               3, 92, 22, host, (HMENU)IDC_SEARCH_CLEAR, plugin.hDllInstance, nullptr);
    ApplyFont(v->label);
    ApplyFont(v->edit);
    ApplyFont(v->clear);
    v->old_edit = (WNDPROC)SetWindowLongPtrW(v->edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
  }

  v->lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                          0, node.kind == kSearch ? kBarH : 0, pr.right, pr.bottom, host, (HMENU)1,
                          plugin.hDllInstance, nullptr);
  ListView_SetExtendedListViewStyle(v->lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
  InitColumns(v->lv);
  ApplyFont(v->lv);
  SkinList(v);
  AttachMlSkin(v);
  v->old_lv = (WNDPROC)SetWindowLongPtrW(v->lv, GWLP_WNDPROC, (LONG_PTR)ListProc);
  LayoutView(v);
  LoadNode(v, node);
  g_active_host = host;
  return host;
}

LRESULT CALLBACK WaSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (m == WM_WA_IPC &&
      (l == IPC_SKIN_CHANGED || l == IPC_FF_ONCOLORTHEMECHANGED) && g_active_host &&
      IsWindow(g_active_host)) {
    // After Winamp finishes WADlg_init; defer so colors are current.
    PostMessageW(g_active_host, kReskinMsg, 0, 0);
  }
  return DefSubclassProc(h, m, w, l);
}

void HookWinampSkin() {
  HWND wa = plugin.hwndWinampParent;
  if (!wa) return;
  SetWindowSubclass(wa, WaSubclass, kSkinSubclassId, 0);
}

void UnhookWinampSkin() {
  HWND wa = plugin.hwndWinampParent;
  if (wa) RemoveWindowSubclass(wa, WaSubclass, kSkinSubclassId);
}

int init() {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&icc);
  HookWinampSkin();
  g_root_id = AddTree(0, "Spotify", true);
  g_nodes[g_root_id] = {kRoot, {}, {}, "Spotify"};
  return 0;
}

void quit() {
  UnhookWinampSkin();
  g_active_host = nullptr;
}

INT_PTR MessageProc(int message_type, INT_PTR param1, INT_PTR param2, INT_PTR) {
  if (message_type == ML_MSG_TREE_ONCREATEVIEW) {
    int id = (int)param1;
    if (g_nodes.count(id)) {
      if (id == g_root_id) EnsureChildren();
      return (INT_PTR)CreateView((HWND)param2, id);
    }
  }
  if (message_type == ML_MSG_TREE_ONCLICK && param2 == ML_ACTION_DBLCLICK) {
    int id = (int)param1;
    auto it = g_nodes.find(id);
    if (it == g_nodes.end()) return 0;
    std::thread([n = it->second] {
      if (n.kind == kPlaylist)
        PlayPlaylist(n.uri.empty() ? n.id : n.uri);
      else if (n.kind == kLiked)
        PlayLiked();
    }).detach();
    return 1;
  }
  return 0;
}

char desc[] = "Spotify Playlists (Winamplify)";

}  // namespace

extern "C" __declspec(dllexport) winampMediaLibraryPlugin* winampGetMediaLibraryPlugin() {
  plugin.version = MLHDR_VER;
  plugin.description = desc;
  plugin.init = init;
  plugin.quit = quit;
  plugin.MessageProc = MessageProc;
  return &plugin;
}
