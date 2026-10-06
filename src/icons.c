/*
 *  Selection Menu - icons.c
 *
 *  Both glyphs are drawn as vectors so they stay crisp from XP to a 4K
 *  Windows 11 desktop.  Icon 0 is a translation pair of language bubbles;
 *  icon 1 is a compact camera representing screenshot capture.
 */
#include "app.h"
#include <shellapi.h>
#include <string.h>

static void FillGlyphRect(HDC hdc, RECT r, int radius, COLORREF fill)
{
    HPEN   pen = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ op = SelectObject(hdc, pen);
    HGDIOBJ ob = SelectObject(hdc, br);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
}

static void FillPoly(HDC hdc, POINT *pts, int n, COLORREF color)
{
    HBRUSH  br = CreateSolidBrush(color);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, pts, n);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(br);
}

/* icon 0: two language bubbles with compact A / 文 marks */
static void DrawTranslation(HDC hdc, int cx, int cy, int s, COLORREF fg, COLORREF bg)
{
    RECT left, right;
    POINT tail[3];
    HPEN pen;
    HGDIOBJ oldPen;
    int width = s / 12;

    if (width < 1) width = 1;
    left.left = cx - s * 46 / 100; left.top = cy - s * 34 / 100;
    left.right = cx - s * 4 / 100; left.bottom = cy + s * 8 / 100;
    right.left = cx + s * 4 / 100; right.top = cy - s * 8 / 100;
    right.right = cx + s * 46 / 100; right.bottom = cy + s * 34 / 100;

    FillGlyphRect(hdc, left, s * 9 / 100, fg);
    FillGlyphRect(hdc, right, s * 9 / 100, fg);

    tail[0].x = left.left + s * 8 / 100; tail[0].y = left.bottom - 1;
    tail[1].x = left.left + s * 2 / 100; tail[1].y = left.bottom + s * 12 / 100;
    tail[2].x = left.left + s * 22 / 100; tail[2].y = left.bottom - 1;
    FillPoly(hdc, tail, 3, fg);
    tail[0].x = right.right - s * 8 / 100; tail[0].y = right.bottom - 1;
    tail[1].x = right.right - s * 2 / 100; tail[1].y = right.bottom + s * 12 / 100;
    tail[2].x = right.right - s * 22 / 100; tail[2].y = right.bottom - 1;
    FillPoly(hdc, tail, 3, fg);

    pen = CreatePen(PS_SOLID, width, bg);
    oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, left.left + s * 9 / 100, left.bottom - s * 7 / 100, NULL);
    LineTo(hdc, (left.left + left.right) / 2, left.top + s * 8 / 100);
    LineTo(hdc, left.right - s * 9 / 100, left.bottom - s * 7 / 100);
    MoveToEx(hdc, left.left + s * 13 / 100, left.bottom - s * 13 / 100, NULL);
    LineTo(hdc, left.right - s * 13 / 100, left.bottom - s * 13 / 100);

    MoveToEx(hdc, right.left + s * 9 / 100, right.top + s * 16 / 100, NULL);
    LineTo(hdc, right.right - s * 9 / 100, right.top + s * 16 / 100);
    MoveToEx(hdc, right.left + s * 12 / 100, right.top + s * 9 / 100, NULL);
    LineTo(hdc, right.right - s * 12 / 100, right.bottom - s * 9 / 100);
    MoveToEx(hdc, right.right - s * 12 / 100, right.top + s * 9 / 100, NULL);
    LineTo(hdc, right.left + s * 12 / 100, right.bottom - s * 9 / 100);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* icon 1: compact camera / screenshot glyph */
static void DrawScreenshot(HDC hdc, int cx, int cy, int s, COLORREF fg, COLORREF bg)
{
    RECT body, bump, lens, inner;

    bump.left = cx - s * 17 / 100; bump.top = cy - s * 34 / 100;
    bump.right = cx + s * 7 / 100; bump.bottom = cy - s * 10 / 100;
    FillGlyphRect(hdc, bump, s * 6 / 100, fg);

    body.left = cx - s * 43 / 100; body.top = cy - s * 18 / 100;
    body.right = cx + s * 43 / 100; body.bottom = cy + s * 36 / 100;
    FillGlyphRect(hdc, body, s * 10 / 100, fg);

    lens.left = cx - s * 13 / 100; lens.top = cy - s * 4 / 100;
    lens.right = cx + s * 13 / 100; lens.bottom = cy + s * 22 / 100;
    FillGlyphRect(hdc, lens, s * 13 / 100, bg);
    inner.left = cx - s * 5 / 100; inner.top = cy + s * 4 / 100;
    inner.right = cx + s * 5 / 100; inner.bottom = cy + s * 14 / 100;
    FillGlyphRect(hdc, inner, s * 5 / 100, fg);
}

