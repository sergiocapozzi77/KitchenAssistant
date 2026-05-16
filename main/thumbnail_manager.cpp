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
// #include "thumbnail_cache.h"  // DISABLED
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

// Must be called with lv_lock held.
// Safe to call even if ctx->shimmer is nullptr or already invalid.
void delete_ctx_shimmer(ThumbContext *ctx)
{
    if (!ctx || !ctx->shimmer)
        return;

    // CRITICAL: Only delete shimmer if it's still a child of ctx->thumb.
    // Shimmer is created as a child of thumb (create_shimmer_overlay(thumb)).
    // If thumb was cascade-deleted (screen transition), shimmer was also
    // cascade-deleted and ctx->shimmer is a dangling pointer.
    //
    // lv_obj_is_valid() alone is NOT sufficient here: if the freed shimmer
    // memory was reused by another valid LVGL object, lv_obj_is_valid()
    // would incorrectly return true, causing us to delete the wrong object
    // and corrupt the event system.
    //
    // By verifying shimmer's parent is still ctx->thumb, we catch both:
    // 1. Cascade-deleted shimmer (parent was cleared by LVGL)
    // 2. Freed-and-reused memory (the imposter object has a different parent)
    if (ctx->thumb &&
        lv_obj_is_valid(ctx->thumb) &&
        lv_obj_is_valid(ctx->shimmer) &&
        lv_obj_get_parent(ctx->shimmer) == ctx->thumb)
    {
        stop_shimmer_animation(ctx->shimmer);
        lv_obj_del(ctx->shimmer);
    }
    ctx->shimmer = nullptr; // always sync, regardless of prior validity
}

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
    ESP_LOGI(TAG, "Queue push: gen=%u url=%.60s", ctx->generation, ctx->url.c_str());
    if (xQueueSend(s_thumb_queue, &ctx, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Thumb queue full, dropping: %s", ctx->url.c_str());

        // FIX: Before deleting ctx we must remove the event callback that was
        // registered on ctx->thumb with ctx as user_data, and clean up the
        // shimmer. Without this, thumb_obj_deleted_cb would fire later with a
        // dangling ctx pointer, causing a crash / memory corruption.
        lv_lock();
        if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
            lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
        // delete_ctx_shimmer handles nullptr / already-invalid shimmer safely.
        delete_ctx_shimmer(ctx);
        lv_unlock();

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
    // Shimmer is a child of thumb (create_shimmer_overlay(thumb)). When this
    // callback fires (thumb's LV_EVENT_DELETE), shimmer has NOT been cascade-
    // deleted yet — LVGL processes children AFTER the delete event. However,
    // shimmer WILL be freed during children cleanup later in obj_delete_core.
    // Null ctx->shimmer now so delete_ctx_shimmer doesn't try to delete it.
    // Do NOT call lv_obj_is_valid on the shimmer pointer here — it's still
    // valid at this point but will be freed imminently.
    ctx->shimmer = nullptr;
}

void free_thumb_data_cb(lv_event_t *e)
{
    ThumbDataCtx *d = (ThumbDataCtx *)lv_event_get_user_data(e);
    if (!d || d->freed)
    {
        if (d && d->freed)
            ESP_LOGW(TAG, "free_thumb_data_cb: double-free prevented");
        return;
    }
    d->freed = true;

    // The image descriptor is externally managed (variable source), so
    // free the pixel data and descriptor directly.  LVGL won't try to
    // render this source after the object is deleted because the delete
    // holds the LVGL lock — no need to detach via lv_image_set_src(NULL).
    void *data = (void *)d->dsc->data;
    if (data)
    {
        free(data);
        d->dsc->data = nullptr;
    }
    delete d->dsc;
    d->dsc = nullptr;
    d->px = nullptr;
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
        ESP_LOGE(TAG, "HTTP request failed (status %d): %.*s",
                 status, (int)response.size(), response.c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "fetch_resized: root JSON parse failed, status=%d, response=%.100s",
                 status, response.c_str());
        return false;
    }

    cJSON *rbField = cJSON_GetObjectItem(root, "responseBody");
    if (!rbField || !cJSON_IsString(rbField))
    {
        ESP_LOGE(TAG, "fetch_resized: responseBody missing, status=%d, response=%.100s",
                 status, response.c_str());
        cJSON_Delete(root);
        return false;
    }

    // FIX: Copy responseBody string into a std::string *before* cJSON_Delete(root),
    // which would free rbField and its valuestring, making any later access UB.
    std::string rb_copy = rbField->valuestring;
    cJSON_Delete(root);

    cJSON *inner = cJSON_Parse(rb_copy.c_str());
    if (!inner)
    {
        ESP_LOGE(TAG, "fetch_resized: inner JSON parse failed, responseBody=%.100s",
                 rb_copy.c_str());
        return false;
    }

    cJSON *imgField = cJSON_GetObjectItem(inner, "image");
    if (!imgField || !cJSON_IsString(imgField))
    {
        ESP_LOGE(TAG, "fetch_resized: image field missing in inner JSON");
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
    // 1. Acquire HTTP semaphore
    SemaphoreGuard semGuard(s_http_concurrency_sem);
    if (!semGuard.acquired())
    {
        ESP_LOGE(TAG, "fetch fail: HTTP semaphore not acquired: %.60s", url.c_str());
        return false;
    }

    // 2. Resolve URL (Leonardo generate: or plain)
    std::string image_url_to_fetch;
    if (url.find("generate:") == 0)
    {
        std::string rest = url.substr(9);
        size_t delim = rest.find("|||");
        std::string recipeName = (delim != std::string::npos) ? rest.substr(0, delim) : rest;
        std::string recipeDesc = (delim != std::string::npos) ? rest.substr(delim + 3) : "";

        if (strlen(LEONARDO_API_KEY) == 0)
        {
            ESP_LOGE(TAG, "fetch fail: Leonardo API key empty: %.60s", url.c_str());
            return false;
        }

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
            {
                ESP_LOGE(TAG, "fetch fail: Leonardo generate returned empty: %.60s", url.c_str());
                return false;
            }

            xSemaphoreTake(s_leonardo_cache_mutex, portMAX_DELAY);
            s_leonardo_url_cache[cache_key] = image_url_to_fetch;
            xSemaphoreGive(s_leonardo_cache_mutex);
        }
    }
    else
    {
        image_url_to_fetch = url;
    }

    ESP_LOGI(TAG, "heap: internal=%u SPIRAM=%u before fetch",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    std::string b64Str;
    bool fetchOk = fetch_resized_base64(image_url_to_fetch, W, H, b64Str);

    if (!fetchOk)
    {
        ESP_LOGE(TAG, "fetch fail: fetch_resized_base64 returned false: %.60s",
                 image_url_to_fetch.c_str());
        return false;
    }

    const uint8_t *b64 = reinterpret_cast<const uint8_t *>(b64Str.c_str());
    size_t b64Len = b64Str.size();

    size_t jpegLen = 0;
    int ret = mbedtls_base64_decode(nullptr, 0, &jpegLen, b64, b64Len);
    if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        ESP_LOGE(TAG, "fetch fail: base64 header decode error=%d: %.60s", ret, url.c_str());
        return false;
    }

    uint8_t *jpeg_buf = (uint8_t *)heap_caps_malloc(jpegLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buf)
    {
        ESP_LOGE(TAG, "fetch fail: OOM for JPEG buf (%zu bytes): %.60s", jpegLen, url.c_str());
        return false;
    }

    ret = mbedtls_base64_decode(jpeg_buf, jpegLen, &jpegLen, b64, b64Len);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "fetch fail: base64 body decode error=%d: %.60s", ret, url.c_str());
        heap_caps_free(jpeg_buf);
        return false;
    }

    // 3. Release semaphore before CPU-intensive decode
    semGuard.release();

    // 4. Decode
    uint8_t *px = nullptr;
    uint16_t decoded_w = 0, decoded_h = 0;
    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!work)
    {
        ESP_LOGE(TAG, "fetch fail: OOM for TJpgD work buf: %.60s", url.c_str());
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
            if (res != JDR_OK)
                ESP_LOGE(TAG, "fetch fail: jd_decomp error=%d: %.60s", (int)res, url.c_str());
        }
        else
        {
            ESP_LOGE(TAG, "fetch fail: OOM for px buf (%ux%u): %.60s",
                     decoded_w, decoded_h, url.c_str());
            res = JDR_MEM1;
        }
    }
    else
    {
        ESP_LOGE(TAG, "fetch fail: jd_prepare error=%d: %.60s", (int)res, url.c_str());
    }

    heap_caps_free(jpeg_buf);
    heap_caps_free(work);

    if (res != JDR_OK)
    {
        if (px)
            heap_caps_free(px);
        return false;
    }

    // 5. Build LVGL descriptor
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

