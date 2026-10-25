#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_COOKBOOK_DETAIL = 2,
    SCREEN_ID_RECIPE_DETAIL = 3
} ScreensEnum;

typedef struct objects_t
{
    lv_obj_t *main;
    lv_obj_t *cookbook_detail;
    lv_obj_t *recipe_detail;

    lv_obj_t *tabview;
    lv_obj_t *tab_inventory;
    lv_obj_t *tab_cookbooks;
    lv_obj_t *tab_settings;

    // Inventory
    lv_obj_t *products_header_pnl;
    lv_obj_t *products_title_lbl;
    lv_obj_t *products_reload_btn;
    lv_obj_t *product_search_btn;
    lv_obj_t *product_search_ta;
    lv_obj_t *product_filter_pill;
    lv_obj_t *product_filter_lbl;
    lv_obj_t *product_sort_pill;
    lv_obj_t *product_sort_lbl;
    lv_obj_t *product_filter_chips;
    lv_obj_t *product_chip_all;
    lv_obj_t *product_chip_expiring;
    lv_obj_t *product_chip_expired;
    lv_obj_t *product_sort_chips;
    lv_obj_t *product_sort_chip_az;
    lv_obj_t *product_sort_chip_expiry;
    lv_obj_t *products_list;

    // Product edit modal
    lv_obj_t *product_edit_modal;
    lv_obj_t *product_edit_panel;
    lv_obj_t *product_edit_title;
    lv_obj_t *product_edit_name_ta;
    lv_obj_t *product_edit_expiry_ta;
    lv_obj_t *product_edit_category_dd;
    lv_obj_t *product_edit_qty_lbl;
    lv_obj_t *product_edit_qty_minus;
    lv_obj_t *product_edit_qty_plus;
    lv_obj_t *product_edit_frozen_cb;
    lv_obj_t *product_edit_delete_btn;
    lv_obj_t *product_edit_cancel_btn;
    lv_obj_t *product_edit_save_btn;

    // Cookbooks
    lv_obj_t *cookbooks_list;
    lv_obj_t *cookbooks_title_lbl;
    lv_obj_t *cookbooks_reload_btn;

    // Cookbook detail
    lv_obj_t *cookbook_detail_back_btn;
    lv_obj_t *cookbook_detail_title_lbl;
    lv_obj_t *cookbook_detail_count_lbl;
    lv_obj_t *cookbook_detail_list;
    lv_obj_t *cookbook_detail_spinner;
    lv_obj_t *cookbook_detail_state_lbl;
    lv_obj_t *cookbook_detail_retry_btn;

    // Recipe detail
    lv_obj_t *recipe_detail_back_btn;
    lv_obj_t *recipe_detail_hero;
    lv_obj_t *recipe_detail_title_lbl;
    lv_obj_t *recipe_detail_source_lbl;
    lv_obj_t *recipe_detail_time_lbl;
    lv_obj_t *recipe_detail_difficulty_lbl;
    lv_obj_t *recipe_detail_tabview;
    lv_obj_t *recipe_detail_ingredients_tab;
    lv_obj_t *recipe_detail_method_tab;

    // Settings
    lv_obj_t *current_wifi_lbl;
    lv_obj_t *settings_wifi_btn;

    // Overlays
    lv_obj_t *snackbar;
    lv_obj_t *snackbar_text;
    lv_obj_t *spinner;
    lv_obj_t *keyboard;
} objects_t;

extern objects_t objects;

void create_screens(void);

#ifdef __cplusplus
}
#endif
