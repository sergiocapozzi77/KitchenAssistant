#include <vector>
#include <map>
#include <string>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include "mbedtls/base64.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "tjpgd.h"
#include "secrets.h"
#include "LeonardoImageGenerator.h"
#include "AppwriteClientInstance.h"
#include "cJSON.h"
#include "thumbnail_cache.h"
#include "thumbnail_manager.h"

static const char *TAG = "THUMB";

// ── SemaphoreGuard ──────────────────────────────────────────────────────────

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

// ── Queue state ─────────────────────────────────────────────────────────────

#define THUMB_QUEUE_DEPTH 16

static QueueHandle_t s_thumb_queue = nullptr;
static SemaphoreHandle_t s_http_concurrency_sem = nullptr;

// Leonardo URL cache + its mutex
static std::map<std::string, std::string> s_leonardo_url_cache;
static SemaphoreHandle_t s_leonardo_cache_mutex = nullptr;

// Thumbnail sizing (set once at init)
uint16_t s_thumb_max_w = 112;
uint16_t s_thumb_max_h = 112;
static bool s_thumb_cache = true;

std::atomic<uint32_t> s_thumb_generation{0};

// ── Init ────────────────────────────────────────────────────────────────────

void thumbnail_manager_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
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
        1, // lowest non-idle — below draw workers (prio 2) to avoid priority inversion
        nullptr,
        1, // core 1 keeps HTTP off the LVGL core
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "Thumbnail manager initialised (%ux%u)", thumbMaxWidth, thumbMaxHeight);
}

// ── Queue API ───────────────────────────────────────────────────────────────

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

// ── Shimmer ─────────────────────────────────────────────────────────────────

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

void stop_all_shimmer_animations()
{
    // Delete ALL animations whose exec_cb is shimmer_anim_cb,
    // regardless of the animated object (var == NULL matches all).
    // This prevents invalidation-walk crashes during screen transitions.
    lv_anim_delete(NULL, shimmer_anim_cb);
}

// ── Thumb lifecycle callbacks ───────────────────────────────────────────────

void thumb_obj_deleted_cb(lv_event_t *e)
{
    ThumbContext *ctx = (ThumbContext *)lv_event_get_user_data(e);
    if (!ctx)
        return;
    ctx->cancelled.store(true);
    ctx->thumb = nullptr;
    // Shimmer is a child of thumb (create_shimmer_overlay), so it has already been
    // cascade-deleted by LVGL before this LV_EVENT_DELETE fires.  Do NOT call
    // lv_obj_is_valid on it — the freed memory may have been reused by another
    // LVGL object, causing a false positive and corrupting that object.
    ctx->shimmer = nullptr;
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

// ── JPEG decode callbacks ───────────────────────────────────────────────────

static size_t tjpgd_in_cb(JDEC *jd, uint8_t *buf, size_t n)
{
    JpegIo *io = (JpegIo *)jd->device;
    size_t avail = io->src_len - io->src_pos;
    n = (n < avail) ? n : avail;
    if (buf)
        memcpy(buf, io->src + io->src_pos, n);
    io->src_pos += n;
    return n;
}

static int tjpgd_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    // ESP_LOGI("WDT", "WDT reset 2");
    //  esp_task_wdt_reset();
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
    // ESP_LOGI("WDT", "WDT reset 3");
    //  esp_task_wdt_reset();

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
        // ESP_LOGI("WDT", "WDT reset 4");
        //  esp_task_wdt_reset();
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

// ── Appwrite image-resize helper ────────────────────────────────────────────

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

// ── Leonardo URL cache ──────────────────────────────────────────────────────

std::string get_leonardo_cached_url(const std::string &url, uint16_t w, uint16_t h)
{
    std::string key = url + "|" + std::to_string(w) + "x" + std::to_string(h);
    xSemaphoreTake(s_leonardo_cache_mutex, portMAX_DELAY);
    auto it = s_leonardo_url_cache.find(key);
    std::string result = (it != s_leonardo_url_cache.end()) ? it->second : "";
    xSemaphoreGive(s_leonardo_cache_mutex);
    return result;
}

// ── Main fetch-and-decode ──────────────────────────────────────────────────

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

    std::string b64Str;
    bool fetchOk = fetch_resized_base64(image_url_to_fetch, W, H, b64Str);

    if (!fetchOk)
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
            // ESP_LOGI("WDT", "WDT reset 5");
            //  esp_task_wdt_reset();
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

// Centralize shimmer deletion so ctx->shimmer is always kept in sync
static void delete_ctx_shimmer(ThumbContext *ctx)
{
    // Must be called with lv_lock held
    if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
    {
        stop_shimmer_animation(ctx->shimmer);
        lv_obj_del(ctx->shimmer);
    }
    ctx->shimmer = nullptr; // always null, even if already invalid
}

// ── Worker task ─────────────────────────────────────────────────────────────

void thumb_worker_task(void *)
{
    ThumbContext *ctx = nullptr;

    while (true)
    {

        // esp_task_wdt_reset();
        if (xQueueReceive(s_thumb_queue, &ctx, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;

        // Stale check
        if (ctx->generation != s_thumb_generation.load())
        {
            lv_lock();
            // If cancelled, thumb_obj_deleted_cb already ran and nulled both pointers.
            // Do NOT call lv_obj_is_valid on shimmer — freed memory may have been reused.
            if (!ctx->cancelled.load())
            {
                if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
                {
                    lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
                    if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
                    {
                        stop_shimmer_animation(ctx->shimmer);
                        delete_ctx_shimmer(ctx);
                    }
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
                                        &dsc, &px, ctx->cacheAllowed);

        lv_lock();

        // Always clean up shimmer first
        if (ctx->shimmer && lv_obj_is_valid(ctx->shimmer))
        {
            stop_shimmer_animation(ctx->shimmer);
            delete_ctx_shimmer(ctx);
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
