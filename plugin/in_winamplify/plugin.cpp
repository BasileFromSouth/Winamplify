#include "in2.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shlobj.h>
#include <psapi.h>
#include <stdio.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "psapi.lib")

#ifndef AUDCLNT_STREAMFLAGS_LOOPBACK
#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#endif

namespace {

In_Module g_mod{};
std::atomic<bool> g_play{false};
std::atomic<bool> g_paused{false};
std::thread g_th;
ULONGLONG g_start_tick = 0;
int g_len_ms = 24 * 60 * 60 * 1000;
char g_desc[] = "Winamplify system vis (feeds Winamp vis from WASAPI)";
char g_ext[] = "lsv\0Winamplify vis\0";

void InLog(const char* fmt, ...) {
  wchar_t dir[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) return;
  std::wstring path = std::wstring(dir) + L"\\Winamp\\Plugins\\spotify";
  CreateDirectoryW((std::wstring(dir) + L"\\Winamp").c_str(), nullptr);
  CreateDirectoryW((std::wstring(dir) + L"\\Winamp\\Plugins").c_str(), nullptr);
  CreateDirectoryW(path.c_str(), nullptr);
  path += L"\\vis.log";
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    fwrite(buf, 1, (size_t)n, f);
    fputc('\n', f);
  }
  fclose(f);
}

void ConvertPacket(const BYTE* src, UINT32 frames, const WAVEFORMATEX* fmt, std::vector<int16_t>* stereo) {
  if (!src || !frames || !fmt) {
    stereo->assign((size_t)frames * 2, 0);
    return;
  }
  int nch = fmt->nChannels > 0 ? fmt->nChannels : 2;
  int bps = fmt->wBitsPerSample;
  bool ieee = (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
  if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
    auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
    ieee = (ext->SubFormat.Data1 == 3);
    bps = ext->Samples.wValidBitsPerSample ? ext->Samples.wValidBitsPerSample : fmt->wBitsPerSample;
  }
  stereo->resize((size_t)frames * 2);
  for (UINT32 i = 0; i < frames; i++) {
    int16_t l = 0, r = 0;
    if (ieee && bps == 32) {
      const float* f = reinterpret_cast<const float*>(src) + (size_t)i * nch;
      float fl = f[0];
      float fr = nch > 1 ? f[1] : fl;
      if (fl > 1.f) fl = 1.f;
      if (fl < -1.f) fl = -1.f;
      if (fr > 1.f) fr = 1.f;
      if (fr < -1.f) fr = -1.f;
      l = (int16_t)(fl * 32767.f);
      r = (int16_t)(fr * 32767.f);
    } else if (bps == 16) {
      const int16_t* s = reinterpret_cast<const int16_t*>(src) + (size_t)i * nch;
      l = s[0];
      r = nch > 1 ? s[1] : l;
    } else if (bps == 32) {
      const int32_t* s = reinterpret_cast<const int32_t*>(src) + (size_t)i * nch;
      l = (int16_t)(s[0] >> 16);
      r = nch > 1 ? (int16_t)(s[1] >> 16) : l;
    } else {
      l = r = 0;
    }
    (*stereo)[i * 2] = l;
    (*stereo)[i * 2 + 1] = r;
  }
}

struct Cap {
  IAudioClient* client = nullptr;
  IAudioCaptureClient* cap = nullptr;
  WAVEFORMATEX* mix = nullptr;
  std::vector<int16_t> fifo;
  void Close() {
    if (client) client->Stop();
    if (cap) {
      cap->Release();
      cap = nullptr;
    }
    if (mix) {
      CoTaskMemFree(mix);
      mix = nullptr;
    }
    if (client) {
      client->Release();
      client = nullptr;
    }
    fifo.clear();
  }
  bool Open(IMMDevice* dev) {
    Close();
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) return false;
    if (FAILED(client->GetMixFormat(&mix)) || !mix) {
      Close();
      return false;
    }
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000, 0, mix, nullptr))) {
      Close();
      return false;
    }
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap)) || !cap) {
      Close();
      return false;
    }
    if (FAILED(client->Start())) {
      Close();
      return false;
    }
    return true;
  }
  void Drain() {
    if (!cap || !mix) return;
    UINT32 pkt = 0;
    if (FAILED(cap->GetNextPacketSize(&pkt))) return;
    while (pkt) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
      std::vector<int16_t> st;
      if (data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
        ConvertPacket(data, frames, mix, &st);
      else
        st.assign((size_t)frames * 2, 0);
      cap->ReleaseBuffer(frames);
      fifo.insert(fifo.end(), st.begin(), st.end());
      if (FAILED(cap->GetNextPacketSize(&pkt))) break;
    }
    if (fifo.size() > 48000 * 2) fifo.erase(fifo.begin(), fifo.end() - 48000 * 2);
  }
};

