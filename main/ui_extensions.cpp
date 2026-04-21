#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <ctime>
#include <cmath>
#include "mbedtls/base64.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "styles.h"
#include "models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "secrets.h"
#include "HttpClientHelper.h"
#include "LeonardoImageGenerator.h"
#include "AppwriteClientInstance.h"
#include "cJSON.h"
#include "thumbnail_cache.h"

static const char *TAG = "UIEXTENSIONS";

// Global variables defined here
uint32_t s_thumb_generation = 0;
static SemaphoreHandle_t s_http_concurrency_sem = NULL;
std::map<std::string, std::string> s_leonardo_url_cache; // Maps "generate:prompt|WxH" -> Leonardo URL

lv_style_t style_card;
lv_style_t style_header;
lv_style_t style_row;
lv_style_t style_qty_cont;
lv_style_t style_qty_btn;
lv_style_t style_del_btn;
lv_style_t style_expiry_badge;
lv_style_t style_checkbox_indicator;
bool styles_initialized = false;

// Shimmer animation functions (declared in ui_extensions_internal.h)
static void shimmer_anim_cb(void *var, int32_t v)
{
    lv_obj_t *shimmer_bar = (lv_obj_t *)var;
    if (!shimmer_bar || !lv_obj_is_valid(shimmer_bar))
        return;
    lv_obj_set_x(shimmer_bar, v);
}

lv_obj_t *create_shimmer_overlay(lv_obj_t *parent)
{
    lv_obj_t *shimmer = lv_obj_create(parent);
    lv_obj_set_size(shimmer, 50, lv_obj_get_height(parent));
    lv_obj_set_style_bg_color(shimmer, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(shimmer, LV_OPA_30, 0);
    lv_obj_set_style_radius(shimmer, 0, 0);
    lv_obj_set_style_border_width(shimmer, 0, 0);
    lv_obj_set_style_shadow_width(shimmer, 0, 0);
    // Gradient from transparent white to white to transparent white
    lv_obj_set_style_bg_grad_color(shimmer, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(shimmer, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_stop(shimmer, 255, 0);
    lv_obj_set_style_bg_main_stop(shimmer, 0, 0);
    // Initially positioned left outside parent
    lv_obj_set_x(shimmer, -lv_obj_get_width(shimmer));
    lv_obj_set_y(shimmer, 0);
    return shimmer;
}

void start_shimmer_animation(lv_obj_t *shimmer_bar, lv_obj_t *parent)
{
    int32_t start_x = -lv_obj_get_width(shimmer_bar);
    int32_t end_x = lv_obj_get_width(parent);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, shimmer_bar);
    lv_anim_set_exec_cb(&a, shimmer_anim_cb);
    lv_anim_set_values(&a, start_x, end_x);
    lv_anim_set_time(&a, 1500);
    lv_anim_set_playback_time(&a, 1500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void stop_shimmer_animation(lv_obj_t *shimmer_bar)
{
    lv_anim_delete(shimmer_bar, shimmer_anim_cb);
    // Delete the shimmer bar object (caller should do this)
}

static TimerHandle_t snackbar_timer = nullptr;

// === REUSABLE STYLES (created once, applied many times) ===
void init_styles()
{
    if (styles_initialized)
        return;

    // Card style
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0xFFFFFF));
    lv_style_set_radius(&style_card, 12);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0xE0E0E0));
    lv_style_set_pad_all(&style_card, 0);

    // Header style
    lv_style_init(&style_header);
    lv_style_set_bg_color(&style_header, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_header, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_header, 0);
    lv_style_set_pad_hor(&style_header, 15);

    // Row style
    lv_style_init(&style_row);
    lv_style_set_border_side(&style_row, LV_BORDER_SIDE_TOP);
    lv_style_set_border_width(&style_row, 1);
    lv_style_set_border_color(&style_row, lv_color_hex(0xF1F3F5));
    lv_style_set_pad_hor(&style_row, 15);
    lv_style_set_bg_color(&style_row, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_row, LV_OPA_COVER);

    // Quantity container style
    lv_style_init(&style_qty_cont);
    lv_style_set_radius(&style_qty_cont, 4);
    lv_style_set_bg_color(&style_qty_cont, lv_color_hex(0xF8F9FA));
    lv_style_set_border_width(&style_qty_cont, 1);
    lv_style_set_border_color(&style_qty_cont, lv_color_hex(0xE9ECEF));
    lv_style_set_pad_all(&style_qty_cont, 0);

    // Quantity button style
    lv_style_init(&style_qty_btn);
    lv_style_set_bg_color(&style_qty_btn, lv_color_hex(0xF8F9FA));
    lv_style_set_bg_opa(&style_qty_btn, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_qty_btn, 0);

    // Delete button style
    lv_style_init(&style_del_btn);
    lv_style_set_bg_color(&style_del_btn, lv_color_hex(0xF8F9FA));
    lv_style_set_bg_opa(&style_del_btn, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_del_btn, 0);
    lv_style_set_border_width(&style_del_btn, 0);
    lv_style_set_border_side(&style_del_btn, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&style_del_btn, 1);
    lv_style_set_border_color(&style_del_btn, lv_color_hex(0xE9ECEF));

    // Expiry badge style
    lv_style_init(&style_expiry_badge);
    lv_style_set_bg_opa(&style_expiry_badge, LV_OPA_COVER);
    lv_style_set_text_color(&style_expiry_badge, lv_color_white());
    lv_style_set_pad_hor(&style_expiry_badge, 8);
    lv_style_set_pad_ver(&style_expiry_badge, 2);
    lv_style_set_radius(&style_expiry_badge, 6);

    // Checkbox indicator style
    lv_style_init(&style_checkbox_indicator);
    lv_style_set_border_color(&style_checkbox_indicator, lv_color_hex(0x007AFF));

    // Create HTTP concurrency semaphore (max 2 concurrent requests)
    if (s_http_concurrency_sem == NULL)
    {
        s_http_concurrency_sem = xSemaphoreCreateCounting(2, 2);
        if (s_http_concurrency_sem == NULL)
        {
            ESP_LOGE(TAG, "Failed to create HTTP concurrency semaphore");
        }
        else
        {
            ESP_LOGI(TAG, "HTTP concurrency semaphore created");
        }
    }

    styles_initialized = true;
    ESP_LOGI(TAG, "Styles initialized");
}

