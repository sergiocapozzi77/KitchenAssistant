#include "actions.h"

#include "CookbooksManager.h"
#include "ProductService.h"
#include "ProductsManager.h"
#include "RecipesManager.h"
#include "WiFiSettingsUI.h"
#include "screens.h"
#include "thumbnail_manager.h"
#include "ui_extensions.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

static const char *TAG = "ACTIONS";

namespace
{

constexpr uint32_t kTaskStack = 16384;
constexpr UBaseType_t kTaskPriority = 5;
constexpr BaseType_t kTaskCore = 1;

std::atomic<bool> s_reload_running{false};
std::atomic<bool> s_cookbooks_running{false};
std::mutex s_cookbook_mutex;
std::set<std::string> s_cookbook_inflight;
lv_timer_t *s_search_timer = nullptr;

struct ProductUpdateCtx
{
    Product product;
};

struct ProductDeleteCtx
{
    std::string id;
};

bool spawn_task(TaskFunction_t fn, const char *name, void *arg)
{
    BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
        fn, name, kTaskStack, arg, kTaskPriority, nullptr, kTaskCore,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task %s", name);
        return false;
    }
    return true;
}

void products_reload_task(void *)
{
    productsManager.fetchProductsSync();

    lv_lock();
    s_reload_running.store(false);
    hideSpinner();
    lv_unlock();

    vTaskDelete(nullptr);
}

void product_update_task(void *arg)
{
    auto *ctx = static_cast<ProductUpdateCtx *>(arg);

    bool ok = productService.updateProduct(ctx->product);
    if (ok && !ctx->product.barcode.empty())
    {
        productService.upsertBarcode(ctx->product.barcode, ctx->product.name,
                                     ctx->product.category);
    }

    lv_lock();
    if (ok)
    {
        productsManager.updateProduct(ctx->product);
        close_product_edit_modal();
        productsManager.populateProductList();
        showSnackbar("Product updated", 2500);
    }
    else
    {
        showSnackbar("Couldn't update product", 4000);
        if (objects.product_edit_save_btn)
            lv_obj_clear_state(objects.product_edit_save_btn, LV_STATE_DISABLED);
    }
    hideSpinner();
    lv_unlock();

    delete ctx;
    vTaskDelete(nullptr);
}

void product_delete_task(void *arg)
{
    auto *ctx = static_cast<ProductDeleteCtx *>(arg);

    bool ok = productService.deleteProduct(ctx->id);

    lv_lock();
    if (ok)
    {
        productsManager.deleteProduct(ctx->id);
        productsManager.populateProductList();
        showSnackbar("Deleted", 2000);
    }
    else
    {
        showSnackbar("Couldn't delete product", 4000);
    }
    lv_unlock();

    delete ctx;
    vTaskDelete(nullptr);
}

void search_timer_cb(lv_timer_t *)
{
    s_search_timer = nullptr;
    if (!objects.product_search_ta || !lv_obj_is_valid(objects.product_search_ta))
        return;
    setProductSearchFilter(lv_textarea_get_text(objects.product_search_ta));
    productsManager.populateProductList();
}

void close_search()
{
    if (objects.product_search_ta)
    {
        lv_textarea_set_text(objects.product_search_ta, "");
        lv_obj_add_flag(objects.product_search_ta, LV_OBJ_FLAG_HIDDEN);
    }
    if (objects.keyboard)
        lv_obj_add_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
    setProductSearchFilter("");
    productsManager.populateProductList();
}

struct CookbooksLoadCtx
{
    bool refresh = false;
};

void cookbooks_reload_task(void *arg)
{
    auto *ctx = static_cast<CookbooksLoadCtx *>(arg);
    bool refresh = ctx->refresh;
    delete ctx;

    bool ok = refresh ? cookbooksManager.refreshSync()
                      : cookbooksManager.fetchSync();

    lv_lock();
    if (ok)
        populateCookbooksUi();
    else
        showSnackbar("Couldn't load cookbooks", 4000);
    s_cookbooks_running.store(false);
    lv_unlock();

    vTaskDelete(nullptr);
}

