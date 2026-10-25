#include "ui_theme.h"

#include "fonts.h"

namespace ui_theme
{

const lv_font_t *font_display = &ui_font_ext_font_montserrat_36;
const lv_font_t *font_title = &ui_font_ext_font_montserrat_26;
const lv_font_t *font_body = &ui_font_ext_font_montserrat_18;
const lv_font_t *font_small = &lv_font_montserrat_16;
const lv_font_t *font_icon = &lv_font_montserrat_26;
const lv_font_t *font_icon_small = &lv_font_montserrat_16;

namespace
{

lv_style_t s_card;
lv_style_t s_pill;
lv_style_t s_field;
lv_style_t s_primary;
lv_style_t s_secondary;
lv_style_t s_danger;
bool s_ready = false;

void init_style(lv_style_t *style)
{
    lv_style_init(style);
}

} // namespace

void init()
{
    if (s_ready)
        return;
    s_ready = true;

    init_style(&s_card);
    lv_style_set_bg_color(&s_card, lv_color_hex(UI_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, 24);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_border_color(&s_card, lv_color_hex(UI_COLOR_CARD_BORDER));
    lv_style_set_pad_all(&s_card, 0);
    lv_style_set_shadow_width(&s_card, 0);

    init_style(&s_pill);
    lv_style_set_bg_color(&s_pill, lv_color_hex(UI_COLOR_SURFACE_HIGH));
    lv_style_set_bg_opa(&s_pill, LV_OPA_COVER);
    lv_style_set_radius(&s_pill, 32);
    lv_style_set_border_width(&s_pill, 0);
    lv_style_set_pad_hor(&s_pill, 20);
    lv_style_set_pad_ver(&s_pill, 0);
    lv_style_set_shadow_width(&s_pill, 0);

    init_style(&s_field);
    lv_style_set_bg_color(&s_field, lv_color_hex(UI_COLOR_SURFACE_HIGH));
    lv_style_set_bg_opa(&s_field, LV_OPA_COVER);
    lv_style_set_radius(&s_field, 12);
    lv_style_set_border_width(&s_field, 1);
    lv_style_set_border_color(&s_field, lv_color_hex(UI_COLOR_FILTER_STROKE));
    lv_style_set_pad_hor(&s_field, 16);
    lv_style_set_pad_ver(&s_field, 14);
    lv_style_set_text_font(&s_field, font_body);
    lv_style_set_text_color(&s_field, lv_color_hex(UI_COLOR_TEXT_PRIMARY));

    init_style(&s_primary);
    lv_style_set_bg_color(&s_primary, lv_color_hex(UI_COLOR_ACCENT));
    lv_style_set_bg_opa(&s_primary, LV_OPA_COVER);
    lv_style_set_radius(&s_primary, 16);
    lv_style_set_border_width(&s_primary, 0);
    lv_style_set_shadow_width(&s_primary, 0);
    lv_style_set_text_color(&s_primary, lv_color_white());
    lv_style_set_text_font(&s_primary, font_body);

    init_style(&s_secondary);
    lv_style_set_bg_color(&s_secondary, lv_color_hex(UI_COLOR_SURFACE_HIGHEST));
    lv_style_set_bg_opa(&s_secondary, LV_OPA_COVER);
    lv_style_set_radius(&s_secondary, 16);
    lv_style_set_border_width(&s_secondary, 0);
    lv_style_set_shadow_width(&s_secondary, 0);
    lv_style_set_text_color(&s_secondary, lv_color_hex(UI_COLOR_TEXT_PRIMARY));
    lv_style_set_text_font(&s_secondary, font_body);

    init_style(&s_danger);
    lv_style_set_bg_color(&s_danger, lv_color_hex(UI_COLOR_ERROR_SURFACE));
    lv_style_set_bg_opa(&s_danger, LV_OPA_COVER);
    lv_style_set_radius(&s_danger, 16);
    lv_style_set_border_width(&s_danger, 0);
    lv_style_set_shadow_width(&s_danger, 0);
    lv_style_set_text_color(&s_danger, lv_color_hex(UI_COLOR_ERROR_TEXT));
    lv_style_set_text_font(&s_danger, font_body);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    return label;
}

lv_obj_t *make_badge(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t fg)
{
    lv_obj_t *badge = make_label(parent, text, font_body, fg);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_bg_color(badge, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge, 12, 0);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_pad_top(badge, 10, 0);
    return badge;
}

lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol, int size,
                           uint32_t bg)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);

    lv_obj_t *icon = lv_label_create(button);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, font_icon, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(icon);
    return button;
}

lv_obj_t *make_chip(lv_obj_t *parent, const char *text, bool active)
{
    lv_obj_t *chip = lv_button_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_radius(chip, 28, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_set_style_pad_hor(chip, 20, 0);
    lv_obj_set_style_pad_ver(chip, 0, 0);

    lv_obj_t *label = make_label(chip, text, font_body, UI_COLOR_TEXT_SECONDARY);
    lv_obj_center(label);
    set_chip_active(chip, active);
    return chip;
}

void style_card(lv_obj_t *obj) { lv_obj_add_style(obj, &s_card, 0); }
void style_pill(lv_obj_t *obj) { lv_obj_add_style(obj, &s_pill, 0); }
void style_field(lv_obj_t *obj) { lv_obj_add_style(obj, &s_field, 0); }
void style_primary_button(lv_obj_t *obj) { lv_obj_add_style(obj, &s_primary, 0); }
void style_secondary_button(lv_obj_t *obj) { lv_obj_add_style(obj, &s_secondary, 0); }
void style_danger_button(lv_obj_t *obj) { lv_obj_add_style(obj, &s_danger, 0); }

void set_chip_active(lv_obj_t *chip, bool active)
{
    lv_obj_set_style_bg_color(chip, lv_color_hex(UI_COLOR_SURFACE_HIGHEST), 0);
    lv_obj_set_style_bg_opa(chip, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(
        chip, lv_color_hex(active ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    if (label)
        lv_obj_set_style_text_color(
            label, lv_color_hex(active ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY), 0);
}

} // namespace ui_theme