// === HELPER FUNCTIONS ===

int days_until_expiry(const std::string &isoDate, bool frozen)
{
    if (isoDate.empty())
        return 9999;

    std::tm tm_exp = {};
    int parsed = sscanf(isoDate.c_str(), "%d-%d-%d",
                        &tm_exp.tm_year,
                        &tm_exp.tm_mon,
                        &tm_exp.tm_mday);

    // Validate parsing succeeded
    if (parsed != 3)
    {
        ESP_LOGW(TAG, "Invalid date format: %s", isoDate.c_str());
        return 9999;
    }

    // Validate date ranges
    if (tm_exp.tm_year < 1900 || tm_exp.tm_year > 3000 ||
        tm_exp.tm_mon < 1 || tm_exp.tm_mon > 12 ||
        tm_exp.tm_mday < 1 || tm_exp.tm_mday > 31)
    {
        ESP_LOGW(TAG, "Date out of range: %s", isoDate.c_str());
        return 9999;
    }

    // If frozen, delay expiry by 4 months
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
        ESP_LOGW(TAG, "mktime failed for date: %s", isoDate.c_str());
        return 9999;
    }

    // Normalize now to midnight so we compare dates, not timestamps
    time_t now_raw = time(nullptr);
    std::tm tm_now = {};
    localtime_r(&now_raw, &tm_now);
    tm_now.tm_hour = 0;
    tm_now.tm_min = 0;
    tm_now.tm_sec = 0;
    time_t now_midnight = mktime(&tm_now);

    double diff = difftime(exp_time, now_midnight);
    return (int)round(diff / (60.0 * 60.0 * 24.0));
}

lv_color_t get_expiry_color(int days)
{
    if (days < 0)
        return lv_color_hex(0xE74C3C); // red
    else if (days <= 3)
        return lv_color_hex(0xF39C12); // orange
    else
        return lv_color_hex(0x27AE60); // green
}

// === COMMON EVENT CALLBACKS ===

void row_click_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    if (!checkbox)
        return;

    // Toggle checkbox state
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

