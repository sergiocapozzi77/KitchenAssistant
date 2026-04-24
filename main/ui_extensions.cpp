#include <vector>
#include <map>
#include <string>
#include <atomic>
#include <algorithm>
#include <ctime>
#include <cmath>
#include "mbedtls/base64.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#ifdef CONFIG_ESP_LP_WDT_ENABLE
#include "esp_lp_wdt.h"
#endif
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
#include "freertos/queue.h"
#include "secrets.h"
#include "HttpClientHelper.h"
#include "LeonardoImageGenerator.h"
#include "AppwriteClientInstance.h"
#include "cJSON.h"
#include "thumbnail_cache.h"

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
std::atomic<uint32_t> s_thumb_generation{0};

// ── SemaphoreGuard (internal only) ───────────────────────────────────────────

class SemaphoreGuard
{
public:
    explicit SemaphoreGuard(SemaphoreHandle_t sem) : sem_(sem)
    {
        acquired_ = (sem_ && xSemaphoreTake(sem_, portMAX_DELAY) == pdTRUE);
    }
    ~SemaphoreGuard()
    {
        if (acquired_)
            xSemaphoreGive(sem_);
    }
    bool acquired() const { return acquired_; }
    void release()
    {
        if (acquired_)
        {
            xSemaphoreGive(sem_);
            acquired_ = false;
        }
    }

private:
    SemaphoreHandle_t sem_;
    bool acquired_;
};

// ── Thumbnail queue state (internal) ─────────────────────────────────────────

#define THUMB_QUEUE_DEPTH 16

static QueueHandle_t s_thumb_queue = nullptr;
static SemaphoreHandle_t s_http_concurrency_sem = nullptr;

// Leonardo URL cache + its mutex
static std::map<std::string, std::string> s_leonardo_url_cache;
static SemaphoreHandle_t s_leonardo_cache_mutex = nullptr;

// Thumbnail sizing (set once at init)
static uint16_t s_thumb_max_w = 112;
static uint16_t s_thumb_max_h = 112;
static bool s_thumb_cache = true;

// Forward declarations
void thumb_worker_task(void *);
bool fetch_and_decode_jpeg(const std::string &url,
                           uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc,
                           uint8_t **out_px,
                           bool useCache);

// ── Public init ──────────────────────────────────────────────────────────────