typedef struct {
    UINT32 version;
    void  *debugEvent;
    BOOL   suppressBackground;
    BOOL   suppressExternalCodecs;
} GdiplusStartupInputLocal;

typedef int  (WINAPI *GdiplusStartupFn)(ULONG_PTR *, const void *, void *);
typedef void (WINAPI *GdiplusShutdownFn)(ULONG_PTR);
typedef int  (WINAPI *GdipCreateBitmapFromFileFn)(const WCHAR *, void **);
typedef int  (WINAPI *GdipCreateHICONFromBitmapFn)(void *, HICON *);
typedef int  (WINAPI *GdipDisposeImageFn)(void *);

static BOOL EndsWithIco(const TCHAR *path)
{
    const TCHAR *p = path;
    const TCHAR *dot = NULL;

    while (*p != 0) {
        if (*p == L'.') dot = p;
        p++;
    }
    return (dot != NULL && lstrcmpiW(dot, L".ico") == 0);
}

static HICON LoadImageFileIcon(const TCHAR *path)
{
    HMODULE module;
    GdiplusStartupInputLocal input;
    GdiplusStartupFn startup;
    GdiplusShutdownFn shutdown;
    GdipCreateBitmapFromFileFn createBitmap;
    GdipCreateHICONFromBitmapFn createIcon;
    GdipDisposeImageFn disposeImage;
    ULONG_PTR token = 0;
    void *bitmap = NULL;
    HICON icon = NULL;
    FARPROC proc;

    if (EndsWithIco(path)) {
        icon = (HICON)LoadImageW(NULL, path, IMAGE_ICON, 0, 0,
                                 LR_LOADFROMFILE | LR_DEFAULTSIZE);
        if (icon != NULL) return icon;
    }

    module = LoadLibraryW(L"gdiplus.dll");
    if (module == NULL) return NULL;
    proc = GetProcAddress(module, "GdiplusStartup");
    memcpy(&startup, &proc, sizeof(startup));
    proc = GetProcAddress(module, "GdiplusShutdown");
    memcpy(&shutdown, &proc, sizeof(shutdown));
    proc = GetProcAddress(module, "GdipCreateBitmapFromFile");
    memcpy(&createBitmap, &proc, sizeof(createBitmap));
    proc = GetProcAddress(module, "GdipCreateHICONFromBitmap");
    memcpy(&createIcon, &proc, sizeof(createIcon));
    proc = GetProcAddress(module, "GdipDisposeImage");
    memcpy(&disposeImage, &proc, sizeof(disposeImage));
    if (startup == NULL || shutdown == NULL || createBitmap == NULL ||
        createIcon == NULL || disposeImage == NULL) {
        FreeLibrary(module);
        return NULL;
    }

    ZeroMemory(&input, sizeof(input));
    input.version = 1;
    if (startup(&token, &input, NULL) != 0) {
        FreeLibrary(module);
        return NULL;
    }
    if (createBitmap(path, &bitmap) == 0 && bitmap != NULL) {
        createIcon(bitmap, &icon);
        disposeImage(bitmap);
    }
    shutdown(token);
    FreeLibrary(module);
    return icon;
}

static HICON LoadShellFallbackIcon(const TCHAR *path)
{
    SHFILEINFOW info;

    ZeroMemory(&info, sizeof(info));
    if (SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_ICON) == 0) {
        return NULL;
    }
    return info.hIcon;
}

void ActionIconRelease(Action *action)
{
    if (action == NULL) return;
    if (action->fileIcon != NULL) DestroyIcon(action->fileIcon);
    action->fileIcon = NULL;
    action->fileIconState = 0;
}

static HICON GetActionFileIcon(Action *action)
{
    if (action->iconPath[0] == 0) return NULL;
    if (action->fileIconState == 0) {
        action->fileIcon = LoadImageFileIcon(action->iconPath);
        if (action->fileIcon == NULL) {
            action->fileIcon = LoadShellFallbackIcon(action->iconPath);
        }
        action->fileIconState = (action->fileIcon != NULL) ? 1 : 2;
    }
    return action->fileIcon;
}

void DrawActionIcon(HDC hdc, Action *action, int cx, int cy, int size, COLORREF color, COLORREF bg)
{
    HICON fileIcon;

    fileIcon = GetActionFileIcon(action);
    if (fileIcon != NULL) {
        DrawIconEx(hdc, cx - size / 2, cy - size / 2, fileIcon,
                   size, size, 0, NULL, DI_NORMAL);
        return;
    }
    if (action->icon == ICON_TRANSLATION) {
        DrawTranslation(hdc, cx, cy, size, color, bg);
    } else {
        DrawScreenshot(hdc, cx, cy, size, color, bg);
    }
}
