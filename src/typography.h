/*
 *  Selection Menu - typography.h
 *  GitHub Primer-inspired semantic type ramp, converted to the 1/10-point
 *  units used by UiFont so Windows XP through Windows 11 share one scale.
 */
#ifndef SELECTIONMENU_TYPOGRAPHY_H
#define SELECTIONMENU_TYPOGRAPHY_H

/* 12px caption / 14px body / 16px subtitle / 20px title / 24px display */
#define TYPE_CAPTION_PT10   90
#define TYPE_BODY_PT10     105
#define TYPE_SUBTITLE_PT10 120
#define TYPE_TITLE_PT10    150
#define TYPE_DISPLAY_PT10  180

#define TYPE_CAPTION        UiFont(TYPE_CAPTION_PT10, 0)
#define TYPE_BODY           UiFont(TYPE_BODY_PT10, 0)
#define TYPE_BODY_STRONG    UiFont(TYPE_BODY_PT10, 1)
#define TYPE_SUBTITLE       UiFont(TYPE_SUBTITLE_PT10, 1)
#define TYPE_TITLE          UiFont(TYPE_TITLE_PT10, 1)
#define TYPE_DISPLAY        UiFont(TYPE_DISPLAY_PT10, 1)

#endif /* SELECTIONMENU_TYPOGRAPHY_H */