std::mutex g_pcm_mu;
int16_t g_pcm[576 * 2]{};
std::atomic<unsigned> g_pcm_seq{0};
unsigned g_pcm_seen = 0;
int g_feed_n = 0;

void PushPcm(const int16_t* pcm, int frames) {
  if (!pcm || frames < 576) return;
  std::lock_guard<std::mutex> lock(g_pcm_mu);
  memcpy(g_pcm, pcm, sizeof(int16_t) * 576 * 2);
  g_pcm_seq++;
}

void FeedNow() {
  int16_t local[576 * 2];
  unsigned seq;
  {
    std::lock_guard<std::mutex> lock(g_pcm_mu);
    seq = g_pcm_seq.load();
    if (seq == g_pcm_seen) return;
    memcpy(local, g_pcm, sizeof(local));
    g_pcm_seen = seq;
  }
  if (!g_mod.SAAddPCMData) return;
  int ts = (int)(GetTickCount64() - g_start_tick);
  g_mod.SAAddPCMData(local, 2, 16, ts);
  if (g_mod.VSAAddPCMData) g_mod.VSAAddPCMData(local, 2, 16, ts);
  if ((++g_feed_n % 50) == 1) {
    int pk = 0;
    for (int i = 0; i < 576 * 2; i++) {
      int a = local[i];
      if (a < 0) a = -a;
      if (a > pk) pk = a;
    }
    int mode = g_mod.SAGetMode ? g_mod.SAGetMode() : -1;
    InLog("in: feed n=%d peak=%d mode=%d ts=%d", g_feed_n, pk, mode, ts);
  }
}

void StealVisFns() {
  if (g_mod.SAAddPCMData && g_mod.SAVSAInit) return;
  HMODULE mods[256]{};
  DWORD need = 0;
  if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return;
  int n = (int)(need / sizeof(HMODULE));
  if (n > 256) n = 256;
  for (int i = 0; i < n; i++) {
    wchar_t name[MAX_PATH]{};
    if (!GetModuleBaseNameW(GetCurrentProcess(), mods[i], name, MAX_PATH)) continue;
    if (_wcsnicmp(name, L"in_", 3) != 0) continue;
    auto get = (In_Module * (*)()) GetProcAddress(mods[i], "winampGetInModule2");
    if (!get) continue;
    In_Module* o = get();
    if (!o || o == &g_mod || !o->SAAddPCMData) continue;
    g_mod.SAVSAInit = o->SAVSAInit;
    g_mod.SAVSADeInit = o->SAVSADeInit;
    g_mod.SAAddPCMData = o->SAAddPCMData;
    g_mod.SAGetMode = o->SAGetMode;
    g_mod.SAAdd = o->SAAdd;
    g_mod.VSAAddPCMData = o->VSAAddPCMData;
    g_mod.VSAGetMode = o->VSAGetMode;
    g_mod.VSAAdd = o->VSAAdd;
    g_mod.VSASetInfo = o->VSASetInfo;
    g_mod.SetInfo = o->SetInfo;
    InLog("in: vis fns copied SAAdd=%p", g_mod.SAAddPCMData);
    return;
  }
}

void VisInitUi(int srate) {
  StealVisFns();
  if (g_mod.SAVSAInit) g_mod.SAVSAInit(40, srate);
  if (g_mod.VSASetInfo) g_mod.VSASetInfo(srate, 2);
  if (g_mod.SetInfo) g_mod.SetInfo(1411, srate / 1000, 1, 1);
  int mode = g_mod.SAGetMode ? g_mod.SAGetMode() : -1;
  InLog("in: VisInitUi SAAdd=%p SAVSA=%p mode=%d srate=%d", g_mod.SAAddPCMData, g_mod.SAVSAInit, mode, srate);
}

void PlayThread() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  int srate = 48000;
  InLog("in: thread start SAAdd=%p", g_mod.SAAddPCMData);

  IMMDeviceEnumerator* en = nullptr;
  std::vector<Cap*> caps;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 (void**)&en)) &&
      en) {
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
      UINT n = 0;
      col->GetCount(&n);
      for (UINT i = 0; i < n; i++) {
        IMMDevice* dev = nullptr;
        if (FAILED(col->Item(i, &dev)) || !dev) continue;
        auto* c = new Cap();
        if (c->Open(dev))
          caps.push_back(c);
        else {
          c->Close();
          delete c;
        }
        dev->Release();
      }
      col->Release();
    }
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def) {
      auto* c = new Cap();
      if (c->Open(def))
        caps.push_back(c);
      else {
        c->Close();
        delete c;
      }
      def->Release();
    }
  }
  InLog("in: loopback devices=%d", (int)caps.size());

  while (g_play.load()) {
    if (g_paused.load()) {
      Sleep(20);
      continue;
    }
    for (auto* c : caps) c->Drain();
    const int need = 576;
    for (;;) {
      Cap* best = nullptr;
      int best_pk = -1;
      for (auto* c : caps) {
        if (c->fifo.size() < (size_t)need * 2) continue;
        int pk = 0;
        for (int k = 0; k < need * 2; k++) {
          int a = c->fifo[k];
          if (a < 0) a = -a;
          if (a > pk) pk = a;
        }
        if (pk > best_pk) {
          best_pk = pk;
          best = c;
        }
      }
      if (!best) break;
      PushPcm(best->fifo.data(), need);
      for (auto* c : caps) {
        if (c->fifo.size() >= (size_t)need * 2)
          c->fifo.erase(c->fifo.begin(), c->fifo.begin() + need * 2);
      }
    }
    Sleep(10);
  }

  for (auto* c : caps) {
    c->Close();
    delete c;
  }
  if (en) en->Release();
  CoUninitialize();
}

