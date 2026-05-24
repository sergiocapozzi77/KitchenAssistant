#include <vector>
#include <string>
#include <ctime>
#include <cmath>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "styles.h"
#include "models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "thumbnail_manager.h"

static const char *TAG = "UIEXTENSIONS";

// ── Styles ───────────────────────────────────────────────────────────────────

lv_style_t style_card;
lv_style_t style_header;
lv_style_t style_row;
lv_style_t style_qty_cont;
lv_style_t style_qty_btn;
lv_style_t style_del_btn;
lv_style_t style_expiry_badge;
lv_style_t style_checkbox_indicator;
bool styles_initialized = false;

// ── Public init ──────────────────────────────────────────────────────────────

void ui_extensions_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                        bool thumbEnableCache)
{
    thumbnail_manager_init(thumbMaxWidth, thumbMaxHeight, thumbEnableCache);
    init_styles();
    ESP_LOGI(TAG, "ui_extensions initialised (%ux%u thumbnails)", thumbMaxWidth, thumbMaxHeight);
}

// ── Styles ────────────────────────────────────────────────────────────────────

void init_styles()
{
    if (styles_initialized)
        return;

    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0xFFFFFF));
    lv_style_set_radius(&style_card, 12);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0xE0E0E0));
    lv_style_set_pad_all(&style_card, 0);

    lv_style_init(&style_header);
    lv_style_set_bg_color(&style_header, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_header, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_header, 0);
    lv_style_set_pad_hor(&style_header, 15);

    lv_style_init(&style_row);
    lv_style_set_border_side(&style_row, LV_BORDER_SIDE_TOP);
    lv_style_set_border_width(&style_row, 1);
    lv_style_set_border_color(&style_row, lv_color_hex(0xF1F3F5));
    lv_style_set_pad_hor(&style_row, 15);
    lv_style_set_bg_color(&style_row, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_row, LV_OPA_COVER);

    lv_style_init(&style_qty_cont);
    lv_style_set_radius(&style_qty_cont, 4);
    lv_style_set_bg_color(&style_qty_cont, lv_color_hex(0xF8F9FA));
    lv_style_set_border_width(&style_qty_cont, 1);
    lv_style_set_border_color(&style_qty_cont, lv_color_hex(0xE9ECEF));
    lv_style_set_pad_all(&style_qty_cont, 0);

    lv_style_init(&style_qty_btn);
    lv_style_set_bg_color(&style_qty_btn, lv_color_hex(0xF8F9FA));
    lv_style_set_bg_opa(&style_qty_btn, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_qty_btn, 0);

    lv_style_init(&style_del_btn);
    lv_style_set_bg_color(&style_del_btn, lv_color_hex(0xF8F9FA));
    lv_style_set_bg_opa(&style_del_btn, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_del_btn, 0);
    lv_style_set_border_width(&style_del_btn, 0);
    lv_style_set_border_side(&style_del_btn, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&style_del_btn, 1);
    lv_style_set_border_color(&style_del_btn, lv_color_hex(0xE9ECEF));

    lv_style_init(&style_expiry_badge);
    lv_style_set_bg_opa(&style_expiry_badge, LV_OPA_COVER);
    lv_style_set_text_color(&style_expiry_badge, lv_color_white());
    lv_style_set_pad_hor(&style_expiry_badge, 8);
    lv_style_set_pad_ver(&style_expiry_badge, 2);
    lv_style_set_radius(&style_expiry_badge, 6);

    lv_style_init(&style_checkbox_indicator);
    lv_style_set_border_color(&style_checkbox_indicator, lv_color_hex(0x007AFF));

    styles_initialized = true;
    ESP_LOGI(TAG, "Styles initialised");
}

// ── Helper functions ──────────────────────────────────────────────────────────

int days_until_expiry(const std::string &isoDate, bool frozen)
{
    if (isoDate.empty())
        return 9999;

    std::tm tm_exp = {};
    int parsed = sscanf(isoDate.c_str(), "%d-%d-%d",
                        &tm_exp.tm_year, &tm_exp.tm_mon, &tm_exp.tm_mday);

    if (parsed != 3)
    {
        ESP_LOGW(TAG, "Invalid date: %s", isoDate.c_str());
        return 9999;
    }
    if (tm_exp.tm_year < 1900 || tm_exp.tm_year > 3000 ||
        tm_exp.tm_mon < 1 || tm_exp.tm_mon > 12 ||
        tm_exp.tm_mday < 1 || tm_exp.tm_mday > 31)
    {
        ESP_LOGW(TAG, "Date out of range: %s", isoDate.c_str());
        return 9999;
    }

    if (frozen)
    {
        tm_exp.tm_mon += 4;
        if (tm_exp.tm_mon > 12)
        {
            tm_exp.tm_year += (tm_exp.tm_mon - 1) / 12;
            tm_exp.tm_mon = ((tm_exp.tm_mon - 1) % 12) + 1;
        }
    }

    tm_exp.tm_year -= 1900;
    tm_exp.tm_mon -= 1;

    time_t exp_time = mktime(&tm_exp);
    if (exp_time == -1)
    {
        ESP_LOGW(TAG, "mktime failed: %s", isoDate.c_str());
        return 9999;
    }

    time_t now_raw = time(nullptr);
    std::tm tm_now = {};
    localtime_r(&now_raw, &tm_now);
    tm_now.tm_hour = tm_now.tm_min = tm_now.tm_sec = 0;
    time_t now_midnight = mktime(&tm_now);

    return (int)round(difftime(exp_time, now_midnight) / 86400.0);
}

