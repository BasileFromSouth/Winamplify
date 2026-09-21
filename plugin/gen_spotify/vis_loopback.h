#pragma once
#include <windows.h>

void VisInit(HWND winamp);
void VisBindProc(WNDPROC orig);
void VisSetActive(bool active);
void VisShutdown();
bool VisIsActive();
bool VisIsOurFileA(const char* f);
bool VisIsOurFileW(const wchar_t* f);
bool VisIsStarting();
// Point Winamp's "current file" at a new vis.lsv (per-track folder) so Album Art reloads.
void VisRetargetFile(const wchar_t* vis_lsv_path);