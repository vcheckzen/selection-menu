/*
 *  Selection Menu - uia.c
 *
 *  Pure C port of the selection detection pipeline from
 *  0xfullex/selection-hook: UI Automation first, MSAA second, and a
 *  clipboard copy as the final compatibility fallback.
 *
 *  The application only needs a yes/no answer. Coordinates, selected text,
 *  program filtering, event emission, and hook lifecycle code are omitted.
 */
#define COBJMACROS
#include "app.h"
#include <initguid.h>
#include <uiautomation.h>
#include <oleacc.h>
#include <oleauto.h>

#ifndef UIA_IsSelectionActivePropertyId
#define UIA_IsSelectionActivePropertyId 30034
#endif

#define SELECTION_TIMEOUT_MS 1800
#define MAX_UIA_ANCESTORS   10

typedef struct SelectionRequest {
    HWND            target;
    POINT           point;
    UINT            token;
    HANDLE          done;
    volatile LONG   refs;
    volatile LONG   cancelled;
} SelectionRequest;

typedef struct ClipboardItem {
    UINT    format;
    BOOL    isEnhancedMetafile;
    SIZE_T  size;
    BYTE   *data;
} ClipboardItem;

typedef struct ClipboardBackup {
    ClipboardItem  *items;
    UINT            count;
    BOOL            hadData;
} ClipboardBackup;

static volatile LONG g_clipboardBusy = 0;

BOOL SelectionCopyInProgress(void)
{
    return InterlockedCompareExchange(&g_clipboardBusy, 0, 0) != 0;
}

static BOOL IsCancelled(const SelectionRequest *request)
{
    return request != NULL &&
           InterlockedCompareExchange((volatile LONG *)&request->cancelled, 0, 0) != 0;
}

static void ReleaseSelectionRequest(SelectionRequest *request)
{
    if (request == NULL) return;
    if (InterlockedDecrement(&request->refs) == 0) {
        if (request->done != NULL) CloseHandle(request->done);
        HeapFree(GetProcessHeap(), 0, request);
    }
}

static void PostSelectionResult(HWND target, POINT point, UINT token,
                                int state, int source)
{
    SelectionResult *result;

    if (g_mainWnd == NULL) return;
    result = (SelectionResult *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          sizeof(*result));
    if (result == NULL) return;
    result->target = target;
    result->point = point;
    result->token = token;
    result->state = state;
    result->source = source;
    if (!PostMessageW(g_mainWnd, WM_APP_UIA_RESULT, 0, (LPARAM)result)) {
        HeapFree(GetProcessHeap(), 0, result);
    }
}

/*
 * Match the upstream cleanup rule: remove U+FFFC and one adjacent line break
 * on each side, then decide whether any selected text remains.
 */
static BOOL HasNonEmptyText(const WCHAR *text, UINT length)
{
    WCHAR *clean;
    UINT   sourceIndex;
    UINT   cleanLength = 0;
    BOOL   result;

    if (text == NULL || length == 0) return FALSE;
    clean = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
                               ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (clean == NULL) return length > 0;

    for (sourceIndex = 0; sourceIndex < length; sourceIndex++) {
        WCHAR ch = text[sourceIndex];
        if (ch != 0xFFFC) {
            clean[cleanLength++] = ch;
            continue;
        }

        if (cleanLength > 0 && clean[cleanLength - 1] == L'\n') {
            cleanLength--;
            if (cleanLength > 0 && clean[cleanLength - 1] == L'\r') {
                cleanLength--;
            }
        } else if (cleanLength > 0 && clean[cleanLength - 1] == L'\r') {
            cleanLength--;
        }

        if (sourceIndex + 1 < length && text[sourceIndex + 1] == L'\r') {
            sourceIndex++;
            if (sourceIndex + 1 < length && text[sourceIndex + 1] == L'\n') {
                sourceIndex++;
            }
        } else if (sourceIndex + 1 < length && text[sourceIndex + 1] == L'\n') {
            sourceIndex++;
        }
    }

    clean[cleanLength] = L'\0';
    result = cleanLength > 0;
    HeapFree(GetProcessHeap(), 0, clean);
    return result;
}

static BOOL BstrHasSelectedText(BSTR text)
{
    BOOL result;

    if (text == NULL) return FALSE;
    result = HasNonEmptyText(text, SysStringLen(text));
    SysFreeString(text);
    return result;
}

