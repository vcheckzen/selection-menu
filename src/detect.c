/*
 *  Selection Menu - detect.c
 *
 *  Windows never hands out a "text was selected" notification, so we look at
 *  what the system does expose, in order of reliability:
 *
 *    1. WM_GETSEL on the focused control - the authoritative answer for edit
 *       controls and anything that forwards to them.
 *    2. The selection-hook pipeline, run asynchronously by uia.c: UI
 *       Automation first, MSAA second, and clipboard copy as the final
 *       compatibility fallback.
 *    3. One configured trigger scheme schedules a check. Legacy mode uses
 *       double/triple/drag/Ctrl+A/Ctrl+C; selection-hook mode uses its
 *       validated drag and double-click events.
 *
 *  Hook events only schedule these checks; the selection query runs on a worker
 *  thread. There is no polling loop anywhere in the application.
 */
#include "app.h"

/* squared drag distance, so the hook never has to call sqrt() */
#define MAX_ANCESTORS 8

/* Ask the focused control (and its parent, which is where many subclasses
   forward the message) whether it currently has a non-empty selection. */
static BOOL LooksLikeEditClass(HWND hwnd)
{
    TCHAR name[80];
    TCHAR prefix[20];

    if (hwnd == NULL || GetClassNameW(hwnd, name, 80) == 0) return FALSE;
    lstrcpynW(prefix, name, 20);
    return lstrcmpiW(name, L"Edit") == 0 ||
           lstrcmpiW(name, L"RichEdit") == 0 ||
           lstrcmpiW(name, L"RichEditD2DPT") == 0 ||
           lstrcmpiW(name, L"RichEditD2D") == 0 ||
           lstrcmpiW(name, L"RichEdit20A") == 0 ||
           lstrcmpiW(name, L"RichEdit20W") == 0 ||
           lstrcmpiW(name, L"RichEdit50A") == 0 ||
           lstrcmpiW(name, L"RichEdit50W") == 0 ||
           lstrcmpiW(name, L"TextBox") == 0 ||
           lstrcmpiW(prefix, L"WindowsForms10.EDIT") == 0;
}

static int QueryControlSelection(void)
{
    GUITHREADINFO gi;
    HWND          queried[6];
    HWND          fg;
    POINT         cursor;
    HWND          pointHwnd;
    int           count = 0;
    int           index;
    DWORD_PTR     result = 0;
    BOOL          edit = FALSE;

    fg = GetForegroundWindow();
    if (fg == NULL) return SELECT_UNKNOWN;

    ZeroMemory(&gi, sizeof(gi));
    gi.cbSize = sizeof(gi);
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gi))
        return SELECT_UNKNOWN;

    if (gi.hwndFocus != NULL) queried[count++] = gi.hwndFocus;
    if (gi.hwndCaret != NULL &&
        (count == 0 || gi.hwndCaret != queried[0])) {
        queried[count++] = gi.hwndCaret;
    }
    if (GetCursorPos(&cursor)) {
        pointHwnd = WindowFromPoint(cursor);
        if (pointHwnd != NULL) {
            int duplicate = FALSE;
            int existing;
            for (existing = 0; existing < count; existing++) {
                if (queried[existing] == pointHwnd) {
                    duplicate = TRUE;
                    break;
                }
            }
            if (!duplicate && count < (int)(sizeof(queried) / sizeof(queried[0]))) {
                queried[count++] = pointHwnd;
            }
        }
    }

    if (count > 0) {
        HWND ancestor = queried[0];
        int depth;
        for (depth = 0; depth < 4 && ancestor != NULL; depth++) {
            int duplicate = FALSE;
            int existing;
            for (existing = 0; existing < count; existing++) {
                if (queried[existing] == ancestor) {
                    duplicate = TRUE;
                    break;
                }
            }
            if (!duplicate && count < (int)(sizeof(queried) / sizeof(queried[0]))) {
                queried[count++] = ancestor;
            }
            ancestor = GetParent(ancestor);
        }
    }

    for (index = 0; index < count; index++) {
        LONG start = 0;
        LONG end = 0;
        result = 0;
        if (SendMessageTimeoutW(queried[index], WM_GETSEL,
                                (WPARAM)&start, (LPARAM)&end,
                                SMTO_ABORTIFHUNG | SMTO_NORMAL,
                                index == 0 ? 60 : 30, &result)) {
            if (end > start) return SELECT_SELECTION;
            if (LooksLikeEditClass(queried[index])) edit = TRUE;
        }
    }
    /* A read-only selectable control may answer WM_GETSEL without being an
       Edit/RichEdit class; an empty range is only definitive for edit classes. */
    return edit ? SELECT_NONE : SELECT_UNKNOWN;
}

/* ---------------------------------------------------------------- guards */

static void ClassOf(HWND hwnd, TCHAR *buf, int cch)
{
    buf[0] = 0;
    if (hwnd != NULL) GetClassNameW(hwnd, buf, cch);
}

