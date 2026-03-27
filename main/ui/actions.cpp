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

static void keyboard_ready_cb(lv_event_t *e)
{
    lv_obj_add_flag(objects.obj3, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_cancel_cb(lv_event_t *e)
{
    lv_obj_add_flag(objects.obj3, LV_OBJ_FLAG_HIDDEN);
}

static void keywords_textarea_focused_cb(lv_event_t *e)
{
    // Show keyboard and position at bottom of screen
    // Screen height 1280, keyboard height 299, tab bar 60
    // Calculate y position: 1280 - 299 - 60 = 921
    /// lv_obj_set_pos(objects.obj3, 0, 921);
    lv_obj_clear_flag(objects.obj3, LV_OBJ_FLAG_HIDDEN);
}

static void keywords_textarea_defocused_cb(lv_event_t *e)
{
    // Hide keyboard when textarea loses focus
    lv_obj_add_flag(objects.obj3, LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_add_flag(objects.obj3, LV_OBJ_FLAG_HIDDEN);
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
    create_filter_panel();
    lv_obj_add_event_cb(objects.tabview, tabview_tab_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Keyboard initialization
    lv_obj_add_flag(objects.obj3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(objects.obj3, keyboard_ready_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(objects.obj3, keyboard_cancel_cb, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(objects.products_filters_panel__keywords_text, keywords_textarea_focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(objects.products_filters_panel__keywords_text, keywords_textarea_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
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

static std::string index_to_category(int idx) {
    static const char* categories[] = {
        "Baby", "Bakery", "Beverages", "Breakfast & Cereal", "Condiments & Dressing",
        "Cooking & Baking", "Dairy", "Deli", "Frozen Foods", "Grains",
        "Pasta & Sides", "Health & Personal Care", "Household & Cleaning", "Meat",
        "Pet Supplies", "Produce", "Seafood", "Snacks", "Soups & Canned Food",
        "Wine, Beer & Spirit", "Other"
    };
    if (idx < 0 || idx >= 21) idx = 20;
    return categories[idx];
}

void action_product_edit_save(lv_event_t *e)
{
    ESP_LOGI("actions", "Product edit save clicked");

    // Get modal widgets (startWidgetIndex = 0)
    lv_obj_t* panel = ((lv_obj_t **)&objects)[0]; // product_edit_panel
    if (!panel || !lv_obj_is_valid(panel)) {
        ESP_LOGE("actions", "Panel not found");
        return;
    }

    // Get rowId from panel user data (set by edit_btn_cb)
    std::string* rowId = static_cast<std::string*>(lv_obj_get_user_data(panel));
    if (!rowId) {
        ESP_LOGE("actions", "RowId not found in panel");
        return;
    }

    lv_obj_t* name_ta = ((lv_obj_t **)&objects)[4];  // product_edit_name_ta
    lv_obj_t* expiry_ta = ((lv_obj_t **)&objects)[6]; // product_edit_expiry_ta
    lv_obj_t* category_dd = ((lv_obj_t **)&objects)[8]; // product_edit_category_dd

    if (!name_ta || !lv_obj_is_valid(name_ta) ||
        !expiry_ta || !lv_obj_is_valid(expiry_ta) ||
        !category_dd || !lv_obj_is_valid(category_dd)) {
        ESP_LOGE("actions", "One or more widgets invalid");
        return;
    }

    // Read values
    const char* name = lv_textarea_get_text(name_ta);
    const char* expiry = lv_textarea_get_text(expiry_ta);
    int cat_idx = lv_dropdown_get_selected(category_dd);
    std::string category = index_to_category(cat_idx);

    ESP_LOGI("actions", "Updating product %s: name='%s', expiry='%s', category='%s'",
             rowId->c_str(), name, expiry, category.c_str());

    // Create updated product, preserving existing quantity
    Product product;
    product.rowId = *rowId;
    product.name = name ? name : "";
    product.expiry = expiry ? expiry : "";
    product.category = category;

    // Preserve quantity from existing product
    auto allProducts = productsManager.getAllProducts();
    auto it = std::find_if(allProducts.begin(), allProducts.end(),
                           [&rowId](const Product& p) { return p.rowId == *rowId; });
    if (it != allProducts.end()) {
        product.quantity = it->quantity;
    } else {
        product.quantity = 0; // default if not found
    }

    // Call service to update
    bool success = productService.updateProduct(product);
    if (success) {
        ESP_LOGI("actions", "Product updated successfully");
        showSnackbar("Product updated", 3000);
        // Refresh product list
        productsManager.fetchProducts();
    } else {
        ESP_LOGE("actions", "Failed to update product");
        showSnackbar("Failed to update product", 5000);
    }

    // Close modal
    close_product_edit_modal();
}

void action_product_sort_value_changed(lv_event_t *e)
{
    productsManager.pupulateProductList();
}

void action_product_filter_change(lv_event_t *e)
{
    productsManager.pupulateProductList();
}