#ifndef _WA_IPC_H_
#define _WA_IPC_H_

#include <windows.h>

#define WM_WA_IPC WM_USER

#define IPC_GETVERSION 0
#define IPC_PLAYFILE 100
#define IPC_STARTPLAY 102
#define IPC_ISPLAYING 104
#define IPC_GETOUTPUTTIME 105
#define IPC_JUMPTOTIME 106
#define IPC_SETPLAYLISTPOS 121
#define IPC_SETVOLUME 122
#define IPC_GETLISTLENGTH 124
#define IPC_GETLISTPOS 125
#define IPC_GETPLAYLISTFILE 211
#define IPC_GETPLAYLISTFILEW 214
#define IPC_GET_SHUFFLE 250
#define IPC_SET_SHUFFLE 252
#define IPC_GETWND 260
#define IPC_GETINIFILE 334
#define IPC_GETINIFILEW 1334
#define IPC_GETPLUGINDIRECTORY 336
#define IPC_GETPLUGINDIRECTORYW 1336
#define IPC_UPDTITLE 243
#define IPC_HOOK_TITLES 850
#define IPC_HOOK_TITLESW 851
#define IPC_CB_MISC 603
#define IPC_CB_MISC_TITLE 0
#define IPC_CB_MISC_STATUS 2
#define IPC_CB_MISC_INFO 4
#define IPC_GET_EXTENDED_FILE_INFO 290
#define IPC_GET_EXTENDED_FILE_INFOW 291
#define IPC_GET_EXTENDED_FILE_INFO_HOOKABLE 296
#define IPC_GET_EXTENDED_FILE_INFOW_HOOKABLE 297
#define IPC_PLAYING_FILE 3003
#define IPC_PLAYING_FILEW 13003
#define IPC_GET_PLAYING_TITLE 3034
#define IPC_PLAYFILEW 1300
#define IPC_CHANGECURRENTFILE 245
#define IPC_CHANGECURRENTFILEW 1245
#define IPC_FF_FIRST 2000
#define IPC_FF_ONCOLORTHEMECHANGED (IPC_FF_FIRST + 3)
#define IPC_SKIN_CHANGED 3018
#define WINAMP_VISPLUGIN 40192

typedef struct {
  char* filename;
  char* title;
  int length;
} enqueueFileWithMetaStruct;

typedef struct {
  const char* filename;
  char* title;
  int length;
  int force_useformatting;
} waHookTitleStruct;

typedef struct {
  const wchar_t* filename;
  wchar_t* title;
  int length;
  int force_useformatting;
} waHookTitleStructW;

typedef struct {
  const char* filename;
  const char* metadata;
  char* ret;
  size_t retlen;
} extendedFileInfoStruct;

typedef struct {
  const wchar_t* filename;
  const wchar_t* metadata;
  wchar_t* ret;
  size_t retlen;
} extendedFileInfoStructW;

#define WINAMP_BUTTON1 40044
#define WINAMP_BUTTON2 40045
#define WINAMP_BUTTON3 40046
#define WINAMP_BUTTON4 40047
#define WINAMP_BUTTON5 40048
#define WINAMP_BUTTON1_SHIFT 40144
#define WINAMP_BUTTON2_SHIFT 40145
#define WINAMP_BUTTON3_SHIFT 40146
#define WINAMP_BUTTON4_SHIFT 40147
#define WINAMP_BUTTON5_SHIFT 40148
#define WINAMP_BUTTON1_CTRL 40154
#define WINAMP_BUTTON2_CTRL 40155
#define WINAMP_BUTTON3_CTRL 40156
#define WINAMP_BUTTON4_CTRL 40157
#define WINAMP_BUTTON5_CTRL 40158
#define WINAMP_FILE_PLAY 40029
#define WINAMP_FILE_SHUFFLE 40023
#define WINAMP_FILE_LOC 40177
#define WINAMP_FILE_DIR 40187

#endif
