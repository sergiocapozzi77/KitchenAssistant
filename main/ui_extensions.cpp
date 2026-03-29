#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <ctime>
#include <cmath>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "UIEXTENSIONS";

// Global variables defined here
uint32_t s_thumb_generation = 0;

lv_style_t style_card;
lv_style_t style_header;
lv_style_t style_row;
lv_style_t style_qty_cont;
lv_style_t style_qty_btn;
lv_style_t style_del_btn;
lv_style_t style_expiry_badge;
lv_style_t style_checkbox_indicator;
bool styles_initialized = false;
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

    styles_initialized = true;
    ESP_LOGI(TAG, "Styles initialized");
}

// === HELPER FUNCTIONS ===

int days_until_expiry(const std::string &isoDate)
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
        ctx->thumb = nullptr;
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

bool fetch_and_decode_jpeg(const std::string &url, uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc, uint8_t **out_px)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 4096;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "http_client_init failed");
        return false;
    }

    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "http_client_open failed");
        esp_http_client_cleanup(client);
        return false;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d, content-length: %d", status, content_len);

    size_t buf_sz = (content_len > 0) ? (size_t)content_len : 64 * 1024;
    uint8_t *jpeg_buf = (uint8_t *)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buf)
    {
        ESP_LOGE(TAG, "jpeg_buf malloc failed, requested: %u", buf_sz);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    size_t total = 0;
    int n;
    while ((n = esp_http_client_read(client, (char *)jpeg_buf + total, buf_sz - total)) > 0)
        total += (size_t)n;
    ESP_LOGI(TAG, "Downloaded %u bytes", total);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total == 0)
    {
        ESP_LOGE(TAG, "No data received");
        free(jpeg_buf);
        return false;
    }

    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_INTERNAL);
    if (!work)
    {
        ESP_LOGE(TAG, "work malloc failed");
        free(jpeg_buf);
        return false;
    }

    JpegIo io = {jpeg_buf, total, 0, nullptr, 0};
    JDEC jd;

    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, 3100, &io);
    ESP_LOGI(TAG, "jd_prepare: %d  img size: %ux%u", res, jd.width, jd.height);

    uint8_t *px = nullptr;
    if (res == JDR_OK)
    {
        if (jd.width >= W * 8)
            jd.scale = 3; // 1/8
        else if (jd.width >= W * 4)
            jd.scale = 2; // 1/4
        else if (jd.width >= W * 2)
            jd.scale = 1; // 1/2
        else
            jd.scale = 0; // full

        ESP_LOGI(TAG, "Decoding with scale: 1/%d", 1 << jd.scale);

        uint16_t decoded_w = jd.width >> jd.scale;
        uint16_t decoded_h = jd.height >> jd.scale;
        px = (uint8_t *)heap_caps_malloc(decoded_w * decoded_h * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!px)
        {
            ESP_LOGE(TAG, "px malloc failed (%ux%u)", decoded_w, decoded_h);
            free(jpeg_buf);
            free(work);
            return false;
        }
        io.dst = px;
        io.out_w = decoded_w;
        W = decoded_w;
        H = decoded_h;

        res = jd_decomp(&jd, tjpgd_out_cb, jd.scale);
        ESP_LOGI(TAG, "jd_decomp: %d", res);
    }

    free(jpeg_buf);
    free(work);

    if (res != JDR_OK)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %d", res);
        free(px);
        return false;
    }

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

static void snackbar_timer_callback(TimerHandle_t xTimer)
{
    lv_lock();
    lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
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
    lv_label_set_text(objects.snackbar_text, message);
    lv_obj_clear_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_clear_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();
}

void hideSpinner()
{
    lv_lock();
    lv_obj_add_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();
}

void thumb_worker_task(void *arg)
{
    ThumbWorkerCtx *wctx = (ThumbWorkerCtx *)arg;

    for (ThumbContext *ctx : wctx->items)
    {
        // esp_task_wdt_reset();
        if (ctx->generation != s_thumb_generation)
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
            lv_unlock();
            delete ctx;
            continue;
        }

        ESP_LOGI(TAG, "Fetching thumb: %s", ctx->url.c_str());
        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;

        if (fetch_and_decode_jpeg(ctx->url, 112, 112, &dsc, &px))
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
            {
                lv_image_set_src(thumb, dsc);
                lv_obj_set_size(thumb, 112, 112);
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
                ThumbDataCtx *data_ctx = new ThumbDataCtx{dsc, px};
                lv_obj_add_event_cb(thumb, free_thumb_data_cb, LV_EVENT_DELETE, data_ctx);
            }
            else
            {
                free(px);
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
            lv_unlock();
            ESP_LOGW(TAG, "Thumb fetch failed: %s", ctx->url.c_str());
        }

        delete ctx;
    }

    delete wctx;
    vTaskDelete(NULL);
}