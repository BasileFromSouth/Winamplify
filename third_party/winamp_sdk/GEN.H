#ifndef NULLSOFT_WINAMP_GEN_H
#define NULLSOFT_WINAMP_GEN_H

#include <windows.h>

#define GPPHDR_VER 0x10

typedef struct {
  int version;
  char *description;
  int (*init)();
  void (*config)();
  void (*quit)();
  HWND hwndParent;
  HINSTANCE hDllInstance;
} winampGeneralPurposePlugin;

typedef winampGeneralPurposePlugin *(*winampGeneralPurposePluginGetter)();

#endif
