/*
 *  Selection Menu - util.c
 *  UI helpers: font cache, rounded rectangle painting, 32bpp layer buffers,
 *  registry run-key handling.  Only Windows 2000+ APIs are used.
 */
#include "app.h"
#include <math.h>

#define MAX_FONTS 24

typedef struct {
    HFONT h;
    int   pt10;
    int   bold;
    int   italic;
    int   aa;
} FontEntry;

static FontEntry g_fonts[MAX_FONTS];
static int       g_fontCount = 0;
static int       g_dpi = 0;
static BOOL      g_faceDone = FALSE;
static TCHAR     g_face[LF_FACESIZE] = L"MS Shell Dlg";

static const TCHAR *g_faceCandidates[] = {
    L"Microsoft YaHei UI", L"Microsoft YaHei", L"Segoe UI", L"Tahoma",
    L"MS Shell Dlg 2", L"MS Shell Dlg", NULL
};

/* ---------------------------------------------------------------- fonts */

static BOOL CALLBACK FaceEnumProc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
    int i;
    (void)tm; (void)type; (void)lp;
    for (i = 0; g_faceCandidates[i] != NULL; i++) {
        if (lstrcmpiW(lf->lfFaceName, g_faceCandidates[i]) == 0) {
            lstrcpynW(g_face, g_faceCandidates[i], LF_FACESIZE);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL CALLBACK FaceExactEnumProc(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                       DWORD type, LPARAM lp)
{
    const TCHAR *wanted = (const TCHAR *)lp;

    (void)tm;
    (void)type;
    if (wanted != NULL && lstrcmpiW(lf->lfFaceName, wanted) == 0) {
        lstrcpynW(g_face, wanted, LF_FACESIZE);
        return FALSE;
    }
    return TRUE;
}

static BOOL TryFaceFamily(const TCHAR *name)
{
    HDC hdc;

    g_face[0] = 0;
    hdc = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
    if (hdc == NULL) return FALSE;
    EnumFontFamiliesW(hdc, name, (FONTENUMPROCW)FaceExactEnumProc, (LPARAM)name);
    DeleteDC(hdc);
    return g_face[0] != 0;
}

const TCHAR *UiFontFace(void)
{
    if (!g_faceDone) {
        g_faceDone = TRUE;
        if (!TryFaceFamily(L"Microsoft YaHei UI") &&
            !TryFaceFamily(L"Microsoft YaHei")) {
            lstrcpynW(g_face, L"MS Shell Dlg", LF_FACESIZE);
            EnumFontFamiliesExW(NULL, NULL, (FONTENUMPROCW)FaceEnumProc, 0, 0);
        }
    }
    return g_face;
}

void UiInit(void)
{
    HDC hdc;
    UiFontFace();
    hdc = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
    if (hdc != NULL) {
        g_dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        DeleteDC(hdc);
    }
    if (g_dpi <= 0) {
        g_dpi = 96;
    }
}

HFONT UiFontEx(int pt10, int bold, int italic, int antialiased)
{
    int   i;
    int   dpi;
    HFONT h;

    dpi = g_dpi;
    for (i = 0; i < g_fontCount; i++) {
        if (g_fonts[i].pt10 == pt10 && g_fonts[i].bold == bold &&
            g_fonts[i].italic == italic && g_fonts[i].aa == antialiased) {
            return g_fonts[i].h;
        }
    }
    if (g_fontCount >= MAX_FONTS) {
        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    h = CreateFontW(
            -MulDiv(pt10, dpi, 720), 0, 0, 0,
            bold ? FW_SEMIBOLD : FW_NORMAL,
            italic ? TRUE : FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            antialiased ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            UiFontFace());
    if (h == NULL) {
        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    g_fonts[g_fontCount].h = h;
    g_fonts[g_fontCount].pt10 = pt10;
    g_fonts[g_fontCount].bold = bold;
    g_fonts[g_fontCount].italic = italic;
    g_fonts[g_fontCount].aa = antialiased;
    g_fontCount++;
    return h;
}

HFONT UiFont(int pt10, int bold)
{
    return UiFontEx(pt10, bold, 0, 0);
}

/* --------------------------------------------------------------- drawing */

void FillRoundRect(HDC hdc, RECT r, int radius, COLORREF fill, COLORREF border)
{
    HPEN    pen;
    HBRUSH  br;
    HGDIOBJ oldPen, oldBr;

    if (radius < 1) radius = 1;
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) {
        return;
    }
    if (border == CLR_NONE) {
        pen = (HPEN)GetStockObject(NULL_PEN);
    } else {
        pen = CreatePen(PS_SOLID, 1, border);
    }
    if (fill == CLR_NONE) {
        br = (HBRUSH)GetStockObject(NULL_BRUSH);
    } else {
        br = CreateSolidBrush(fill);
    }
    oldPen = SelectObject(hdc, pen);
    oldBr  = SelectObject(hdc, br);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    if (border != CLR_NONE) DeleteObject(pen);
    if (fill != CLR_NONE)   DeleteObject(br);
}

int TextWidth(HDC hdc, HFONT font, const TCHAR *text)
{
    SIZE sz;
    HGDIOBJ old = SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

void DrawLabel(HDC hdc, HFONT font, COLORREF color, RECT r, const TCHAR *text, UINT flags)
{
    HGDIOBJ oldFont  = SelectObject(hdc, font);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(TRANSPARENT_BRUSH));
    int oldBkMode = GetBkMode(hdc);

    /* DrawText uses the DC background mode, not the brush selected above.
       Leaving it opaque painted a bright rectangle behind each glyph while
       the surrounding control remained dark. */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    flags |= DT_NOPREFIX;
    if ((flags & DT_SINGLELINE) != 0) flags |= DT_END_ELLIPSIS;
    DrawTextW(hdc, text, -1, &r, flags);
    SetBkMode(hdc, oldBkMode);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldFont);
}

BOOL RectHasPoint(RECT r, POINT p)
{
    if (p.x < r.left || p.x >= r.right || p.y < r.top || p.y >= r.bottom) {
        return FALSE;
    }
    return TRUE;
}

int InStrI(const TCHAR *haystack, const TCHAR *needle)
{
    int hl = lstrlenW(haystack);
    int nl = lstrlenW(needle);
    int i;

    if (nl == 0 || hl < nl) return 0;
    for (i = 0; i <= hl - nl; i++) {
        if (CompareStringW(GetThreadLocale(), NORM_IGNORECASE, haystack + i, nl, needle, nl) == CSTR_EQUAL) return i + 1;
    }
    return 0;
}

POINT LParamToPoint(LPARAM lp)
{
    POINT p;
    p.x = (int)(short)(WORD)LOWORD(lp);
    p.y = (int)(short)(WORD)HIWORD(lp);
    return p;
}

/* ------------------------------------------------- 32bpp layer buffer */

BOOL Dib32Create(Dib32 *d, int w, int h)
{
    BITMAPINFO bi;

    d->bmp = NULL;
    d->old = NULL;
    d->dc  = NULL;
    d->px  = NULL;
    d->w = w;
    d->h = h;
    if (w <= 0 || h <= 0) return FALSE;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize       = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth      = w;
    bi.bmiHeader.biHeight     = -h;          /* top-down */
    bi.bmiHeader.biPlanes     = 1;
    bi.bmiHeader.biBitCount   = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    d->dc = CreateCompatibleDC(NULL);
    if (d->dc == NULL) return FALSE;
    d->bmp = CreateDIBSection(d->dc, &bi, DIB_RGB_COLORS, (void **)&d->px, NULL, 0);
    if (d->bmp == NULL || d->px == NULL) {
        DeleteDC(d->dc);
        d->dc = NULL;
        Dib32Destroy(d);
        return FALSE;
    }
    d->old = SelectObject(d->dc, d->bmp);
    return TRUE;
}

void Dib32Destroy(Dib32 *d)
{
    if (d->dc != NULL) {
        if (d->old != NULL) SelectObject(d->dc, d->old);
        if (d->bmp != NULL) DeleteObject(d->bmp);
        DeleteDC(d->dc);
    }
    d->dc = NULL;
    d->bmp = NULL;
    d->old = NULL;
    d->px = NULL;
}

/* Exact distance from (x,y) to a rounded rectangle (0 when inside). */
double DistOutsideRoundRectF(int x, int y, RECT r, int radius)
{
    double px = (double)x + 0.5;
    double py = (double)y + 0.5;
    double dx, dy, dl, dr, dt, db, m, ox, oy;
    int    inCornerX, inCornerY;

    inCornerX = (px < r.left + radius) || (px > r.right - radius);
    inCornerY = (py < r.top + radius)  || (py > r.bottom - radius);

    if (inCornerX && inCornerY) {
        double cx = (px < r.left + radius) ? (double)(r.left + radius)
                                           : (double)(r.right - radius);
        double cy = (py < r.top + radius)  ? (double)(r.top + radius)
                                           : (double)(r.bottom - radius);
        dx = px - cx;
        dy = py - cy;
        return sqrt(dx * dx + dy * dy) - (double)radius;
    }
    /* signed distance: negative inside the rectangle, positive outside */
    dl = px - (double)r.left;
    dr = (double)r.right - px;
    dt = py - (double)r.top;
    db = (double)r.bottom - py;
    m = dl;
    if (dr < m) m = dr;
    if (dt < m) m = dt;
    if (db < m) m = db;
    if (m >= 0.0) return -m;
    ox = (dl < 0.0) ? -dl : ((dr < 0.0) ? -dr : 0.0);
    oy = (dt < 0.0) ? -dt : ((db < 0.0) ? -db : 0.0);
    if (ox <= 0.0) return oy;
    if (oy <= 0.0) return ox;
    return sqrt(ox * ox + oy * oy);
}

int DistOutsideRoundRect(int x, int y, RECT r, int radius)
{
    return (int)DistOutsideRoundRectF(x, y, r, radius);
}

/* ------------------------------------------------------------- misc */

BOOL IsFullScreenWindow(HWND hwnd)
{
    RECT  wr;
    HMONITOR mon;
    MONITORINFO mi;
    LONG  style;

    if (hwnd == NULL || !IsWindowVisible(hwnd)) return FALSE;
    if (!GetWindowRect(hwnd, &wr)) return FALSE;
    if (wr.right - wr.left <= 0 || wr.bottom - wr.top <= 0) return FALSE;

    /* A maximised window covers the whole monitor too, so geometry alone is
       not enough - a real fullscreen window has no caption and no border. */
    style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & WS_CAPTION) return FALSE;
    if (style & WS_THICKFRAME) return FALSE;

    mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (mon == NULL) return FALSE;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return FALSE;
    if (wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
        wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom) {
        return TRUE;
    }
    return FALSE;
}

void OpenPath(const TCHAR *path, const TCHAR *param)
{
    SHELLEXECUTEINFOW sei;

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize        = sizeof(sei);
    sei.fMask         = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb        = L"open";
    sei.lpFile        = path;
    sei.lpParameters  = param;
    sei.nShow         = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess != NULL) CloseHandle(sei.hProcess);
    }
}