/*
 * SELECT_NONE here means a definite non-text/object selection. The caller
 * must not synthesize Ctrl+C for it, matching the upstream engine.
 */
static int ProcessAccessibleSelection(IAccessible *accessible)
{
    VARIANT selection;
    HRESULT hr;
    int result = SELECT_UNKNOWN;

    if (accessible == NULL) return SELECT_UNKNOWN;
    VariantInit(&selection);
    hr = IAccessible_get_accSelection(accessible, &selection);
    if (SUCCEEDED(hr)) {
        if (selection.vt == VT_BSTR && selection.bstrVal != NULL) {
            if (HasNonEmptyText(selection.bstrVal, SysStringLen(selection.bstrVal))) {
                result = SELECT_SELECTION;
            }
        } else if (selection.vt == VT_UNKNOWN ||
                   (selection.vt & VT_ARRAY) != 0) {
            /* An object collection may contain a selectable read-only text
               range.  Keep it ambiguous so the clipboard fallback can prove
               whether text was actually selected. */
            result = SELECT_UNKNOWN;
        } else if (selection.vt == VT_I4) {
            /* Object selection in read-only lists is not a definite "no text".
               Keep it unknown so the clipboard fallback can prove selection. */
            result = SELECT_UNKNOWN;
        } else if (selection.vt == VT_DISPATCH &&
                   selection.pdispVal != NULL) {
            IAccessible *selected = NULL;
            VARIANT child;

            hr = selection.pdispVal->lpVtbl->QueryInterface(
                selection.pdispVal, &IID_IAccessible, (void **)&selected);
            if (SUCCEEDED(hr) && selected != NULL) {
                VariantInit(&child);
                child.vt = VT_I4;
                child.lVal = CHILDID_SELF;
                result = SELECT_UNKNOWN;
                VariantClear(&child);
                IAccessible_Release(selected);
            }
        }
    }
    VariantClear(&selection);
    return result;
}

static BOOL TryTextPattern(IUIAutomationElement *element,
                           BOOL allowDocumentFallback)
{
    IUIAutomationTextPattern *pattern = NULL;
    IUIAutomationTextRangeArray *ranges = NULL;
    HRESULT hr;
    BOOL found = FALSE;
    int count = 0;
    int index;

    if (element == NULL) return FALSE;
    hr = IUIAutomationElement_GetCurrentPatternAs(
        element, UIA_TextPatternId, &IID_IUIAutomationTextPattern,
        (void **)&pattern);
    if (FAILED(hr) || pattern == NULL) return FALSE;

    hr = IUIAutomationTextPattern_GetSelection(pattern, &ranges);
    if (SUCCEEDED(hr) && ranges != NULL) {
        hr = IUIAutomationTextRangeArray_get_Length(ranges, &count);
        if (SUCCEEDED(hr)) {
            for (index = 0; index < count && !found; index++) {
                IUIAutomationTextRange *range = NULL;
                BSTR text = NULL;

                hr = IUIAutomationTextRangeArray_GetElement(ranges, index, &range);
                if (FAILED(hr) || range == NULL) continue;
                hr = IUIAutomationTextRange_GetText(range, -1, &text);
                if (SUCCEEDED(hr) && text != NULL) {
                    found = BstrHasSelectedText(text);
                }
                IUIAutomationTextRange_Release(range);
            }
        }
        IUIAutomationTextRangeArray_Release(ranges);
    }

    if (!found && allowDocumentFallback) {
        IUIAutomationTextRange *documentRange = NULL;
        hr = IUIAutomationTextPattern_get_DocumentRange(pattern, &documentRange);
        if (SUCCEEDED(hr) && documentRange != NULL) {
            VARIANT active;

            VariantInit(&active);
            hr = IUIAutomationTextRange_GetAttributeValue(
                documentRange, UIA_IsSelectionActivePropertyId, &active);
            if (SUCCEEDED(hr) && active.vt == VT_BOOL &&
                active.boolVal == VARIANT_TRUE) {
                BSTR text = NULL;
                hr = IUIAutomationTextRange_GetText(documentRange, -1, &text);
                if (SUCCEEDED(hr) && text != NULL) {
                    found = BstrHasSelectedText(text);
                }
            }
            VariantClear(&active);

            if (!found) {
                hr = IUIAutomationTextRange_ExpandToEnclosingUnit(
                    documentRange, TextUnit_Document);
                if (SUCCEEDED(hr)) {
                    VariantInit(&active);
                    hr = IUIAutomationTextRange_GetAttributeValue(
                        documentRange, UIA_IsSelectionActivePropertyId, &active);
                    if (SUCCEEDED(hr) && active.vt == VT_BOOL &&
                        active.boolVal == VARIANT_TRUE) {
                        BSTR text = NULL;
                        hr = IUIAutomationTextRange_GetText(documentRange, -1, &text);
                        if (SUCCEEDED(hr) && text != NULL) {
                            found = BstrHasSelectedText(text);
                        }
                    }
                    VariantClear(&active);
                }
            }
            IUIAutomationTextRange_Release(documentRange);
        }
    }

    IUIAutomationTextPattern_Release(pattern);
    return found;
}

