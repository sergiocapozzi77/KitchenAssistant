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
    // Other fields left empty - will be fetched by detail screen if needed
    showRecipeDetailScreen(recipe);
}

// === HELPER FUNCTIONS (reused from recipes) ===

static void make_children_bubble(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++)
        make_children_bubble(lv_obj_get_child(obj, i));
}

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
        lv_obj_t *card = lv_obj_create(root);
        lv_obj_add_style(card, &style_card, 0);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_set_style_pad_column(card, 12, 0);

        // === THUMBNAIL PLACEHOLDER ===
        lv_obj_t *thumb = lv_image_create(card);
        lv_obj_set_size(thumb, 112, 112);
        lv_obj_set_style_bg_color(thumb, lv_color_hex(0xDEE2E6), 0); // grey until loaded
        lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(thumb, 8, 0);
        lv_obj_set_style_border_width(thumb, 0, 0);
        lv_image_set_inner_align(thumb, LV_IMAGE_ALIGN_COVER);

        if (!fav.imageUrl.empty())
        {
            ESP_LOGI(TAG, "Scheduling thumb fetch for favourite: %s", fav.name.c_str());
            ThumbContext *tctx = new ThumbContext{thumb, fav.imageUrl, s_thumb_generation};

            ESP_LOGI(TAG, ">>> about to create task, internal heap: %" PRIu32,
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

            pending_thumbs.push_back(tctx);
        }
        else
        {
            ESP_LOGI(TAG, "No image URL for favourite: %s", fav.name.c_str());
        }

        // === RIGHT COLUMN ===
        lv_obj_t *info = lv_obj_create(card);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_set_style_border_width(info, 0, 0);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_row(info, 8, 0);

        // Title
        lv_obj_t *title = lv_label_create(info);
        lv_label_set_text(title, fav.name.c_str());
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_color(title, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

        // Description
        if (!fav.description.empty())
        {
            std::string cleaned = fav.description;
            // Remove <p> and </p> tags
            size_t pos = 0;
            while ((pos = cleaned.find("<p>", pos)) != std::string::npos)
                cleaned.erase(pos, 3);
            pos = 0;
            while ((pos = cleaned.find("</p>", pos)) != std::string::npos)
                cleaned.erase(pos, 4);
            lv_obj_t *desc = lv_label_create(info);
            lv_label_set_text(desc, cleaned.c_str());
            lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(desc, lv_pct(100));
            lv_obj_set_style_text_color(desc, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(desc, &ui_font_ext_font_montserrat_18, 0);
        }

        // === BADGES ROW (time + difficulty) ===
        lv_obj_t *badges = lv_obj_create(info);
        lv_obj_set_width(badges, lv_pct(100));
        lv_obj_set_height(badges, LV_SIZE_CONTENT);
        lv_obj_clear_flag(badges, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badges, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(badges, 0, 0);
        lv_obj_set_style_border_width(badges, 0, 0);
        lv_obj_set_style_bg_opa(badges, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_column(badges, 10, 0);

        auto make_badge = [&](lv_obj_t *parent, const char *symbol, const std::string &text)
        {
            if (text.empty())
                return;
            lv_obj_t *wrap = lv_obj_create(parent);
            lv_obj_set_height(wrap, LV_SIZE_CONTENT);
            lv_obj_set_width(wrap, LV_SIZE_CONTENT);
            lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(wrap, 0, 0);
            lv_obj_set_style_border_width(wrap, 0, 0);
            lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_column(wrap, 4, 0);

            lv_obj_t *ico = lv_label_create(wrap);
            lv_label_set_text(ico, symbol);
            lv_obj_set_style_text_color(ico, lv_color_hex(0x212529), 0);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);

            lv_obj_t *lbl = lv_label_create(wrap);
            lv_label_set_text(lbl, text.c_str());
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x495057), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        };

        make_badge(badges, LV_SYMBOL_LOOP, fav.totalTime); // clock-like symbol
        make_badge(badges, LV_SYMBOL_EDIT, fav.difficulty);

        // Click handler — open detail screen
        FavouriteClickCtx *fctx = new FavouriteClickCtx{fav};
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        make_children_bubble(card);
        lv_obj_add_event_cb(card, favourite_card_click_cb, LV_EVENT_CLICKED, fctx);
        lv_obj_add_event_cb(card, free_favourite_click_ctx_cb, LV_EVENT_DELETE, fctx);

        // Visual press feedback
        lv_obj_set_style_bg_color(card, lv_color_hex(0xF1F3F5), LV_STATE_PRESSED);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Restore scroll position
    lv_obj_scroll_to_y(root, scroll_y, LV_ANIM_OFF);

    lv_unlock();

    if (!pending_thumbs.empty())
    {
        ThumbWorkerCtx *wctx = new ThumbWorkerCtx{pending_thumbs};
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
            thumb_worker_task, "thumb_worker", 8192, wctx, 5, NULL, 1,
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