lv_color_t get_expiry_color(int days)
{
    if (days < 0)
        return lv_color_hex(0xE74C3C);
    if (days <= 3)
        return lv_color_hex(0xF39C12);
    return lv_color_hex(0x27AE60);
}

// ── Event callbacks ───────────────────────────────────────────────────────────

void row_click_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    if (!checkbox)
        return;
    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
        lv_obj_clear_state(checkbox, LV_STATE_CHECKED);
    else
        lv_obj_add_state(checkbox, LV_STATE_CHECKED);
    lv_obj_send_event(checkbox, LV_EVENT_VALUE_CHANGED, NULL);
}

void ingredient_checkbox_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!checkbox)
        return;
    IngredientCheckboxContext *ctx =
        static_cast<IngredientCheckboxContext *>(lv_event_get_user_data(e));
    if (!ctx || !ctx->label)
        return;

    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
    {
        lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_decor(ctx->label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
    }
    else
    {
        lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_decor(ctx->label, LV_TEXT_DECOR_NONE, 0);
    }
}

void free_ingredient_checkbox_ctx_cb(lv_event_t *e)
{
    delete (IngredientCheckboxContext *)lv_event_get_user_data(e);
}

// ── Ingredients UI ────────────────────────────────────────────────────────────

void setupIngredientsContainer(lv_obj_t *container)
{
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(container, 12, 0);
    lv_obj_set_style_pad_row(container, 12, 0);
}