void Config(HWND) {}
void About(HWND) {
  MessageBoxW(g_mod.hMainWindow,
              L"Feeds Winamp visualizations from Windows audio (including Spotify).\n"
              L"Does not open Winamp's output, so there is no double playback.",
              L"Winamplify vis", MB_OK);
}
void Init() { InLog("in: loaded SAVSAInit=%p SAAdd=%p", g_mod.SAVSAInit, g_mod.SAAddPCMData); }
void Quit() {
  g_play = false;
  if (g_th.joinable()) g_th.join();
}

void GetFileInfo(const wchar_t* file, wchar_t* title, int* length_in_ms) {
  if (length_in_ms) *length_in_ms = g_len_ms;
  if (title) lstrcpynW(title, L"Spotify", 256);
  (void)file;
}

int InfoBox(const wchar_t*, HWND) { return 0; }

int IsOurFile(const wchar_t* fn) {
  int ok = fn && (wcsstr(fn, L"winamplify://") != nullptr || wcsstr(fn, L"wamplify://") != nullptr ||
                  wcsstr(fn, L"loosamp://") != nullptr || wcsstr(fn, L".lsv") != nullptr);
  if (ok && fn) {
    char a[260]{};
    WideCharToMultiByte(CP_ACP, 0, fn, -1, a, 260, nullptr, nullptr);
    InLog("in: IsOurFile %s", a);
  }
  return ok;
}

int Play(const wchar_t* fn) {
  char a[260]{};
  if (fn) WideCharToMultiByte(CP_ACP, 0, fn, -1, a, 260, nullptr, nullptr);
  if (g_play.load() && g_th.joinable()) {
    InLog("in: Play keep %s", a);
    g_paused = false;
    if (!g_mod.SAAddPCMData) VisInitUi(48000);
    return 0;
  }
  InLog("in: Play %s", a);
  g_play = false;
  if (g_th.joinable()) g_th.join();
  g_paused = false;
  VisInitUi(48000);
  g_play = true;
  g_start_tick = GetTickCount64();
  g_th = std::thread(PlayThread);
  return 0;
}

void Pause() { g_paused = true; }
void UnPause() { g_paused = false; }
int IsPaused() { return g_paused ? 1 : 0; }

void Stop() {
  g_play = false;
  if (g_th.joinable()) g_th.join();
  if (g_mod.SAVSADeInit) g_mod.SAVSADeInit();
}

int GetLength() { return g_len_ms; }
int GetOutputTime() {
  if (!g_play) return 0;
  return (int)(GetTickCount64() - g_start_tick);
}
void SetOutputTime(int) {}
void SetVolume(int) {}
void SetPan(int) {}
void EQSet(int, char[10], int) {}

}  // namespace

extern "C" __declspec(dllexport) In_Module* winampGetInModule2() {
  g_mod.version = IN_VER;
  g_mod.description = g_desc;
  g_mod.FileExtensions = g_ext;
  g_mod.is_seekable = 0;
  g_mod.UsesOutputPlug = 0;
  g_mod.Config = Config;
  g_mod.About = About;
  g_mod.Init = Init;
  g_mod.Quit = Quit;
  g_mod.GetFileInfo = GetFileInfo;
  g_mod.InfoBox = InfoBox;
  g_mod.IsOurFile = IsOurFile;
  g_mod.Play = Play;
  g_mod.Pause = Pause;
  g_mod.UnPause = UnPause;
  g_mod.IsPaused = IsPaused;
  g_mod.Stop = Stop;
  g_mod.GetLength = GetLength;
  g_mod.GetOutputTime = GetOutputTime;
  g_mod.SetOutputTime = SetOutputTime;
  g_mod.SetVolume = SetVolume;
  g_mod.SetPan = SetPan;
  g_mod.EQSet = EQSet;
  return &g_mod;
}

extern "C" __declspec(dllexport) void winamplify_vis_pump() { FeedNow(); }
