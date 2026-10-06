/*
 *  Selection Menu - hotkey.c
 *
 *  Combo formatting, name lookup and synthetic key delivery.  The key-up half
 *  of a combo is sent from a WM_TIMER callback so the UI thread is never
 *  blocked by a Sleep() while a shortcut is in flight.
 */
#include "app.h"

typedef struct {
    UINT         vk;
    const TCHAR *name;
} NamedKey;

static const NamedKey g_named[] = {
    { VK_BACK,      L"Backspace" }, { VK_TAB,      L"Tab"       }, { VK_RETURN,   L"Enter"    },
    { VK_ESCAPE,    L"Esc"        }, { VK_SPACE,    L"Space"     }, { VK_PRIOR,    L"PgUp"     },
    { VK_NEXT,      L"PgDn"       }, { VK_END,      L"End"       }, { VK_HOME,     L"Home"     },
    { VK_LEFT,      L"Left"       }, { VK_RIGHT,    L"Right"     }, { VK_UP,       L"Up"       },
    { VK_DOWN,      L"Down"       }, { VK_INSERT,   L"Insert"    }, { VK_DELETE,   L"Delete"   },
    { VK_CAPITAL,   L"CapsLock"   }, { VK_NUMLOCK,  L"NumLock"   }, { VK_SCROLL,   L"ScrollLock" },
    { VK_PAUSE,     L"Pause"      }, { VK_SNAPSHOT, L"PrtSc"     }, { VK_APPS,     L"Menu"     },
    { VK_ADD,       L"Num +"      }, { VK_SUBTRACT, L"Num -"     }, { VK_MULTIPLY, L"Num *"     },
    { VK_DIVIDE,    L"Num /"      }, { VK_DECIMAL,  L"Num ."     },
    { VK_OEM_1,     L";"          }, { VK_OEM_2,    L"/"         }, { VK_OEM_3,    L"-"        },
    { VK_OEM_4,     L"."          }, { VK_OEM_5,    L"'"         }, { VK_OEM_6,    L"`"        },
    { VK_OEM_7,     L"["          }, { VK_OEM_8,    L"]"         }, { VK_OEM_COMMA, L","        },
    { VK_OEM_PERIOD,L"."           }, { VK_OEM_PLUS, L"+"         },
    { 0, NULL }
};

static const TCHAR *g_modNames[4] = { L"Ctrl", L"Alt", L"Shift", L"Win" };

const TCHAR *VkToName(UINT vk)
{
    static TCHAR one[4];
    int i;

    for (i = 0; g_named[i].name != NULL; i++) {
        if (g_named[i].vk == vk) return g_named[i].name;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        static TCHAR fn[8];
        wsprintfW(fn, L"F%d", (int)(vk - VK_F1 + 1));
        return fn;
    }
    if (vk >= '0' && vk <= '9') { one[0] = (TCHAR)vk; one[1] = 0; return one; }
    if (vk >= 'A' && vk <= 'Z') { one[0] = (TCHAR)vk; one[1] = 0; return one; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        static TCHAR nb[8];
        wsprintfW(nb, L"Num%d", (int)(vk - VK_NUMPAD0));
        return nb;
    }
    one[0] = (TCHAR)vk;
    one[1] = 0;
    return one;
}

void FormatCombo(const Action *a, TCHAR *buf, int cch)
{
    int pos = 0;
    int i;
    int first = TRUE;

    if (buf == NULL || cch <= 0) return;
    buf[0] = 0;
    if (a == NULL) return;

    for (i = 0; i < 4; i++) {
        if (a->mods & (1 << i)) {
            pos += wsprintfW(buf + pos, first ? L"%s" : L" + %s", g_modNames[i]);
            first = FALSE;
        }
    }
    for (i = 0; i < a->count; i++) {
        if (pos > 0) {
            pos += wsprintfW(buf + pos, L" + ");
        }
        pos += wsprintfW(buf + pos, L"%s", VkToName(a->vk[i]));
    }
    if (pos >= cch - 1) buf[cch - 1] = 0;
}

UINT CurrentModifiers(void)
{
    UINT m = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= MODF_CTRL;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= MODF_ALT;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= MODF_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) m |= MODF_WIN;
    return m;
}

int ActionIsValid(const Action *a)
{
    if (a == NULL) return FALSE;
    if (a->count <= 0 || a->count > MAX_KEYS_PER_COMBO) return FALSE;
    return TRUE;
}

static void FillKey(INPUT *in, WORD vk, BOOL up)
{
    ZeroMemory(in, sizeof(INPUT));
    in->type = INPUT_KEYBOARD;
    in->ki.wVk = vk;
    in->ki.wScan = 0;
    in->ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    in->ki.time = 0;
    in->ki.dwExtraInfo = 0;
}

static WORD ModifierVk(int bit)
{
    if (bit == 0) return VK_CONTROL;
    if (bit == 1) return VK_MENU;
    if (bit == 2) return VK_SHIFT;
    return VK_LWIN;
}

/* modifiers down, then the keys down */
static int BuildKeyDownInputs(const Action *a, INPUT *buf)
{
    int n = 0;
    int i;

    for (i = 0; i < 4; i++) {
        if (a->mods & (1 << i)) FillKey(&buf[n++], ModifierVk(i), FALSE);
    }
    for (i = 0; i < a->count; i++) {
        FillKey(&buf[n++], a->vk[i], FALSE);
    }
    return n;
}

/* keys up first, then the modifiers up (reverse of the press order) */
static int BuildKeyUpInputs(const Action *a, INPUT *buf)
{
    int n = 0;
    int i;

    for (i = 0; i < a->count; i++) {
        FillKey(&buf[n++], a->vk[i], TRUE);
    }
    for (i = 3; i >= 0; i--) {
        if (a->mods & (1 << i)) FillKey(&buf[n++], ModifierVk(i), TRUE);
    }
    return n;
}

static Action g_sendAction;

static VOID CALLBACK KeyUpTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    INPUT up[MAX_KEYS_PER_COMBO + 4];
    int   total;

    (void)msg;
    (void)time;
    KillTimer(hwnd, id);
    total = BuildKeyUpInputs(&g_sendAction, up);
    if (total > 0) {
        SendInput((UINT)total, up, sizeof(INPUT));
    }
}

void SendActionKeys(const Action *a)
{
    INPUT down[MAX_KEYS_PER_COMBO + 4];
    int   total;

    if (!ActionIsValid(a)) return;
    total = BuildKeyDownInputs(a, down);
    if (total == 0) return;

    g_sendAction = *a;
    if (SendInput((UINT)total, down, sizeof(INPUT)) == 0) {
        /* blocked (e.g. UIPI) - fall back to the legacy API, both phases */
        int i;
        int nMod = 0;
        for (i = 0; i < 4; i++) {
            if (a->mods & (1 << i)) nMod++;
        }
        for (i = 0; i < nMod + a->count; i++) {
            keybd_event((BYTE)down[i].ki.wVk, 0, 0, 0);
        }
        for (i = a->count - 1; i >= 0; i--) {
            keybd_event((BYTE)down[nMod + i].ki.wVk, 0, KEYEVENTF_KEYUP, 0);
        }
        for (i = nMod - 1; i >= 0; i--) {
            keybd_event((BYTE)down[i].ki.wVk, 0, KEYEVENTF_KEYUP, 0);
        }
        return;
    }

    if (g_mainWnd != NULL) {
        SetTimer(g_mainWnd, TIMER_KEYUP, 12, KeyUpTimerProc);
    }
}
