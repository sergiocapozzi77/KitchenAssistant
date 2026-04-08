#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: FlatButton
//

void init_style_flat_button_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_opa(style, 0);
    lv_style_set_bg_grad_opa(style, 0);
    lv_style_set_bg_main_opa(style, 0);
    lv_style_set_border_width(style, 2);
    lv_style_set_border_color(style, lv_color_hex(0xffe9ecef));
    lv_style_set_border_opa(style, 255);
};

lv_style_t *get_style_flat_button_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_flat_button_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_flat_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_flat_button_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_flat_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_flat_button_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: DropDownWithShadow
//

void init_style_drop_down_with_shadow_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_shadow_width(style, 15);
    lv_style_set_shadow_opa(style, 30);
    lv_style_set_border_color(style, lv_color_hex(theme_colors[active_theme_index][2]));
    lv_style_set_border_width(style, 1);
};

lv_style_t *get_style_drop_down_with_shadow_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_drop_down_with_shadow_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_drop_down_with_shadow(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_drop_down_with_shadow_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_drop_down_with_shadow(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_drop_down_with_shadow_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: MainButton
//

void init_style_main_button_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xffcbe4ff));
    lv_style_set_bg_grad_dir(style, LV_GRAD_DIR_VER);
    lv_style_set_bg_grad_color(style, lv_color_hex(0xff90c5ff));
    lv_style_set_text_color(style, lv_color_hex(theme_colors[active_theme_index][5]));
    lv_style_set_text_font(style, &lv_font_montserrat_26);
    lv_style_set_radius(style, 30);
    lv_style_set_border_width(style, 1);
    lv_style_set_border_color(style, lv_color_hex(0xff9aa5ff));
    lv_style_set_shadow_width(style, 20);
    lv_style_set_shadow_ofs_x(style, 2);
    lv_style_set_shadow_ofs_y(style, 5);
    lv_style_set_shadow_spread(style, 2);
};

lv_style_t *get_style_main_button_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_main_button_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_main_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_main_button_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_main_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_main_button_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: TextAreaWithShadow
//

void init_style_text_area_with_shadow_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_border_color(style, lv_color_hex(theme_colors[active_theme_index][2]));
    lv_style_set_border_width(style, 1);
    lv_style_set_shadow_width(style, 15);
    lv_style_set_shadow_opa(style, 30);
};

lv_style_t *get_style_text_area_with_shadow_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_text_area_with_shadow_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_text_area_with_shadow(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_text_area_with_shadow_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_text_area_with_shadow(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_text_area_with_shadow_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: CheckboxDefault
//

void init_style_checkbox_default_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_border_color(style, lv_color_hex(theme_colors[active_theme_index][6]));
};

lv_style_t *get_style_checkbox_default_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_checkbox_default_INDICATOR_DEFAULT(style);
    }
    return style;
};

void add_style_checkbox_default(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_checkbox_default_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

void remove_style_checkbox_default(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_checkbox_default_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_flat_button,
        add_style_drop_down_with_shadow,
        add_style_main_button,
        add_style_text_area_with_shadow,
        add_style_checkbox_default,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_flat_button,
        remove_style_drop_down_with_shadow,
        remove_style_main_button,
        remove_style_text_area_with_shadow,
        remove_style_checkbox_default,
    };
    remove_style_funcs[styleIndex](obj);
}