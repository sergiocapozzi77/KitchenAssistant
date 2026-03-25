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

// Global variable for detail screen
static lv_obj_t *s_detail_screen = nullptr;

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
    lv_obj_t *screen;
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
    if (obj == ctx->screen)
        ctx->screen = nullptr;
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
        lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    else
        lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    s_detail_screen = nullptr;
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

    // Hide spinner
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
                lv_obj_set_style_radius(step_card, 10, 0);
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

void showRecipeDetailScreen(const RecipeSuggestion &recipe)
{
    lv_obj_t *prev_screen = lv_scr_act();

    // Create new full screen
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF8F9FA), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_detail_screen = scr;

    // ── Fixed top bar ──────────────────────────────────────────────────────
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, lv_pct(100), 56);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_pad_hor(top_bar, 8, 0);
    lv_obj_set_style_pad_ver(top_bar, 0, 0);
    lv_obj_set_style_shadow_opa(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);

    // Back button
    lv_obj_t *back_btn = lv_button_create(top_bar);
    lv_obj_set_size(back_btn, 44, 44);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_opa(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x212529), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, recipe_detail_back_cb, LV_EVENT_CLICKED, prev_screen);

    // Title in top bar
    lv_obj_t *bar_title = lv_label_create(top_bar);
    lv_label_set_text(bar_title, recipe.name.c_str());
    lv_label_set_long_mode(bar_title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(bar_title, 1);
    lv_obj_set_style_text_font(bar_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bar_title, lv_color_hex(0x212529), 0);
    lv_obj_set_style_pad_left(bar_title, 8, 0);

    // ── Scrollable content below top bar ──────────────────────────────────
    lv_obj_t *scroll = lv_obj_create(scr);
    lv_obj_set_size(scroll, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, 56);
    // Make it fill remaining height
    lv_obj_set_height(scroll, LV_VER_RES - 56);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_style_radius(scroll, 0, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 0, 0);

    // ── Header image (grey placeholder, filled by task) ────────────────────
    lv_obj_t *header_img = lv_image_create(scroll);
    lv_obj_set_size(header_img, lv_pct(100), 280);
    lv_obj_set_style_bg_color(header_img, lv_color_hex(0xDEE2E6), 0);
    lv_obj_set_style_bg_opa(header_img, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header_img, 0, 0);
    lv_obj_set_style_border_width(header_img, 0, 0);
    lv_image_set_inner_align(header_img, LV_IMAGE_ALIGN_COVER);

    // ── Info card (meta row) ───────────────────────────────────────────────
    lv_obj_t *meta_card = lv_obj_create(scroll);
    lv_obj_set_width(meta_card, lv_pct(100));
    lv_obj_set_height(meta_card, LV_SIZE_CONTENT);
    lv_obj_clear_flag(meta_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(meta_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta_card, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(meta_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(meta_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(meta_card, 0, 0);
    lv_obj_set_style_radius(meta_card, 0, 0);
    lv_obj_set_style_pad_all(meta_card, 16, 0);
    lv_obj_set_style_shadow_opa(meta_card, 0, 0);
    lv_obj_set_style_border_side(meta_card, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(meta_card, lv_color_hex(0xE9ECEF), 0);
    lv_obj_set_style_border_width(meta_card, 1, 0);

    auto make_meta_item = [&](const char *symbol, const std::string &val, const char *label_text)
    {
        if (val.empty())
            return;
        lv_obj_t *col = lv_obj_create(meta_card);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_row(col, 2, 0);

        lv_obj_t *ico = lv_label_create(col);
        lv_label_set_text(ico, symbol);
        lv_obj_set_style_text_color(ico, lv_color_hex(0x4CAF50), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);

        lv_obj_t *val_lbl = lv_label_create(col);
        lv_label_set_text(val_lbl, val.c_str());
        lv_obj_set_style_text_color(val_lbl, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_16, 0);

        lv_obj_t *sub_lbl = lv_label_create(col);
        lv_label_set_text(sub_lbl, label_text);
        lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_14, 0);
    };

    make_meta_item(LV_SYMBOL_LOOP, recipe.totalTime, "Total");
    make_meta_item(LV_SYMBOL_EDIT, recipe.difficulty, "Difficulty");

    // ── Content area (padding) ─────────────────────────────────────────────
    lv_obj_t *content = lv_obj_create(scroll);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(content, 0, 0);

    // Loading spinner
    lv_obj_t *detail_spinner = lv_spinner_create(content);
    lv_obj_set_size(detail_spinner, 60, 60);
    lv_obj_set_style_arc_color(detail_spinner, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);

    // Ingredients section
    make_section_header(content, "Ingredients");
    lv_obj_t *ing_cont = lv_obj_create(content);
    lv_obj_set_width(ing_cont, lv_pct(100));
    lv_obj_set_height(ing_cont, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ing_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ing_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ing_cont, 12, 0);
    lv_obj_set_style_pad_row(ing_cont, 16, 0); // Increased row padding for better spacing
    lv_obj_set_style_bg_color(ing_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ing_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ing_cont, 1, 0);
    lv_obj_set_style_border_color(ing_cont, lv_color_hex(0xE9ECEF), 0);
    lv_obj_set_style_radius(ing_cont, 12, 0);
    lv_obj_set_style_shadow_opa(ing_cont, 0, 0);

    // Method section
    make_section_header(content, "Method");
    lv_obj_t *method_cont = lv_obj_create(content);
    lv_obj_set_width(method_cont, lv_pct(100));
    lv_obj_set_height(method_cont, LV_SIZE_CONTENT);
    lv_obj_clear_flag(method_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(method_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(method_cont, 0, 0);
    lv_obj_set_style_pad_row(method_cont, 10, 0);
    lv_obj_set_style_border_width(method_cont, 0, 0);
    lv_obj_set_style_bg_opa(method_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(method_cont, 0, 0);

    // Extra bottom padding
    lv_obj_t *bottom_pad = lv_obj_create(content);
    lv_obj_set_size(bottom_pad, 1, 40);
    lv_obj_set_style_border_width(bottom_pad, 0, 0);
    lv_obj_set_style_bg_opa(bottom_pad, LV_OPA_TRANSP, 0);

    // Transition in
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

    // Kick off detail fetch task
    DetailFetchCtx *fctx = new DetailFetchCtx{
        recipe,
        detail_spinner,
        ing_cont,
        method_cont,
        header_img,
        scr};

    // Register delete guard callbacks
    lv_obj_add_event_cb(detail_spinner, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(ing_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(method_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(header_img, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(scr, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);

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