void free_ingredient_checkbox_ctx_cb(lv_event_t *e)
{
    delete (IngredientCheckboxContext *)lv_event_get_user_data(e);
}

// === INGREDIENTS UI HELPERS ===

void setupIngredientsContainer(lv_obj_t *container)
{
    // Set up container for two-column layout
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

    // Checkbox for ingredient
    lv_obj_t *checkbox = lv_checkbox_create(row);
    lv_checkbox_set_text(checkbox, "");
    lv_obj_set_style_pad_right(checkbox, 8, 0);
    add_style_checkbox_default(checkbox);
    // Set explicit size for checkbox indicator (larger for recipe details)
    lv_obj_set_style_width(checkbox, 32, LV_PART_INDICATOR);
    lv_obj_set_style_height(checkbox, 32, LV_PART_INDICATOR);

    // Ingredient label - bigger font
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, displayText.c_str());
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

    return row;
}

void populateIngredientsUI(lv_obj_t *container, const std::vector<std::string> &displayTexts)
{
    setupIngredientsContainer(container);
    for (const auto &text : displayTexts)
    {
        createIngredientRow(container, text);
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to LVGL to render incrementally
    }
}

// === RECIPE CARD HELPERS ===

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
        {
            stack.push_back(lv_obj_get_child(current, i));
        }
    }
}

