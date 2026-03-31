#include <vector>
#include <string>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "models.h"
#include "RecipeDetailService.h"

static const char *TAG = "UIEXTENSIONS";

// External theme variables (defined elsewhere)
extern uint32_t theme_colors[1][3];
extern uint32_t active_theme_index;

// === RECIPE DETAIL SPECIFIC STRUCTS ===

struct DetailFetchCtx
{
    RecipeSuggestion recipe;
    // LVGL widget refs (nulled on delete)
    lv_obj_t *spinner;
    lv_obj_t *ingredients_cont;
    lv_obj_t *method_cont;
    lv_obj_t *header_img;
};

struct IngredientCheckboxContext
{
    lv_obj_t *label;
    lv_obj_t *line;
};

// === CLEANUP CALLBACKS ===

static void free_ingredient_checkbox_ctx_cb(lv_event_t *e)
{
    delete (IngredientCheckboxContext *)lv_event_get_user_data(e);
}

// === EVENT CALLBACKS ===

static void detail_widget_deleted_cb(lv_event_t *e)
{
    DetailFetchCtx *ctx = (DetailFetchCtx *)lv_event_get_user_data(e);
    if (!ctx)
        return;
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (obj == ctx->spinner)
        ctx->spinner = nullptr;
    if (obj == ctx->ingredients_cont)
        ctx->ingredients_cont = nullptr;
    if (obj == ctx->method_cont)
        ctx->method_cont = nullptr;
    if (obj == ctx->header_img)
        ctx->header_img = nullptr;
}

static void ingredient_checkbox_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!checkbox)
        return;

    IngredientCheckboxContext *ctx = static_cast<IngredientCheckboxContext *>(lv_event_get_user_data(e));
    if (!ctx || !ctx->label)
        return;

    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
    {
        // Apply strikethrough: gray color and text decoration
        lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_decor(ctx->label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
    }
    else
    {
        // Restore normal
        lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_decor(ctx->label, LV_TEXT_DECOR_NONE, 0);
    }
}

