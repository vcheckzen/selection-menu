/*
 *  Selection Menu - settings.c
 *  Fully self painted settings window: everything is drawn with GDI so the
 *  look is identical on Windows XP and on Windows 11 (no reliance on
 *  theme/DWM APIs that only exist on newer systems).
 */
#include "app.h"
#include "theme.h"
#include "typography.h"
#include <commdlg.h>

#define SW_W 780
#define SW_H 620
#define TITLE_H 36
#define SIDEBAR_W 208
#define PAD_X 32
#define CONTROL_W 132
#define TRIGGER_MENU_ITEM_H 30
#define TRIGGER_MENU_H (TRIGGER_MENU_ITEM_H * 2 + 2)
#define TRIGGER_MENU_KEY RGB(1, 2, 3)

#define TAB_GENERAL 0
#define TAB_ACTIONS 1
#define TAB_ABOUT   2

#define HIT_TAB_GENERAL 100
#define HIT_TAB_ACTIONS 101
#define HIT_TAB_ABOUT   102
#define HIT_CLOSE       110

#define HIT_TOG_ENAB    200
#define HIT_TOG_FULL    204
#define HIT_TOG_DRAG    205
#define HIT_TRIGGER_COMBO 206
#define HIT_ICON_SMALL   230
#define HIT_ICON_LARGE   231
#define HIT_ADD          232
#define HIT_DELETE_0     250
#define HIT_CARD_0       340
#define HIT_OPACITY_DEC  240
#define HIT_OPACITY_INC  241

#define HIT_REC_0        300
#define HIT_CLR_0        310
#define HIT_REC_1        301
#define HIT_CLR_1        311
#define HIT_ICON_0       320
#define HIT_ICON_1       321

#define HIT_SAVE         400
#define HIT_CANCEL       401
#define HIT_RESET        402
#define HIT_OPENINI      410
#define HIT_TOG_TRACE    411

#define IDM_ICON_TRANSLATION 5000
#define IDM_ICON_SCREENSHOT  5001
#define IDM_ICON_CHOOSE_FILE 5002
#define IDM_TRIGGER_LEGACY   5010
#define IDM_TRIGGER_HOOK     5011

#define TOAST_TIMER          901
#define MAX_HITS 160

static const TCHAR *SETTINGS_CLASS = L"SelectionMenuSettingsWnd";
static const TCHAR *TRIGGER_MENU_CLASS = L"SelectionMenuTriggerMenuWnd";

static HWND  g_hwnd = NULL;
static HICON g_taskIcon = NULL;
static HICON g_taskIconSmall = NULL;
static int   g_tab = TAB_GENERAL;
static int   g_hover = 0;
static int   g_pressed = 0;
static int   g_recording = -1;
static int   g_iconMenuAction = -1;
static BOOL  g_visible = FALSE;
static RECT  g_hits[MAX_HITS];
static int   g_hitIds[MAX_HITS];
static int   g_hitCount = 0;
static BOOL  g_toastVisible = FALSE;
static BOOL  g_toastSuccess = FALSE;
static HWND  g_triggerMenuWnd = NULL;
static RECT  g_triggerComboRect;
static BOOL  g_triggerMenuVisible = FALSE;
static int   g_triggerMenuHover = -1;
static int   g_triggerMenuPressed = -1;

static void TriggerMenuHide(void);
static LRESULT CALLBACK TriggerMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ------------------------------------------------------------ hit list */

static void AddHit(int id, RECT r)
{
    if (g_hitCount >= MAX_HITS) return;
    g_hits[g_hitCount] = r;
    g_hitIds[g_hitCount] = id;
    g_hitCount++;
}

static int FindHit(POINT p)
{
    int i;
    for (i = g_hitCount - 1; i >= 0; i--) {
        if (RectHasPoint(g_hits[i], p)) return g_hitIds[i];
    }
    return 0;
}

/* ------------------------------------------------------------ controls */

static void DrawToggle(HDC hdc, RECT r, int on, int hover)
{
    RECT  t;
    int   w = 38, h = 20, kr, kx;
    COLORREF track, edge;

    t.left   = r.right - w;
    t.top    = (r.top + r.bottom) / 2 - h / 2;
    t.right  = t.left + w;
    t.bottom = t.top + h;

    if (on) {
        track = hover ? TH_ACCENT_HOVER : TH_ACCENT;
        edge  = CLR_NONE;
    } else {
        track = TH_TRACK_OFF;
        edge  = hover ? TH_INPUT_HOVER : CLR_NONE;
    }
    FillRoundRect(hdc, t, h / 2, track, edge);

    kr = 7;
    kx = on ? (t.right - 4 - kr * 2) : (t.left + 4);
    {
        RECT k;
        k.left = kx; k.top = t.top + (h - kr * 2) / 2;
        k.right = kx + kr * 2; k.bottom = k.top + kr * 2;
        FillRoundRect(hdc, k, kr, RGB(0xFF, 0xFF, 0xFF), CLR_NONE);
    }
}

