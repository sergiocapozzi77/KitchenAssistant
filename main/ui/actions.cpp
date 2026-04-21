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
#include "FavouritesManager.h"
#include "RecipeSuggestionsManager.h"
#include "filters_ui.h"
#include <algorithm>
#include <string>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "RecipeDetailService.h"
#include "RecipeStepsAggregationService.h"
#include "StepProgressBar.h"
#include "ui_extensions_internal.h"
#include "ui_extensions_recipe_steps.h"
#include "filters_ui.h"

filter_panel_t products_panel;
filter_panel_t recipes_panel;

// Favourites pagination state
static int favouritesCurrentPage = 1;
const int favouritesPageSize = 6;

static void updateFavouritesPaginationButtons()
{
    lv_lock();
    if (!objects.favourites_prev_btn || !lv_obj_is_valid(objects.favourites_prev_btn) ||
        !objects.favourites_next_btn || !lv_obj_is_valid(objects.favourites_next_btn))
    {
        lv_unlock();
        return;
    }

    // Update previous button
    if (favouritesCurrentPage > 1)
    {
        lv_obj_clear_state(objects.favourites_prev_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(objects.favourites_prev_btn, LV_STATE_DISABLED);
    }

    // Update next button
    std::vector<Favorite> favourites = favouritesManager.getFavourites();
    int totalPages = favourites.empty() ? 0 : ((favourites.size() + favouritesPageSize - 1) / favouritesPageSize);
    if (totalPages == 0 || favouritesCurrentPage < totalPages)
    {
        lv_obj_clear_state(objects.favourites_next_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(objects.favourites_next_btn, LV_STATE_DISABLED);
    }
    lv_unlock();
}

void showCurrentPageFavourites(bool force)
{
    if (!objects.favourites_list || !lv_obj_is_valid(objects.favourites_list))
    {
        ESP_LOGE("favourites", "favourites_list is invalid");
        return;
    }
    if (lv_obj_get_child_count(objects.favourites_list) > 0 && !force)
    {
        updateFavouritesPaginationButtons();
        return; // Don't repopulate if already populated (e.g. when switching tabs)
    }

    std::vector<Favorite> favourites = favouritesManager.getFavourites();

    int start = (favouritesCurrentPage - 1) * favouritesPageSize;
    int end = std::min(start + favouritesPageSize, (int)favourites.size());

    // If start is beyond the end of the vector, show empty page
    if (start >= favourites.size() || start < 0)
    {
        ESP_LOGI("favourites", "No favourites to show for page %d", favouritesCurrentPage);
        // Optionally adjust page number to last valid page
        if (favouritesCurrentPage > 1)
        {
            favouritesCurrentPage = (favourites.size() + favouritesPageSize - 1) / favouritesPageSize;
            if (favouritesCurrentPage < 1)
                favouritesCurrentPage = 1;
            start = (favouritesCurrentPage - 1) * favouritesPageSize;
            end = std::min(start + favouritesPageSize, (int)favourites.size());
        }
        else
        {
            // Empty list
            populateFavouritesList(objects.favourites_list, {});
            updateFavouritesPaginationButtons();
            return;
        }
    }

    std::vector<Favorite> pageItems(
        favourites.begin() + start,
        favourites.begin() + end);

    ESP_LOGI("favourites", "Showing favourites page %d, items %d-%d of %d",
             favouritesCurrentPage, start, end, (int)favourites.size());
    populateFavouritesList(objects.favourites_list, pageItems);
    updateFavouritesPaginationButtons();
}

// Structure for barcode upsert task
struct BarcodeUpsertCtx
{
    std::string barcode;
    std::string name;
    std::string category;
};

// Structure for product update task
struct ProductUpdateCtx
{
    Product product;
    std::string barcode;
    bool success = false;
};

// Structure for product delete task
struct ProductDeleteCtx
{
    std::string rowId;
    bool success = false;
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

static void product_update_ui_cb(void *arg)
{
    ProductUpdateCtx *ctx = (ProductUpdateCtx *)arg;
    if (!ctx)
        return;
    if (ctx->success)
    {
        ESP_LOGI("actions", "Product updated successfully");
        showSnackbar("Product updated", 3000);
        productsManager.updateProduct(ctx->product);
        // Spawn barcode upsert task if barcode exists
        if (!ctx->barcode.empty())
        {
            BarcodeUpsertCtx *barcode_ctx = new BarcodeUpsertCtx{ctx->barcode, ctx->product.name, ctx->product.category};
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
        close_product_edit_modal();
        productsManager.populateProductList();
    }
    else
    {
        ESP_LOGE("actions", "Failed to update product");
        showSnackbar("Failed to update product", 5000);
        close_product_edit_modal();
    }
    delete ctx;
}

static void product_delete_ui_cb(void *arg)
{
    ProductDeleteCtx *ctx = (ProductDeleteCtx *)arg;
    if (!ctx)
        return;
    if (ctx->success)
    {
        ESP_LOGI("actions", "Product deleted successfully");
        showSnackbar("Product deleted", 5000);
        productsManager.deleteProduct(ctx->rowId);
        close_product_edit_modal();
        productsManager.populateProductList();
    }
    else
    {
        ESP_LOGE("actions", "Failed to delete product");
        showSnackbar("Failed to delete product", 5000);
        close_product_edit_modal();
    }
    delete ctx;
}

static void product_update_task(void *arg)
{
    ProductUpdateCtx *ctx = (ProductUpdateCtx *)arg;
    if (ctx)
    {
        ctx->success = productService.updateProduct(ctx->product);
        lv_async_call(product_update_ui_cb, ctx);
    }
    vTaskDelete(NULL);
}

static void product_delete_task(void *arg)
{
    ProductDeleteCtx *ctx = (ProductDeleteCtx *)arg;
    if (ctx)
    {
        ctx->success = productService.deleteProduct(ctx->rowId);
        lv_async_call(product_delete_ui_cb, ctx);
    }
    vTaskDelete(NULL);
}

static void keyboard_ready_cb(lv_event_t *e)
{
    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_cancel_cb(lv_event_t *e)
{
    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keywords_textarea_focused_cb(lv_event_t *e)
{
    // Get the textarea that was focused
    lv_obj_t *focused_textarea = (lv_obj_t *)lv_event_get_target(e);

    // Assign keyboard to this textarea
    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_keyboard_set_textarea(objects.keywords_keyboard, focused_textarea);
        // Show keyboard and position at bottom of screen
        // Screen height 1280, keyboard height 299, tab bar 60
        // Calculate y position: 1280 - 299 - 60 = 921
        /// lv_obj_set_pos(objects.keywords_keyboard, 0, 921);
        lv_obj_clear_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keywords_textarea_defocused_cb(lv_event_t *e)
{
    // Hide keyboard when textarea loses focus
    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void product_search_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *textarea = (lv_obj_t *)lv_event_get_target(e);
    const char *text = lv_textarea_get_text(textarea);
    if (!text)
        text = "";
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
        lv_obj_clean(objects.favourites_list);
    }
    else if (tab == 1)
    {
        // Switching TO Recipes tab — rebuild from cached data (no network call)
        if (recipeSuggestionsManager.getSuggestionSize() > 0)
        {
            lv_obj_add_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
            recipeSuggestionsManager.showCurrentPageRecipes();
        }
        else
        {
            lv_obj_clear_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
        }
        // Hide keyboard when switching away from products tab
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
        // Cancel any in-flight thumbnail fetches for favourites (disabled to allow background downloads)
        // s_thumb_generation++;
        lv_obj_clean(objects.favourites_list);
    }
    else if (tab == 2)
    {
        // Switching TO Favourites tab — rebuild from cached favourites
        // Cancel any in-flight thumbnail fetches for recipes (disabled to allow background downloads)
        // s_thumb_generation++;
        lv_obj_clean(objects.recipes_list);
        // Hide keyboard when switching away from products tab
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
        showCurrentPageFavourites();
    }
}

void recipe_detail_back_cb(lv_event_t *e)
{
    lv_obj_t *prev = (lv_obj_t *)lv_event_get_user_data(e);
    if (prev && lv_obj_is_valid(prev))
        lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    else
        lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

void action_screen_loading(lv_event_t *e)
{
    // This function can be used to perform actions when the loading screen is shown
    ESP_LOGI("actions", "Loading screen shown");
    set_tab_icon(objects.tabview, 0, &img_shopping);
    set_tab_icon(objects.tabview, 1, &img_chef);
    set_tab_icon(objects.tabview, 2, &img_favourite);

    lv_obj_set_parent(objects.snackbar, lv_layer_top());
    lv_obj_move_foreground(objects.snackbar); // Ensure it's the front-most child of the top layer

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

    lv_obj_add_event_cb(objects.recipe_back_btn, recipe_detail_back_cb, LV_EVENT_CLICKED, lv_scr_act());
    lv_obj_add_event_cb(objects.phase_back_btn, recipe_detail_back_cb, LV_EVENT_CLICKED, objects.recipe_detail);
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
    lv_obj_add_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_add_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
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

void action_favourites_next(lv_event_t *e)
{
    std::vector<Favorite> favourites = favouritesManager.getFavourites();
    int totalPages = (favourites.size() + favouritesPageSize - 1) / favouritesPageSize;
    if (totalPages == 0)
        totalPages = 1; // at least one page even if empty
    if (favouritesCurrentPage < totalPages)
    {
        favouritesCurrentPage++;
        showCurrentPageFavourites(true);
    }
    updateFavouritesPaginationButtons();
}

void action_favourites_prev(lv_event_t *e)
{
    if (favouritesCurrentPage > 1)
    {
        favouritesCurrentPage--;
        showCurrentPageFavourites(true);
    }
    updateFavouritesPaginationButtons();
}

void action_products_reload_click(lv_event_t *e)
{
    productsManager.fetchProducts();
}

void action_favourites_reload_click(lv_event_t *e)
{
    lv_lock();
    lv_obj_clean(objects.favourites_list);
    lv_unlock();
    favouritesCurrentPage = 1;
    favouritesManager.startBackgroundFetch();
    updateFavouritesPaginationButtons();
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

    // Create async update task
    ProductUpdateCtx *ctx = new ProductUpdateCtx{product, barcode};
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        product_update_task, "ProductUpdate",
        16384, ctx, 3, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        ESP_LOGE("actions", "Failed to create product update task");
        delete ctx;
        showSnackbar("Failed to start update", 5000);
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

    // Create async delete task
    ProductDeleteCtx *ctx = new ProductDeleteCtx{*rowId};
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        product_delete_task, "ProductDelete",
        16384, ctx, 3, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        ESP_LOGE("actions", "Failed to create product delete task");
        delete ctx;
        showSnackbar("Failed to start delete", 5000);
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

void action_create_recipe_steps_click(lv_event_t *e)
{
    lv_scr_load_anim(objects.recipe_phase, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

    UIExtensionsRecipeSteps::clearCurrentRecipe();

    lv_obj_clear_flag(objects.phase_detail_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.step_progress, LV_OBJ_FLAG_HIDDEN);

    // Disable navigation buttons until recipe is loaded
    lv_lock();
    UIExtensionsRecipeSteps::updatePhaseNavigationButtons();
    lv_unlock();

    // Delegate the task creation to the new class method
    UIExtensionsRecipeSteps::createRecipeStepsTask();
}

void action_recipe_phase_next(lv_event_t *e)
{
    UIExtensionsRecipeSteps::navigateNext();
}

void action_recipe_phase_prev(lv_event_t *e)
{
    UIExtensionsRecipeSteps::navigatePrev();
}

void action_ingredients_factor(lv_event_t *e)
{
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    float factor = UIExtensionsRecipeSteps::getScalingFactor(); // default to current factor if not determined below
    if (target == objects.phase_ingredients_factor_1)
    {
        factor = 1.0f;
    }
    else if (target == objects.phase_ingredients_factor_minus)
    {
        if (factor < 0.2f) // prevent scaling to zero or negative
        {
            return;
        }

        factor = factor - 0.05f;
    }
    else if (target == objects.phase_ingredients_factor_plus)
    {
        factor = factor + 0.05f;
    }
    else
    {
        // fallback: try to interpret user data as float pointer (for backward compatibility)
        float *ptr = (float *)lv_event_get_user_data(e);
        if (ptr)
        {
            factor = *ptr;
        }
    }
    UIExtensionsRecipeSteps::applyScalingFactor(factor);
}

void action_create_recipe_close_click(lv_event_t *e)
{
    lv_obj_add_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
}

int find_index(const char *value, filter_option_t *options, int count)
{
    if (!value)
        return 0;
    for (int i = 0; i < count; i++)
    {
        if (strcmp(value, options[i].value) == 0)
            return i + 1;
    }
    return 0;
}

void action_generate_ai_recipes_click(lv_event_t *e)
{
    ESP_LOGI("actions", "Generate Recipe button clicked");
    log_filter_state();

    lv_lock();
    lv_obj_add_flag(objects.recipe_list_filter_container, LV_OBJ_FLAG_HIDDEN);
    lv_tabview_set_active(objects.tabview, 1, LV_ANIM_OFF);
    lv_dropdown_set_selected(objects.recipes_filters_panel__source_dropdown, find_index("ai-deepseek", source_options, source_count)); // Set source filter to AI DeepSeek
    lv_unlock();

    // Ensure filter state source is set to ai-deepseek
    filter_state_t* filterState = get_filter_state();
    filterState->source = (char*)"ai-deepseek";

    // Somewhere in initTasks() or after WiFi connects:
    recipeSuggestionsManager.reset();
    recipeSuggestionsManager.loadCurrentPage();
}