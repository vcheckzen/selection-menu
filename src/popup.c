/*
 *  Selection Menu - popup.c
 *  The action palette (1..10 items).  Implemented as a layered (per pixel alpha) topmost
 *  window so rounded corners and the drop shadow look the same on XP and on
 *  Windows 11.  UpdateLayeredWindow has been available since Windows 2000.
 */
#include "app.h"
#include "theme.h"

#define SHADOW_MARGIN      7
#define SHADOW_SIDE_ALPHA  8
#define SHADOW_BOTTOM_ALPHA 64
#define SHADOW_SIDE_RANGE  4

/* macOS-style floating palette: a quiet near-white surface, a hairline
   border, and a system-blue hover state with white glyphs. */
#define MACOS_BG         RGB(0xF5, 0xF5, 0xF7)
#define MACOS_BORDER     RGB(0xD1, 0xD1, 0xD6)
#define MACOS_FG         RGB(0x3A, 0x3A, 0x3C)
#define MACOS_INVALID    RGB(0x8E, 0x8E, 0x93)
#define MACOS_HOVER_BG   RGB(0xE5, 0xE5, 0xE8)
#define MACOS_HOVER_FG   MACOS_FG
#define MACOS_SHADOW     RGB(0x60, 0x60, 0x66)

static HWND   g_hwnd = NULL;
static HWND   g_owner = NULL;
static int    g_btn = 34;
static int    g_pad = 6;
static int    g_vpad = 1;
static int    g_gap = 4;
static int    g_radius = 8;
static int    g_w = 0;
static int    g_h = 0;
static int    g_hover = -1;
static BOOL   g_visible = FALSE;
static BOOL   g_tracking = FALSE;
static RECT   g_btnRect[MAX_ACTIONS];

static const TCHAR *POPUP_CLASS = L"SelectionMenuPopupWnd";

static void ComputeLayout(void)
{
    int i;

    g_btn    = (g_cfg.iconSize == 1) ? 44 : 34;
    g_pad    = (g_cfg.iconSize == 1) ? 7 : 6;
    g_vpad   = (g_cfg.iconSize == 1) ? 2 : 1;
    g_gap    = 4;
    g_radius = (g_cfg.iconSize == 1) ? 12 : 10;
    g_w = SHADOW_MARGIN * 2 + g_pad * 2 + g_btn * g_cfg.actionCount +
          g_gap * (g_cfg.actionCount - 1);
    g_h = SHADOW_MARGIN * 2 + g_vpad * 2 + g_btn;

    for (i = 0; i < g_cfg.actionCount; i++) {
        int x = SHADOW_MARGIN + g_pad + i * (g_btn + g_gap);
        SetRect(&g_btnRect[i], x, SHADOW_MARGIN + g_vpad,
                x + g_btn, SHADOW_MARGIN + g_vpad + g_btn);
    }
}

static int HitTest(POINT p)
{
    int i;
    for (i = 0; i < g_cfg.actionCount; i++) {
        if (RectHasPoint(g_btnRect[i], p)) return i;
    }
    return -1;
}

/* Turn the 32bpp buffer into a premultiplied alpha image: fully opaque inside
   the rounded panel, a soft black shadow below it, transparent elsewhere. */