void ui_extensions_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                        bool thumbEnableCache)
{
    s_thumb_max_w = thumbMaxWidth;
    s_thumb_max_h = thumbMaxHeight;
    s_thumb_cache = thumbEnableCache;

    // HTTP concurrency: 1 = fully serialised
    s_http_concurrency_sem = xSemaphoreCreateCounting(1, 1);
    configASSERT(s_http_concurrency_sem);

    s_leonardo_cache_mutex = xSemaphoreCreateMutex();
    configASSERT(s_leonardo_cache_mutex);

    s_thumb_queue = xQueueCreate(THUMB_QUEUE_DEPTH, sizeof(ThumbContext *));
    configASSERT(s_thumb_queue);

    xTaskCreatePinnedToCoreWithCaps(
        thumb_worker_task, "thumb_worker",
        8192, nullptr,
        4, // below LVGL, above idle
        nullptr,
        1, // core 1 keeps HTTP off the LVGL core
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // Styles
    init_styles();

    ESP_LOGI(TAG, "ui_extensions initialised (%ux%u thumbnails)", thumbMaxWidth, thumbMaxHeight);
}

// ── Thumbnail queue public API ────────────────────────────────────────────────

void thumb_queue_push(ThumbContext *ctx)
{
    ctx->generation = s_thumb_generation.load();
    if (xQueueSend(s_thumb_queue, &ctx, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Thumb queue full, dropping: %s", ctx->url.c_str());
        delete ctx;
    }
}

void thumb_queue_cancel_all()
{
    s_thumb_generation.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "Thumb generation bumped to %u", s_thumb_generation.load());
}

// ── Shimmer ───────────────────────────────────────────────────────────────────

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
    lv_obj_set_style_bg_grad_color(shimmer, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(shimmer, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_stop(shimmer, 255, 0);
    lv_obj_set_style_bg_main_stop(shimmer, 0, 0);
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
}

void stop_and_delete_shimmer(lv_obj_t *shimmer)
{
    if (!shimmer || !lv_obj_is_valid(shimmer))
        return;
    lv_anim_delete(shimmer, shimmer_anim_cb);
    lv_obj_del(shimmer);
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

// ── Thumb lifecycle callbacks ─────────────────────────────────────────────────

void thumb_obj_deleted_cb(lv_event_t *e)
{
    ThumbContext *ctx = (ThumbContext *)lv_event_get_user_data(e);
    if (!ctx)
        return;
    ctx->cancelled.store(true);
    ctx->thumb = nullptr;
    if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
    {
        stop_shimmer_animation(ctx->shimmer);
        ctx->shimmer = nullptr;
    }
}

void free_thumb_data_cb(lv_event_t *e)
{
    ThumbDataCtx *d = (ThumbDataCtx *)lv_event_get_user_data(e);
    if (!d)
        return;
    free((void *)d->dsc->data);
    delete d->dsc;
    delete d;
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
        vTaskDelay(pdMS_TO_TICKS(10));
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
                                          const std::string &recipeSource)
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
        lv_obj_t *shimmer = create_shimmer_overlay(thumb);
        start_shimmer_animation(shimmer, thumb);

        ThumbContext *tctx = new ThumbContext{thumb, shimmer, thumbUrl, 0 /*set by push*/, {}};
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
                                    recipe.totalTime, recipe.recipeSource);
}

lv_obj_t *createRecipeCard(lv_obj_t *parent, const Favorite &fav)
{
    return createRecipeCardInternal(parent, fav.name, fav.description,
                                    fav.imageUrl, fav.difficulty,
                                    fav.totalTime, "");
}

// ── JPEG fetch & decode ───────────────────────────────────────────────────────

size_t tjpgd_in_cb(JDEC *jd, uint8_t *buf, size_t n)
{
    esp_task_wdt_reset();
#ifdef CONFIG_ESP_LP_WDT_ENABLE
    esp_lp_wdt_feed();
#endif
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
    esp_task_wdt_reset();
#ifdef CONFIG_ESP_LP_WDT_ENABLE
    esp_lp_wdt_feed();
#endif
    static int s_rect_count = 0;
    if ((++s_rect_count & 7) == 0)
        taskYIELD();

    JpegIo *io = (JpegIo *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    int cols = rect->right - rect->left + 1;

    for (int y = rect->top; y <= rect->bottom; y++)
    {
        uint8_t *dst = io->dst + (y * io->out_w + rect->left) * 3;
        for (int x = 0; x < cols; x++, src += 3, dst += 3)
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        }
    }
    return 1;
}

static bool decode_jpeg_buffer(uint8_t *jpeg_buf, size_t jpeg_len,
                               uint8_t **out_px,
                               uint16_t *out_width, uint16_t *out_height)
{
    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_INTERNAL);
    if (!work)
    {
        ESP_LOGE(TAG, "work malloc failed");
        return false;
    }

    JpegIo io = {jpeg_buf, jpeg_len, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, 3100, &io);
    esp_task_wdt_reset();

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
            heap_caps_free(work);
            return false;
        }

        io.dst = px;
        io.out_w = decoded_w;
        *out_width = decoded_w;
        *out_height = decoded_h;

        res = jd_decomp(&jd, tjpgd_out_cb, jd.scale);
        esp_task_wdt_reset();
    }

    heap_caps_free(work);

    if (res != JDR_OK)
    {
        if (px)
            heap_caps_free(px);
        return false;
    }
    *out_px = px;
    return true;
}