static int QueryLegacySelection(IUIAutomationElement *element)
{
    IUIAutomationLegacyIAccessiblePattern *legacy = NULL;
    IAccessible *accessible = NULL;
    HRESULT hr;
    int result = SELECT_UNKNOWN;

    if (element == NULL) return SELECT_UNKNOWN;
    hr = IUIAutomationElement_GetCurrentPatternAs(
        element, UIA_LegacyIAccessiblePatternId,
        &IID_IUIAutomationLegacyIAccessiblePattern, (void **)&legacy);
    if (FAILED(hr) || legacy == NULL) return SELECT_UNKNOWN;

    hr = IUIAutomationLegacyIAccessiblePattern_GetIAccessible(
        legacy, &accessible);
    if (SUCCEEDED(hr) && accessible != NULL) {
        result = ProcessAccessibleSelection(accessible);
        IAccessible_Release(accessible);
    }
    IUIAutomationLegacyIAccessiblePattern_Release(legacy);
    return result;
}

static BOOL TryElementAndAncestors(IUIAutomation *automation,
                                   IUIAutomationElement *start,
                                   const SelectionRequest *request)
{
    IUIAutomationTreeWalker *walker = NULL;
    IUIAutomationElement *current;
    HRESULT hr;
    BOOL found = FALSE;
    int level;

    if (automation == NULL || start == NULL || IsCancelled(request)) {
        return FALSE;
    }
    if (TryTextPattern(start, FALSE)) return TRUE;

    hr = IUIAutomation_get_ControlViewWalker(automation, &walker);
    if (FAILED(hr) || walker == NULL) return FALSE;

    current = start;
    IUIAutomationElement_AddRef(current);
    for (level = 0; level < MAX_UIA_ANCESTORS && !IsCancelled(request);
         level++) {
        IUIAutomationElement *parent = NULL;

        hr = IUIAutomationTreeWalker_GetParentElement(walker, current, &parent);
        IUIAutomationElement_Release(current);
        current = parent;
        if (FAILED(hr) || current == NULL) break;

        /* Ancestors use GetSelection only; DocumentRange can expose an entire
           Chromium page as though it were the current selection. */
        if (TryTextPattern(current, FALSE)) {
            found = TRUE;
            break;
        }
    }
    if (current != NULL) IUIAutomationElement_Release(current);
    IUIAutomationTreeWalker_Release(walker);
    return found;
}

static int QueryUiaSelection(HWND target, const SelectionRequest *request)
{
    IUIAutomation *automation = NULL;
    IUIAutomationElement *windowElement = NULL;
    IUIAutomationElement *focused = NULL;
    IUIAutomationElement *underPoint = NULL;
    HRESULT comHr;
    HRESULT hr;
    int result = SELECT_UNKNOWN;
    int legacyResult = SELECT_UNKNOWN;

    if (target == NULL || IsCancelled(request)) return SELECT_UNKNOWN;
    comHr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) return SELECT_UNKNOWN;

    hr = CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUIAutomation, (void **)&automation);
    if (FAILED(hr) || automation == NULL) goto done;

    /* Preserve the upstream HWND validity probe before querying focus. */
    hr = IUIAutomation_ElementFromHandle(automation, (UIA_HWND)target,
                                          &windowElement);
    if (FAILED(hr) || windowElement == NULL) goto done;

    hr = IUIAutomation_GetFocusedElement(automation, &focused);
    if (FAILED(hr) || focused == NULL) {
        if (IsCancelled(request)) goto done;
        goto point_query;
    }

    if (TryTextPattern(focused, TRUE)) {
        result = SELECT_SELECTION;
        goto done;
    }

    legacyResult = QueryLegacySelection(focused);
    if (legacyResult == SELECT_SELECTION) {
        result = SELECT_SELECTION;
        goto done;
    }

    if (TryElementAndAncestors(automation, focused, request)) {
        result = SELECT_SELECTION;
    }