static lv_obj_t *createRecipeCardInternal(lv_obj_t *parent, const std::string &name, const std::string &description, const std::string &imageUrl, const std::string &difficulty, const std::string &totalTime, const std::string &recipeSource, std::vector<ThumbContext *> &pending_thumbs)
{
    // === CARD ===
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_column(card, 17, 0); // increased from 12 for better spacing

    // === THUMBNAIL PLACEHOLDER ===
    lv_obj_t *thumb = lv_image_create(card);
    lv_obj_set_size(thumb, 112, 112);
    lv_obj_set_style_bg_color(thumb, lv_color_hex(0xDEE2E6), 0); // grey until loaded
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb, 12, 0);        // increased from 8 for more rounded corners
    lv_obj_set_style_clip_corner(thumb, true, 0); // clip image to rounded corners
    lv_obj_set_style_border_width(thumb, 0, 0);
    lv_image_set_inner_align(thumb, LV_IMAGE_ALIGN_COVER);

    std::string thumbUrl = imageUrl;
    if (imageUrl.empty() && recipeSource == "ai-deepseek")
    {
        // Generate a placeholder URL that will trigger AI image generation
        thumbUrl = "generate:" + name + "|||" + description;
        ESP_LOGI(TAG, "AI recipe with no image, will generate: %s", name.c_str());
    }

    if (!thumbUrl.empty())
    {
        ESP_LOGI(TAG, "Scheduling thumb fetch for recipe: %s", name.c_str());
        lv_obj_t *shimmer = create_shimmer_overlay(thumb);
        start_shimmer_animation(shimmer, thumb);
        ThumbContext *tctx = new ThumbContext{thumb, shimmer, thumbUrl, s_thumb_generation};
        ESP_LOGI(TAG, ">>> about to create task, internal heap: %" PRIu32,
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        pending_thumbs.push_back(tctx);
    }
    else
    {
        ESP_LOGI(TAG, "No image URL for recipe: %s", name.c_str());
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
    lv_obj_set_style_pad_row(info, 13, 0); // increased from 8 for better spacing

    // Title
    lv_obj_t *title = lv_label_create(info);
    lv_label_set_text(title, name.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_color(title, lv_color_hex(0x212529), 0);
    lv_obj_set_style_text_font(title, &ui_font_ext_font_montserrat_26, 0);

    // Description
    if (!description.empty())
    {
        std::string cleaned = description;
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

    auto make_badge = [&](lv_obj_t *parent_badge, const char *symbol, const std::string &text)
    {
        if (text.empty())
            return;
        lv_obj_t *wrap = lv_obj_create(parent_badge);
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

    make_badge(badges, LV_SYMBOL_LOOP, totalTime); // clock-like symbol
    make_badge(badges, LV_SYMBOL_EDIT, difficulty);

    // Visual press feedback (caller will add click handler)
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF1F3F5), LV_STATE_PRESSED);
    vTaskDelay(pdMS_TO_TICKS(10));
    return card;
}

lv_obj_t *createRecipeCard(lv_obj_t *parent, const RecipeSuggestion &recipe, std::vector<ThumbContext *> &pending_thumbs)
{
    return createRecipeCardInternal(parent, recipe.name, recipe.description, recipe.imageUrl, recipe.difficulty, recipe.totalTime, recipe.recipeSource, pending_thumbs);
}

lv_obj_t *createRecipeCard(lv_obj_t *parent, const Favorite &fav, std::vector<ThumbContext *> &pending_thumbs)
{
    return createRecipeCardInternal(parent, fav.name, fav.description, fav.imageUrl, fav.difficulty, fav.totalTime, "", pending_thumbs);
}

// === THUMBNAIL FETCH/DECODE ===

size_t tjpgd_in_cb(JDEC *jd, uint8_t *buf, size_t n)
{
    JpegIo *io = (JpegIo *)jd->device;
    size_t avail = io->src_len - io->src_pos;
    n = (n < avail) ? n : avail;
    if (buf)
        memcpy(buf, io->src + io->src_pos, n);
    io->src_pos += n;
    return n;
}

int tjpgd_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    JpegIo *io = (JpegIo *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    int cols = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom; y++)
    {
        uint8_t *dst = io->dst + (y * io->out_w + rect->left) * 3;
        for (int x = 0; x < cols; x++, src += 3, dst += 3)
        {
            dst[0] = src[2]; // B
            dst[1] = src[1]; // G
            dst[2] = src[0]; // R
        }
    }
    return 1;
}

// Fired under lv_lock if thumb is deleted while task is still running
void thumb_obj_deleted_cb(lv_event_t *e)
{
    ThumbContext *ctx = (ThumbContext *)lv_event_get_user_data(e);
    if (ctx)
    {
        ctx->cancelled.store(true);
        ctx->thumb = nullptr;
        // If shimmer exists (child of thumb), it will be automatically deleted when thumb is deleted
        // but we should stop its animation and null the pointer
        if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
        {
            stop_shimmer_animation(ctx->shimmer);
            // shimmer object will be deleted by LVGL when parent (thumb) is deleted
            ctx->shimmer = nullptr;
        }
    }
}

// Fired under lv_lock when thumb is deleted after image data was set
void free_thumb_data_cb(lv_event_t *e)
{
    ThumbDataCtx *d = (ThumbDataCtx *)lv_event_get_user_data(e);
    if (!d)
        return;
    free((void *)d->dsc->data);
    delete d->dsc;
    delete d;
}

static bool decode_jpeg_buffer(uint8_t *jpeg_buf, size_t jpeg_len, uint8_t **out_px, uint16_t *out_width, uint16_t *out_height)
{
    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_INTERNAL);
    if (!work)
    {
        ESP_LOGE(TAG, "work malloc failed");
        return false;
    }
    ESP_LOGI(TAG, "decode_jpeg_buffer: work buffer allocated, jpeg_len=%u", jpeg_len);

    JpegIo io = {jpeg_buf, jpeg_len, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, 3100, &io);
    esp_task_wdt_reset();
    ESP_LOGI(TAG, "jd_prepare: %d  img size: %ux%u", res, jd.width, jd.height);

    uint8_t *px = nullptr;
    if (res == JDR_OK)
    {
        jd.scale = 0;
        uint16_t decoded_w = jd.width >> jd.scale;
        uint16_t decoded_h = jd.height >> jd.scale;

        px = (uint8_t *)heap_caps_malloc(decoded_w * decoded_h * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!px)
        {
            ESP_LOGE(TAG, "px malloc failed (%ux%u)", decoded_w, decoded_h);
            heap_caps_free(work);
            return false;
        }
        ESP_LOGI(TAG, "decode_jpeg_buffer: pixel buffer allocated %ux%u = %u bytes", decoded_w, decoded_h, decoded_w * decoded_h * 3);

        io.dst = px;
        io.out_w = decoded_w;
        *out_width = decoded_w;
        *out_height = decoded_h;

        ESP_LOGI(TAG, "px buffer: %u bytes, decode expects: %u bytes",
                 decoded_w * decoded_h * 3,
                 jd.width * jd.height * 3); // what tjpgd will actually write
        assert(decoded_w * decoded_h * 3 >= (jd.width >> jd.scale) * (jd.height >> jd.scale) * 3);

        res = jd_decomp(&jd, tjpgd_out_cb, jd.scale);
        esp_task_wdt_reset();
        ESP_LOGI(TAG, "jd_decomp: %d", res);
    }

    heap_caps_free(work);

    if (res != JDR_OK)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %d", res);
        if (px)
            heap_caps_free(px);
        return false;
    }

    *out_px = px;
    ESP_LOGI(TAG, "decode_jpeg_buffer succeeded: %ux%u", *out_width, *out_height);
    return true;
}

