/*
 *  Selection Menu - main.c
 *  Hidden message window, low level hooks, tray glue and the message loop.
 */
#include "app.h"
#include <stdlib.h>

#define MAIN_CLASS L"SelectionMenuMainWnd"
#define MUTEX_NAME  L"Local\\SelectionMenu.SingleInstance"

static HHOOK  g_mouseHook = NULL;
static HHOOK  g_keyHook = NULL;
static UINT   g_taskbarMsg = 0;
static Action g_pending;
static HWND   g_settingsWnd = NULL;
static BOOL   g_quitting = FALSE;

static int   g_dragDist2 = 0;
static POINT g_downPt;
static HWND  g_downRoot = NULL;
static RECT  g_downRootRect;
static BOOL  g_haveDown = FALSE;
static DWORD g_lastDownTick = 0;
static POINT g_lastDownPt;
static int   g_clickCount = 0;
static DWORD g_lastFireTick = 0;
static int   g_keySelFlags = CLICKF_NONE;
static int   g_mouseSelFlags = CLICKF_NONE;
static int   g_mouseSelDragDist2 = 0;
static POINT g_mouseSelPt;
static DWORD g_mouseSelTick = 0;
static UINT  g_detectToken = 0;
static BOOL  g_detectPending = FALSE;

/* ------------------------------------------------------------------ */

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info)
{
    HANDLE file;
    DWORD written = 0;
    TCHAR dir[MAX_PATH];
    TCHAR path[MAX_PATH];
    TCHAR line[160];

    GetExecutableDir(dir, MAX_PATH);
    if (dir[0] == 0) return EXCEPTION_EXECUTE_HANDLER;
    wsprintfW(path, L"%s\\crash.log", dir);
    file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        wsprintfW(line, L"exception=0x%08lX address=0x%08lX\r\n",
                  (DWORD)(info != NULL && info->ExceptionRecord != NULL ?
                          info->ExceptionRecord->ExceptionCode : 0),
                  (DWORD)(ULONG_PTR)(info != NULL && info->ExceptionRecord != NULL ?
                          info->ExceptionRecord->ExceptionAddress : NULL));
        WriteFile(file, line, (DWORD)(lstrlenW(line) * sizeof(TCHAR)), &written, NULL);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static BOOL PointOnOurWindow(POINT pt)
{
    HWND h;
    DWORD pid = 0;

    h = WindowFromPoint(pt);
    if (h == NULL) return FALSE;
    h = GetAncestor(h, GA_ROOT);
    if (h == NULL) return FALSE;
    GetWindowThreadProcessId(h, &pid);
    return (pid == GetCurrentProcessId());
}

static void FireAction(int index)
{
    if (index < 0 || index >= g_cfg.actionCount) return;
    g_pending = g_cfg.actions[index];
    g_lastFireTick = GetTickCount();
    SetTimer(g_mainWnd, TIMER_FIRE, FIRE_DELAY_MS, NULL);
}

static void InvalidateDetection(void)
{
    KillTimer(g_mainWnd, TIMER_MOUSESEL);
    g_mouseSelFlags = CLICKF_NONE;
    g_mouseSelDragDist2 = 0;
    g_detectToken++;
    g_detectPending = FALSE;
}

static void DoCheckSelection(LPARAM lParam)
{
    POINT pt;
    int   reason = SELECT_NONE;
    int   state;
    int   flags = (int)lParam;
    HWND  target;

    if (!g_cfg.enabled) {
        TraceLine(L"check: disabled, ignoring");
        return;
    }
    GetCursorPos(&pt);
    if (PointOnOurWindow(pt)) {
        TraceLine(L"check: pointer is over one of our own windows");
        return;
    }
    g_detectToken++;
    g_detectPending = FALSE;
    target = GetForegroundWindow();
    state = DetectSelection(&pt, &reason, g_dragDist2, flags);
    if (state == SELECT_SELECTION) {
        TraceLine(L"detect: SHOW (reason=native)");
        PopupShowAt(pt);
        return;
    }
    if (state == SELECT_UNKNOWN) {
        g_detectPending = TRUE;
        if (!UiaQuerySelectionAsync(target, pt, g_detectToken)) {
            g_detectPending = FALSE;
            TraceLine(L"detect: selection worker could not be started");
        } else {
            TraceLine(L"detect: selection query scheduled");
        }
        return;
    }
    TraceLine(L"detect: no confirmed selection, not showing");
}

/* Keyboard and Shift+click selection are shared by both schemes. */
static int SelectionKeyFlags(int vk, int mods)
{
    if ((mods & MODF_CTRL) != 0 &&
        (mods & (MODF_SHIFT | MODF_ALT | MODF_WIN)) == 0 &&
        (vk == 'A' || vk == 'C')) {
        return CLICKF_KEYSEL;
    }
    if ((mods & MODF_SHIFT) != 0 &&
        (mods & (MODF_CTRL | MODF_ALT | MODF_WIN)) == 0) {
        switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_HOME: case VK_END:   case VK_PRIOR: case VK_NEXT:
            return CLICKF_KEYSEL;
        }
    }
    return CLICKF_NONE;
}