point_query:
    /*
     * Read-only edit boxes and custom selectable controls often expose their
     * TextPattern on the element under the pointer rather than on the focused
     * container. GetSelection is still authoritative, so no bounding-rectangle
     * or editability check is needed here.
     */
    if (result != SELECT_SELECTION && !IsCancelled(request)) {
        hr = IUIAutomation_ElementFromPoint(automation, request->point,
                                            &underPoint);
        if (SUCCEEDED(hr) && underPoint != NULL) {
            if (TryElementAndAncestors(automation, underPoint, request)) {
                result = SELECT_SELECTION;
            }
            IUIAutomationElement_Release(underPoint);
        }
    }

    if (result != SELECT_SELECTION && legacyResult == SELECT_NONE) {
        result = SELECT_NONE;
    }

done:
    if (focused != NULL) IUIAutomationElement_Release(focused);
    if (windowElement != NULL) IUIAutomationElement_Release(windowElement);
    if (automation != NULL) IUIAutomation_Release(automation);
    if (SUCCEEDED(comHr)) CoUninitialize();
    return result;
}

static int QueryMsaaObject(HWND hwnd)
{
    IAccessible *accessible = NULL;
    HRESULT hr;
    int result = SELECT_UNKNOWN;

    if (hwnd == NULL) return SELECT_UNKNOWN;
    hr = AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, &IID_IAccessible,
                                    (void **)&accessible);
    if (SUCCEEDED(hr) && accessible != NULL) {
        result = ProcessAccessibleSelection(accessible);
        IAccessible_Release(accessible);
    }
    return result;
}

static int QueryMsaaSelection(HWND target, const SelectionRequest *request)
{
    GUITHREADINFO info;
    HWND underPoint;
    int result = SELECT_UNKNOWN;

    if (target == NULL || IsCancelled(request)) return SELECT_UNKNOWN;

    /* The focused child is where legacy read-only controls expose selection. */
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetWindowThreadProcessId(target, NULL), &info) &&
        info.hwndFocus != NULL) {
        result = QueryMsaaObject(info.hwndFocus);
        if (result == SELECT_SELECTION || result == SELECT_NONE) return result;
    }

    result = QueryMsaaObject(target);
    if (result == SELECT_SELECTION || result == SELECT_NONE) return result;

    underPoint = WindowFromPoint(request->point);
    if (underPoint != NULL && underPoint != target &&
        underPoint != info.hwndFocus) {
        result = QueryMsaaObject(underPoint);
    }
    return result;
}

static BOOL SkipClipboardFormat(UINT format)
{
    switch (format) {
    case CF_TEXT:
    case CF_OEMTEXT:
    case CF_LOCALE:
    case CF_BITMAP:
    case CF_PALETTE:
    case CF_METAFILEPICT:
    case CF_OWNERDISPLAY:
    case CF_DSPTEXT:
    case CF_DSPBITMAP:
    case CF_DSPMETAFILEPICT:
    case CF_DSPENHMETAFILE:
        return TRUE;
    default:
        break;
    }
    if (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST) return TRUE;
    if (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST) return TRUE;
    return FALSE;
}

static void FreeClipboardBackup(ClipboardBackup *backup)
{
    UINT index;

    if (backup == NULL) return;
    for (index = 0; index < backup->count; index++) {
        if (backup->items[index].data != NULL) {
            HeapFree(GetProcessHeap(), 0, backup->items[index].data);
        }
    }
    if (backup->items != NULL) HeapFree(GetProcessHeap(), 0, backup->items);
    ZeroMemory(backup, sizeof(*backup));
}