static void recipe_detail_back_cb(lv_event_t *e)
{
    lv_obj_t *prev = (lv_obj_t *)lv_event_get_user_data(e);
    if (prev && lv_obj_is_valid(prev))
        lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    else
        lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

// === HELPER FUNCTIONS ===

static lv_obj_t *make_section_header(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);
    lv_obj_set_style_pad_top(lbl, 12, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

static void scroll_begin_hide_img_cb(lv_event_t *e)
{
    lv_obj_t *img = (lv_obj_t *)lv_event_get_user_data(e);
    if (img && lv_obj_is_valid(img))
        lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);
}

static void scroll_end_show_img_cb(lv_event_t *e)
{
    lv_obj_t *img = (lv_obj_t *)lv_event_get_user_data(e);
    if (img && lv_obj_is_valid(img))
        lv_obj_set_style_opa(img, LV_OPA_COVER, 0);
}

// FreeRTOS task: fetch details, then populate ingredients & method widgets
static void fetch_recipe_detail_task(void *arg)
{
    DetailFetchCtx *ctx = (DetailFetchCtx *)arg;

    bool ok = recipeDetailService.fetchDetails(ctx->recipe);
    ESP_LOGI("RecipeDetail", "fetchDetails: %s, ings=%d steps=%d",
             ok ? "ok" : "fail",
             (int)ctx->recipe.ingredients.size(),
             (int)ctx->recipe.methodSteps.size());

    lv_lock();

    if (ctx->spinner && lv_obj_is_valid(ctx->spinner))
        lv_obj_add_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);

    // Header image
    if (!ok && ctx->header_img && lv_obj_is_valid(ctx->header_img))
    {
        // nothing extra — already has thumbnail or grey placeholder
    }

    // Ingredients
    if (ctx->ingredients_cont && lv_obj_is_valid(ctx->ingredients_cont))
    {
        if (ok && !ctx->recipe.ingredients.empty())
        {
            for (const auto &ing : ctx->recipe.ingredients)
            {
                lv_obj_t *row = lv_obj_create(ctx->ingredients_cont);
                lv_obj_set_width(row, lv_pct(100));
                lv_obj_set_height(row, LV_SIZE_CONTENT);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_top(row, 8, 0);
                lv_obj_set_style_pad_bottom(row, 8, 0);
                lv_obj_set_style_pad_left(row, 0, 0);
                lv_obj_set_style_pad_right(row, 0, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_column(row, 8, 0);

                // Checkbox for ingredient
                lv_obj_t *checkbox = lv_checkbox_create(row);
                lv_checkbox_set_text(checkbox, "");
                lv_obj_set_style_pad_right(checkbox, 8, 0);
                lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR);
                lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);
                // Set explicit size for checkbox indicator (larger for recipe details)
                lv_obj_set_style_width(checkbox, 32, LV_PART_INDICATOR);
                lv_obj_set_style_height(checkbox, 32, LV_PART_INDICATOR);

                // Ingredient label - bigger font
                lv_obj_t *lbl = lv_label_create(row);
                lv_label_set_text(lbl, ing.c_str());
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(lbl, 1);
                lv_obj_set_style_text_font(lbl, &ui_font_ext_font_montserrat_18, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);

                // Make row clickable to toggle checkbox
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, checkbox);

                // Context to connect checkbox with label
                IngredientCheckboxContext *ctx = new IngredientCheckboxContext{lbl, nullptr};
                lv_obj_add_event_cb(checkbox, ingredient_checkbox_cb, LV_EVENT_VALUE_CHANGED, ctx);
                lv_obj_add_event_cb(checkbox, free_ingredient_checkbox_ctx_cb, LV_EVENT_DELETE, ctx);
            }
        }
        else
        {
            lv_obj_t *lbl = lv_label_create(ctx->ingredients_cont);
            lv_label_set_text(lbl, "Could not load ingredients.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }
    }

    // Method steps
    if (ctx->method_cont && lv_obj_is_valid(ctx->method_cont))
    {
        if (ok && !ctx->recipe.methodSteps.empty())
        {
            int step_num = 1;
            for (const auto &step : ctx->recipe.methodSteps)
            {
                lv_obj_t *step_card = lv_obj_create(ctx->method_cont);
                lv_obj_set_width(step_card, lv_pct(100));
                lv_obj_set_height(step_card, LV_SIZE_CONTENT);
                lv_obj_clear_flag(step_card, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_flex_flow(step_card, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(step_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_all(step_card, 12, 0);
                lv_obj_set_style_pad_column(step_card, 12, 0);
                lv_obj_set_style_border_width(step_card, 1, 0);
                lv_obj_set_style_border_color(step_card, lv_color_hex(0xE9ECEF), 0);
                lv_obj_set_style_radius(step_card, 0, 0); // No rounded corners
                lv_obj_set_style_bg_color(step_card, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(step_card, LV_OPA_COVER, 0);

                // Step number circle
                lv_obj_t *num_cont = lv_obj_create(step_card);
                lv_obj_set_size(num_cont, 32, 32);
                lv_obj_clear_flag(num_cont, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_radius(num_cont, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(num_cont, lv_color_hex(theme_colors[active_theme_index][0]), 0);
                lv_obj_set_style_bg_opa(num_cont, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(num_cont, 0, 0);
                lv_obj_set_style_pad_all(num_cont, 0, 0);
                lv_obj_set_flex_flow(num_cont, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(num_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t *num_lbl = lv_label_create(num_cont);
                char nbuf[8];
                snprintf(nbuf, sizeof(nbuf), "%d", step_num++);
                lv_label_set_text(num_lbl, nbuf);
                lv_obj_set_style_text_color(num_lbl, lv_color_white(), 0);
                lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_16, 0);

                lv_obj_t *text_lbl = lv_label_create(step_card);
                lv_label_set_text(text_lbl, step.c_str());
                lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(text_lbl, 1);
                lv_obj_set_style_text_font(text_lbl, &ui_font_ext_font_montserrat_18, 0); // Increased from 16 to 18
                lv_obj_set_style_text_color(text_lbl, lv_color_hex(0x212529), 0);
            }
        }
        else if (!ok)
        {
            lv_obj_t *lbl = lv_label_create(ctx->method_cont);
            lv_label_set_text(lbl, "Could not load method steps.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }
    }

    lv_unlock();

    // Kick off header image fetch at larger size if we have a URL
    if (!ctx->recipe.imageUrl.empty() && ctx->header_img)
    {
        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;
        if (fetch_and_decode_jpeg(ctx->recipe.imageUrl, 800, 280, &dsc, &px))
        {
            lv_lock();
            if (ctx->header_img && lv_obj_is_valid(ctx->header_img))
            {
                lv_image_set_src(ctx->header_img, dsc);
                lv_obj_set_size(ctx->header_img, lv_pct(100), 280);
                ThumbDataCtx *data_ctx = new ThumbDataCtx{dsc, px};
                lv_obj_add_event_cb(ctx->header_img, free_thumb_data_cb, LV_EVENT_DELETE, data_ctx);
            }
            else
            {
                free(px);
                delete dsc;
            }
            lv_unlock();
        }
    }

    delete ctx;
    vTaskDelete(NULL);
}

// === PUBLIC FUNCTION ===
// === PUBLIC FUNCTION ===
void showRecipeDetailScreen(const RecipeSuggestion &recipe)
{
    lv_obj_t *prev_screen = lv_scr_act();

    // Load screen from EEZ Studio UI manager
    lv_scr_load_anim(objects.recipe_detail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

    // Get UI components by identifier
    lv_obj_t *bar_title = objects.recipe_title;
    lv_obj_t *back_btn = objects.recipe_back_btn;
    lv_obj_t *header_img = objects.recipe_header_img;
    lv_obj_t *detail_spinner = objects.recipe_detail_spinner;
    lv_obj_t *ing_cont = objects.recipe_ing_cont;
    lv_obj_t *method_cont = objects.recipe_method_cont;
    lv_obj_t *total_time_val = objects.recipe_total_time_val;
    lv_obj_t *difficulty_val = objects.recipe_difficulty_val;

    // Set recipe title
    lv_label_set_text(bar_title, recipe.name.c_str());

    // Set meta information
    if (!recipe.totalTime.empty())
    {
        lv_label_set_text(total_time_val, recipe.totalTime.c_str());
    }

    if (!recipe.difficulty.empty())
    {
        lv_label_set_text(difficulty_val, recipe.difficulty.c_str());
    }

    lv_obj_clear_flag(detail_spinner, LV_OBJ_FLAG_HIDDEN);

    // Set up back button callback
    lv_obj_add_event_cb(back_btn, recipe_detail_back_cb, LV_EVENT_CLICKED, prev_screen);
    // Kick off detail fetch task
    DetailFetchCtx *fctx = new DetailFetchCtx{
        recipe,
        detail_spinner,
        ing_cont,
        method_cont,
        header_img};

    // Register delete guard callbacks
    lv_obj_add_event_cb(detail_spinner, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(ing_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(method_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(header_img, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        fetch_recipe_detail_task, "RecipeDetail",
        16384, fctx, 5, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create RecipeDetail task");
        lv_obj_add_flag(detail_spinner, LV_OBJ_FLAG_HIDDEN);
        // fctx will be cleaned up by widget delete events
    }
}