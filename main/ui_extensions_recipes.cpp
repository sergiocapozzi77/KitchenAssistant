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
#include "esp_task_wdt.h"

static const char *TAG = "UIEXTENSIONS";

// === RECIPE LIST SPECIFIC STRUCTS ===

struct RecipeClickCtx
{
    RecipeSuggestion recipe;
};

// === CLEANUP CALLBACKS ===

static void free_recipe_click_ctx_cb(lv_event_t *e)
{
    delete (RecipeClickCtx *)lv_event_get_user_data(e);
}

// === EVENT CALLBACKS ===

static void recipe_card_click_cb(lv_event_t *e)
{
    RecipeClickCtx *ctx = (RecipeClickCtx *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    ESP_LOGI(TAG, "Recipe clicked");
    showRecipeDetailScreen(ctx->recipe);
}

// === HELPER FUNCTIONS ===

// === MAIN POPULATE FUNCTION ===

void populateRecipeList(lv_obj_t *root, const std::vector<RecipeSuggestion> &recipes)
{
    if (!root || !lv_obj_is_valid(root))
    {
        ESP_LOGE(TAG, "Root object is NULL or invalid");
        return;
    }

    // esp_task_wdt_reset();
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

    for (const auto &r : recipes)
    {
        // esp_task_wdt_reset();
        //  === CARD ===
        lv_obj_t *card = createRecipeCard(root, r);

        // Click handler — open detail screen
        RecipeClickCtx *rctx = new RecipeClickCtx{r};
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        make_children_bubble(card);
        lv_obj_add_event_cb(card, recipe_card_click_cb, LV_EVENT_CLICKED, rctx);
        lv_obj_add_event_cb(card, free_recipe_click_ctx_cb, LV_EVENT_DELETE, rctx);
    }
    // Restore scroll position
    lv_obj_scroll_to_y(root, scroll_y, LV_ANIM_OFF);

    lv_unlock();
}