struct CookbookFetchCtx
{
    std::string id;
};

void cookbook_recipes_task(void *arg)
{
    auto *ctx = static_cast<CookbookFetchCtx *>(arg);

    recipesManager.fetchForCookbookSync(ctx->id);

    lv_lock();
    {
        std::lock_guard<std::mutex> lock(s_cookbook_mutex);
        s_cookbook_inflight.erase(ctx->id);
    }
    // Discard the result when the user already left this cookbook.
    if (openCookbookId() == ctx->id)
        populateCookbookRecipesUi(ctx->id);
    lv_unlock();

    delete ctx;
    vTaskDelete(nullptr);
}

} // namespace

void action_products_reload(lv_event_t *)
{
    if (s_reload_running.exchange(true))
        return;

    if (!spawn_task(products_reload_task, "products_reload", nullptr))
    {
        s_reload_running.store(false);
        showSnackbar("Couldn't start reload", 3000);
        return;
    }
    showSpinner();
}

void action_product_search_open(lv_event_t *)
{
    if (!objects.product_search_ta || !lv_obj_is_valid(objects.product_search_ta))
        return;

    if (lv_obj_has_flag(objects.product_search_ta, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(objects.product_search_ta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(objects.product_search_ta);
        if (objects.keyboard && lv_obj_is_valid(objects.keyboard))
        {
            lv_keyboard_set_textarea(objects.keyboard, objects.product_search_ta);
            lv_obj_clear_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(objects.keyboard);
        }
    }
    else
    {
        close_search();
    }
}

void action_product_search_changed(lv_event_t *)
{
    if (s_search_timer)
        lv_timer_delete(s_search_timer);
    s_search_timer = lv_timer_create(search_timer_cb, 300, nullptr);
    lv_timer_set_repeat_count(s_search_timer, 1);
}

void action_product_filter_toggle(lv_event_t *)
{
    if (!objects.product_filter_chips)
        return;
    if (lv_obj_has_flag(objects.product_filter_chips, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(objects.product_filter_chips, LV_OBJ_FLAG_HIDDEN);
        if (objects.product_sort_chips)
            lv_obj_add_flag(objects.product_sort_chips, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(objects.product_filter_chips, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_product_sort_toggle(lv_event_t *)
{
    if (!objects.product_sort_chips)
        return;
    if (lv_obj_has_flag(objects.product_sort_chips, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(objects.product_sort_chips, LV_OBJ_FLAG_HIDDEN);
        if (objects.product_filter_chips)
            lv_obj_add_flag(objects.product_filter_chips, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(objects.product_sort_chips, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_product_filter_chip(lv_event_t *event)
{
    auto filter = static_cast<ProductFilter>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    setProductFilter(filter);
    if (objects.product_filter_chips)
        lv_obj_add_flag(objects.product_filter_chips, LV_OBJ_FLAG_HIDDEN);
}

void action_product_sort_chip(lv_event_t *event)
{
    auto sort = static_cast<ProductSort>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    setProductSort(sort);
    if (objects.product_sort_chips)
        lv_obj_add_flag(objects.product_sort_chips, LV_OBJ_FLAG_HIDDEN);
}

void action_product_edit_save(lv_event_t *)
{
    Product product;
    if (!get_product_modal_values(product))
        return;

    auto *ctx = new ProductUpdateCtx{product};
    if (objects.product_edit_save_btn)
        lv_obj_add_state(objects.product_edit_save_btn, LV_STATE_DISABLED);
    showSpinner();

    if (!spawn_task(product_update_task, "product_update", ctx))
    {
        delete ctx;
        if (objects.product_edit_save_btn)
            lv_obj_clear_state(objects.product_edit_save_btn, LV_STATE_DISABLED);
        hideSpinner();
        showSnackbar("Couldn't start update", 3000);
    }
}

void action_product_edit_cancel(lv_event_t *)
{
    close_product_edit_modal();
}

void action_product_edit_delete(lv_event_t *)
{
    std::string id;
    if (!product_modal_id(id))
        return;
    close_product_edit_modal();
    request_product_delete(id);
}

void action_product_qty_minus(lv_event_t *)
{
    adjust_product_modal_quantity(-1);
}

void action_product_qty_plus(lv_event_t *)
{
    adjust_product_modal_quantity(1);
}

void action_settings_wifi(lv_event_t *)
{
    WiFiSettingsUI::toggleDialog();
}

void request_product_delete(const std::string &id)
{
    if (id.empty())
        return;

    auto *ctx = new ProductDeleteCtx{id};
    if (!spawn_task(product_delete_task, "product_delete", ctx))
    {
        delete ctx;
        showSnackbar("Couldn't start delete", 3000);
    }
}

void request_cookbooks_load()
{
    if (cookbooksManager.loaded())
    {
        if (objects.cookbooks_list && lv_obj_is_valid(objects.cookbooks_list))
            populateCookbooksUi();
        return;
    }

    if (s_cookbooks_running.exchange(true))
        return;

    auto *ctx = new CookbooksLoadCtx{false};
    if (!spawn_task(cookbooks_reload_task, "cookbooks_load", ctx))
    {
        delete ctx;
        s_cookbooks_running.store(false);
        showSnackbar("Couldn't load cookbooks", 3000);
    }
}

void action_cookbooks_reload(lv_event_t *)
{
    if (s_cookbooks_running.exchange(true))
        return;

    auto *ctx = new CookbooksLoadCtx{true};
    if (!spawn_task(cookbooks_reload_task, "cookbooks_reload", ctx))
    {
        delete ctx;
        s_cookbooks_running.store(false);
        showSnackbar("Couldn't load cookbooks", 3000);
    }
}

void action_cookbook_open(lv_event_t *event)
{
    auto *id = static_cast<std::string *>(lv_event_get_user_data(event));
    if (!id || id->empty())
        return;

    showCookbookDetail(*id);
    if (recipesManager.hasCookbook(*id))
        populateCookbookRecipesUi(*id);
    else
        request_cookbook_recipes(*id);
}

void action_cookbook_back(lv_event_t *)
{
    leaveCookbookDetail();
    if (objects.main && lv_obj_is_valid(objects.main))
        lv_screen_load_anim(objects.main, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0,
                            false);
}

void action_cookbook_retry(lv_event_t *)
{
    std::string id = openCookbookId();
    if (id.empty())
        return;

    retryCookbookDetail();
    request_cookbook_recipes(id);
}

void action_recipe_open(lv_event_t *event)
{
    auto *recipe = static_cast<Recipe *>(lv_event_get_user_data(event));
    if (!recipe)
        return;
    showRecipeDetailScreen(*recipe);
}

void action_recipe_back(lv_event_t *)
{
    thumb_queue_cancel_all();
    if (objects.main && lv_obj_is_valid(objects.main))
        lv_screen_load_anim(objects.main, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0,
                            false);
}

void request_cookbook_recipes(const std::string &cookbookId)
{
    if (cookbookId.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(s_cookbook_mutex);
        if (!s_cookbook_inflight.insert(cookbookId).second)
            return;
    }

    auto *ctx = new CookbookFetchCtx{cookbookId};
    if (!spawn_task(cookbook_recipes_task, "cookbook_recipes", ctx))
    {
        delete ctx;
        {
            std::lock_guard<std::mutex> lock(s_cookbook_mutex);
            s_cookbook_inflight.erase(cookbookId);
        }
        showSnackbar("Couldn't load recipes", 3000);
        showCookbookDetailFailure(cookbookId);
    }
}
