/*
 *  Selection Menu - app.h
 *  Shared declarations.  Compiles as C89/C90, targets Windows XP .. Windows 11.
 */
#ifndef SELECTIONMENU_APP_H
#define SELECTIONMENU_APP_H

#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <windows.h>

#ifndef CLR_NONE
#define CLR_NONE ((COLORREF)0xFFFFFFFF)
#endif
#ifndef TRANSPARENT_BRUSH
#define TRANSPARENT_BRUSH 5
#endif
#ifndef NULL_BRUSH
#define NULL_BRUSH 0
#endif
#ifndef NULL_PEN
#define NULL_PEN 0
#endif
#ifndef WM_GETSEL
#define WM_GETSEL 0x00B0
#endif
#ifndef GUI_CARETVISIBLE
#define GUI_CARETVISIBLE 0x0001
#endif

#define APP_TITLE        L"Selection Menu"
#define APP_TITLE_CN     L"选择菜单"
#define APP_VERSION      L"1.0.0"

#define WM_APP_TRAY            (WM_APP + 10)
#define WM_APP_SELECTION_CHECK (WM_APP + 11)
#define WM_APP_FIRE_ACTION     (WM_APP + 12)
#define WM_APP_SHOW_SETTINGS   (WM_APP + 13)
#define WM_APP_HIDE_POPUP      (WM_APP + 14)
#define WM_APP_SHOW_ABOUT      (WM_APP + 15)
#define WM_APP_UIA_RESULT      (WM_APP + 16)

/* tray menu / command ids */
#define IDM_TRAY_SETTINGS 1001
#define IDM_TRAY_TOGGLE   1002
#define IDM_TRAY_STARTUP  1003
#define IDM_TRAY_ABOUT    1005
#define IDM_TRAY_EXIT     1006

#define IDM_ACTION_FIRST  2001
#define IDM_ACTION_0      2001
#define IDM_ACTION_1      2002

#define TIMER_FIRE        1
#define TIMER_KEYUP       2
#define TIMER_KEYSEL      3
#define TIMER_MOUSESEL    4
#define FIRE_DELAY_MS     60

/* modifier bits */
#define MODF_CTRL   0x01
#define MODF_ALT    0x02
#define MODF_SHIFT  0x04
#define MODF_WIN    0x08

#define MAX_KEYS_PER_COMBO 4
#define MAX_ACTIONS        10
#define ICON_TRANSLATION   0
#define ICON_SCREENSHOT    1
#define TRIGGER_MODE_LEGACY        0
#define TRIGGER_MODE_SELECTION_HOOK 1

typedef struct {
    int  mods;                 /* MOD_* */
    int  count;                /* number of non-modifier keys */
    WORD vk[MAX_KEYS_PER_COMBO];
    int  icon;
    TCHAR iconPath[MAX_PATH];
    HICON fileIcon;
    int  fileIconState;        /* 0 = not loaded, 1 = loaded, 2 = failed */
} Action;

typedef struct {
    int enabled;
    int triggerMode;         /* legacy gestures or selection-hook triggers */
    int dragSelect;            /* enable drag-selection detection             */
    int showInFullscreen;
    int startup;
    int trace;                /* write a trigger trace beside the executable */
    int iconSize;              /* 0 = small(34px) 1 = medium(44px) */
    int opacity;               /* 60..100 */
    int actionCount;           /* visible popup items: 1..10 */
    Action actions[MAX_ACTIONS];
} Config;

extern Config   g_cfg;
extern HINSTANCE g_inst;
extern HWND      g_mainWnd;

/* config.c */
void  ConfigLoad(void);
BOOL  ConfigSave(void);
void  ConfigResetDefaults(void);
BOOL  ConfigApplyStartup(int enable);
BOOL  ConfigIsStartup(void);
const TCHAR *ConfigIniPath(void);

