/*
 *  Selection Menu - tray.c
 *  Notification area icon and its context menu.
 */
#include "app.h"

#define TRAY_ICON_ID 1

static NOTIFYICONDATAW g_nid;
static HWND  g_hwnd = NULL;
static UINT  g_taskbarMsg = 0;
static BOOL  g_added = FALSE;
static HICON g_icon = NULL;
static BOOL  g_iconOwned = FALSE;

static void BuildTip(void)
{
    TCHAR combo[64];
    TCHAR tip[192];

    if (!g_cfg.enabled) {
        lstrcpynW(tip, APP_TITLE L"  -  已禁用", 192);
    } else {
        FormatCombo(&g_cfg.actions[0], combo, 64);
        wsprintfW(tip, APP_TITLE L"  -  %s", combo);
    }
    lstrcpynW(g_nid.szTip, tip, 128);
}

BOOL TrayCreate(HWND owner)
{
    int iconSize;

    g_hwnd = owner;
    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = owner;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    if (g_icon != NULL && g_iconOwned) DestroyIcon(g_icon);
    g_icon = NULL;
    g_iconOwned = FALSE;
    iconSize = GetSystemMetrics(SM_CXICON);
    g_nid.hIcon = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                             iconSize, iconSize, LR_DEFAULTCOLOR);
    if (g_nid.hIcon == NULL) {
        g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    } else {
        g_icon = g_nid.hIcon;
        g_iconOwned = TRUE;
    }
    BuildTip();
    g_added = (Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE);
    return g_added;
}

void TrayDestroy(void)
{
    if (g_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_added = FALSE;
    }
    if (g_icon != NULL && g_iconOwned) DestroyIcon(g_icon);
    g_icon = NULL;
    g_iconOwned = FALSE;
    g_nid.hIcon = NULL;
}

void TraySyncMenu(void)
{
    if (!g_added) return;
    BuildTip();
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void TrayShowMenu(void)
{
    HMENU   h;
    POINT   pt;

    if (g_hwnd == NULL) return;
    h = CreatePopupMenu();
    if (h == NULL) return;

    AppendMenuW(h, MF_STRING, IDM_TRAY_SETTINGS, L"设置");
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING | (g_cfg.enabled ? MF_CHECKED : 0u), IDM_TRAY_TOGGLE, L"启用功能");
    AppendMenuW(h, MF_STRING | (g_cfg.startup ? MF_CHECKED : 0u), IDM_TRAY_STARTUP, L"开机自动启动");
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING, IDM_TRAY_ABOUT, L"关于");
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING, IDM_TRAY_EXIT, L"退出");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(h, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                   pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(h);
}

void TrayHandleMessage(WPARAM wp, LPARAM lp, UINT taskbarMsg)
{
    if (taskbarMsg != 0 && lp == (LPARAM)taskbarMsg) {
        g_added = FALSE;
        TrayCreate(g_hwnd);
        return;
    }
    if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
        PostMessageW(g_hwnd, WM_APP_SHOW_SETTINGS, 0, 0);
        return;
    }
    if (lp == WM_RBUTTONUP) {
        TrayShowMenu();
        return;
    }
    (void)wp;
}
