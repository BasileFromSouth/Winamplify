/* Subset of Nullsoft gen_ml/ml.h (zlib-style license, Nullsoft 2003).
   Altered: INT_PTR MessageProc for Winamp 5.x 32-bit. */

#ifndef _ML_H_
#define _ML_H_

#include <windows.h>

#define MLHDR_VER 0x15

typedef struct {
  int version;
  char *description;
  int (*init)();
  void (*quit)();
  INT_PTR (*MessageProc)(int message_type, INT_PTR param1, INT_PTR param2, INT_PTR param3);
  HWND hwndWinampParent;
  HWND hwndLibraryParent;
  HINSTANCE hDllInstance;
} winampMediaLibraryPlugin;

#define ML_MSG_TREE_ONCREATEVIEW 0x100
#define ML_MSG_TREE_ONCLICK 0x101
#define ML_ACTION_RCLICK 0
#define ML_ACTION_DBLCLICK 1
#define ML_ACTION_ENTER 2
#define ML_MSG_CONFIG 0x400

#define WM_ML_IPC (WM_USER + 0x1000)

#define ML_IPC_ADDTREEITEM 0x0101
#define ML_IPC_SETTREEITEM 0x0102
#define ML_IPC_DELTREEITEM 0x0103
#define ML_IPC_GETCURTREEITEM 0x0104
#define ML_IPC_SETCURTREEITEM 0x0105

#define ML_IPC_SKIN_LISTVIEW 0x0500
#define ML_IPC_UNSKIN_LISTVIEW 0x0501
#define ML_IPC_LISTVIEW_UPDATE 0x0502
#define ML_IPC_SKIN_WADLG_GETFUNC 0x0600

#define WADLG_ITEMBG 0
#define WADLG_ITEMFG 1
#define WADLG_WNDBG 2
#define WADLG_BUTTONFG 3
#define WADLG_WNDFG 4
#define WADLG_HILITE 5
#define WADLG_SELCOLOR 6
#define WADLG_LISTHEADER_BGCOLOR 7
#define WADLG_LISTHEADER_FONTCOLOR 8

typedef struct {
  int parent_id;
  char *title;
  int has_children;
  int this_id;
} mlAddTreeItemStruct;

#endif