static bool fetch_resized_base64(const std::string &image_url,
                                 uint16_t W, uint16_t H,
                                 std::string &out_b64)
{
    cJSON *bodyJson = cJSON_CreateObject();
    if (!bodyJson)
        return false;
    cJSON_AddStringToObject(bodyJson, "url", image_url.c_str());
    cJSON_AddNumberToObject(bodyJson, "maxWidth", W);
    cJSON_AddNumberToObject(bodyJson, "maxHeight", H);

    char *bodyStr = cJSON_PrintUnformatted(bodyJson);
    cJSON_Delete(bodyJson);
    if (!bodyStr)
        return false;

    int status = 0;
    std::string response = getAppwriteClient().executeFunction(
        APPWRITE_IMAGE_RESIZE_FUNCTION_ID, bodyStr, false, status);
    free(bodyStr);

    if ((status != 200 && status != 201 && status != 202) || response.empty())
    {
        ESP_LOGE(TAG, "HTTP request failed (status %d)", status);
        return false;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
        return false;

    cJSON *rbField = cJSON_GetObjectItem(root, "responseBody");
    if (!rbField || !cJSON_IsString(rbField))
    {
        cJSON_Delete(root);
        return false;
    }

    cJSON *inner = cJSON_Parse(rbField->valuestring);
    cJSON_Delete(root);
    if (!inner)
        return false;

    cJSON *imgField = cJSON_GetObjectItem(inner, "image");
    if (!imgField || !cJSON_IsString(imgField))
    {
        cJSON_Delete(inner);
        return false;
    }

    out_b64 = imgField->valuestring;
    cJSON_Delete(inner);
    return true;
}

std::string get_leonardo_cached_url(const std::string &url, uint16_t w, uint16_t h)
{
    std::string key = url + "|" + std::to_string(w) + "x" + std::to_string(h);
    xSemaphoreTake(s_leonardo_cache_mutex, portMAX_DELAY);
    auto it = s_leonardo_url_cache.find(key);
    std::string result = (it != s_leonardo_url_cache.end()) ? it->second : "";
    xSemaphoreGive(s_leonardo_cache_mutex);
    return result;
}

bool fetch_and_decode_jpeg(const std::string &url,
                           uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc,
                           uint8_t **out_px,
                           bool useCache)
{
    uint16_t reqW = W, reqH = H;

    // 1. Cache lookup
    std::vector<uint8_t> cached_jpeg;
    if (useCache && thumbnail_cache::get(url, W, H, cached_jpeg))
    {
        uint8_t *buf = (uint8_t *)heap_caps_malloc(cached_jpeg.size(),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf)
            return false;
        memcpy(buf, cached_jpeg.data(), cached_jpeg.size());

        uint8_t *px = nullptr;
        uint16_t dw, dh;
        if (!decode_jpeg_buffer(buf, cached_jpeg.size(), &px, &dw, &dh))
        {
            heap_caps_free(buf);
            return false;
        }
        heap_caps_free(buf);

        lv_image_dsc_t *dsc = new lv_image_dsc_t{};
        dsc->header.cf = LV_COLOR_FORMAT_RGB888;
        dsc->header.w = dw;
        dsc->header.h = dh;
        dsc->header.stride = dw * 3;
        dsc->data_size = dw * dh * 3;
        dsc->data = px;
        *out_dsc = dsc;
        *out_px = px;
        return true;
    }

    // 2. Acquire HTTP semaphore
    SemaphoreGuard semGuard(s_http_concurrency_sem);
    if (!semGuard.acquired())
        return false;

    // 3. Resolve URL (Leonardo generate: or plain)
    std::string image_url_to_fetch;
    if (url.find("generate:") == 0)
    {
        std::string rest = url.substr(9);
        size_t delim = rest.find("|||");
        std::string recipeName = (delim != std::string::npos) ? rest.substr(0, delim) : rest;
        std::string recipeDesc = (delim != std::string::npos) ? rest.substr(delim + 3) : "";

        if (strlen(LEONARDO_API_KEY) == 0)
            return false;

        // Check Leonardo URL cache (mutex-protected)
        std::string cache_key = url + "|" + std::to_string(W) + "x" + std::to_string(H);
        {
            xSemaphoreTake(s_leonardo_cache_mutex, portMAX_DELAY);
            auto it = s_leonardo_url_cache.find(cache_key);
            if (it != s_leonardo_url_cache.end())
                image_url_to_fetch = it->second;
            xSemaphoreGive(s_leonardo_cache_mutex);
        }

        if (image_url_to_fetch.empty())
        {
            std::string prompt = "Generate an appetizing, high-quality food photography image of ";
            prompt += recipeName;
            if (!recipeDesc.empty())
            {
                prompt += ". Description: ";
                prompt += recipeDesc;
            }
            prompt += ". Professional food photography, realistic, well-lit.";

            static LeonardoImageGenerator leonardoGen(LEONARDO_ENDPOINT, LEONARDO_IMAGE_MODEL,
                                                      LEONARDO_API_KEY, 120000);
            int status = 0;
            image_url_to_fetch = leonardoGen.generateImage(prompt, W, H, status);
            if (image_url_to_fetch.empty())
                return false;

            xSemaphoreTake(s_leonardo_cache_mutex, portMAX_DELAY);
            s_leonardo_url_cache[cache_key] = image_url_to_fetch;
            xSemaphoreGive(s_leonardo_cache_mutex);
        }
    }
    else
    {
        image_url_to_fetch = url;
    }

    // 4. Fetch + base64 decode
    std::string b64Str;
    if (!fetch_resized_base64(image_url_to_fetch, W, H, b64Str))
        return false;

    const uint8_t *b64 = reinterpret_cast<const uint8_t *>(b64Str.c_str());
    size_t b64Len = b64Str.size();

    size_t jpegLen = 0;
    int ret = mbedtls_base64_decode(nullptr, 0, &jpegLen, b64, b64Len);
    if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
        return false;

    uint8_t *jpeg_buf = (uint8_t *)heap_caps_malloc(jpegLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buf)
        return false;

    ret = mbedtls_base64_decode(jpeg_buf, jpegLen, &jpegLen, b64, b64Len);
    if (ret != 0)
    {
        heap_caps_free(jpeg_buf);
        return false;
    }

    // 5. Release semaphore before CPU-intensive decode
    semGuard.release();

    // 6. Decode
    uint8_t *px = nullptr;
    uint16_t decoded_w = 0, decoded_h = 0;
    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_INTERNAL);
    if (!work)
    {
        heap_caps_free(jpeg_buf);
        return false;
    }

    JpegIo io = {jpeg_buf, jpegLen, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, 3100, &io);

    if (res == JDR_OK)
    {
        jd.scale = 0;
        decoded_w = jd.width >> jd.scale;
        decoded_h = jd.height >> jd.scale;

        px = (uint8_t *)heap_caps_malloc(decoded_w * decoded_h * 3,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (px)
        {
            io.dst = px;
            io.out_w = decoded_w;
            W = decoded_w;
            H = decoded_h;
            res = jd_decomp(&jd, tjpgd_out_cb, jd.scale);
            esp_task_wdt_reset();
        }
        else
            res = JDR_MEM1;
    }

    // 7. Store in cache
    if (res == JDR_OK && useCache)
        thumbnail_cache::put(url, reqW, reqH, jpeg_buf, jpegLen);

    heap_caps_free(jpeg_buf);
    heap_caps_free(work);

    if (res != JDR_OK)
    {
        if (px)
            heap_caps_free(px);
        return false;
    }

    // 8. Build LVGL descriptor
    lv_image_dsc_t *dsc = new lv_image_dsc_t{};
    dsc->header.cf = LV_COLOR_FORMAT_RGB888;
    dsc->header.w = W;
    dsc->header.h = H;
    dsc->header.stride = W * 3;
    dsc->data_size = W * H * 3;
    dsc->data = px;
    *out_dsc = dsc;
    *out_px = px;
    return true;
}

// ── Thumb worker (single persistent task) ────────────────────────────────────

void thumb_worker_task(void *)
{
    esp_task_wdt_add(NULL);
    ThumbContext *ctx = nullptr;

    while (true)
    {

        esp_task_wdt_reset();
        if (xQueueReceive(s_thumb_queue, &ctx, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;

        // Stale check
        if (ctx->generation != s_thumb_generation.load())
        {
            ESP_LOGI(TAG, "Stale thumb, skipping: %s", ctx->url.c_str());
            lv_lock();
            if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
            {
                lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
                if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
                {
                    stop_shimmer_animation(ctx->shimmer);
                    lv_obj_del(ctx->shimmer);
                }
            }
            lv_unlock();
            delete ctx;
            continue;
        }

        vTaskDelay(1);
        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;
        uint16_t w = ctx->maxW ? ctx->maxW : s_thumb_max_w;
        uint16_t h = ctx->maxH ? ctx->maxH : s_thumb_max_h;
        bool ok = fetch_and_decode_jpeg(ctx->url, w, h,
                                        &dsc, &px, s_thumb_cache);

        lv_lock();

        // Always clean up shimmer first
        if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
        {
            stop_shimmer_animation(ctx->shimmer);
            lv_obj_del(ctx->shimmer);
            ctx->shimmer = nullptr;
        }

        if (ok && !ctx->cancelled.load() &&
            ctx->thumb && lv_obj_is_valid(ctx->thumb))
        {
            lv_image_set_src(ctx->thumb, dsc);
            lv_obj_set_style_clip_corner(ctx->thumb, true, 0);
            lv_obj_set_style_bg_opa(ctx->thumb, LV_OPA_TRANSP, 0);
            lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
            lv_obj_add_event_cb(ctx->thumb, free_thumb_data_cb,
                                LV_EVENT_DELETE, new ThumbDataCtx{dsc, px});
        }
        else
        {
            if (ok)
            {
                heap_caps_free(px);
                delete dsc;
            }
            if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
                lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
        }

        lv_unlock();
        delete ctx;

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
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