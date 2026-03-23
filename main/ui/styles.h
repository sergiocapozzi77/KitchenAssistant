#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: FlatButton
lv_style_t *get_style_flat_button_MAIN_DEFAULT();
void add_style_flat_button(lv_obj_t *obj);
void remove_style_flat_button(lv_obj_t *obj);

// Style: DropDownWithShadow
lv_style_t *get_style_drop_down_with_shadow_MAIN_DEFAULT();
void add_style_drop_down_with_shadow(lv_obj_t *obj);
void remove_style_drop_down_with_shadow(lv_obj_t *obj);

// Style: MainButton
lv_style_t *get_style_main_button_MAIN_DEFAULT();
void add_style_main_button(lv_obj_t *obj);
void remove_style_main_button(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/