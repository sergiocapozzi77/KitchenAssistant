#include "actions.h"
#include "lvgl.h"
#include "ui.h"
#include "styles.h"
#include "esp_log.h"
#include "vars.h"
#include "images.h"
#include "ProductsManager.h"
#include "ui_extensions.h"
#include "RecipeSuggestionsManager.h"
#include "filters_ui.h"

void set_tab_icon(lv_obj_t *tabview, uint32_t index, const void *img_src)
{
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_t *btn = lv_obj_get_child(tab_btns, index);

    if (!btn)
        return;

    // Remove existing children (label)
    uint32_t child_cnt = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < child_cnt; i++)
    {
        lv_obj_del(lv_obj_get_child(btn, 0));
    }

    // Add image
    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, img_src);
    lv_obj_center(img);
}

static void tabview_tab_changed_cb(lv_event_t *e)
{
    lv_obj_t *tabview = (lv_obj_t *)lv_event_get_target(e);
    uint32_t tab = lv_tabview_get_tab_active(tabview);

    if (tab == 0)
    {
        // Switching TO Products tab — destroy recipe widgets to free PSRAM thumbnails
        // Cancel any in-flight thumbnail fetches first
        s_thumb_generation++; // defined extern in ui_extensions.h
        lv_obj_clean(objects.recipes_list);
    }
    else if (tab == 1)
    {
        // Switching TO Recipes tab — rebuild from cached data (no network call)
        if (recipeSuggestionsManager.getSuggestionSize() > 0)
            recipeSuggestionsManager.showCurrentPageRecipes();
    }
}

void action_screen_loading(lv_event_t *e)
{
    // This function can be used to perform actions when the loading screen is shown
    ESP_LOGI("actions", "Loading screen shown");
    set_tab_icon(objects.tabview, 0, &img_shopping);
    set_tab_icon(objects.tabview, 1, &img_chef);

    lv_obj_add_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
    create_filter_panel(objects.filters_container);
    lv_obj_add_event_cb(objects.tabview, tabview_tab_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

void action_generate_recipe_click(lv_event_t *e)
{
    ESP_LOGI("actions", "Generate Recipe button clicked");
    log_filter_state();

    lv_lock();
    lv_tabview_set_active(objects.tabview, 1, LV_ANIM_OFF);
    lv_unlock();

    // Somewhere in initTasks() or after WiFi connects:
    recipeSuggestionsManager.reset();
    recipeSuggestionsManager.loadCurrentPage();
}

void action_snack_bar_hide_clicked(lv_event_t *e)
{
    lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
}

void action_recipe_suggestion_next(lv_event_t *e)
{
    recipeSuggestionsManager.loadNextPage();
}

void action_recipe_suggestion_prev(lv_event_t *e)
{
    recipeSuggestionsManager.loadPrevPage();
}

void action_products_reload_click(lv_event_t *e)
{
    productsManager.fetchProducts();
}

void action_product_edit_close(lv_event_t *e)
{
    close_product_edit_modal();
}

extern void action_product_edit_save(lv_event_t *e)
{
}