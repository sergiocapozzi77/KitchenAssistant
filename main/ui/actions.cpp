#include "actions.h"
#include "lvgl.h"
#include "ui.h"
#include "styles.h"
#include "esp_log.h"
#include "vars.h"
#include "images.h"
#include "ProductsManager.h"
#include "ProductService.h"
#include "ui_extensions.h"
#include "RecipeSuggestionsManager.h"
#include "filters_ui.h"
#include <algorithm>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

filter_panel_t products_panel;
filter_panel_t recipes_panel;

// Structure for barcode upsert task
struct BarcodeUpsertCtx
{
    std::string barcode;
    std::string name;
    std::string category;
};

static void barcode_upsert_task(void *arg)
{
    BarcodeUpsertCtx *ctx = (BarcodeUpsertCtx *)arg;
    if (ctx && !ctx->barcode.empty())
    {
        bool success = productService.upsertBarcode(ctx->barcode, ctx->name, ctx->category);
        if (success)
        {
            ESP_LOGI("actions", "Barcode upserted: %s", ctx->barcode.c_str());
        }
        else
        {
            ESP_LOGE("actions", "Failed to upsert barcode: %s", ctx->barcode.c_str());
        }
    }
    delete ctx;
    vTaskDelete(NULL);
}

static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_cancel_cb(lv_event_t *e)
{
    lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keywords_textarea_focused_cb(lv_event_t *e)
{
    // Get the textarea that was focused
    lv_obj_t *focused_textarea = (lv_obj_t *)lv_event_get_target(e);
    
    // Assign keyboard to this textarea
    lv_keyboard_set_textarea(objects.keywords_keyboard, focused_textarea);
    
    // Show keyboard and position at bottom of screen
    // Screen height 1280, keyboard height 299, tab bar 60
    // Calculate y position: 1280 - 299 - 60 = 921
    /// lv_obj_set_pos(objects.keywords_keyboard, 0, 921);
    lv_obj_clear_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keywords_textarea_defocused_cb(lv_event_t *e)
{
    // Hide keyboard when textarea loses focus
    lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void product_search_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *textarea = (lv_obj_t *)lv_event_get_target(e);
    const char *text = lv_textarea_get_text(textarea);
    if (!text) text = "";
    setProductSearchFilter(text);
    productsManager.populateProductList();
}

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
        // Hide keyboard when switching away from products tab
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
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

    init_products_filter_panel(&products_panel);
    create_filter_panel(&products_panel);

    init_recipes_filter_panel(&recipes_panel);
    create_filter_panel(&recipes_panel);

    lv_obj_add_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(objects.tabview, tabview_tab_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Keyboard initialization
    lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(objects.keywords_keyboard, keyboard_ready_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(objects.keywords_keyboard, keyboard_cancel_cb, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(objects.products_filters_panel__keywords_text, keywords_textarea_focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(objects.products_filters_panel__keywords_text, keywords_textarea_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(objects.recipes_filters_panel__keywords_text, keywords_textarea_focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(objects.recipes_filters_panel__keywords_text, keywords_textarea_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(objects.product_search_ta, keywords_textarea_focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(objects.product_search_ta, keywords_textarea_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(objects.product_search_ta, product_search_value_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

void action_main_screen_loaded(lv_event_t *e)
{
}

void action_update_recipes_from_filter_panel(lv_event_t *e)
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

static std::string index_to_category(int idx)
{
    static const char *categories[] = {
        "Baby", "Bakery", "Beverages", "Breakfast & Cereal", "Condiments & Dressing",
        "Cooking & Baking", "Dairy", "Deli", "Frozen Foods", "Grains",
        "Pasta & Sides", "Health & Personal Care", "Household & Cleaning", "Meat",
        "Pet Supplies", "Produce", "Seafood", "Snacks", "Soups & Canned Food",
        "Wine, Beer & Spirit", "Other"};
    if (idx < 0 || idx >= 21)
        idx = 20;
    return categories[idx];
}

void action_product_edit_save(lv_event_t *e)
{
    ESP_LOGI("actions", "Product edit save clicked");

    lv_obj_t *panel = objects.product_edit__product_edit_panel;

    std::string *rowId = static_cast<std::string *>(lv_obj_get_user_data(panel));
    if (!rowId)
    {
        ESP_LOGE("actions", "RowId not found in panel");
        return;
    }

    lv_obj_t *name_ta = objects.product_edit__product_edit_name_ta;         // product_edit_name_ta
    lv_obj_t *expiry_ta = objects.product_edit__product_edit_expiry_ta;     // product_edit_expiry_ta
    lv_obj_t *category_dd = objects.product_edit__product_edit_category_dd; // product_edit_category_dd
    lv_obj_t *frozen_cb = objects.product_edit__product_edit_frozen_cb;     // product_edit_frozen_cb

    if (!name_ta || !lv_obj_is_valid(name_ta) ||
        !expiry_ta || !lv_obj_is_valid(expiry_ta) ||
        !category_dd || !lv_obj_is_valid(category_dd) ||
        !frozen_cb || !lv_obj_is_valid(frozen_cb))
    {
        ESP_LOGE("actions", "One or more widgets invalid");
        return;
    }

    const char *name = lv_textarea_get_text(name_ta);
    const char *expiry = lv_textarea_get_text(expiry_ta);
    int cat_idx = lv_dropdown_get_selected(category_dd);
    std::string category = index_to_category(cat_idx);
    bool frozen = lv_obj_has_state(frozen_cb, LV_STATE_CHECKED);

    ESP_LOGI("actions", "Updating product %s: name='%s', expiry='%s', category='%s', frozen='%s'",
             rowId->c_str(), name, expiry, category.c_str(), frozen ? "true" : "false");

    Product product;
    product.rowId = *rowId;
    product.name = name ? name : "";
    product.expiry = expiry ? expiry : "";
    product.category = category;
    product.frozen = frozen;

    // Preserve quantity from existing product
    auto allProducts = productsManager.getAllProducts();
    auto it = std::find_if(allProducts.begin(), allProducts.end(),
                           [&rowId](const Product &p)
                           { return p.rowId == *rowId; });
    product.quantity = (it != allProducts.end()) ? it->quantity : 0;
    std::string barcode = (it != allProducts.end()) ? it->barcode : "";
    // NOTE: do NOT mutate allProducts — it's a local copy

    bool success = productService.updateProduct(product);
    if (success)
    {
        ESP_LOGI("actions", "Product updated successfully");
        showSnackbar("Product updated", 3000);
        productsManager.updateProduct(product);

        // Spawn barcode upsert task if barcode exists
        if (!barcode.empty())
        {
            BarcodeUpsertCtx *barcode_ctx = new BarcodeUpsertCtx{barcode, product.name, product.category};
            BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
                barcode_upsert_task, "BarcodeUpsert",
                16384, barcode_ctx, 3, NULL, tskNO_AFFINITY,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            
            if (ret != pdPASS)
            {
                ESP_LOGE("actions", "Failed to create barcode upsert task");
                delete barcode_ctx;
            }
        }

        // Defer close + refresh — we are still inside the button's event chain.
        // Destroying the modal now would free objects still on the call stack.
        lv_async_call([](void *)
                      {
            close_product_edit_modal();
            productsManager.populateProductList(); }, nullptr);
    }
    else
    {
        ESP_LOGE("actions", "Failed to update product");
        showSnackbar("Failed to update product", 5000);

        lv_async_call([](void *)
                      { close_product_edit_modal(); }, nullptr);
    }
}

void action_product_sort_value_changed(lv_event_t *e)
{
    productsManager.populateProductList();
}

void action_product_filter_change(lv_event_t *e)
{
    productsManager.populateProductList();
}

void action_product_edit_calendar(lv_event_t *e)
{
    ESP_LOGI("actions", "Product calendar");

    lv_obj_t *calendar = objects.product_edit__calendar_editproduct; // product_edit_name_ta

    // Set today's date
    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);
    lv_calendar_date_t today;
    today.year = tm_now->tm_year + 1900;
    today.month = tm_now->tm_mon + 1;
    today.day = tm_now->tm_mday;
    lv_calendar_set_today_date(calendar, today.year, today.month, today.day);
    lv_calendar_set_showed_date(calendar, today.year, today.month);

    lv_obj_clear_flag(calendar, LV_OBJ_FLAG_HIDDEN);
}

void action_edit_product_panel_clicked(lv_event_t *e)
{

    lv_obj_t *calendar = objects.product_edit__calendar_editproduct;

    lv_obj_add_flag(calendar, LV_OBJ_FLAG_HIDDEN);
}

void action_product_calendar_value_changed(lv_event_t *e)
{
    lv_obj_t *calendar = (lv_obj_t *)lv_event_get_current_target(e);

    ESP_LOGI("actions", "Product day selected");

    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(calendar, &date);

    // Format as YYYY-MM-DD
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", date.year, date.month, date.day);
    lv_obj_t *expiry_ta = objects.product_edit__product_edit_expiry_ta; // product_edit_expiry_ta
    lv_textarea_set_text(expiry_ta, buf);

    lv_obj_add_flag(calendar, LV_OBJ_FLAG_HIDDEN);
}

void action_product_edit_delete(lv_event_t *e)
{
    ESP_LOGI("actions", "Product edit delete clicked");

    lv_obj_t *panel = objects.product_edit__product_edit_panel;

    std::string *rowId = static_cast<std::string *>(lv_obj_get_user_data(panel));
    if (!rowId)
    {
        ESP_LOGE("actions", "RowId not found in panel");
        return;
    }

    bool success = productService.deleteProduct(*rowId);
    if (success)
    {
        ESP_LOGI("actions", "Product delete successfully");
        showSnackbar("Product deleted", 5000);
        productsManager.deleteProduct(*rowId);

        // Defer close + refresh — we are still inside the button's event chain.
        // Destroying the modal now would free objects still on the call stack.
        lv_async_call([](void *)
                      {
            close_product_edit_modal();
            productsManager.populateProductList(); }, nullptr);
    }
    else
    {
        ESP_LOGE("actions", "Failed to delete product");
        showSnackbar("Failed to update product", 5000);

        lv_async_call([](void *)
                      { close_product_edit_modal(); }, nullptr);
    }
}

void action_product_edit_frozen(lv_event_t *e)
{
    lv_obj_t *checkbox = (lv_obj_t *)lv_event_get_current_target(e);
    bool frozen = lv_obj_has_state(checkbox, LV_STATE_CHECKED);

    ESP_LOGI("actions", "Product frozen state changed: %s", frozen ? "true" : "false");

    // Store frozen state in checkbox's user data for later retrieval during save
    lv_obj_set_user_data(checkbox, (void *)frozen);
}

void action_recipes_filter_panel_toggle(lv_event_t *e)
{
    if (lv_obj_has_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
    }
}