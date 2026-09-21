#ifndef _IN2_H_
#define _IN2_H_
#include <windows.h>

#define IN_UNICODE 0x0F000000
#define IN_VER (IN_UNICODE | 0x100)

typedef struct In_Module {
  int version;
  char* description;
  HWND hMainWindow;
  HINSTANCE hDllInstance;
  char* FileExtensions;
  int is_seekable;
  int UsesOutputPlug;
  void (*Config)(HWND hwndParent);
  void (*About)(HWND hwndParent);
  void (*Init)();
  void (*Quit)();
  void (*GetFileInfo)(const wchar_t* file, wchar_t* title, int* length_in_ms);
  int (*InfoBox)(const wchar_t* file, HWND hwndParent);
  int (*IsOurFile)(const wchar_t* fn);
  int (*Play)(const wchar_t* fn);
  void (*Pause)();
  void (*UnPause)();
  int (*IsPaused)();
  void (*Stop)();
  int (*GetLength)();
  int (*GetOutputTime)();
  void (*SetOutputTime)(int time_in_ms);
  void (*SetVolume)(int volume);
  void (*SetPan)(int pan);
  void (*SAVSAInit)(int maxlatency_in_ms, int srate);
  void (*SAVSADeInit)();
  void (*SAAddPCMData)(void* PCMData, int nch, int bps, int timestamp);
  int (*SAGetMode)();
  int (*SAAdd)(void* data, int timestamp, int csa);
  void (*VSAAddPCMData)(void* PCMData, int nch, int bps, int timestamp);
  int (*VSAGetMode)(int* specNch, int* waveNch);
  int (*VSAAdd)(void* data, int timestamp);
  void (*VSASetInfo)(int srate, int nch);
  int (*dsp_isactive)();
  int (*dsp_dosamples)(short int* samples, int numsamples, int bps, int nch, int srate);
  void (*EQSet)(int on, char data[10], int preamp);
  void (*SetInfo)(int bitrate, int srate, int stereo, int synched);
  void* outMod;
} In_Module;

#endif
