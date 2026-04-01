#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_RECIPE_DETAIL = 2,
    _SCREEN_ID_LAST = 2
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *recipe_detail;
    lv_obj_t *tabview;
    lv_obj_t *products_header_pnl;
    lv_obj_t *product_filter_dropdown;
    lv_obj_t *product_sort_dropdown;
    lv_obj_t *products_reload_btn;
    lv_obj_t *product_search_ta;
    lv_obj_t *products_list;
    lv_obj_t *create_recipe_pnl;
    lv_obj_t *products_filters_panel;
    lv_obj_t *products_filters_panel__keywords_text;
    lv_obj_t *products_filters_panel__filters_panel;
    lv_obj_t *products_filters_panel__meal_type_dropdown;
    lv_obj_t *products_filters_panel__total_time_dropdown;
    lv_obj_t *products_filters_panel__diet_dropdown;
    lv_obj_t *products_filters_panel__difficulty_dropdown;
    lv_obj_t *products_filters_panel__cuisine_dropdown;
    lv_obj_t *products_filters_panel__calories_dropdown;
    lv_obj_t *products_filters_panel__source_dropdown;
    lv_obj_t *products_filters_panel__poducts_selected_cb;
    lv_obj_t *products_filters_panel__product_selected_lbl;
    lv_obj_t *generate_recipe_btn;
    lv_obj_t *product_edit_modal;
    lv_obj_t *product_edit;
    lv_obj_t *product_edit__product_edit_panel;
    lv_obj_t *product_edit__product_edit_close_btn;
    lv_obj_t *product_edit__product_edit_title_lbl;
    lv_obj_t *product_edit__obj0;
    lv_obj_t *product_edit__product_edit_name_ta;
    lv_obj_t *product_edit__obj1;
    lv_obj_t *product_edit__product_edit_expiry_ta;
    lv_obj_t *product_edit__obj2;
    lv_obj_t *product_edit__product_edit_category_dd;
    lv_obj_t *product_edit__product_edit_save_btn;
    lv_obj_t *product_edit__product_edit_cancel_btn;
    lv_obj_t *product_edit__product_edit_selectdate;
    lv_obj_t *product_edit__calendar_editproduct;
    lv_obj_t *product_edit__product_edit_delete_btn;
    lv_obj_t *product_edit__product_edit_frozen_cb;
    lv_obj_t *recipes_header_pnl;
    lv_obj_t *recipe_suggestion_prev_btn;
    lv_obj_t *recipe_suggestion_next_btn;
    lv_obj_t *recipes_list;
    lv_obj_t *recipe_list_filter_container;
    lv_obj_t *recipes_filters_panel;
    lv_obj_t *recipes_filters_panel__keywords_text;
    lv_obj_t *recipes_filters_panel__filters_panel;
    lv_obj_t *recipes_filters_panel__meal_type_dropdown;
    lv_obj_t *recipes_filters_panel__total_time_dropdown;
    lv_obj_t *recipes_filters_panel__diet_dropdown;
    lv_obj_t *recipes_filters_panel__difficulty_dropdown;
    lv_obj_t *recipes_filters_panel__cuisine_dropdown;
    lv_obj_t *recipes_filters_panel__calories_dropdown;
    lv_obj_t *recipes_filters_panel__source_dropdown;
    lv_obj_t *recipes_filters_panel__poducts_selected_cb;
    lv_obj_t *recipes_filters_panel__product_selected_lbl;
    lv_obj_t *recipe_filter_panel_update_bt;
    lv_obj_t *keywords_keyboard;
    lv_obj_t *spinner;
    lv_obj_t *snackbar;
    lv_obj_t *obj0;
    lv_obj_t *snackbar_text;
    lv_obj_t *obj1;
    lv_obj_t *root_container;
    lv_obj_t *top_bar;
    lv_obj_t *recipe_back_btn;
    lv_obj_t *obj2;
    lv_obj_t *recipe_title;
    lv_obj_t *recipe_header_img;
    lv_obj_t *meta_card;
    lv_obj_t *meta_item_time;
    lv_obj_t *obj3;
    lv_obj_t *recipe_total_time_val;
    lv_obj_t *obj4;
    lv_obj_t *meta_item_difficulty;
    lv_obj_t *obj5;
    lv_obj_t *recipe_difficulty_val;
    lv_obj_t *obj6;
    lv_obj_t *detail_tabview;
    lv_obj_t *recipe_ing_cont;
    lv_obj_t *recipe_method_cont;
    lv_obj_t *recipe_detail_spinner;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_recipe_detail();
void tick_screen_recipe_detail();

void create_user_widget_product_edit(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_product_edit(int startWidgetIndex);

void create_user_widget_filters_panel(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_filters_panel(int startWidgetIndex);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

// Color themes

enum Themes {
    THEME_ID_DEFAULT,
};
enum Colors {
    COLOR_ID_ACCENT,
    COLOR_ID_ACCENT_DARK,
    COLOR_ID_BORDER,
};
void change_color_theme(uint32_t themeIndex);
extern uint32_t theme_colors[1][3];
extern uint32_t active_theme_index;

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/