lv_obj_t *createIngredientRow(lv_obj_t *parent, const std::string &displayText)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(48));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(theme_colors[active_theme_index][4]), 0);
    lv_obj_set_style_radius(row, 26, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(theme_colors[active_theme_index][3]), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *checkbox = lv_checkbox_create(row);
    // Clear SCROLL_ON_FOCUS so touching the checkbox won't trigger
    // lv_obj_scroll_to_view_recursive → lv_obj_update_layout while the
    // background task is still populating siblings (layout walk can hit
    // a null style pointer when internal SRAM is fragmented).
    lv_obj_clear_flag(checkbox, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(checkbox, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_checkbox_set_text(checkbox, "");
    lv_obj_set_style_pad_right(checkbox, 8, 0);
    add_style_checkbox_default(checkbox);
    lv_obj_set_style_width(checkbox, 32, LV_PART_INDICATOR);
    lv_obj_set_style_height(checkbox, 32, LV_PART_INDICATOR);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, displayText.c_str());
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_text_font(lbl, &ui_font_ext_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);

    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, checkbox);

    IngredientCheckboxContext *ctx = new IngredientCheckboxContext{lbl, nullptr};
    lv_obj_add_event_cb(checkbox, ingredient_checkbox_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(checkbox, free_ingredient_checkbox_ctx_cb, LV_EVENT_DELETE, ctx);

    return row;
}

void populateIngredientsUI(lv_obj_t *container, const std::vector<std::string> &displayTexts)
{
    setupIngredientsContainer(container);
    for (const auto &text : displayTexts)
    {
        createIngredientRow(container, text);
    }
}

// ── Recipe card builder ───────────────────────────────────────────────────────

void make_children_bubble(lv_obj_t *obj)
{
    std::vector<lv_obj_t *> stack;
    stack.push_back(obj);
    while (!stack.empty())
    {
        lv_obj_t *current = stack.back();
        stack.pop_back();
        lv_obj_add_flag(current, LV_OBJ_FLAG_EVENT_BUBBLE);
        uint32_t count = lv_obj_get_child_count(current);
        for (uint32_t i = 0; i < count; i++)
            stack.push_back(lv_obj_get_child(current, i));
    }
}

// No pending_thumbs parameter – cards push directly to the queue
static lv_obj_t *createRecipeCardInternal(lv_obj_t *parent,
                                          const std::string &name,
                                          const std::string &description,
                                          const std::string &imageUrl,
                                          const std::string &difficulty,
                                          const std::string &totalTime,
                                          const std::string &recipeSource,
                                          bool useCache)
{
    // Card
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_column(card, 17, 0);

    // Thumbnail placeholder
    lv_obj_t *thumb = lv_image_create(card);
    lv_obj_set_size(thumb, s_thumb_max_w, s_thumb_max_h);
    lv_obj_set_style_bg_color(thumb, lv_color_hex(0xDEE2E6), 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb, 12, 0);
    lv_obj_set_style_clip_corner(thumb, true, 0);
    lv_obj_set_style_border_width(thumb, 0, 0);
    lv_image_set_inner_align(thumb, LV_IMAGE_ALIGN_COVER);

    std::string thumbUrl = imageUrl;
    if (imageUrl.empty() && recipeSource == "ai-deepseek")
    {
        thumbUrl = "generate:" + name + "|||" + description;
        ESP_LOGI(TAG, "AI recipe – will generate image: %s", name.c_str());
    }

    if (!thumbUrl.empty())
    {
        // lv_obj_t *shimmer = create_shimmer_overlay(thumb);  // DISABLED — shimmer caused crashes
        // start_shimmer_animation(shimmer, thumb);

        ThumbContext *tctx = new ThumbContext{thumb, thumbUrl, 0 /*set by push*/, {}};
        tctx->cacheAllowed = useCache;
        lv_obj_add_event_cb(thumb, thumb_obj_deleted_cb, LV_EVENT_DELETE, tctx);

        // Push-and-forget; worker owns tctx from here
        thumb_queue_push(tctx);
    }

    // Right column
    lv_obj_t *info = lv_obj_create(card);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(info, 13, 0);

    lv_obj_t *title = lv_label_create(info);
    lv_label_set_text(title, name.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_color(title, lv_color_hex(0x212529), 0);
    lv_obj_set_style_text_font(title, &ui_font_ext_font_montserrat_26, 0);

    if (!description.empty())
    {
        std::string cleaned = description;
        for (const auto &tag : {"<p>", "</p>"})
        {
            size_t pos = 0;
            while ((pos = cleaned.find(tag, pos)) != std::string::npos)
                cleaned.erase(pos, strlen(tag));
        }
        lv_obj_t *desc = lv_label_create(info);
        lv_label_set_text(desc, cleaned.c_str());
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(desc, lv_pct(100));
        lv_obj_set_style_text_color(desc, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_font(desc, &ui_font_ext_font_montserrat_18, 0);
    }

    // Badges
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

    auto make_badge = [&](lv_obj_t *parent_badge, const char *symbol, const std::string &text)
    {
        if (text.empty())
            return;
        lv_obj_t *wrap = lv_obj_create(parent_badge);
        lv_obj_set_size(wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
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

    make_badge(badges, LV_SYMBOL_LOOP, totalTime);
    make_badge(badges, LV_SYMBOL_EDIT, difficulty);

    lv_obj_set_style_bg_color(card, lv_color_hex(0xF1F3F5), LV_STATE_PRESSED);
    vTaskDelay(pdMS_TO_TICKS(10));
    return card;
}

lv_obj_t *createRecipeCard(lv_obj_t *parent, const RecipeSuggestion &recipe)
{
    return createRecipeCardInternal(parent, recipe.name, recipe.description,
                                    recipe.imageUrl, recipe.difficulty,
                                    recipe.totalTime, recipe.recipeSource, false);
}

lv_obj_t *createRecipeCard(lv_obj_t *parent, const Favorite &fav)
{
    return createRecipeCardInternal(parent, fav.name, fav.description,
                                    fav.imageUrl, fav.difficulty,
                                    fav.totalTime, "", true);
}

// ── Snackbar / Spinner ────────────────────────────────────────────────────────

static TimerHandle_t snackbar_timer = nullptr;

static void snackbar_timer_callback(TimerHandle_t)
{
    lv_lock();
    if (objects.snackbar && lv_obj_is_valid(objects.snackbar))
        lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();
}

void showSnackbar(const char *message, int duration_ms)
{
    if (snackbar_timer)
    {
        xTimerStop(snackbar_timer, 0);
        xTimerDelete(snackbar_timer, 0);
        snackbar_timer = nullptr;
    }
    lv_lock();
    if (objects.snackbar_text && lv_obj_is_valid(objects.snackbar_text) &&
        objects.snackbar && lv_obj_is_valid(objects.snackbar))
    {
        lv_label_set_text(objects.snackbar_text, message);
        lv_obj_clear_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_unlock();

    snackbar_timer = xTimerCreate("snackbar", pdMS_TO_TICKS(duration_ms),
                                  pdFALSE, nullptr, snackbar_timer_callback);
    if (snackbar_timer)
        xTimerStart(snackbar_timer, 0);
}

void showSpinner()
{
    lv_lock();
    if (objects.spinner && lv_obj_is_valid(objects.spinner))
        lv_obj_clear_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();
}

void hideSpinner()
{
    lv_lock();
    if (objects.spinner && lv_obj_is_valid(objects.spinner))
        lv_obj_add_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();
}