// ── Centralized shimmer cleanup ──────────────────────────────────────────────

// ── Worker task ─────────────────────────────────────────────────────────────

void thumb_worker_task(void *)
{
    ThumbContext *ctx = nullptr;

    while (true)
    {
        if (xQueueReceive(s_thumb_queue, &ctx, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            static uint32_t s_wait_count = 0;
            if ((++s_wait_count & 15) == 0) // every ~16 seconds
                ESP_LOGI(TAG, "Worker idle (waiting for queue)...");
            continue;
        }

        ESP_LOGI(TAG, "Worker got: gen=%u cur_gen=%u url=%.60s",
                 ctx->generation, s_thumb_generation.load(), ctx->url.c_str());

        // ── Stale check ────────────────────────────────────────────────────
        if (ctx->generation != s_thumb_generation.load())
        {
            ESP_LOGI(TAG, "Stale: gen=%u cur=%u url=%.60s",
                     ctx->generation, s_thumb_generation.load(), ctx->url.c_str());

            lv_lock();
            if (!ctx->cancelled.load())
            {
                if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
                    lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
                // Do NOT call delete_ctx_shimmer here — the shimmer belongs to a
                // past generation and its memory may have been freed and reused by
                // another LVGL child of ctx->thumb (e.g. during a recipe list
                // rebuild).  Deleting what looks like a valid child would corrupt
                // the event system.  LVGL will cascade-delete the real shimmer
                // when the thumb itself is cleaned up.
                ctx->shimmer = nullptr;
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
        bool ok = fetch_and_decode_jpeg(ctx->url, w, h, &dsc, &px, ctx->cacheAllowed);

        ESP_LOGI(TAG, "fetch%s %s ok=%d w=%u h=%u gen=%u",
                 (ctx->cacheAllowed ? "" : " (no cache)"),
                 ctx->url.c_str(), ok, w, h, ctx->generation);

        lv_lock();

        // Re-check generation AFTER the lock.  The network fetch (above) ran
        // without the lock, and the recipe list could have been rebuilt in the
        // meantime — deleting ctx->thumb's children (including shimmer) and
        // incrementing s_thumb_generation.  If we proceed now, ctx->shimmer
        // may be a dangling pointer whose memory was reused by a different
        // LVGL child of ctx->thumb, and delete_ctx_shimmer would delete the
        // wrong object and corrupt the event system.
        if (ctx->generation != s_thumb_generation.load())
        {
            ESP_LOGI(TAG, "Stale after fetch: gen=%u cur=%u url=%.60s",
                     ctx->generation, s_thumb_generation.load(), ctx->url.c_str());
            if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
                lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
            // ctx->shimmer may point to freed memory or an imposter — do NOT
            // call delete_ctx_shimmer.  The shimmer is already gone or belongs
            // to a fresher context.  Just null the pointer and let LVGL handle
            // the shimmers of current-generation thumbs.
            ctx->shimmer = nullptr;
            // dsc / px were allocated by fetch_and_decode_jpeg — free them.
            if (ok) { heap_caps_free(px); px = nullptr; delete dsc; dsc = nullptr; }
            lv_unlock();
            delete ctx;
            continue;
        }

        // Always clean up shimmer first — regardless of fetch outcome.
        // FIX: delete_ctx_shimmer already nulls ctx->shimmer; don't repeat it.
        delete_ctx_shimmer(ctx);

        if (ok && !ctx->cancelled.load() &&
            ctx->thumb && lv_obj_is_valid(ctx->thumb))
        {
            lv_image_set_src(ctx->thumb, dsc);
            lv_obj_set_style_clip_corner(ctx->thumb, true, 0);
            lv_obj_set_style_bg_opa(ctx->thumb, LV_OPA_TRANSP, 0);
            lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb, ctx);
            lv_obj_add_event_cb(ctx->thumb, free_thumb_data_cb,
                                LV_EVENT_DELETE, new ThumbDataCtx{dsc, px});
            ESP_LOGI(TAG, "Image set: %.50s", ctx->url.c_str());
        }
        else
        {
            ESP_LOGI(TAG, "Skip img: ok=%d cancelled=%d thumb_valid=%d url=%.50s",
                     ok, ctx->cancelled.load(),
                     (ctx->thumb && lv_obj_is_valid(ctx->thumb)) ? 1 : 0,
                     ctx->url.c_str());
            if (ok)
            {
                heap_caps_free(px);
                px = nullptr;
                delete dsc;
                dsc = nullptr;
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