/* util.c */
void  GetExecutableDir(TCHAR *out, int cch);
void  UiInit(void);
void  TraceLine(const TCHAR *line);
const TCHAR *TracePath(void);
HFONT UiFont(int pt10, int bold);
HFONT UiFontEx(int pt10, int bold, int italic, int antialiased);
const TCHAR *UiFontFace(void);
void  FillRoundRect(HDC hdc, RECT r, int radius, COLORREF fill, COLORREF border);
int   TextWidth(HDC hdc, HFONT font, const TCHAR *text);
void  DrawLabel(HDC hdc, HFONT font, COLORREF color, RECT r, const TCHAR *text, UINT flags);
BOOL  RectHasPoint(RECT r, POINT p);
POINT LParamToPoint(LPARAM lp);
int   InStrI(const TCHAR *haystack, const TCHAR *needle);
BOOL  IsFullScreenWindow(HWND hwnd);
void  OpenPath(const TCHAR *path, const TCHAR *param);
BOOL  EnsureDir(const TCHAR *path);

typedef struct {
    HBITMAP bmp;
    HBITMAP old;
    HDC     dc;
    int     w;
    int     h;
    DWORD  *px;
} Dib32;

BOOL Dib32Create(Dib32 *d, int w, int h);
void Dib32Destroy(Dib32 *d);
int  DistOutsideRoundRect(int x, int y, RECT r, int radius);
double DistOutsideRoundRectF(int x, int y, RECT r, int radius);

/* icons.c */
void DrawActionIcon(HDC hdc, Action *action, int cx, int cy, int size, COLORREF color, COLORREF bg);
void ActionIconRelease(Action *action);

/* hotkey.c */
void SendActionKeys(const Action *a);
void FormatCombo(const Action *a, TCHAR *buf, int cch);
UINT CurrentModifiers(void);
int  ActionIsValid(const Action *a);

/* detect.c */
#define SELECT_NONE       0
#define SELECT_SELECTION  1
#define SELECT_UNKNOWN    2
#define SELECT_DOUBLE     4

/* what the user just did - handed to DetectSelection */
#define CLICKF_NONE       0x00
#define CLICKF_DOUBLE     0x01  /* second click of a double click           */
#define CLICKF_TRIPLE     0x02  /* third click, selects the whole paragraph */
#define CLICKF_KEYSEL     0x04  /* legacy Ctrl+A / Ctrl+C selection trigger   */
#define CLICKF_HOOKDRAG   0x10  /* selection-hook validated drag            */
#define DRAG_MIN_PX2      (5 * 5)
#define HOOK_DRAG_MIN_PX2 (8 * 8)
#define HOOK_MAX_DRAG_MS  8000

int  DetectSelection(POINT *pt, int *reason, int dragDist2, int clickFlags);

typedef struct {
    HWND   target;
    POINT  point;
    UINT   token;
    int    state;
    int    source;
} SelectionResult;

#define SELSOURCE_NONE      0
#define SELSOURCE_UIA       1
#define SELSOURCE_MSAA      2
#define SELSOURCE_CLIPBOARD 3

/* uia.c */
BOOL UiaQuerySelectionAsync(HWND target, POINT point, UINT token);
BOOL SelectionCopyInProgress(void);

/* popup.c */
BOOL PopupCreate(HINSTANCE inst, HWND owner);
void PopupShowAt(POINT p);
void PopupHide(void);
BOOL PopupIsVisible(void);
BOOL PopupContainsPoint(POINT p);
void PopupRefresh(void);
void PopupRecalcSize(void);
BOOL PopupDumpBitmap(const TCHAR *path);

/* tray.c */
BOOL TrayCreate(HWND owner);
void TrayDestroy(void);
void TraySyncMenu(void);
void TrayShowMenu(void);
void TrayHandleMessage(WPARAM wp, LPARAM lp, UINT taskbarMsg);

/* settings.c */
HWND SettingsCreate(HINSTANCE inst);
void SettingsShow(void);
void SettingsShowTab(int tab);
void SettingsRefresh(void);

#endif /* SELECTIONMENU_APP_H */
