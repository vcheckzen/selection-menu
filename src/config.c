/*
 *  Selection Menu - config.c
 *  Settings live beside the executable as settings.ini (plain INI, easy to
 *  inspect and to back up).
 */
#include "app.h"
#include <stdlib.h>

Config   g_cfg;
HINSTANCE g_inst;
HWND      g_mainWnd;

static TCHAR g_iniPath[MAX_PATH];

static const TCHAR *kIniSection = L"SelectionMenu";

static void GetIniDir(TCHAR *out, int cch)
{
    GetExecutableDir(out, cch);
    if (out[0] == 0) lstrcpynW(out, L".", cch);
    EnsureDir(out);
}

const TCHAR *ConfigIniPath(void)
{
    if (g_iniPath[0] == 0) {
        TCHAR dir[MAX_PATH];
        GetIniDir(dir, MAX_PATH);
        wsprintfW(g_iniPath, L"%s\\settings.ini", dir);
    }
    return g_iniPath;
}

static void ComboToStr(const Action *a, TCHAR *buf, int cch)
{
    int i;
    int pos;

    pos = wsprintfW(buf, L"%d", a->mods);
    for (i = 0; i < a->count; i++) {
        pos += wsprintfW(buf + pos, L"|0x%02X", a->vk[i]);
    }
    if (pos >= cch - 1) {
        buf[cch - 1] = 0;
    }
}

static void StrToCombo(const TCHAR *s, Action *a)
{
    const TCHAR *p = s;
    TCHAR        token[32];
    int          len;

    a->mods = 0;
    a->count = 0;

    while (*p != 0 && a->count < MAX_KEYS_PER_COMBO) {
        len = 0;
        while (*p != 0 && *p != L'|' && len < (int)(sizeof(token) / sizeof(token[0])) - 1) {
            token[len++] = *p++;
        }
        token[len] = 0;
        while (*p == L'|') p++;
        if (len == 0) continue;
        {
            unsigned long v = wcstoul(token, NULL, 16);
            if (a->count == 0 && v <= 15 && wcschr(token, L'x') == NULL) {
                a->mods = (int)v;
            } else {
                a->vk[a->count++] = (WORD)v;
            }
        }
    }
    if (a->count <= 0) a->count = 0;
}

void ConfigResetDefaults(void)
{
    int i;

    for (i = 0; i < MAX_ACTIONS; i++) {
        ActionIconRelease(&g_cfg.actions[i]);
    }
    ZeroMemory(&g_cfg, sizeof(g_cfg));
    g_cfg.enabled           = 1;
    g_cfg.triggerMode       = TRIGGER_MODE_SELECTION_HOOK;
    g_cfg.dragSelect        = 1;
    g_cfg.showInFullscreen  = 0;
    g_cfg.startup           = ConfigIsStartup() ? 1 : 0;
    g_cfg.trace             = 0;
    g_cfg.iconSize          = 0;
    g_cfg.opacity           = 100;
    g_cfg.actionCount       = 2;

    for (i = 0; i < MAX_ACTIONS; i++) {
        if (i < 2) {
            g_cfg.actions[i].mods = MODF_CTRL | MODF_SHIFT;
            g_cfg.actions[i].count = 1;
            g_cfg.actions[i].vk[0] = (i == 0) ? (WORD)'C' : (WORD)'V';
        } else {
            g_cfg.actions[i].mods = 0;
            g_cfg.actions[i].count = 0;
        }
        g_cfg.actions[i].icon = (i % 2 == 0) ? ICON_TRANSLATION : ICON_SCREENSHOT;
    }
}