static void DrawSegmented(HDC hdc, RECT r, const TCHAR **items, int count, int index, int hoverItem)
{
    int i;
    int w = (r.right - r.left) / count;
    int m = 3;

    FillRoundRect(hdc, r, 4, TH_INPUT_BG, TH_INPUT_BORDER);
    for (i = 0; i < count; i++) {
        RECT cell;
        RECT ir;
        cell.left = r.left + i * w;
        cell.right = r.left + (i + 1) * w;
        cell.top = r.top + m;
        cell.bottom = r.bottom - m + 1;
        ir = cell;
        if (i == 0) {
            ir.left += m;
            if (index != i) ir.right -= m;
        } else {
            ir.right -= m;
            if (index != i) ir.left += m;
        }
        if (i == index) {
            FillRoundRect(hdc, ir, 3, TH_ACCENT, CLR_NONE);
        } else if (i == hoverItem) {
            FillRoundRect(hdc, ir, 3, TH_INPUT_HOVER, CLR_NONE);
        }
        {
            RECT tr = cell;
            DrawLabel(hdc, TYPE_BODY,
                      (i == index) ? TH_TEXT_STRONG : TH_TEXT,
                      tr, items[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static void DrawStepper(HDC hdc, RECT r, const TCHAR *value, BOOL hoverDec, BOOL hoverInc)
{
    int   side = 26;
    RECT  dec, inc;
    HPEN  pen;

    FillRoundRect(hdc, r, 4, TH_INPUT_BG, TH_INPUT_BORDER);

    dec.left = r.left + 1;  dec.top = r.top + 1;
    dec.right = dec.left + side; dec.bottom = r.bottom - 1;
    inc.left = r.right - side - 1; inc.top = r.top + 1;
    inc.right = r.right - 1; inc.bottom = r.bottom - 1;

    if (hoverDec) FillRoundRect(hdc, dec, 4, TH_INPUT_HOVER, CLR_NONE);
    if (hoverInc) FillRoundRect(hdc, inc, 4, TH_INPUT_HOVER, CLR_NONE);

    pen = CreatePen(PS_SOLID, 1, TH_INPUT_BORDER);
    {
        HGDIOBJ op = SelectObject(hdc, pen);
        MoveToEx(hdc, dec.right, r.top + 6, NULL);
        LineTo(hdc, dec.right, r.bottom - 6);
        MoveToEx(hdc, inc.left, r.top + 6, NULL);
        LineTo(hdc, inc.left, r.bottom - 6);
        SelectObject(hdc, op);
    }
    DeleteObject(pen);

    DrawLabel(hdc, TYPE_BODY, hoverDec ? TH_TEXT_STRONG : TH_TEXT, dec,
              L"-", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(hdc, TYPE_BODY, hoverInc ? TH_TEXT_STRONG : TH_TEXT, inc,
              L"+", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    {
        RECT mid;
        mid.left = dec.right; mid.right = inc.left; mid.top = r.top; mid.bottom = r.bottom;
        DrawLabel(hdc, TYPE_BODY, TH_TEXT, mid, value, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void DrawCombo(HDC hdc, RECT r, const TCHAR *value, BOOL hover, BOOL pressed)
{
    COLORREF bg;
    COLORREF edge;
    HPEN pen;
    int cx;
    int cy;
    RECT textRect;

    bg = TH_INPUT_BG;
    edge = hover ? TH_ACCENT : TH_INPUT_BORDER;
    FillRoundRect(hdc, r, 4, bg, edge);

    textRect = r;
    textRect.left += 10;
    textRect.right -= 26;
    DrawLabel(hdc, TYPE_BODY, TH_TEXT, textRect,
              value, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    cx = r.right - 13;
    cy = (r.top + r.bottom) / 2;
    pen = CreatePen(PS_SOLID, 1, hover ? TH_TEXT_STRONG : TH_TEXT_DIM);
    {
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        MoveToEx(hdc, cx - 4, cy - 2, NULL);
        LineTo(hdc, cx, cy + 2);
        LineTo(hdc, cx + 4, cy - 2);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }
    DeleteObject(pen);
}

static void DrawButton(HDC hdc, RECT r, const TCHAR *text, int kind, int hover, int pressed)
{
    COLORREF bg, fg, border;

    if (kind == 1) {                       /* primary */
        bg = pressed ? RGB(0x1B, 0x5E, 0x9E) : (hover ? TH_ACCENT_HOVER : TH_ACCENT);
        fg = TH_TEXT_STRONG;
        border = CLR_NONE;
    } else if (kind == 2) {                /* ghost / text only */
        bg = CLR_NONE;
        fg = (kind == 2 && hover) ? TH_TEXT_STRONG : TH_TEXT;
        border = CLR_NONE;
    } else {                               /* secondary */
        bg = pressed ? RGB(0x2A, 0x2D, 0x2E) : (hover ? TH_BTN_HOVER : TH_INPUT_BG);
        fg = TH_TEXT;
        border = TH_INPUT_BORDER;
    }
    FillRoundRect(hdc, r, 4, bg, border);
    DrawLabel(hdc, TYPE_BODY_STRONG, fg, r, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}



static void FillSolid(HDC hdc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(hdc, r, b);
    DeleteObject(b);
}

static void DrawAppIcon(HDC hdc, int x, int y, int size)
{
    HICON h = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size,
                                LR_DEFAULTCOLOR);
    if (h != NULL) {
        DrawIconEx(hdc, x, y, h, size, size, 0, NULL, DI_NORMAL);
        DestroyIcon(h);
    }
}

static void DrawCenteredCross(HDC hdc, int cx, int cy, int half, COLORREF color)
{
    int i;

    SetPixel(hdc, cx, cy, color);
    for (i = 1; i <= half; i++) {
        SetPixel(hdc, cx - i, cy - i, color);
        SetPixel(hdc, cx + i, cy + i, color);
        SetPixel(hdc, cx + i, cy - i, color);
        SetPixel(hdc, cx - i, cy + i, color);
    }
}

static void PaintTitleBar(HDC hdc)
{
    RECT r;
    r.left = 0; r.top = 0; r.right = SW_W; r.bottom = TITLE_H;
    FillSolid(hdc, &r, TH_APP_BG);
    r.left = 0; r.top = TITLE_H - 1; r.right = SW_W; r.bottom = TITLE_H;
    FillSolid(hdc, &r, TH_BORDER);

    /* window buttons */
    {
        RECT cl;
        cl.left = SW_W - 36; cl.top = 0; cl.right = SW_W; cl.bottom = TITLE_H;
        AddHit(HIT_CLOSE, cl);

        if (g_hover == HIT_CLOSE)   FillSolid(hdc, &cl, TH_CLOSE_HOVER);

        {
            COLORREF gc = (g_hover == HIT_CLOSE) ? TH_TEXT_STRONG : TH_TEXT;
            int cx = (cl.left + cl.right) / 2;
            int cy = TITLE_H / 2;
            DrawCenteredCross(hdc, cx, cy, 5, gc);
        }
    }
}

static void PaintSidebar(HDC hdc)
{
    RECT r;
    int  i;
    static const TCHAR *tabs[3] = { L"常规", L"快捷键", L"关于" };
    static const int    ids[3]  = { HIT_TAB_GENERAL, HIT_TAB_ACTIONS, HIT_TAB_ABOUT };

    r.left = 0; r.top = 0; r.right = SIDEBAR_W; r.bottom = SW_H;
    FillSolid(hdc, &r, TH_SIDE_BG);

    DrawAppIcon(hdc, 18, TITLE_H + 10, 32);
    r.left = 62; r.top = TITLE_H + 12; r.right = SIDEBAR_W - 8; r.bottom = TITLE_H + 42;
    DrawLabel(hdc, TYPE_SUBTITLE, TH_TEXT_STRONG, r, APP_TITLE_CN, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (i = 0; i < 3; i++) {
        RECT nr;
        BOOL active = (g_tab == i);
        nr.left = 10; nr.right = SIDEBAR_W - 10;
        nr.top = TITLE_H + 58 + i * 36;
        nr.bottom = nr.top + 32;

        if (active) {
            FillRoundRect(hdc, nr, 5, TH_ITEM_ACTIVE, CLR_NONE);
            {
                RECT bar;
                bar.left = nr.left; bar.right = nr.left + 2;
                bar.top = nr.top + 6; bar.bottom = nr.bottom - 6;
                FillSolid(hdc, &bar, TH_ACCENT);
            }
        } else if (g_hover == ids[i]) {
            FillRoundRect(hdc, nr, 5, TH_ITEM_HOVER, CLR_NONE);
        }
        AddHit(ids[i], nr);
        {
            RECT tr = nr;
            tr.left = nr.left + 14;
            DrawLabel(hdc, UiFont(TYPE_BODY_PT10, active), active ? TH_TEXT_STRONG : TH_TEXT,
                      tr, tabs[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    r.left = 20; r.top = SW_H - 34; r.right = SIDEBAR_W - 16; r.bottom = SW_H - 14;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r, L"v" APP_VERSION,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void PaintHeader(HDC hdc, const TCHAR *title, const TCHAR *subtitle)
{
    RECT r;
    r.left = SIDEBAR_W + PAD_X; r.right = SW_W - PAD_X;
    r.top = TITLE_H + 20; r.bottom = TITLE_H + 50;
    DrawLabel(hdc, TYPE_TITLE, TH_TEXT_STRONG, r, title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    r.top = TITLE_H + 50; r.bottom = TITLE_H + 74;
    DrawLabel(hdc, TYPE_BODY, TH_TEXT_DIM, r, subtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void PaintRowText(HDC hdc, int y, int height, const TCHAR *label, const TCHAR *desc)
{
    RECT r;
    r.left = SIDEBAR_W + PAD_X;
    r.right = SIDEBAR_W + PAD_X + 400;
    r.top = y + 4;
    r.bottom = y + 26;
    DrawLabel(hdc, TYPE_BODY_STRONG, TH_TEXT, r, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (desc != NULL && desc[0] != 0) {
        r.top = y + 26;
        r.bottom = y + height - 2;
        DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r, desc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static RECT ControlRect(int y, int height, int w);

static void PaintTriggerRow(HDC hdc, int y, int rowHeight)
{
    const TCHAR *commonDesc = L"通用：Ctrl+A/C · Shift+方向键";
    const TCHAR *modeDesc =
        (g_cfg.triggerMode == TRIGGER_MODE_SELECTION_HOOK)
            ? L"精确模式：经校验的拖选、双击"
            : L"兼容模式：双击、三击、拖选";
    RECT r;
    RECT c;

    c = ControlRect(y, rowHeight, CONTROL_W);
    g_triggerComboRect = c;
    r.left = SIDEBAR_W + PAD_X;
    r.right = c.left - 16;
    r.top = y + 4;
    r.bottom = y + 26;
    DrawLabel(hdc, TYPE_BODY_STRONG, TH_TEXT, r, L"触发方式",
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    AddHit(HIT_TRIGGER_COMBO, c);
    DrawCombo(hdc, c,
              (g_cfg.triggerMode == TRIGGER_MODE_SELECTION_HOOK)
                  ? L"精确模式"
                  : L"兼容模式",
              g_hover == HIT_TRIGGER_COMBO && !g_triggerMenuVisible,
              g_pressed == HIT_TRIGGER_COMBO);

    r.left = SIDEBAR_W + PAD_X;
    r.right = c.left - 12;
    r.top = y + 26;
    r.bottom = y + 44;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r, commonDesc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    r.left = SIDEBAR_W + PAD_X;
    r.right = SW_W - PAD_X;
    r.top = y + 44;
    r.bottom = y + 62;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r, modeDesc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static RECT ControlRect(int y, int height, int w)
{
    RECT r;
    r.right = SW_W - PAD_X;
    r.left = r.right - w;
    r.top = y + (height - 28) / 2;
    r.bottom = r.top + 28;
    return r;
}

static void PaintGeneral(HDC hdc)
{
    static const int y0 = TITLE_H + 86;
    const int rowH = 46;
    const int triggerRowH = 64;
    int  y;
    RECT c;

    PaintHeader(hdc, L"常规", L"控制图标菜单的触发方式与外观");

    y = y0;
    PaintRowText(hdc, y, rowH, L"启用功能", L"选中文字后在鼠标旁弹出快捷图标");
    c = ControlRect(y, rowH, 38);
    AddHit(HIT_TOG_ENAB, c);
    DrawToggle(hdc, c, g_cfg.enabled, g_hover == HIT_TOG_ENAB);
    y += rowH;

    PaintTriggerRow(hdc, y, rowH);
    y += triggerRowH;

    if (g_cfg.triggerMode == TRIGGER_MODE_LEGACY) {
        PaintRowText(hdc, y, rowH, L"拖动选中后检测",
                     L"松手后确认存在文字选区才显示，浏览器同样有效");
        c = ControlRect(y, rowH, 38);
        AddHit(HIT_TOG_DRAG, c);
        DrawToggle(hdc, c, g_cfg.dragSelect, g_hover == HIT_TOG_DRAG);
    } else {
        PaintRowText(hdc, y, rowH, L"拖动选中后检测",
                     L"selection-hook 方案已固定包含经过校验的拖选");
        c = ControlRect(y, rowH, 92);
        DrawLabel(hdc, TYPE_BODY, TH_TEXT_DIM, c, L"自动启用",
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    y += rowH;

    PaintRowText(hdc, y, rowH, L"全屏程序中不显示", L"全屏游戏或演示时不打扰");
    c = ControlRect(y, rowH, 38);
    AddHit(HIT_TOG_FULL, c);
    DrawToggle(hdc, c, g_cfg.showInFullscreen, g_hover == HIT_TOG_FULL);
    y += rowH;

    /* icon size ------------------------------------------------------- */
    {
        static const TCHAR *items[2] = { L"小", L"中" };
        PaintRowText(hdc, y, rowH, L"图标大小", L"小图标更不遮挡正文");
        c = ControlRect(y, rowH, CONTROL_W);
        {
            int seg = (c.right - c.left) / 2;
            RECT a, b;
            a.left = c.left; a.right = c.left + seg; a.top = c.top; a.bottom = c.bottom;
            b.left = a.right; b.right = c.right; b.top = c.top; b.bottom = c.bottom;
            AddHit(HIT_ICON_SMALL, a);
            AddHit(HIT_ICON_LARGE, b);
            DrawSegmented(hdc, c, items, 2, g_cfg.iconSize,
                          (g_hover == HIT_ICON_SMALL) ? 0 : ((g_hover == HIT_ICON_LARGE) ? 1 : -1));
        }
    }
    y += rowH;

    /* opacity --------------------------------------------------------- */
    {
        TCHAR text[32];
        wsprintfW(text, L"%d%%", g_cfg.opacity);
        PaintRowText(hdc, y, rowH, L"不透明度", L"调低后菜单更低调");
        c = ControlRect(y, rowH, CONTROL_W);
        {
            RECT a, b;
            a.left = c.left; a.right = c.left + 26; a.top = c.top; a.bottom = c.bottom;
            b.left = c.right - 26; b.right = c.right; b.top = c.top; b.bottom = c.bottom;
            AddHit(HIT_OPACITY_DEC, a);
            AddHit(HIT_OPACITY_INC, b);
            DrawStepper(hdc, c, text, g_hover == HIT_OPACITY_DEC, g_hover == HIT_OPACITY_INC);
        }
    }
}

static void ChooseActionIconFile(int index)
{
    OPENFILENAMEW ofn;
    TCHAR file[MAX_PATH];
    TCHAR dest[MAX_PATH];
    TCHAR iconDir[MAX_PATH];
    TCHAR *slash;
    const TCHAR *name = file;
    BOOL copied;
    static const TCHAR filter[] =
        L"图标和图片 (*.ico;*.png;*.bmp;*.jpg;*.jpeg;*.gif)\0"
        L"*.ico;*.png;*.bmp;*.jpg;*.jpeg;*.gif\0"
        L"所有文件 (*.*)\0*.*\0\0";

    if (index < 0 || index >= g_cfg.actionCount) return;
    ZeroMemory(file, sizeof(file));
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择动作图标";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    GetExecutableDir(iconDir, MAX_PATH);
    if (iconDir[0] == 0) lstrcpynW(iconDir, L".", MAX_PATH);
    slash = file;
    while (*slash != 0) {
        if (*slash == L'\\' || *slash == L'/') name = slash + 1;
        slash++;
    }
    wsprintfW(dest, L"%s\\action-%d-%s", iconDir, index, name);
    copied = CopyFileW(file, dest, TRUE) != FALSE;
    if (lstrcmpiW(file, dest) == 0) copied = TRUE;

    ActionIconRelease(&g_cfg.actions[index]);
    lstrcpynW(g_cfg.actions[index].iconPath, copied ? dest : file, MAX_PATH);
    PopupRefresh();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void ShowActionIconMenu(int index)
{
    HMENU menu;
    POINT pt;
    Action *action;

    if (index < 0 || index >= g_cfg.actionCount) return;
    g_iconMenuAction = index;
    action = &g_cfg.actions[index];
    menu = CreatePopupMenu();
    if (menu == NULL) return;
    AppendMenuW(menu, MF_STRING |
                ((action->iconPath[0] == 0 && action->icon == ICON_TRANSLATION) ?
                 MF_CHECKED : 0u), IDM_ICON_TRANSLATION, L"翻译图标");
    AppendMenuW(menu, MF_STRING |
                ((action->iconPath[0] == 0 && action->icon == ICON_SCREENSHOT) ?
                 MF_CHECKED : 0u), IDM_ICON_SCREENSHOT, L"截图图标");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_ICON_CHOOSE_FILE, L"从硬盘选择图标...");
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_LEFTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                   pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void ApplyActionIconCommand(int command)
{
    Action *action;

    if (g_iconMenuAction < 0 || g_iconMenuAction >= g_cfg.actionCount) return;
    action = &g_cfg.actions[g_iconMenuAction];
    if (command == IDM_ICON_TRANSLATION || command == IDM_ICON_SCREENSHOT) {
        ActionIconRelease(action);
        action->iconPath[0] = 0;
        action->icon = (command == IDM_ICON_TRANSLATION) ?
                       ICON_TRANSLATION : ICON_SCREENSHOT;
        PopupRefresh();
        InvalidateRect(g_hwnd, NULL, FALSE);
    } else if (command == IDM_ICON_CHOOSE_FILE) {
        ChooseActionIconFile(g_iconMenuAction);
    }
    g_iconMenuAction = -1;
}

static BOOL ActionIsHovered(int index)
{
    return g_hover == HIT_CARD_0 + index ||
           g_hover == HIT_ICON_0 + index ||
           g_hover == HIT_REC_0 + index ||
           g_hover == HIT_CLR_0 + index ||
           g_hover == HIT_DELETE_0 + index;
}

static void DrawCardCloseGlyph(HDC hdc, RECT r, BOOL hover)
{
    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;
    const int lineLength = 11;
    const int half = (lineLength - 1) / 2;

    FillRoundRect(hdc, r, 9, hover ? TH_CLOSE_HOVER : TH_INPUT_HOVER, CLR_NONE);
    DrawCenteredCross(hdc, cx - 1, cy - 1, half, TH_TEXT_STRONG);
}

static void PaintActions(HDC hdc)
{
    int i;
    int rows;
    int hasAdd;
    const int gridLeft = SIDEBAR_W + PAD_X;
    const int gridRight = SW_W - PAD_X;
    const int gap = 10;
    const int cardW = (gridRight - gridLeft - gap) / 2;
    const int cardH = 74;
    const int gridTop = TITLE_H + 82;
    const int rowStride = 82;
    static const TCHAR *hint =
        L"点击图标更换图标，点“录制”设置快捷键；悬停卡片可删除，空白卡片用于添加。";
    RECT r;

    PaintHeader(hdc, L"快捷键", L"点击图标选择内置或硬盘图标，录制后发送组合键");

    for (i = 0; i < g_cfg.actionCount; i++) {
        int col = i % 2;
        int row = i / 2;
        RECT card;
        TCHAR combo[64];
        TCHAR name[32];
        RECT rec, clr;
        RECT iconHit;
        COLORREF iconBg;

        card.left = gridLeft + col * (cardW + gap);
        card.right = card.left + cardW;
        card.top = gridTop + row * rowStride;
        card.bottom = card.top + cardH;

        AddHit(HIT_CARD_0 + i, card);
        FillRoundRect(hdc, card, 8,
                      ActionIsHovered(i) ? TH_ITEM_ACTIVE : TH_PANEL,
                      (g_recording == i) ? TH_ACCENT : TH_INPUT_BORDER);

        iconHit.left = card.left + 6; iconHit.right = card.left + 52;
        iconHit.top = card.top + 8; iconHit.bottom = card.bottom - 8;
        AddHit(HIT_ICON_0 + i, iconHit);
        if (g_hover == HIT_ICON_0 + i) {
            FillRoundRect(hdc, iconHit, 7, TH_ITEM_HOVER, CLR_NONE);
        }
        iconBg = (g_hover == HIT_ICON_0 + i) ? TH_ITEM_HOVER :
                 (ActionIsHovered(i) ? TH_ITEM_ACTIVE : TH_PANEL);
        DrawActionIcon(hdc, &g_cfg.actions[i], card.left + 29, card.top + 37, 42,
                       ActionIsValid(&g_cfg.actions[i]) ? TH_TEXT : TH_TEXT_FAINT,
                       iconBg);

        wsprintfW(name, L"动作 %d", i + 1);
        r.left = card.left + 58;
        r.right = card.right - ((g_cfg.actionCount > 1) ? 30 : 8);
        r.top = card.top + 5; r.bottom = card.top + 24;
        DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_DIM, r, name,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        r.top = card.top + 25; r.bottom = card.top + 45;
        if (g_recording == i) {
            DrawLabel(hdc, TYPE_BODY_STRONG, TH_ACCENT, r, L"录制中...",
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            FormatCombo(&g_cfg.actions[i], combo, 64);
            if (!ActionIsValid(&g_cfg.actions[i])) {
                lstrcpynW(combo, L"未设置", 64);
            }
            DrawLabel(hdc, TYPE_BODY_STRONG,
                      ActionIsValid(&g_cfg.actions[i]) ? TH_TEXT_STRONG : TH_TEXT_FAINT,
                      r, combo, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        clr.left = card.right - 50;
        clr.right = card.right - 8;
        clr.top = card.top + 46; clr.bottom = card.top + 68;
        rec.left = card.right - 112;
        rec.right = card.right - 56;
        rec.top = clr.top; rec.bottom = clr.bottom;

        AddHit(HIT_REC_0 + i, rec);
        AddHit(HIT_CLR_0 + i, clr);
        if (g_recording == i) {
            FillRoundRect(hdc, rec, 4, TH_ACCENT, CLR_NONE);
            DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_STRONG, rec, L"取消",
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            DrawButton(hdc, rec, L"录制", 0,
                       g_hover == HIT_REC_0 + i, g_pressed == HIT_REC_0 + i);
        }
        DrawButton(hdc, clr, L"清除", 0, g_hover == HIT_CLR_0 + i, g_pressed == HIT_CLR_0 + i);

        if (g_cfg.actionCount > 1) {
            RECT del;
            del.left = card.right - 26;
            del.right = card.right - 6;
            del.top = card.top + 5;
            del.bottom = card.top + 25;
            AddHit(HIT_DELETE_0 + i, del);
            if (ActionIsHovered(i)) {
                DrawCardCloseGlyph(hdc, del, g_hover == HIT_DELETE_0 + i);
            }
        }
    }

    hasAdd = (g_cfg.actionCount < MAX_ACTIONS) ? 1 : 0;
    if (hasAdd) {
        int row = g_cfg.actionCount / 2;
        int col = g_cfg.actionCount % 2;
        RECT addCard;
        BOOL addHover;
        int cx, cy;
        HPEN pen;
        HGDIOBJ oldPen;

        addCard.left = gridLeft + col * (cardW + gap);
        addCard.right = addCard.left + cardW;
        addCard.top = gridTop + row * rowStride;
        addCard.bottom = addCard.top + cardH;
        addHover = (g_hover == HIT_ADD || g_pressed == HIT_ADD);
        AddHit(HIT_ADD, addCard);
        FillRoundRect(hdc, addCard, 8,
                      addHover ? TH_INPUT_HOVER : TH_INPUT_BG,
                      addHover ? TH_ACCENT : TH_INPUT_BORDER);

        cx = (addCard.left + addCard.right) / 2;
        cy = addCard.top + 27;
        pen = CreatePen(PS_SOLID, 2, addHover ? TH_TEXT_STRONG : TH_ACCENT);
        oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 8, cy, NULL);
        LineTo(hdc, cx + 8, cy);
        SetPixel(hdc, cx - 8, cy - 1, addHover ? TH_TEXT_STRONG : TH_ACCENT);
        SetPixel(hdc, cx - 8, cy, addHover ? TH_TEXT_STRONG : TH_ACCENT);
        SetPixel(hdc, cx + 8, cy - 1, addHover ? TH_TEXT_STRONG : TH_ACCENT);
        SetPixel(hdc, cx + 8, cy, addHover ? TH_TEXT_STRONG : TH_ACCENT);
        MoveToEx(hdc, cx, cy - 8, NULL);
        LineTo(hdc, cx, cy + 8);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        r.left = addCard.left + 8;
        r.right = addCard.right - 8;
        r.top = addCard.top + 43;
        r.bottom = addCard.top + 67;
        DrawLabel(hdc, TYPE_BODY_STRONG, addHover ? TH_TEXT_STRONG : TH_TEXT,
                  r, L"添加快捷键", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    rows = (g_cfg.actionCount + hasAdd + 1) / 2;
    r.left = gridLeft;
    r.right = gridRight;
    r.top = gridTop + rows * rowStride + 2;
    r.bottom = r.top + 20;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r, hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
static void PaintAbout(HDC hdc)
{
    RECT r;
    TCHAR text[256];
    RECT openBtn;
    int  cx = SIDEBAR_W + PAD_X;

    PaintHeader(hdc, L"关于", APP_TITLE);

    DrawAppIcon(hdc, cx, TITLE_H + 88, 72);
    r.left = cx + 96; r.right = SW_W - PAD_X;
    r.top = TITLE_H + 94; r.bottom = TITLE_H + 132;
    DrawLabel(hdc, TYPE_DISPLAY, TH_TEXT_STRONG, r, APP_TITLE_CN, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    r.top = TITLE_H + 134; r.bottom = TITLE_H + 156;
    wsprintfW(text, L"v%s  ·  选中文字后的一键快捷键菜单", APP_VERSION);
    DrawLabel(hdc, TYPE_BODY, TH_TEXT_DIM, r, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    r.left = cx; r.right = SW_W - PAD_X;
    r.top = TITLE_H + 176; r.bottom = TITLE_H + 232;
    DrawLabel(hdc, TYPE_BODY, TH_TEXT, r,
              L"选中文字后，在鼠标旁浮现最多 10 个小图标；点击即向当前窗口发送你设定的组合键。"
              L"常用于翻译、划词搜索、AI 处理等一键操作。",
              DT_LEFT | DT_TOP | DT_WORDBREAK);

    r.top = TITLE_H + 244; r.bottom = TITLE_H + 268;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_DIM, r, L"兼容性", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    r.left = cx + 110;
    DrawLabel(hdc, TYPE_BODY, TH_TEXT, r, L"Windows XP SP3 及以上，包含 Windows 11",
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    r.left = cx; r.top = TITLE_H + 276; r.bottom = TITLE_H + 300;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_DIM, r, L"配置文件", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    r.left = cx + 110; r.right = SW_W - PAD_X - 130;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT, r, ConfigIniPath(),
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    openBtn.left = SW_W - PAD_X - 110;
    openBtn.right = SW_W - PAD_X;
    openBtn.top = TITLE_H + 270;
    openBtn.bottom = openBtn.top + 32;
    AddHit(HIT_OPENINI, openBtn);
    DrawButton(hdc, openBtn, L"打开目录", 0, g_hover == HIT_OPENINI, g_pressed == HIT_OPENINI);

    r.left = cx; r.right = SW_W - PAD_X;
    r.top = TITLE_H + 344; r.bottom = TITLE_H + 368;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_DIM, r, L"提示", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    r.left = cx + 110;
    DrawLabel(hdc, TYPE_BODY, TH_TEXT, r,
              L"托盘图标：左键打开设置，右键打开菜单", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    /* troubleshooting switch: log why a trigger did or did not fire */
    r.left = cx; r.right = SW_W - PAD_X - 130;
    r.top = TITLE_H + 378; r.bottom = TITLE_H + 402;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_DIM, r, L"触发日志", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    r.left = cx + 110; r.right = SW_W - PAD_X - 130;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r, TracePath(),
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    {
        RECT tg;
        tg = ControlRect(TITLE_H + 378, 24, 38);
        AddHit(HIT_TOG_TRACE, tg);
        DrawToggle(hdc, tg, g_cfg.trace, g_hover == HIT_TOG_TRACE);
    }
    r.left = cx + 110; r.right = SW_W - PAD_X;
    r.top = TITLE_H + 404; r.bottom = TITLE_H + 424;
    DrawLabel(hdc, TYPE_CAPTION, TH_TEXT_FAINT, r,
              L"某个程序里不弹出时打开它，日志会写明卡在哪一步", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void PaintFooter(HDC hdc)
{
    RECT save, cancel, reset;
    int y = SW_H - 58;

    save.left = SW_W - PAD_X - 88; save.right = SW_W - PAD_X;
    cancel.left = save.left - 8 - 76; cancel.right = save.left - 8;
    reset.left = cancel.left - 8 - 96; reset.right = cancel.left - 8;
    save.top = y; save.bottom = y + 32;
    cancel.top = y; cancel.bottom = y + 32;
    reset.top = y; reset.bottom = y + 32;

    AddHit(HIT_SAVE, save);
    AddHit(HIT_CANCEL, cancel);
    AddHit(HIT_RESET, reset);
    DrawButton(hdc, reset, L"恢复默认", 0, g_hover == HIT_RESET, g_pressed == HIT_RESET);
    DrawButton(hdc, cancel, L"取消", 0, g_hover == HIT_CANCEL, g_pressed == HIT_CANCEL);
    DrawButton(hdc, save, L"保存", 1, g_hover == HIT_SAVE, g_pressed == HIT_SAVE);
}

static void DrawCheckMark(HDC hdc, int cx, int cy)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(0xFF, 0xFF, 0xFF));
    HGDIOBJ old = SelectObject(hdc, pen);

    MoveToEx(hdc, cx - 6, cy + 1, NULL);
    LineTo(hdc, cx - 2, cy + 5);
    LineTo(hdc, cx + 7, cy - 5);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void PaintToast(HDC hdc)
{
    RECT card;
    RECT icon;
    RECT text;
    COLORREF accent;
    const TCHAR *message;

    if (!g_toastVisible) return;

    card.left = SW_W - 190;
    card.right = SW_W - 20;
    card.top = TITLE_H + 12;
    card.bottom = TITLE_H + 52;
    accent = g_toastSuccess ? RGB(0x1F, 0xB9, 0x78) : RGB(0xEF, 0x53, 0x50);
    message = g_toastSuccess ? L"保存成功" : L"保存失败";

    {
        RECT shadow = card;
        OffsetRect(&shadow, 3, 3);
        FillSolid(hdc, &shadow, RGB(0x12, 0x14, 0x16));
    }
    FillRoundRect(hdc, card, 9, RGB(0x25, 0x29, 0x2E), RGB(0x3B, 0x41, 0x48));

    icon.left = card.left + 10;
    icon.right = icon.left + 24;
    icon.top = (card.top + card.bottom - 24) / 2;
    icon.bottom = icon.top + 24;
    FillRoundRect(hdc, icon, 12, accent, CLR_NONE);
    {
        int cx = (icon.left + icon.right) / 2;
        int cy = (icon.top + icon.bottom) / 2;
        if (g_toastSuccess) {
            DrawCheckMark(hdc, cx, cy);
        } else {
            DrawCenteredCross(hdc, cx, cy, 4, RGB(0xFF, 0xFF, 0xFF));
        }
    }

    text.left = card.left + 44;
    text.right = card.right - 14;
    text.top = card.top + 4;
    text.bottom = card.bottom - 4;
    DrawLabel(hdc, TYPE_BODY_STRONG, TH_TEXT_STRONG, text, message,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void ShowToast(BOOL success)
{
    g_toastSuccess = success;
    g_toastVisible = TRUE;
    SetTimer(g_hwnd, TOAST_TIMER, 1800, NULL);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC         hdc;
    HDC         mem;
    HBITMAP     bmp;
    HGDIOBJ     old;
    RECT        r;

    hdc = BeginPaint(hwnd, &ps);
    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, SW_W, SW_H);
    old = SelectObject(mem, bmp);

    g_hitCount = 0;

    SetBkMode(mem, TRANSPARENT);
    r.left = 0; r.top = 0; r.right = SW_W; r.bottom = SW_H;
    FillSolid(mem, &r, TH_CONTENT_BG);

    PaintTitleBar(mem);
    PaintSidebar(mem);
    switch (g_tab) {
    case TAB_ACTIONS: PaintActions(mem); break;
    case TAB_ABOUT:   PaintAbout(mem);   break;
    default:          PaintGeneral(mem); break;
    }
    PaintFooter(mem);
    PaintToast(mem);

    /* outer frame */
    {
        HPEN pen = CreatePen(PS_SOLID, 1, TH_INPUT_BORDER);
        HGDIOBJ op = SelectObject(mem, pen);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, 0, 0, SW_W, SW_H);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(pen);
    }

    BitBlt(hdc, 0, 0, SW_W, SW_H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void SettingsHide(void)
{
    g_recording = -1;
    g_toastVisible = FALSE;
    TriggerMenuHide();
    if (g_hwnd != NULL) KillTimer(g_hwnd, TOAST_TIMER);
    if (g_hwnd != NULL) {
        ShowWindow(g_hwnd, SW_HIDE);
    }
    g_visible = FALSE;
}

static void ResetActionAt(int index)
{
    if (index < 0 || index >= MAX_ACTIONS) return;
    ActionIconRelease(&g_cfg.actions[index]);
    ZeroMemory(&g_cfg.actions[index], sizeof(Action));
    g_cfg.actions[index].icon = (index % 2 == 0) ? ICON_TRANSLATION : ICON_SCREENSHOT;
}

static void AddAction(void)
{
    int index;

    if (g_cfg.actionCount >= MAX_ACTIONS) return;
    index = g_cfg.actionCount;
    ResetActionAt(index);
    g_cfg.actionCount++;
    g_recording = -1;
    PopupRecalcSize();
    PopupRefresh();
    TraySyncMenu();
}

static void DeleteAction(int index)
{
    int i;

    if (g_cfg.actionCount <= 1 || index < 0 || index >= g_cfg.actionCount) return;
    ActionIconRelease(&g_cfg.actions[index]);
    for (i = index + 1; i < g_cfg.actionCount; i++) {
        g_cfg.actions[i - 1] = g_cfg.actions[i];
    }
    g_cfg.actionCount--;
    ZeroMemory(&g_cfg.actions[g_cfg.actionCount], sizeof(Action));
    g_cfg.actions[g_cfg.actionCount].icon = ICON_TRANSLATION;
    if (g_recording == index) {
        g_recording = -1;
    } else if (g_recording > index) {
        g_recording--;
    }
    if (g_iconMenuAction == index) {
        g_iconMenuAction = -1;
    } else if (g_iconMenuAction > index) {
        g_iconMenuAction--;
    }
    PopupRecalcSize();
    PopupRefresh();
    TraySyncMenu();
}

static int TriggerMenuHit(POINT p)
{
    int index;

    if (p.x < 1 || p.x >= CONTROL_W - 1 ||
        p.y < 1 || p.y >= TRIGGER_MENU_H - 1) {
        return -1;
    }
    index = (p.y - 1) / TRIGGER_MENU_ITEM_H;
    return (index >= 0 && index < 2) ? index : -1;
}

static void TriggerMenuHide(void)
{
    if (g_triggerMenuWnd == NULL) return;
    g_triggerMenuVisible = FALSE;
    g_triggerMenuHover = -1;
    g_triggerMenuPressed = -1;
    if (GetCapture() == g_triggerMenuWnd) ReleaseCapture();
    ShowWindow(g_triggerMenuWnd, SW_HIDE);
}

static BOOL TriggerMenuEnsure(void)
{
    WNDCLASSEXW wc;

    if (g_triggerMenuWnd != NULL && IsWindow(g_triggerMenuWnd)) return TRUE;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = TriggerMenuProc;
    wc.hInstance     = g_inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = TRIGGER_MENU_CLASS;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return FALSE;
    }
    g_triggerMenuWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        TRIGGER_MENU_CLASS, L"", WS_POPUP,
        0, 0, CONTROL_W, TRIGGER_MENU_H, g_hwnd, NULL, g_inst, NULL);
    if (g_triggerMenuWnd == NULL) return FALSE;
    SetLayeredWindowAttributes(g_triggerMenuWnd, TRIGGER_MENU_KEY, 255, LWA_COLORKEY);
    return TRUE;
}

static void TriggerMenuShow(void)
{
    MONITORINFO mi;
    HMONITOR    mon;
    POINT       anchor;
    RECT        combo;
    int         x;
    int         y;

    if (g_hwnd == NULL || g_triggerComboRect.right <= g_triggerComboRect.left ||
        !TriggerMenuEnsure()) {
        return;
    }

    combo = g_triggerComboRect;
    MapWindowPoints(g_hwnd, NULL, (LPPOINT)&combo, 2);
    anchor.x = (combo.left + combo.right) / 2;
    anchor.y = (combo.top + combo.bottom) / 2;
    mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (mon == NULL || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }

    x = combo.left;
    y = combo.bottom;
    if (y + TRIGGER_MENU_H > mi.rcWork.bottom) y = combo.top - TRIGGER_MENU_H;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    if (x + CONTROL_W > mi.rcWork.right) x = mi.rcWork.right - CONTROL_W;
    if (x < mi.rcWork.left) x = mi.rcWork.left;

    g_triggerMenuHover = -1;
    g_triggerMenuPressed = -1;
    SetWindowPos(g_triggerMenuWnd, HWND_TOP, x, y, CONTROL_W, TRIGGER_MENU_H,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    g_triggerMenuVisible = TRUE;
    InvalidateRect(g_triggerMenuWnd, NULL, FALSE);
    SetForegroundWindow(g_triggerMenuWnd);
    SetCapture(g_triggerMenuWnd);
}

static void TriggerMenuSelect(int index)
{
    if (index == 0) {
        g_cfg.triggerMode = TRIGGER_MODE_LEGACY;
    } else if (index == 1) {
        g_cfg.triggerMode = TRIGGER_MODE_SELECTION_HOOK;
    } else {
        return;
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK TriggerMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client;
        RECT panel;
        int i;

        SetRect(&client, 0, 0, CONTROL_W, TRIGGER_MENU_H);
        FillSolid(hdc, &client, TRIGGER_MENU_KEY);
        SetRect(&panel, 0, 0, CONTROL_W - 1, TRIGGER_MENU_H - 1);
        FillRoundRect(hdc, panel, 4, TH_INPUT_BG, TH_INPUT_BORDER);
        for (i = 0; i < 2; i++) {
            RECT item;
            BOOL selected = ((i == 0 && g_cfg.triggerMode == TRIGGER_MODE_LEGACY) ||
                             (i == 1 && g_cfg.triggerMode == TRIGGER_MODE_SELECTION_HOOK));
            SetRect(&item, 1, 1 + i * TRIGGER_MENU_ITEM_H,
                    CONTROL_W - 2, 1 + (i + 1) * TRIGGER_MENU_ITEM_H);
            if (selected) {
                FillRoundRect(hdc, item, 3, TH_INPUT_BG, TH_ACCENT);
            } else if (i == g_triggerMenuHover) {
                FillRoundRect(hdc, item, 3, TH_INPUT_HOVER, CLR_NONE);
            }
            DrawLabel(hdc, TYPE_BODY, selected ? TH_TEXT_STRONG : TH_TEXT, item,
                      (i == 0) ? L"兼容模式" : L"精确模式",
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int hit = TriggerMenuHit(LParamToPoint(lp));
        if (hit != g_triggerMenuHover) {
            g_triggerMenuHover = hit;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        g_triggerMenuPressed = TriggerMenuHit(LParamToPoint(lp));
        if (g_triggerMenuPressed < 0) TriggerMenuHide();
        return 0;

    case WM_LBUTTONUP: {
        int hit = TriggerMenuHit(LParamToPoint(lp));
        if (hit >= 0 && hit == g_triggerMenuPressed) TriggerMenuSelect(hit);
        TriggerMenuHide();
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_DOWN || wp == VK_UP) {
            g_triggerMenuHover = (g_triggerMenuHover == 0) ? 1 : 0;
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == VK_RETURN) {
            TriggerMenuSelect(g_triggerMenuHover >= 0 ? g_triggerMenuHover :
                               (g_cfg.triggerMode == TRIGGER_MODE_LEGACY ? 0 : 1));
            TriggerMenuHide();
        } else if (wp == VK_ESCAPE) {
            TriggerMenuHide();
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g_triggerMenuVisible) TriggerMenuHide();
        return 0;

    case WM_CAPTURECHANGED:
        if (g_triggerMenuVisible) {
            g_triggerMenuVisible = FALSE;
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_CLOSE:
        TriggerMenuHide();
        return 0;

    case WM_DESTROY:
        g_triggerMenuWnd = NULL;
        g_triggerMenuVisible = FALSE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowTriggerModeMenu(void)
{
    TriggerMenuShow();
}

static void Act(int id)
{
    if (id >= HIT_ICON_0 && id < HIT_ICON_0 + MAX_ACTIONS) {
        ShowActionIconMenu(id - HIT_ICON_0);
        return;
    }
    if (id >= HIT_REC_0 && id < HIT_REC_0 + MAX_ACTIONS) {
        int idx = id - HIT_REC_0;
        g_recording = (g_recording == idx) ? -1 : idx;
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }
    if (id >= HIT_CLR_0 && id < HIT_CLR_0 + MAX_ACTIONS) {
        int idx = id - HIT_CLR_0;
        g_cfg.actions[idx].mods = 0;
        g_cfg.actions[idx].count = 0;
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    if (id == HIT_ADD) {
        AddAction();
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }
    if (id >= HIT_DELETE_0 && id < HIT_DELETE_0 + MAX_ACTIONS) {
        DeleteAction(id - HIT_DELETE_0);
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    switch (id) {
    case HIT_TAB_GENERAL: g_tab = TAB_GENERAL; g_recording = -1; break;
    case HIT_TAB_ACTIONS: g_tab = TAB_ACTIONS; g_recording = -1; break;
    case HIT_TAB_ABOUT:   g_tab = TAB_ABOUT;   g_recording = -1; break;

    case HIT_CLOSE:       SettingsHide(); return;

    case HIT_TRIGGER_COMBO:
        ShowTriggerModeMenu();
        return;
    case HIT_TOG_ENAB:
        g_cfg.enabled = !g_cfg.enabled;
        if (!g_cfg.enabled) PopupHide();
        break;
    case HIT_TOG_DRAG:   g_cfg.dragSelect = !g_cfg.dragSelect; break;
    case HIT_TOG_FULL:   g_cfg.showInFullscreen = !g_cfg.showInFullscreen; break;

    case HIT_ICON_SMALL: g_cfg.iconSize = 0; PopupRecalcSize(); PopupRefresh(); break;
    case HIT_ICON_LARGE: g_cfg.iconSize = 1; PopupRecalcSize(); PopupRefresh(); break;
    case HIT_OPACITY_DEC:
        g_cfg.opacity -= 10;
        if (g_cfg.opacity < 60) g_cfg.opacity = 60;
        PopupRefresh();
        break;
    case HIT_OPACITY_INC:
        g_cfg.opacity += 10;
        if (g_cfg.opacity > 100) g_cfg.opacity = 100;
        PopupRefresh();
        break;

    case HIT_SAVE:
        ShowToast(ConfigSave());
        PopupRecalcSize();
        PopupRefresh();
        TraySyncMenu();
        return;

    case HIT_CANCEL:
        ConfigLoad();
        PopupRecalcSize();
        PopupRefresh();
        TraySyncMenu();
        SettingsHide();
        return;

    case HIT_RESET:
        ConfigResetDefaults();
        PopupRecalcSize();
        PopupRefresh();
        break;

    case HIT_TOG_TRACE:
        g_cfg.trace = !g_cfg.trace;
        break;

    case HIT_OPENINI:
        {
            TCHAR dir[MAX_PATH];
            lstrcpynW(dir, ConfigIniPath(), MAX_PATH);
            {
                int i = lstrlenW(dir);
                while (i > 0 && dir[i - 1] != L'\\') i--;
                if (i > 0) dir[i - 1] = 0;
            }
            OpenPath(dir, NULL);
        }
        return;
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void OnKeyDown(WPARAM wp)
{
    int vk = (int)wp;

    if (g_recording >= 0) {
        if (vk == VK_ESCAPE) {
            g_recording = -1;
            InvalidateRect(g_hwnd, NULL, FALSE);
            return;
        }
        if (vk == VK_BACK || vk == VK_DELETE) {
            g_cfg.actions[g_recording].mods = 0;
            g_cfg.actions[g_recording].count = 0;
            g_recording = -1;
            InvalidateRect(g_hwnd, NULL, FALSE);
            return;
        }
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LWIN || vk == VK_RWIN) {
            return;
        }
        {
            Action *a = &g_cfg.actions[g_recording];
            a->mods = (int)CurrentModifiers();
            a->count = 1;
            a->vk[0] = (WORD)vk;
        }
        g_recording = -1;
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    if (vk == VK_ESCAPE) {
        SettingsHide();
    }
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST: {
        POINT p;
        int   hit;
        p = LParamToPoint(lp);
        ScreenToClient(hwnd, &p);
        hit = FindHit(p);
        if (hit == HIT_CLOSE) return HTCLIENT;
        if (p.y >= 0 && p.y < TITLE_H) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (g_triggerMenuVisible) TriggerMenuHide();
        g_pressed = FindHit(LParamToPoint(lp));
        SetCapture(hwnd);
        if (g_pressed != 0) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        int hit = FindHit(LParamToPoint(lp));
        int pressed = g_pressed;
        ReleaseCapture();
        g_pressed = 0;
        if (hit != 0 && hit == pressed) {
            Act(hit);
        } else {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        g_pressed = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_COMMAND:
        ApplyActionIconCommand((int)LOWORD(wp));
        return 0;

    case WM_MOUSEMOVE: {
        int hit = FindHit(LParamToPoint(lp));
        TRACKMOUSEEVENT tme;
        if (hit != g_hover) {
            g_hover = hit;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hover = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        OnKeyDown(wp);
        return 0;

    case WM_TIMER:
        if (wp == TOAST_TIMER) {
            KillTimer(hwnd, TOAST_TIMER);
            g_toastVisible = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_CLOSE:
        SettingsHide();
        return 0;

    case WM_DESTROY:
        if (g_hwnd != NULL) KillTimer(g_hwnd, TOAST_TIMER);
        g_toastVisible = FALSE;
        g_hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND SettingsCreate(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    if (g_hwnd != NULL) return g_hwnd;

    ZeroMemory(&wc, sizeof(wc));
    if (g_taskIcon == NULL) {
        g_taskIcon = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON),
                                GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    }
    if (g_taskIconSmall == NULL) {
        g_taskIconSmall = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXSMICON),
                                     GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    }
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = SettingsProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = g_taskIcon;
    wc.hIconSm       = g_taskIconSmall;
    wc.hbrBackground = NULL;
    wc.lpszClassName = SETTINGS_CLASS;
    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return NULL;
    }

    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, SETTINGS_CLASS, APP_TITLE, WS_POPUP,
                             CW_USEDEFAULT, CW_USEDEFAULT, SW_W, SW_H,
                             NULL, NULL, inst, NULL);
    return g_hwnd;
}

void SettingsShowTab(int tab)
{
    if (tab >= 0 && tab <= 2) g_tab = tab;
    SettingsShow();
}

void SettingsShow(void)
{
    MONITORINFO mi;
    HMONITOR   mon;
    POINT      cur;
    int        x, y;

    if (g_hwnd == NULL) return;
    g_recording = -1;

    GetCursorPos(&cur);
    mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (mon == NULL || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }
    x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - SW_W) / 2;
    y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - SW_H) / 2;

    SetWindowPos(g_hwnd, HWND_TOP, x, y, SW_W, SW_H, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    g_visible = TRUE;
    InvalidateRect(g_hwnd, NULL, FALSE);
}

void SettingsRefresh(void)
{
    if (g_hwnd != NULL && g_visible) {
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}