static int ClickFlagsFromCount(int count)
{
    if (g_cfg.triggerMode == TRIGGER_MODE_SELECTION_HOOK) {
        return (count >= 2) ? CLICKF_DOUBLE : CLICKF_NONE;
    }
    if (count >= 3) return CLICKF_DOUBLE | CLICKF_TRIPLE;
    if (count == 2) return CLICKF_DOUBLE;
    return CLICKF_NONE;
}

static HWND RootWindowAt(POINT p)
{
    HWND hwnd = WindowFromPoint(p);
    return (hwnd != NULL) ? GetAncestor(hwnd, GA_ROOT) : NULL;
}

static BOOL SameWindowAndRect(HWND hwnd, const RECT *rect)
{
    RECT current;

    if (hwnd == NULL || rect == NULL || !IsWindow(hwnd)) return FALSE;
    if (!GetWindowRect(hwnd, &current)) return FALSE;
    return current.left == rect->left && current.top == rect->top &&
           current.right == rect->right && current.bottom == rect->bottom;
}

static void HidePopupOnClick(POINT p, BOOL inside, BOOL preserveDetection)
{
    if (!preserveDetection) InvalidateDetection();
    if (PopupIsVisible() && !inside) {
        PopupHide();
    }
}

/* ---------------------------------------------------------------- hooks */