static BOOL AddClipboardItem(ClipboardBackup *backup, UINT format,
                             const BYTE *data, SIZE_T size, BOOL isEmf)
{
    ClipboardItem *grown;
    ClipboardItem *item;
    SIZE_T itemBytes;

    itemBytes = ((SIZE_T)backup->count + 1) * sizeof(ClipboardItem);
    if (backup->items == NULL) {
        grown = (ClipboardItem *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           itemBytes);
    } else {
        grown = (ClipboardItem *)HeapReAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, backup->items, itemBytes);
    }
    if (grown == NULL) return FALSE;
    backup->items = grown;
    item = &backup->items[backup->count];
    item->data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (item->data == NULL) return FALSE;
    CopyMemory(item->data, data, size);
    item->format = format;
    item->size = size;
    item->isEnhancedMetafile = isEmf;
    backup->count++;
    return TRUE;
}

static BOOL BackupClipboard(ClipboardBackup *backup)
{
    UINT format = 0;
    BOOL ok = TRUE;

    ZeroMemory(backup, sizeof(*backup));
    if (!OpenClipboard(NULL)) return FALSE;
    backup->hadData = CountClipboardFormats() != 0;

    while (ok && (format = EnumClipboardFormats(format)) != 0) {
        HANDLE handle;

        if (SkipClipboardFormat(format)) continue;
        handle = GetClipboardData(format);
        if (handle == NULL) continue;

        if (format == CF_ENHMETAFILE) {
            HENHMETAFILE enhanced = (HENHMETAFILE)handle;
            UINT size = GetEnhMetaFileBits(enhanced, 0, NULL);
            BYTE *bits;

            if (size == 0) continue;
            bits = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
            if (bits == NULL) { ok = FALSE; break; }
            if (GetEnhMetaFileBits(enhanced, size, bits) == 0) {
                HeapFree(GetProcessHeap(), 0, bits);
                ok = FALSE;
                break;
            }
            ok = AddClipboardItem(backup, format, bits, size, TRUE);
            HeapFree(GetProcessHeap(), 0, bits);
        } else {
            SIZE_T size = GlobalSize(handle);
            BYTE *data = NULL;

            if (size == 0) continue;
            data = (BYTE *)GlobalLock(handle);
            if (data == NULL) continue;
            ok = AddClipboardItem(backup, format, data, size, FALSE);
            GlobalUnlock(handle);
        }
    }

    if (ok && backup->hadData && backup->count == 0) ok = FALSE;
    CloseClipboard();
    if (!ok) FreeClipboardBackup(backup);
    return ok;
}

static BOOL RestoreClipboard(const ClipboardBackup *backup)
{
    UINT index;
    BOOL ok = TRUE;

    if (backup == NULL || !OpenClipboard(NULL)) return FALSE;
    EmptyClipboard();
    for (index = 0; index < backup->count && ok; index++) {
        const ClipboardItem *item = &backup->items[index];
        HANDLE restored;

        if (item->isEnhancedMetafile) {
            restored = (HANDLE)SetEnhMetaFileBits((UINT)item->size, item->data);
            if (restored == NULL) ok = FALSE;
            continue;
        }

        restored = GlobalAlloc(GMEM_MOVEABLE, item->size);
        if (restored == NULL) { ok = FALSE; break; }
        {
            void *target = GlobalLock(restored);
            if (target == NULL) {
                GlobalFree(restored);
                ok = FALSE;
                break;
            }
            CopyMemory(target, item->data, item->size);
            GlobalUnlock(restored);
        }
        if (SetClipboardData(item->format, restored) == NULL) {
            GlobalFree(restored);
            ok = FALSE;
        }
    }
    CloseClipboard();
    return ok;
}

static BOOL ClipboardHasText(void)
{
    BOOL result = FALSE;
    HANDLE handle;

    if (!OpenClipboard(NULL)) return FALSE;
    handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != NULL) {
        WCHAR *text = (WCHAR *)GlobalLock(handle);
        SIZE_T chars = GlobalSize(handle) / sizeof(WCHAR);
        SIZE_T index;

        if (text != NULL) {
            for (index = 0; index < chars && text[index] != L'\0'; index++) {
                if (text[index] != L'\0') { result = TRUE; break; }
            }
            GlobalUnlock(handle);
        }
    }
    if (!result) {
        handle = GetClipboardData(CF_TEXT);
        if (handle != NULL) {
            char *text = (char *)GlobalLock(handle);
            SIZE_T size = GlobalSize(handle);
            SIZE_T index;

            if (text != NULL) {
                for (index = 0; index < size && text[index] != '\0'; index++) {
                    result = TRUE;
                    break;
                }
                GlobalUnlock(handle);
            }
        }
    }
    CloseClipboard();
    return result;
}

