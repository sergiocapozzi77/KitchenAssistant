#pragma once

#include "lvgl.h"

// Light Material 3 palette, copied from KitchenAssistantApp/src/theme/variables.scss.
#define UI_COLOR_PAGE_BG 0xF8F6FC
#define UI_COLOR_SURFACE 0xFFFFFF
#define UI_COLOR_SURFACE_HIGH 0xF0EDF7
#define UI_COLOR_SURFACE_HIGHEST 0xE8E4F0
#define UI_COLOR_PRODUCT_ROW 0xEEEBF5
#define UI_COLOR_FILTER_BG 0xF5F2FC
#define UI_COLOR_FILTER_STROKE 0xD8D0E8
#define UI_COLOR_CARD_BORDER 0xE5DCF2
#define UI_COLOR_TEXT_PRIMARY 0x1A1A3E
#define UI_COLOR_TEXT_SECONDARY 0x4A4A6A
#define UI_COLOR_TEXT_MUTED 0x8888AA
#define UI_COLOR_ACCENT 0x7C5CBF
#define UI_COLOR_ERROR_SURFACE 0xFFE8E8
#define UI_COLOR_ERROR_TEXT 0xCC3333
#define UI_COLOR_TODAY_SURFACE 0xFFE0E0
#define UI_COLOR_TODAY_TEXT 0xCC3333
#define UI_COLOR_DAYSLEFT_SURFACE 0xFFF5E0
#define UI_COLOR_DAYSLEFT_TEXT 0x8B7A00
#define UI_COLOR_TIME_BG 0xFFF3D6
#define UI_COLOR_TIME_TEXT 0x8B6E00
#define UI_COLOR_DIFF_BG 0xE8F3E9
#define UI_COLOR_DIFF_TEXT 0x2E7D32
#define UI_COLOR_CUISINE_BG 0xF3E6F5
#define UI_COLOR_CUISINE_TEXT 0x7B1FA2

namespace ui_theme
{

extern const lv_font_t *font_display; // ext 36
extern const lv_font_t *font_title;   // ext 26
extern const lv_font_t *font_body;    // ext 18
extern const lv_font_t *font_small;   // built-in 16
extern const lv_font_t *font_icon;    // built-in 26, carries LV_SYMBOL_*
extern const lv_font_t *font_icon_small;

void init();

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     uint32_t color);
lv_obj_t *make_badge(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t fg);
lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol, int size = 72,
                           uint32_t bg = UI_COLOR_SURFACE_HIGH);
lv_obj_t *make_chip(lv_obj_t *parent, const char *text, bool active);

void style_card(lv_obj_t *obj);
void style_pill(lv_obj_t *obj);
void style_field(lv_obj_t *obj);
void style_primary_button(lv_obj_t *obj);
void style_secondary_button(lv_obj_t *obj);
void style_danger_button(lv_obj_t *obj);
void set_chip_active(lv_obj_t *chip, bool active);

} // namespace ui_theme