static void PushBitmap(Dib32 *d, RECT panel)
{
    int x, y;
    int opacity = g_cfg.opacity;

    if (opacity < 20) opacity = 20;
    if (opacity > 100) opacity = 100;

    for (y = 0; y < d->h; y++) {
        for (x = 0; x < d->w; x++) {
            double dist   = DistOutsideRoundRectF(x, y, panel, g_radius);
            double panelA = 0.0;
            double shadowA = 0.0;
            double total;
            DWORD  source = d->px[y * d->w + x];
            DWORD  p = source;
            DWORD  panelColor;
            DWORD  rgb;
            int    ca, aa;
            double shadowWeight;

            /* pixel coverage: 1 fully inside, 0 fully outside, ramp in between */
            panelA = 0.5 - dist;
            if (panelA > 1.0) panelA = 1.0;
            if (panelA < 0.0) panelA = 0.0;
            if (panelA > 0.0 && source == 0) {
                p = (DWORD)MACOS_BG;
            }
            if (dist >= 0.0) {
                double range = (double)SHADOW_SIDE_RANGE;
                double alpha = (double)SHADOW_SIDE_ALPHA;

                if (x < panel.left || x >= panel.right) {
                    /* Side shadows follow the rounded corners via dist. */
                    range = (double)SHADOW_SIDE_RANGE;
                    alpha = (double)SHADOW_SIDE_ALPHA;
                } else if (y >= panel.bottom) {
                    range = (double)SHADOW_MARGIN;
                    alpha = (double)SHADOW_BOTTOM_ALPHA;
                }
                if (dist < range) {
                    double fade = 1.0 - dist / range;
                    shadowA = (alpha / 255.0) * fade * fade;
                }
            }
            panelA  = panelA  * (double)opacity / 100.0;
            shadowA = shadowA * (double)opacity / 100.0;

            total = panelA + shadowA * (1.0 - panelA);
            aa = (int)(total * 255.0 + 0.5);
            ca = (int)(panelA * 255.0 + 0.5);
            panelColor = (source != 0) ? source : (DWORD)MACOS_BG;
            shadowWeight = shadowA * (1.0 - panelA);
            if (aa <= 0) {
                d->px[y * d->w + x] = 0;
            } else if (aa >= 255 && ca >= 255) {
                d->px[y * d->w + x] = 0xFF000000u | (p & 0x00FFFFFFu);
            } else {
                int rr = (int)(GetRValue(panelColor) * panelA +
                               GetRValue(MACOS_SHADOW) * shadowWeight + 0.5);
                int gg = (int)(GetGValue(panelColor) * panelA +
                               GetGValue(MACOS_SHADOW) * shadowWeight + 0.5);
                int bb = (int)(GetBValue(panelColor) * panelA +
                               GetBValue(MACOS_SHADOW) * shadowWeight + 0.5);
                if (rr > 255) rr = 255;
                if (gg > 255) gg = 255;
                if (bb > 255) bb = 255;
                rgb  = (DWORD)rr;
                rgb |= ((DWORD)gg << 8);
                rgb |= ((DWORD)bb << 16);
                d->px[y * d->w + x] = (((DWORD)aa) << 24) | (rgb & 0x00FFFFFFu);
            }
        }
    }
}

static void BuildBitmap(Dib32 *d)
{
    RECT panel;
    int  i;

    ZeroMemory(d->px, (size_t)d->w * (size_t)d->h * 4);
    SetBkMode(d->dc, TRANSPARENT);

    panel.left = SHADOW_MARGIN;
    panel.top = SHADOW_MARGIN;
    panel.right = d->w - SHADOW_MARGIN;
    panel.bottom = d->h - SHADOW_MARGIN;

    FillRoundRect(d->dc, panel, g_radius, MACOS_BG, CLR_NONE);

    if (g_hover >= 0) {
        RECT r = g_btnRect[g_hover];
        InflateRect(&r, -2, -2);
        FillRoundRect(d->dc, r, g_radius - 2, MACOS_HOVER_BG, CLR_NONE);
    }

    for (i = 0; i < g_cfg.actionCount; i++) {
        RECT     r = g_btnRect[i];
        COLORREF c;
        COLORREF bg;
        if (!ActionIsValid(&g_cfg.actions[i])) {
            c = MACOS_INVALID;
        } else if (i == g_hover) {
            c = MACOS_HOVER_FG;
        } else {
            c = MACOS_FG;
        }
        bg = (i == g_hover) ? MACOS_HOVER_BG : MACOS_BG;
        DrawActionIcon(d->dc, &g_cfg.actions[i],
                       (r.left + r.right) / 2, (r.top + r.bottom) / 2,
                       g_btn * 86 / 100, c, bg);
    }

    PushBitmap(d, panel);
}

static void Render(void)
{
    Dib32 d;
    RECT  wr;
    POINT dst;
    SIZE  sz;
    POINT src;
    BLENDFUNCTION blend;

    if (g_hwnd == NULL) return;
    ComputeLayout();
    if (!Dib32Create(&d, g_w, g_h)) return;
    BuildBitmap(&d);

    GetWindowRect(g_hwnd, &wr);
    dst.x = wr.left;
    dst.y = wr.top;
    sz.cx = g_w;
    sz.cy = g_h;
    src.x = 0;
    src.y = 0;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g_hwnd, NULL, &dst, &sz, d.dc, &src, 0, &blend, ULW_ALPHA);

    Dib32Destroy(&d);
}

