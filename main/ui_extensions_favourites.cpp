#include <vector>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "models.h"

static const char *TAG = "UIEXTENSIONS";

// === FAVOURITE CLICK CONTEXT ===

struct FavouriteClickCtx
{
    Favorite favourite;
};

// === CLEANUP CALLBACKS ===

static void free_favourite_click_ctx_cb(lv_event_t *e)
{
    delete (FavouriteClickCtx *)lv_event_get_user_data(e);
}

// === EVENT CALLBACKS ===

static void favourite_card_click_cb(lv_event_t *e)
{
    FavouriteClickCtx *ctx = (FavouriteClickCtx *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Favourite clicked: %s", ctx->favourite.name.c_str());

    if (!ctx)
        return;

    // Convert Favorite to minimal RecipeSuggestion
    RecipeSuggestion recipe;
    recipe.name = ctx->favourite.name;
    recipe.url = ctx->favourite.url;
    recipe.imageUrl = ctx->favourite.imageUrl;
    recipe.description = ctx->favourite.description;
    recipe.difficulty = ctx->favourite.difficulty;
    recipe.totalTime = ctx->favourite.totalTime;
    showRecipeDetailScreen(recipe);
}

// === HELPER FUNCTIONS (reused from recipes) ===


// === MAIN POPULATE FUNCTION ===

void populateFavouritesList(lv_obj_t *root, const std::vector<Favorite> &favourites)
{
    if (!root)
    {
        ESP_LOGE(TAG, "Root object is NULL");
        return;
    }

    s_thumb_generation++;

    lv_lock();
    init_styles();
    // Capture scroll position before cleaning
    lv_coord_t scroll_y = lv_obj_get_scroll_y(root);
    lv_obj_clean(root);

    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 16, 0);

    std::vector<ThumbContext *> pending_thumbs;

    for (const auto &fav : favourites)
    {
        // === CARD ===
        lv_obj_t *card = createRecipeCard(root, fav, pending_thumbs);

        // Click handler — open detail screen
        FavouriteClickCtx *fctx = new FavouriteClickCtx{fav};
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        make_children_bubble(card);
        lv_obj_add_event_cb(card, favourite_card_click_cb, LV_EVENT_CLICKED, fctx);
        lv_obj_add_event_cb(card, free_favourite_click_ctx_cb, LV_EVENT_DELETE, fctx);

    }
    // Restore scroll position
    lv_obj_scroll_to_y(root, scroll_y, LV_ANIM_OFF);

    lv_unlock();

    if (!pending_thumbs.empty())
    {
        ThumbWorkerCtx *wctx = new ThumbWorkerCtx{pending_thumbs, 112, 112, true, s_thumb_generation};
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
            thumb_worker_task, "thumb_worker", 8192, wctx, 2, NULL, 1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create thumb worker task");
            for (auto *tctx : pending_thumbs)
                delete tctx;
            delete wctx;
        }
    }
}