static BOOL CopyKeyIsHeld(void)
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (GetAsyncKeyState('C') & 0x8000) != 0 ||
           (GetAsyncKeyState('X') & 0x8000) != 0 ||
           (GetAsyncKeyState('V') & 0x8000) != 0;
}

/*
 * Do not disturb a real Ctrl+C/X/V that is already in progress. A sequence
 * change during the wait is read directly instead of injecting another copy.
 */
static BOOL WaitForUserCopyKeys(const SelectionRequest *request,
                                DWORD startingSequence)
{
    int attempts;

    for (attempts = 0; attempts < 40; attempts++) {
        if (IsCancelled(request)) return FALSE;
        if (GetClipboardSequenceNumber() != startingSequence &&
            ClipboardHasText()) {
            return TRUE;
        }
        if (!CopyKeyIsHeld()) return FALSE;
        Sleep(5);
    }
    return CopyKeyIsHeld() == FALSE &&
           GetClipboardSequenceNumber() != startingSequence &&
           ClipboardHasText();
}

static void SendCopyKey(WORD keyCode)
{
    INPUT inputs[6];
    UINT count = 0;
    BOOL ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (ctrlHeld &&
        (GetAsyncKeyState(keyCode) & 0x8000) != 0) {
        return;
    }
    ZeroMemory(inputs, sizeof(inputs));
    if (altHeld) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }
    if (shiftHeld) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }
    if (!ctrlHeld) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_RCONTROL;
        count++;
    }
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = keyCode;
    count++;
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = keyCode;
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    count++;
    if (!ctrlHeld) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_RCONTROL;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }
    SendInput(count, inputs, sizeof(INPUT));
}

static BOOL WaitForClipboardChange(const SelectionRequest *request,
                                   DWORD sequence, int attempts)
{
    int index;

    for (index = 0; index < attempts; index++) {
        if (IsCancelled(request)) return FALSE;
        if (GetClipboardSequenceNumber() != sequence) return TRUE;
        Sleep(5);
    }
    return FALSE;
}

static void SendWindowCopy(HWND target, POINT point)
{
    GUITHREADINFO info;
    HWND focus = NULL;
    HWND under = WindowFromPoint(point);
    DWORD_PTR ignored = 0;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (target != NULL &&
        GetGUIThreadInfo(GetWindowThreadProcessId(target, NULL), &info)) {
        focus = info.hwndFocus;
    }
    if (focus != NULL) {
        SendMessageTimeoutW(focus, WM_COPY, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 40, &ignored);
    }
    if (under != NULL && under != focus) {
        SendMessageTimeoutW(under, WM_COPY, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 40, &ignored);
    }
    if (target != NULL && target != focus && target != under) {
        SendMessageTimeoutW(target, WM_COPY, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 40, &ignored);
    }
}