BOOL PopupDumpBitmap(const TCHAR *path)
{
    Dib32            d;
    HANDLE           f;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    DWORD            bytes;
    DWORD            written;

    ComputeLayout();
    if (!Dib32Create(&d, g_w, g_h)) return FALSE;
    BuildBitmap(&d);

    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                   FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        Dib32Destroy(&d);
        return FALSE;
    }
    bytes = (DWORD)((DWORD)d.w * (DWORD)d.h * 4);
    ZeroMemory(&fh, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = (DWORD)(sizeof(fh) + sizeof(ih));
    fh.bfSize = fh.bfOffBits + bytes;
    ZeroMemory(&ih, sizeof(ih));
    ih.biSize = sizeof(ih);
    ih.biWidth = d.w;
    ih.biHeight = -d.h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = bytes;
    WriteFile(f, &fh, (DWORD)sizeof(fh), &written, NULL);
    WriteFile(f, &ih, (DWORD)sizeof(ih), &written, NULL);
    WriteFile(f, d.px, bytes, &written, NULL);
    CloseHandle(f);
    Dib32Destroy(&d);
    return TRUE;
}

BOOL PopupIsVisible(void)
{
    return g_visible;
}

BOOL PopupContainsPoint(POINT p)
{
    RECT r;
    if (!g_visible) return FALSE;
    if (GetWindowRect(g_hwnd, &r)) {
        InflateRect(&r, -SHADOW_MARGIN, -SHADOW_MARGIN);  /* panel only, not the shadow ring */
        return RectHasPoint(r, p);
    }
    return FALSE;
}

static void StopTracking(void)
{
    TRACKMOUSEEVENT tme;
    if (!g_tracking) return;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = g_hwnd;
    TrackMouseEvent(&tme);
    g_tracking = FALSE;
}

static void StartTracking(void)
{
    TRACKMOUSEEVENT tme;
    if (g_tracking) return;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = g_hwnd;
    TrackMouseEvent(&tme);
    g_tracking = TRUE;
}

void PopupHide(void)
{
    if (g_hwnd == NULL) return;
    if (!g_visible) return;
    g_visible = FALSE;
    g_hover = -1;
    StopTracking();
    ShowWindow(g_hwnd, SW_HIDE);
}

void PopupShowAt(POINT p)
{
    MONITORINFO mi;
    HMONITOR mon;
    int  x, y;

    if (g_hwnd == NULL) return;
    ComputeLayout();
    g_hover = -1;

    mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (mon == NULL || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }

    x = p.x - g_w / 2;
    y = p.y + 20;
    if (y + g_h > mi.rcWork.bottom) y = p.y - g_h - 12;
    if (y < mi.rcWork.top) y = mi.rcWork.top + 2;
    if (y + g_h > mi.rcWork.bottom) y = mi.rcWork.bottom - g_h - 2;
    if (x + g_w > mi.rcWork.right) x = mi.rcWork.right - g_w - 2;
    if (x < mi.rcWork.left)        x = mi.rcWork.left + 2;

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, g_w, g_h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_visible = TRUE;
    Render();
}

void PopupRecalcSize(void)
{
    int  w = g_w, h = g_h;
    BOOL vis = g_visible;
    POINT p;

    if (g_hwnd == NULL) return;
    ComputeLayout();
    if (w == g_w && h == g_h) {
        if (vis) Render();
        return;
    }
    if (vis) {
        GetCursorPos(&p);
        PopupHide();
        PopupShowAt(p);
    }
}

void PopupRefresh(void)
{
    if (g_visible) Render();
}

/* ------------------------------------------------------------- window */

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    POINT pt;

    switch (msg) {
    case WM_MOUSEMOVE:
        pt = LParamToPoint(lp);
        StartTracking();
        {
            int hit = HitTest(pt);
            if (hit != g_hover) {
                g_hover = hit;
                Render();
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        g_tracking = FALSE;
        g_hover = -1;
        Render();
       return 0;

    case WM_LBUTTONDOWN:
        StopTracking();
        return 0;

    case WM_LBUTTONUP:
        pt = LParamToPoint(lp);
        {
            int hit = HitTest(pt);
            if (hit >= 0) {
                int index = hit;
                PopupHide();
                if (g_owner != NULL) {
                    PostMessageW(g_owner, WM_APP_FIRE_ACTION, (WPARAM)index, 0);
                }
            } else {
                PopupHide();
            }
        }
        return 0;

    case WM_CANCELMODE:
        StopTracking();
        return 0;

    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_MOUSEWHEEL:
        PopupHide();
        return 0;

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL PopupCreate(HINSTANCE inst, HWND owner)
{
    WNDCLASSEXW wc;

    g_owner = owner;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = PopupProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = POPUP_CLASS;
    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }

    ComputeLayout();
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                             POPUP_CLASS, L"", WS_POPUP,
                             0, 0, g_w, g_h, owner, NULL, inst, NULL);
    if (g_hwnd == NULL) return FALSE;
    g_hover = -1;
    return TRUE;
}