static bool fetch_resized_base64(const std::string &image_url,
                                 uint16_t W, uint16_t H,
                                 std::string &out_b64)
{
    cJSON *bodyJson = cJSON_CreateObject();
    if (!bodyJson)
    {
        ESP_LOGE(TAG, "Failed to create body JSON object");
        return false;
    }

    cJSON_AddStringToObject(bodyJson, "url", image_url.c_str());
    cJSON_AddNumberToObject(bodyJson, "maxWidth", W);
    cJSON_AddNumberToObject(bodyJson, "maxHeight", H);

    char *bodyJsonStr = cJSON_PrintUnformatted(bodyJson);
    cJSON_Delete(bodyJson);
    if (!bodyJsonStr)
    {
        ESP_LOGE(TAG, "Failed to stringify body JSON");
        return false;
    }

    ESP_LOGI(TAG, "Request payload length: %d", strlen(bodyJsonStr));
    ESP_LOGI(TAG, "Calling Appwrite function %s", APPWRITE_IMAGE_RESIZE_FUNCTION_ID);

    int status = 0;
    std::string response = getAppwriteClient().executeFunction(
        APPWRITE_IMAGE_RESIZE_FUNCTION_ID, bodyJsonStr, false, status);
    free(bodyJsonStr);

    ESP_LOGI(TAG, "HTTP status: %d, response size: %d", status, response.size());

    if ((status != 200 && status != 201 && status != 202) || response.empty())
    {
        ESP_LOGE(TAG, "HTTP request failed or empty response");
        if (!response.empty())
            ESP_LOGE(TAG, "Error response: %.*s", response.size(), response.c_str());
        return false;
    }

    // Parse Appwrite envelope → responseBody → image (base64)
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse Appwrite envelope JSON");
        return false;
    }

    cJSON *responseBodyField = cJSON_GetObjectItem(root, "responseBody");
    if (!responseBodyField || !cJSON_IsString(responseBodyField))
    {
        ESP_LOGE(TAG, "Missing or invalid 'responseBody' field");
        cJSON_Delete(root);
        return false;
    }

    cJSON *innerJson = cJSON_Parse(responseBodyField->valuestring);
    cJSON_Delete(root); // responseBodyField pointer now invalid
    if (!innerJson)
    {
        ESP_LOGE(TAG, "Failed to parse inner responseBody JSON");
        return false;
    }

    cJSON *imageField = cJSON_GetObjectItem(innerJson, "image");
    if (!imageField || !cJSON_IsString(imageField))
    {
        ESP_LOGE(TAG, "Missing or invalid 'image' field in response");
        cJSON_Delete(innerJson);
        return false;
    }

    out_b64 = imageField->valuestring;
    cJSON_Delete(innerJson);
    return true;
}