static int QueryClipboardSelection(HWND target,
                                   const SelectionRequest *request)
{
    ClipboardBackup backup;
    DWORD sequence;
    DWORD startingSequence;
    BOOL backedUp = FALSE;
    BOOL hasText = FALSE;
    int result = SELECT_NONE;
    LONG busy;
    int wait;

    if (target == NULL || IsCancelled(request)) return SELECT_UNKNOWN;
    if (GetWindowThreadProcessId(target, NULL) == GetCurrentThreadId()) {
        return SELECT_NONE;
    }

    busy = InterlockedCompareExchange(&g_clipboardBusy, 1, 0);
    for (wait = 0; busy != 0 && wait < 40; wait++) {
        if (IsCancelled(request)) return SELECT_UNKNOWN;
        Sleep(5);
        busy = InterlockedCompareExchange(&g_clipboardBusy, 1, 0);
    }
    if (busy != 0) return SELECT_UNKNOWN;

    startingSequence = GetClipboardSequenceNumber();
    if (WaitForUserCopyKeys(request, startingSequence)) {
        InterlockedExchange(&g_clipboardBusy, 0);
        return SELECT_SELECTION;
    }
    if (IsCancelled(request) || CopyKeyIsHeld()) {
        InterlockedExchange(&g_clipboardBusy, 0);
        return SELECT_UNKNOWN;
    }

    if (!BackupClipboard(&backup)) {
        InterlockedExchange(&g_clipboardBusy, 0);
        return SELECT_UNKNOWN;
    }
    backedUp = TRUE;
    if (!OpenClipboard(NULL) || !EmptyClipboard()) {
        CloseClipboard();
        RestoreClipboard(&backup);
        FreeClipboardBackup(&backup);
        InterlockedExchange(&g_clipboardBusy, 0);
        return SELECT_UNKNOWN;
    }
    CloseClipboard();

    sequence = GetClipboardSequenceNumber();
    /* Read-only controls frequently implement WM_COPY but ignore synthesized
       Ctrl+C.  Try the message first; it is harmless when no range exists. */
    SendWindowCopy(target, request->point);
    if (WaitForClipboardChange(request, sequence, 8)) {
        Sleep(10);
        hasText = ClipboardHasText();
    }

    sequence = GetClipboardSequenceNumber();
    SendCopyKey(VK_INSERT);
    if (!hasText && WaitForClipboardChange(request, sequence, 20)) {
        Sleep(10);
        hasText = ClipboardHasText();
    }

    if (!hasText && !IsCancelled(request)) {
        sequence = GetClipboardSequenceNumber();
        SendCopyKey('C');
        if (WaitForClipboardChange(request, sequence, 36)) {
            Sleep(10);
            hasText = ClipboardHasText();
        }
    }

    if (backedUp) {
        RestoreClipboard(&backup);
        FreeClipboardBackup(&backup);
    }
    InterlockedExchange(&g_clipboardBusy, 0);
    if (IsCancelled(request)) return SELECT_UNKNOWN;
    result = hasText ? SELECT_SELECTION : SELECT_NONE;
    return result;
}

static int QuerySelectedText(HWND target, const SelectionRequest *request,
                             int *source)
{
    int state;

    state = QueryUiaSelection(target, request);
    if (state == SELECT_SELECTION || IsCancelled(request)) {
        *source = SELSOURCE_UIA;
        return state;
    }

    state = QueryMsaaSelection(target, request);
    if (state == SELECT_SELECTION || IsCancelled(request)) {
        *source = SELSOURCE_MSAA;
        return state;
    }

    *source = SELSOURCE_CLIPBOARD;
    return QueryClipboardSelection(target, request);
}

static DWORD WINAPI SelectionWorkerThread(LPVOID param)
{
    SelectionRequest *request = (SelectionRequest *)param;
    int state;
    int source = SELSOURCE_NONE;

    state = QuerySelectedText(request->target, request, &source);
    if (!IsCancelled(request)) {
        PostSelectionResult(request->target, request->point, request->token,
                            state, source);
    }
    SetEvent(request->done);
    ReleaseSelectionRequest(request);
    return 0;
}

static DWORD WINAPI SelectionWatchdogThread(LPVOID param)
{
    SelectionRequest *request = (SelectionRequest *)param;
    DWORD wait;

    wait = WaitForSingleObject(request->done, SELECTION_TIMEOUT_MS);
    if (wait == WAIT_TIMEOUT) {
        InterlockedExchange(&request->cancelled, 1);
        PostSelectionResult(request->target, request->point, request->token,
                            SELECT_UNKNOWN, SELSOURCE_NONE);
    }
    ReleaseSelectionRequest(request);
    return 0;
}

BOOL UiaQuerySelectionAsync(HWND target, POINT point, UINT token)
{
    SelectionRequest *request;
    HANDLE thread;

    request = (SelectionRequest *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                             sizeof(*request));
    if (request == NULL) return FALSE;
    request->target = target;
    request->point = point;
    request->token = token;
    request->refs = 2;
    request->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (request->done == NULL) {
        HeapFree(GetProcessHeap(), 0, request);
        return FALSE;
    }

    thread = CreateThread(NULL, 0, SelectionWorkerThread, request, 0, NULL);
    if (thread == NULL) {
        CloseHandle(request->done);
        HeapFree(GetProcessHeap(), 0, request);
        return FALSE;
    }
    CloseHandle(thread);

    thread = CreateThread(NULL, 0, SelectionWatchdogThread, request, 0, NULL);
    if (thread == NULL) {
        ReleaseSelectionRequest(request);
    } else {
        CloseHandle(thread);
    }
    return TRUE;
}