/* The desktop, the taskbar and Explorer file lists are not text surfaces: a
   click there selects icons or opens a file, never a word. */
static BOOL OnShellSurface(HWND under)
{
    HWND  h = under;
    TCHAR name[64];
    int   depth = 0;

    if (under == NULL) return TRUE;
    while (h != NULL && depth < MAX_ANCESTORS) {
        ClassOf(h, name, 64);
        if (lstrcmpiW(name, L"Progman") == 0 ||
            lstrcmpiW(name, L"WorkerW") == 0 ||
            lstrcmpiW(name, L"Shell_TrayWnd") == 0 ||
            lstrcmpiW(name, L"Shell_SecondaryTrayWnd") == 0 ||
            lstrcmpiW(name, L"SHELLDLL_DefView") == 0 ||
            lstrcmpiW(name, L"CabinetWClass") == 0 ||
            lstrcmpiW(name, L"ExploreWClass") == 0 ||
            lstrcmpiW(name, L"ScrollBar") == 0) {
            return TRUE;
        }
        h = GetParent(h);
        depth++;
    }

    /* Chromium draws its toolbar in the top-level Chrome_WidgetWin_1 view,
       while page text lives in Chrome_RenderWidgetHostHWND.  Filter only the
       toolbar/title-bar surface so double-click word selection in the page
       continues to work. */
    h = (under != NULL) ? GetAncestor(under, GA_ROOT) : NULL;
    if (h != NULL && GetClassNameW(h, name, 64) != 0 &&
        lstrcmpiW(name, L"Chrome_WidgetWin_1") == 0) {
        TCHAR underClass[64];
        underClass[0] = 0;
        if (under != NULL) GetClassNameW(under, underClass, 64);
        if (lstrcmpiW(underClass, L"Chrome_RenderWidgetHostHWND") != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

int DetectSelection(POINT *pt, int *reason, int dragDist2, int clickFlags)
{
    HWND  fg;
    HWND  under;
    POINT cur;
    BOOL  dragGesture = dragDist2 >= DRAG_MIN_PX2;
    BOOL  acceptedGesture;
    int   native;
    TCHAR line[160];

    *reason = SELECT_NONE;

    fg = GetForegroundWindow();
    if (fg == NULL) {
        TraceLine(L"detect: no foreground window (screen locked / no active window?)");
        return SELECT_NONE;
    }
    if (fg == g_mainWnd) {
        TraceLine(L"detect: foreground is our own message window");
        return SELECT_NONE;
    }
    if (!g_cfg.showInFullscreen && IsFullScreenWindow(fg)) {
        TraceLine(L"detect: foreground looks fullscreen, skipped");
        return SELECT_NONE;
    }

    GetCursorPos(&cur);
    *pt = cur;

    if (g_cfg.triggerMode == TRIGGER_MODE_SELECTION_HOOK) {
        acceptedGesture = (clickFlags &
                           (CLICKF_DOUBLE | CLICKF_HOOKDRAG |
                            CLICKF_KEYSEL)) != 0;
        if (acceptedGesture && (clickFlags & CLICKF_HOOKDRAG) != 0 &&
            dragDist2 < HOOK_DRAG_MIN_PX2) {
            acceptedGesture = FALSE;
        }
    } else {
        acceptedGesture = dragGesture ||
                          (clickFlags & (CLICKF_DOUBLE | CLICKF_TRIPLE |
                                         CLICKF_KEYSEL)) != 0;
            if (acceptedGesture && dragGesture && !g_cfg.dragSelect &&
                (clickFlags & (CLICKF_DOUBLE | CLICKF_TRIPLE |
                               CLICKF_KEYSEL)) == 0) {
            acceptedGesture = FALSE;
        }
    }

    if (!acceptedGesture) {
        TraceLine(L"detect: plain click, no selection gesture, skipped");
        return SELECT_NONE;
    }

    under = WindowFromPoint(cur);
    if (OnShellSurface(under)) {
        TraceLine(L"detect: gesture is not on a text surface");
        return SELECT_NONE;
    }

    native = QueryControlSelection();
    if (native == SELECT_SELECTION) {
        wsprintfW(line, L"detect: WM_GETSEL reports a selection (fg=%lu)",
                  (DWORD)(ULONG_PTR)fg);
        TraceLine(line);
        *reason = SELECT_SELECTION;
        return SELECT_SELECTION;
    }
    if (native == SELECT_NONE &&
        (clickFlags & CLICKF_KEYSEL) == 0) {
        TraceLine(L"detect: native control reports no selection");
        return SELECT_NONE;
    }

    if (native == SELECT_NONE) {
        TraceLine(L"detect: native empty for selection gesture, scheduling UIA");
        *reason = SELECT_UNKNOWN;
        return SELECT_UNKNOWN;
    }

    TraceLine(L"detect: native result unknown, scheduling selection query");
    *reason = SELECT_UNKNOWN;
    return SELECT_UNKNOWN;
}