// -----------------------------------------------------------------------------
// Main function
// -----------------------------------------------------------------------------
bool fetch_and_decode_jpeg(const std::string &url,
                           uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc,
                           uint8_t **out_px,
                           bool useCache)
{
    std::vector<uint8_t> cached_jpeg;
    bool fromCache = false;
    uint16_t reqW = W; // store requested dimensions for cache storage
    uint16_t reqH = H;

    ESP_LOGI(TAG, "fetch_and_decode_jpeg: url=%s, requested size=%ux%u, useCache=%d",
             url.c_str(), W, H, useCache);

    // -------------------------------------------------------------------------
    // 1. Try cache if enabled
    // -------------------------------------------------------------------------
    if (useCache && thumbnail_cache::get(url, W, H, cached_jpeg))
    {
        ESP_LOGI(TAG, "Cache hit for %s %ux%u", url.c_str(), W, H);
        fromCache = true;
    }
    else if (useCache)
    {
        ESP_LOGI(TAG, "Cache miss for %s %ux%u", url.c_str(), W, H);
    }

    if (fromCache)
    {
        // Decode from cache
        uint8_t *jpeg_buf = (uint8_t *)heap_caps_malloc(cached_jpeg.size(),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!jpeg_buf)
        {
            ESP_LOGE(TAG, "jpeg_buf malloc failed for cached data, requested: %u",
                     cached_jpeg.size());
            return false;
        }
        memcpy(jpeg_buf, cached_jpeg.data(), cached_jpeg.size());

        uint8_t *px = nullptr;
        uint16_t decoded_w, decoded_h;
        if (!decode_jpeg_buffer(jpeg_buf, cached_jpeg.size(), &px, &decoded_w, &decoded_h))
        {
            heap_caps_free(jpeg_buf);
            return false;
        }
        heap_caps_free(jpeg_buf);

        lv_image_dsc_t *dsc = new lv_image_dsc_t{};
        dsc->header.cf = LV_COLOR_FORMAT_RGB888;
        dsc->header.w = decoded_w;
        dsc->header.h = decoded_h;
        dsc->header.stride = decoded_w * 3;
        dsc->data_size = decoded_w * decoded_h * 3;
        dsc->data = px;

        *out_dsc = dsc;
        *out_px = px;
        return true;
    }

    // -------------------------------------------------------------------------
    // 2. Network path: acquire concurrency semaphore once
    // -------------------------------------------------------------------------
    SemaphoreGuard semGuard(s_http_concurrency_sem);
    if (!semGuard.acquired())
    {
        ESP_LOGE(TAG, "Failed to take HTTP concurrency semaphore");
        return false;
    }

    std::string image_url_to_fetch;

    // -------------------------------------------------------------------------
    // 3. Handle "generate:" URLs (Leonardo)
    // -------------------------------------------------------------------------
    bool isGenerate = (url.find("generate:") == 0);
    if (isGenerate)
    {
        std::string rest = url.substr(9); // skip "generate:"
        size_t delim = rest.find("|||");
        std::string recipeName = (delim != std::string::npos) ? rest.substr(0, delim) : rest;
        std::string recipeDesc = (delim != std::string::npos) ? rest.substr(delim + 3) : "";

        ESP_LOGI(TAG, "Generating image for AI recipe: %s", recipeName.c_str());

        if (strlen(LEONARDO_API_KEY) == 0)
        {
            ESP_LOGW(TAG, "LEONARDO_API_KEY not configured, cannot generate image");
            return false;
        }

        // Check Leonardo URL cache first
        std::string cache_key = url + "|" + std::to_string(W) + "x" + std::to_string(H);
        auto cache_it = s_leonardo_url_cache.find(cache_key);
        if (cache_it != s_leonardo_url_cache.end())
        {
            ESP_LOGI(TAG, "Leonardo URL cache hit for key: %s", cache_key.c_str());
            image_url_to_fetch = cache_it->second;
        }
        else
        {
            std::string prompt = "Generate an appetizing, high-quality food photography image of ";
            prompt += recipeName;
            if (!recipeDesc.empty())
            {
                prompt += ". Description: ";
                prompt += recipeDesc;
            }
            prompt += ". The image should be ";
            prompt += (W == H) ? "square" : (W > H ? "landscape orientation" : "portrait orientation");
            prompt += ", professional food photography style, realistic, well-lit.";

            int status = 0;
            static LeonardoImageGenerator leonardoGen(LEONARDO_ENDPOINT, LEONARDO_IMAGE_MODEL,
                                                      LEONARDO_API_KEY, 120000);
            std::string generatedUrl = leonardoGen.generateImage(prompt, W, H, status);
            if (generatedUrl.empty())
            {
                ESP_LOGE(TAG, "Leonardo image generation failed with status: %d", status);
                return false;
            }
            // Cache the Leonardo URL for future use
            s_leonardo_url_cache[cache_key] = generatedUrl;
            ESP_LOGI(TAG, "Cached Leonardo URL for key: %s", cache_key.c_str());
            image_url_to_fetch = generatedUrl;
        }
    }
    else
    {
        // Regular URL
        image_url_to_fetch = url;
    }

    // -------------------------------------------------------------------------
    // 4. Common Appwrite fetch + base64 extraction
    // -------------------------------------------------------------------------
    std::string b64Str;
    if (!fetch_resized_base64(image_url_to_fetch, W, H, b64Str))
    {
        return false;
    }

    // -------------------------------------------------------------------------
    // 5. Base64 decode → JPEG buffer
    // -------------------------------------------------------------------------
    const uint8_t *b64 = reinterpret_cast<const uint8_t *>(b64Str.c_str());
    size_t b64Len = b64Str.size();
    ESP_LOGI(TAG, "Base64 length: %u bytes", b64Len);

    size_t jpegLen = 0;
    int ret = mbedtls_base64_decode(nullptr, 0, &jpegLen, b64, b64Len);
    if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        ESP_LOGE(TAG, "mbedtls_base64_decode size check failed: -0x%04X", -ret);
        return false;
    }
    ESP_LOGI(TAG, "JPEG length after base64 decode: %u bytes", jpegLen);

    uint8_t *jpeg_buf = (uint8_t *)heap_caps_malloc(jpegLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buf)
    {
        ESP_LOGE(TAG, "jpeg_buf malloc failed, requested: %u", jpegLen);
        return false;
    }
    ESP_LOGI(TAG, "JPEG buffer allocated: %u bytes", jpegLen);

    ret = mbedtls_base64_decode(jpeg_buf, jpegLen, &jpegLen, b64, b64Len);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "mbedtls_base64_decode failed: -0x%04X", -ret);
        heap_caps_free(jpeg_buf);
        return false;
    }
    ESP_LOGI(TAG, "Decoded JPEG: %u bytes", jpegLen);

    // -------------------------------------------------------------------------
    // 6. Release HTTP semaphore early before CPU‑intensive JPEG decode
    // -------------------------------------------------------------------------
    semGuard.release();

    // -------------------------------------------------------------------------
    // 7. tjpgd decode
    // -------------------------------------------------------------------------
    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_INTERNAL);
    if (!work)
    {
        ESP_LOGE(TAG, "work malloc failed");
        heap_caps_free(jpeg_buf);
        return false;
    }
    ESP_LOGI(TAG, "Work buffer allocated for JPEG decode");

    JpegIo io = {jpeg_buf, jpegLen, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, 3100, &io);
    ESP_LOGI(TAG, "jd_prepare: %d  img size: %ux%u", res, jd.width, jd.height);

    uint8_t *px = nullptr;
    if (res == JDR_OK)
    {
        jd.scale = 0;
        uint16_t decoded_w = jd.width >> jd.scale;
        uint16_t decoded_h = jd.height >> jd.scale;

        px = (uint8_t *)heap_caps_malloc(decoded_w * decoded_h * 3,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!px)
        {
            ESP_LOGE(TAG, "px malloc failed (%ux%u)", decoded_w, decoded_h);
            heap_caps_free(jpeg_buf);
            heap_caps_free(work);
            return false;
        }
        ESP_LOGI(TAG, "Pixel buffer allocated: %u x %u = %u bytes",
                 decoded_w, decoded_h, decoded_w * decoded_h * 3);

        io.dst = px;
        io.out_w = decoded_w;
        W = decoded_w; // update dimensions to actual decoded size
        H = decoded_h;

        res = jd_decomp(&jd, tjpgd_out_cb, jd.scale);
        ESP_LOGI(TAG, "jd_decomp: %d", res);
    }

    // -------------------------------------------------------------------------
    // 8. Store in cache if enabled (only if decode succeeded)
    // -------------------------------------------------------------------------
    if (res == JDR_OK && useCache)
    {
        if (!thumbnail_cache::put(url, reqW, reqH, jpeg_buf, jpegLen))
        {
            ESP_LOGW(TAG, "Failed to store thumbnail in cache");
        }
        else
        {
            ESP_LOGI(TAG, "Thumbnail stored in cache for %s", url.c_str());
        }
    }

    heap_caps_free(jpeg_buf);
    heap_caps_free(work);

    if (res != JDR_OK)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %d", res);
        if (px)
            heap_caps_free(px);
        return false;
    }

    // -------------------------------------------------------------------------
    // 9. Build LVGL image descriptor
    // -------------------------------------------------------------------------
    lv_image_dsc_t *dsc = new lv_image_dsc_t{};
    dsc->header.cf = LV_COLOR_FORMAT_RGB888;
    dsc->header.w = W;
    dsc->header.h = H;
    dsc->header.stride = W * 3;
    dsc->data_size = W * H * 3;
    dsc->data = px;

    *out_dsc = dsc;
    *out_px = px;

    ESP_LOGI(TAG, "fetch_and_decode_jpeg succeeded: %ux%u image", W, H);
    ESP_LOGI("DBG", "Task %s HWM: %u", pcTaskGetName(NULL),
             uxTaskGetStackHighWaterMark(NULL));

    return true;
}

