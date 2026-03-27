#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_product_edit_close(lv_event_t * e);
extern void action_product_edit_save(lv_event_t * e);
extern void action_screen_loading(lv_event_t * e);
extern void action_generate_recipe_click(lv_event_t * e);
extern void action_recipe_suggestion_next(lv_event_t * e);
extern void action_recipe_suggestion_prev(lv_event_t * e);
extern void action_snack_bar_hide_clicked(lv_event_t * e);
extern void action_products_reload_click(lv_event_t * e);
extern void action_product_sort_value_changed(lv_event_t * e);
extern void action_product_filter_change(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/