static LRESULT CALLBACK MouseHookProc(int code, WPARAM wp, LPARAM lp)
{
    MSLLHOOKSTRUCT *hs;

    if (code < 0) {
        return CallNextHookEx(g_mouseHook, code, wp, lp);
    }
    hs = (MSLLHOOKSTRUCT *)lp;

    if (wp == WM_LBUTTONDOWN) {
        POINT p;
        DWORD now;
        BOOL preserveDetection;

        p.x = hs->pt.x;
        p.y = hs->pt.y;
        /* Do not let an unrelated click cancel a Shift+click query that is
           already in flight.  The query is asynchronous, so cancelling it on
           the next down event made the menu appear only intermittently. */
        preserveDetection = g_detectPending ||
                            (g_mouseSelFlags != CLICKF_NONE &&
                             (DWORD)(GetTickCount() - g_mouseSelTick) < 1200);
        if (!preserveDetection) {
            KillTimer(g_mainWnd, TIMER_MOUSESEL);
            g_mouseSelFlags = CLICKF_NONE;
            g_mouseSelDragDist2 = 0;
        }
        HidePopupOnClick(p, PopupContainsPoint(p), preserveDetection);

        /* Windows calls a double click two *button down* events inside
           GetDoubleClickTime() and the double click rectangle.  Comparing the
           up events instead (as this used to) never matches: the second up
           arrives a whole click cycle after the first one. */
        now = GetTickCount();
        if (g_lastDownTick != 0 &&
            (LONG)(now - g_lastDownTick) <= (LONG)GetDoubleClickTime() &&
            abs(p.x - g_lastDownPt.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
            abs(p.y - g_lastDownPt.y) <= GetSystemMetrics(SM_CYDOUBLECLK)) {
            if (g_clickCount < 3) g_clickCount++;
        } else {
            g_clickCount = 1;
        }
        g_lastDownTick = now;
        g_lastDownPt   = p;
        g_downPt       = p;
        g_downRoot     = RootWindowAt(p);
        if (g_downRoot != NULL) GetWindowRect(g_downRoot, &g_downRootRect);
        g_haveDown     = TRUE;
        { TCHAR b[64];
          wsprintfW(b, L"hook: left button down, clicks=%d", g_clickCount);
          TraceLine(b); }
    } else if (wp == WM_LBUTTONUP && !g_quitting) {
        POINT p;
        int   flags;

        p.x = hs->pt.x;
        p.y = hs->pt.y;

        g_dragDist2 = 0;
        if (g_haveDown) {
            int dx = p.x - g_downPt.x;
            int dy = p.y - g_downPt.y;
            g_dragDist2 = dx * dx + dy * dy;
        }
        g_haveDown = FALSE;

        flags = ClickFlagsFromCount(g_clickCount);
        if (g_cfg.triggerMode == TRIGGER_MODE_SELECTION_HOOK) {
            DWORD elapsed = GetTickCount() - g_lastDownTick;

            if (g_dragDist2 >= HOOK_DRAG_MIN_PX2 &&
                elapsed <= HOOK_MAX_DRAG_MS &&
                RootWindowAt(p) == g_downRoot &&
                SameWindowAndRect(g_downRoot, &g_downRootRect)) {
                flags = CLICKF_HOOKDRAG;
            }
        }
        { TCHAR b[96];
          wsprintfW(b, L"hook: left button up, drag=%d px^2, flags=%d", g_dragDist2, flags);
          TraceLine(b); }
        if (flags != CLICKF_NONE ||
            g_mouseSelFlags == CLICKF_NONE) {
            g_mouseSelFlags = flags;
            g_mouseSelDragDist2 = g_dragDist2;
            g_mouseSelPt = p;
            g_mouseSelTick = GetTickCount();
            SetTimer(g_mainWnd, TIMER_MOUSESEL, 40, NULL);
        }
    } else if ((wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN ||
                wp == WM_MOUSEWHEEL) && !g_quitting) {
        POINT p;
        p.x = hs->pt.x;
        p.y = hs->pt.y;
        HidePopupOnClick(p, FALSE, FALSE);
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

static LRESULT CALLBACK KeyHookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code < 0) {
        return CallNextHookEx(g_keyHook, code, wp, lp);
    }
    if ((wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && !g_quitting) {
        KBDLLHOOKSTRUCT *ks = (KBDLLHOOKSTRUCT *)lp;
        /* our own SendInput burst must not re-trigger or dismiss the menu */
        if ((ks->flags & LLKHF_INJECTED) != 0 &&
            (DWORD)(GetTickCount() - g_lastFireTick) < 500) {
            return CallNextHookEx(g_keyHook, code, wp, lp);
        }
        if ((ks->flags & LLKHF_INJECTED) != 0 &&
            SelectionCopyInProgress()) {
            return CallNextHookEx(g_keyHook, code, wp, lp);
        }
        /* Modifier-only repeats must not cancel a pending mouse selection. */
        if (ks->vkCode == VK_SHIFT || ks->vkCode == VK_CONTROL ||
            ks->vkCode == VK_MENU || ks->vkCode == VK_LWIN ||
            ks->vkCode == VK_RWIN) {
            return CallNextHookEx(g_keyHook, code, wp, lp);
        }
        int flags = SelectionKeyFlags((int)ks->vkCode, CurrentModifiers());
        if (flags != CLICKF_NONE) {
            TCHAR line[96];

            /* Let the focused application process the selection key first;
               WM_GETSEL/Caret state is stale if checked inside the hook. */
            wsprintfW(line, L"hook: selection key flags=%d mode=%d",
                      flags, g_cfg.triggerMode);
            TraceLine(line);
            InvalidateDetection();
            g_dragDist2 = 0;
            g_keySelFlags = flags;
            SetTimer(g_mainWnd, TIMER_KEYSEL, 40, NULL);
        } else {
            KillTimer(g_mainWnd, TIMER_KEYSEL);
            g_keySelFlags = CLICKF_NONE;
            InvalidateDetection();
            PostMessageW(g_mainWnd, WM_APP_HIDE_POPUP, 0, 0);
        }
    }
    return CallNextHookEx(g_keyHook, code, wp, lp);
}

/* ---------------------------------------------------------------- window */

static void Shutdown(void)
{
    g_quitting = TRUE;
    if (g_mouseHook != NULL) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
    if (g_keyHook  != NULL) { UnhookWindowsHookEx(g_keyHook);  g_keyHook = NULL; }
    ConfigSave();
    TrayDestroy();
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_APP_TRAY:
        TrayHandleMessage(wp, lp, g_taskbarMsg);
        return 0;

    case WM_APP_SHOW_SETTINGS:
        PopupHide();
        if (g_settingsWnd == NULL) {
            g_settingsWnd = SettingsCreate(g_inst);
        }
        SettingsShow();
        return 0;

    case WM_APP_SHOW_ABOUT:
        PopupHide();
        if (g_settingsWnd == NULL) {
            g_settingsWnd = SettingsCreate(g_inst);
        }
        SettingsShowTab(2);
        return 0;

    case WM_APP_SELECTION_CHECK:
        DoCheckSelection(lp);
        return 0;

    case WM_APP_UIA_RESULT: {
        SelectionResult *result = (SelectionResult *)lp;
        if (result == NULL) return 0;
        if (result->token != g_detectToken || !g_detectPending) {
            HeapFree(GetProcessHeap(), 0, result);
            return 0;
        }
        g_detectPending = FALSE;
        if (result->state == SELECT_SELECTION &&
            IsWindow(result->target) &&
            GetForegroundWindow() == result->target) {
            TCHAR line[96];
            const TCHAR *source = L"unknown";
            if (result->source == SELSOURCE_UIA) source = L"UIA";
            else if (result->source == SELSOURCE_MSAA) source = L"MSAA";
            else if (result->source == SELSOURCE_CLIPBOARD) source = L"clipboard";
            wsprintfW(line, L"detect: %s confirms selection, SHOW", source);
            TraceLine(line);
            PopupShowAt(result->point);
        } else if (result->state == SELECT_NONE) {
            TCHAR line[96];
            const TCHAR *source = L"unknown";
            if (result->source == SELSOURCE_UIA) source = L"UIA";
            else if (result->source == SELSOURCE_MSAA) source = L"MSAA";
            else if (result->source == SELSOURCE_CLIPBOARD) source = L"clipboard";
            wsprintfW(line, L"detect: %s reports no selection, not showing", source);
            TraceLine(line);
        } else {
            TraceLine(L"detect: selection query unavailable or timed out, not showing");
        }
        HeapFree(GetProcessHeap(), 0, result);
        return 0;
    }

    case WM_APP_HIDE_POPUP:
        PopupHide();
        return 0;

    case WM_APP_FIRE_ACTION:
        FireAction((int)wp);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_FIRE) {
            KillTimer(hwnd, TIMER_FIRE);
            SendActionKeys(&g_pending);
        } else if (wp == TIMER_KEYSEL) {
            KillTimer(hwnd, TIMER_KEYSEL);
            DoCheckSelection((LPARAM)g_keySelFlags);
            g_keySelFlags = CLICKF_NONE;
        } else if (wp == TIMER_MOUSESEL) {
            int flags = g_mouseSelFlags;
            int dragDist2 = g_mouseSelDragDist2;

            KillTimer(hwnd, TIMER_MOUSESEL);
            g_mouseSelFlags = CLICKF_NONE;
            g_mouseSelDragDist2 = 0;
            g_dragDist2 = dragDist2;
            DoCheckSelection((LPARAM)flags);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TRAY_SETTINGS:
            PostMessageW(hwnd, WM_APP_SHOW_SETTINGS, 0, 0);
            return 0;
        case IDM_TRAY_TOGGLE:
            g_cfg.enabled = !g_cfg.enabled;
            ConfigSave();
            TraySyncMenu();
            SettingsRefresh();
            PopupHide();
            return 0;
        case IDM_TRAY_STARTUP:
            g_cfg.startup = !g_cfg.startup;
            ConfigSave();
            TraySyncMenu();
            return 0;
        case IDM_TRAY_ABOUT:
            PostMessageW(hwnd, WM_APP_SHOW_ABOUT, 0, 0);
            return 0;
        case IDM_TRAY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------- entry */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nShow)
{
    WNDCLASSEXW wc;
    HANDLE  mutex;
    HWND    hwnd;
    MSG     msg;
    DWORD   oldError;

    (void)hPrev; (void)nShow;

    g_inst = hInstance;
    SetUnhandledExceptionFilter(CrashFilter);
    mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    oldError = GetLastError();
    if (mutex != NULL && oldError == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(MAIN_CLASS, NULL);
        if (other != NULL) {
            PostMessageW(other, WM_APP_SHOW_SETTINGS, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    UiInit();
    ConfigLoad();
    TraceLine(L"app: startup");

    /* diagnostic: render the palette once and exit (no mouse needed) */
    {
        TCHAR early[128];
        lstrcpynW(early, (lpCmdLine != NULL) ? lpCmdLine : L"", 128);
        if (InStrI(early, L"--dump-popup") > 0) {
            TCHAR path[MAX_PATH];
            if (GetModuleFileNameW(g_inst, path, MAX_PATH) != 0) {
                int i = lstrlenW(path);
                while (i > 0 && path[i - 1] != L'\\') i--;
                lstrcpynW(path + i, L"popup-preview.bmp", 32);
                if (PopupDumpBitmap(path)) {
                    MessageBoxW(NULL, path, APP_TITLE, MB_OK | MB_ICONINFORMATION);
                }
            }
            if (mutex != NULL) CloseHandle(mutex);
            return 0;
        }
    }
    if (!g_cfg.startup) g_cfg.startup = ConfigIsStartup();

    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize      = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance   = hInstance;
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, MAIN_CLASS, APP_TITLE, WS_OVERLAPPED,
                           0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (hwnd == NULL) {
        if (mutex != NULL) CloseHandle(mutex);
        return 1;
    }
    g_mainWnd = hwnd;

    if (!PopupCreate(hInstance, hwnd)) {
        MessageBoxW(NULL, L"无法创建弹出菜单窗口。", APP_TITLE, MB_ICONERROR | MB_OK);
    }
    TrayCreate(hwnd);

    /* Normal launches open the main settings UI; --demo keeps a diagnostic path. */
    {
        TCHAR cmd[128];
        lstrcpynW(cmd, (lpCmdLine != NULL) ? lpCmdLine : L"", 128);
        if (InStrI(cmd, L"--demo") > 0) {
            POINT p;
            p.x = GetSystemMetrics(SM_CXSCREEN) / 2;
            p.y = GetSystemMetrics(SM_CYSCREEN) / 2;
            PopupShowAt(p);
        } else {
            PostMessageW(hwnd, WM_APP_SHOW_SETTINGS, 0, 0);
        }
    }

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, hInstance, 0);
    g_keyHook  = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHookProc, hInstance, 0);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex != NULL) CloseHandle(mutex);
    return (int)msg.wParam;
}