void ConfigLoad(void)
{
    TCHAR buf[64];
    TCHAR pathBuf[MAX_PATH];
    int   i;

    ConfigResetDefaults();

    g_cfg.enabled = GetPrivateProfileIntW(kIniSection, L"Enabled", 1, ConfigIniPath());
    g_cfg.triggerMode = GetPrivateProfileIntW(kIniSection, L"TriggerMode",
                                              TRIGGER_MODE_SELECTION_HOOK, ConfigIniPath());
    g_cfg.dragSelect = GetPrivateProfileIntW(kIniSection, L"DragSelect", 1, ConfigIniPath());
    g_cfg.showInFullscreen = GetPrivateProfileIntW(kIniSection, L"ShowInFullscreen", 0, ConfigIniPath());
    g_cfg.startup = GetPrivateProfileIntW(kIniSection, L"Startup", g_cfg.startup, ConfigIniPath());
    g_cfg.trace = GetPrivateProfileIntW(kIniSection, L"Trace", 0, ConfigIniPath());
    g_cfg.iconSize = GetPrivateProfileIntW(kIniSection, L"IconSize", 0, ConfigIniPath());
    g_cfg.opacity = GetPrivateProfileIntW(kIniSection, L"Opacity", 100, ConfigIniPath());
    g_cfg.actionCount = GetPrivateProfileIntW(kIniSection, L"ActionCount", 2, ConfigIniPath());

    if (g_cfg.triggerMode != TRIGGER_MODE_SELECTION_HOOK) {
        g_cfg.triggerMode = TRIGGER_MODE_LEGACY;
    }
    if (g_cfg.iconSize != 0) g_cfg.iconSize = 1;
    if (g_cfg.opacity < 60)  g_cfg.opacity = 60;
    if (g_cfg.opacity > 100) g_cfg.opacity = 100;
    if (g_cfg.actionCount < 1) g_cfg.actionCount = 1;
    if (g_cfg.actionCount > MAX_ACTIONS) g_cfg.actionCount = MAX_ACTIONS;

    for (i = 0; i < MAX_ACTIONS; i++) {
        TCHAR key[32];
        wsprintfW(key, L"Action%d", i);
        if (GetPrivateProfileStringW(kIniSection, key, L"", buf, (int)(sizeof(buf) / sizeof(buf[0])),
                                     ConfigIniPath()) > 0) {
            StrToCombo(buf, &g_cfg.actions[i]);
        }
        wsprintfW(key, L"ActionIcon%d", i);
        g_cfg.actions[i].icon = GetPrivateProfileIntW(kIniSection, key,
                                                      g_cfg.actions[i].icon,
                                                      ConfigIniPath());
        if (g_cfg.actions[i].icon != ICON_SCREENSHOT) {
            g_cfg.actions[i].icon = ICON_TRANSLATION;
        }
        wsprintfW(key, L"ActionIconFile%d", i);
        pathBuf[0] = 0;
        GetPrivateProfileStringW(kIniSection, key, L"", pathBuf, MAX_PATH, ConfigIniPath());
        lstrcpynW(g_cfg.actions[i].iconPath, pathBuf, MAX_PATH);
    }
}

BOOL ConfigSave(void)
{
    TCHAR buf[64];
    TCHAR pathBuf[MAX_PATH];
    int   i;
    BOOL  ok = TRUE;

#define WRITE_INI(key, value) \
    do { if (!WritePrivateProfileStringW(kIniSection, key, value, ConfigIniPath())) ok = FALSE; } while (0)

    WRITE_INI(L"Enabled", g_cfg.enabled ? L"1" : L"0");
    wsprintfW(buf, L"%d", g_cfg.triggerMode);
    WRITE_INI(L"TriggerMode", buf);
    WRITE_INI(L"DragSelect", g_cfg.dragSelect ? L"1" : L"0");
    WRITE_INI(L"ShowInFullscreen", g_cfg.showInFullscreen ? L"1" : L"0");
    WRITE_INI(L"Startup", g_cfg.startup ? L"1" : L"0");
    WRITE_INI(L"Trace", g_cfg.trace ? L"1" : L"0");
    wsprintfW(buf, L"%d", g_cfg.iconSize);
    WRITE_INI(L"IconSize", buf);
    wsprintfW(buf, L"%d", g_cfg.opacity);
    WRITE_INI(L"Opacity", buf);
    wsprintfW(buf, L"%d", g_cfg.actionCount);
    WRITE_INI(L"ActionCount", buf);

    for (i = 0; i < MAX_ACTIONS; i++) {
        TCHAR key[32];
        ComboToStr(&g_cfg.actions[i], buf, (int)(sizeof(buf) / sizeof(buf[0])));
        wsprintfW(key, L"Action%d", i);
        WRITE_INI(key, buf);
        wsprintfW(key, L"ActionIcon%d", i);
        wsprintfW(buf, L"%d", g_cfg.actions[i].icon);
        WRITE_INI(key, buf);
        wsprintfW(key, L"ActionIconFile%d", i);
        lstrcpynW(pathBuf, g_cfg.actions[i].iconPath, MAX_PATH);
        if (pathBuf[0]) {
            WRITE_INI(key, pathBuf);
        } else {
            WritePrivateProfileStringW(kIniSection, key, NULL, ConfigIniPath());
        }
    }

#undef WRITE_INI

    /* drop the keys of options that no longer exist */
    WritePrivateProfileStringW(kIniSection, L"ShowAlways",        NULL, ConfigIniPath());
    WritePrivateProfileStringW(kIniSection, L"TriggerDoubleClick", NULL, ConfigIniPath());
    WritePrivateProfileStringW(kIniSection, L"HideOnLeave",       NULL, ConfigIniPath());
    WritePrivateProfileStringW(kIniSection, L"AutoHideMs",        NULL, ConfigIniPath());
    if (!ConfigApplyStartup(g_cfg.startup)) ok = FALSE;
    return ok;
}