static BOOL g_traceReady = FALSE;
static TCHAR g_tracePath[MAX_PATH];

void GetExecutableDir(TCHAR *out, int cch)
{
    TCHAR path[MAX_PATH];
    DWORD n;
    int i;

    if (out == NULL || cch <= 0) return;
    out[0] = 0;
    n = GetModuleFileNameW(g_inst, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        if (GetCurrentDirectoryW(cch, out) == 0) lstrcpynW(out, L".", cch);
        return;
    }
    for (i = (int)n - 1; i >= 0; i--) {
        if (path[i] == L'\\' || path[i] == L'/') {
            path[i] = 0;
            break;
        }
    }
    lstrcpynW(out, path, cch);
}

static BOOL TraceOpen(void)
{
    if (!g_traceReady) {
        TCHAR dir[MAX_PATH];
        g_traceReady = TRUE;
        GetExecutableDir(dir, MAX_PATH);
        if (dir[0] == 0) return FALSE;
        EnsureDir(dir);
        wsprintfW(g_tracePath, L"%s\\trace.log", dir);
    }
    return (g_tracePath[0] != 0);
}

void TraceLine(const TCHAR *line)
{
    HANDLE h;
    DWORD  written = 0;

    if (!g_cfg.trace) return;
    if (!TraceOpen()) return;
    h = CreateFileW(g_tracePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, line, (DWORD)((lstrlenW(line) + 1) * sizeof(TCHAR)), &written, NULL);
    CloseHandle(h);
}