static void snackbar_timer_callback(TimerHandle_t xTimer)
{
    lv_lock();
    if (objects.snackbar && lv_obj_is_valid(objects.snackbar))
    {
        lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_unlock();
}

void showSnackbar(const char *message, int duration_ms)
{
    // Stop existing timer if any
    if (snackbar_timer != nullptr)
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

    // Create a one-shot timer to hide snackbar after duration_ms
    snackbar_timer = xTimerCreate("snackbar", pdMS_TO_TICKS(duration_ms), pdFALSE, nullptr, snackbar_timer_callback);
    if (snackbar_timer != nullptr)
    {
        xTimerStart(snackbar_timer, 0);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create snackbar timer");
    }
}

void showSpinner()
{
    lv_lock();
    if (objects.spinner && lv_obj_is_valid(objects.spinner))
    {
        lv_obj_clear_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    }
    lv_unlock();
}

void hideSpinner()
{
    lv_lock();
    if (objects.spinner && lv_obj_is_valid(objects.spinner))
    {
        lv_obj_add_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    }
    lv_unlock();
}

void thumb_worker_task(void *arg)
{
    ThumbWorkerCtx *wctx = (ThumbWorkerCtx *)arg;

    for (ThumbContext *ctx : wctx->items)
    {
        esp_task_wdt_reset();
        if (ctx->generation != wctx->generation)
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
            // Clean up shimmer if it exists
            if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
            {
                stop_shimmer_animation(ctx->shimmer);
                lv_obj_del(ctx->shimmer);
                ctx->shimmer = nullptr;
            }
            lv_unlock();
            delete ctx;
            continue;
        }

        ESP_LOGI(TAG, "Fetching thumb: %s", ctx->url.c_str());
        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;

        vTaskDelay(1);
        if (fetch_and_decode_jpeg(ctx->url, wctx->maxWidth, wctx->maxHeight, &dsc, &px, wctx->enableCache))
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (!ctx->cancelled.load() && ctx->thumb && lv_obj_is_valid(ctx->thumb))
            {
                // Clean up shimmer overlay before setting image
                if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
                {
                    stop_shimmer_animation(ctx->shimmer);
                    lv_obj_del(ctx->shimmer);
                    ctx->shimmer = nullptr;
                }
                lv_image_set_src(thumb, dsc);

                lv_obj_set_style_clip_corner(thumb, true, 0);     // ensure rounded corners clip
                lv_obj_set_style_bg_opa(thumb, LV_OPA_TRANSP, 0); // remove grey background
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
                ThumbDataCtx *data_ctx = new ThumbDataCtx{dsc, px};
                lv_obj_add_event_cb(thumb, free_thumb_data_cb, LV_EVENT_DELETE, data_ctx);
            }
            else
            {
                heap_caps_free(px);
                delete dsc;
            }
            lv_unlock();
        }
        else
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
            // Clean up shimmer if it exists
            if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
            {
                stop_shimmer_animation(ctx->shimmer);
                lv_obj_del(ctx->shimmer);
                ctx->shimmer = nullptr;
            }
            lv_unlock();
            ESP_LOGW(TAG, "Thumb fetch failed: %s", ctx->url.c_str());
        }

        delete ctx;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    delete wctx;
    vTaskDelete(NULL);
}