const TCHAR *TracePath(void)
{
    TraceOpen();
    return g_tracePath;
}

BOOL EnsureDir(const TCHAR *path)
{
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        return TRUE;
    }
    return CreateDirectoryW(path, NULL) != FALSE;
}

BOOL ConfigIsStartup(void)
{
    HKEY  hk;
    DWORD cb = 0;
    DWORD type = 0;
    BOOL  found = FALSE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) {
        return FALSE;
    }
    if (RegQueryValueExW(hk, APP_TITLE, NULL, &type, NULL, &cb) == ERROR_SUCCESS &&
        type == REG_SZ) {
        found = TRUE;
    }
    RegCloseKey(hk);
    return found;
}

BOOL ConfigApplyStartup(int enable)
{
    HKEY  hk;
    TCHAR path[MAX_PATH];
    DWORD status;

    status = RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL,
                        &hk, NULL);
    if (status != ERROR_SUCCESS) return FALSE;

    if (enable) {
        if (GetModuleFileNameW(g_inst, path, MAX_PATH) != 0) {
            TCHAR quoted[MAX_PATH + 4];
            quoted[0] = L'"';
            lstrcpynW(quoted + 1, path, MAX_PATH + 2);
            lstrcatW(quoted, L"\"");
            status = RegSetValueExW(hk, APP_TITLE, 0, REG_SZ, (const BYTE *)quoted,
                                    (DWORD)((lstrlenW(quoted) + 1) * sizeof(TCHAR)));
        } else {
            status = GetLastError();
        }
    } else {
        status = RegDeleteValueW(hk, APP_TITLE);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(hk);
    return status == ERROR_SUCCESS;
}
