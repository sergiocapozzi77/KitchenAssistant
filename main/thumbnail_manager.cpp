#include "thumbnail_manager.h"

#include "thumbnail_cache.h"

#include "diag.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tjpgd.h"

#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "THUMB";

namespace
{

// ── Limits ──────────────────────────────────────────────────────────────────

constexpr size_t kMaxImageBytes = 2 * 1024 * 1024;
constexpr size_t kInitialBufferBytes = 64 * 1024;
constexpr size_t kMaxDecodedBytes = 4 * 1024 * 1024;
constexpr size_t kTjpgdWorkBytes = 3100;
constexpr uint8_t kMaxJpegScale = 3; // tjpgd supports 1/1 … 1/8

#define THUMB_QUEUE_DEPTH 16

QueueHandle_t s_thumb_queue = nullptr;
SemaphoreHandle_t s_http_concurrency_sem = nullptr;
bool s_thumb_cache = true;
bool s_cache_ready = false;

// ── PSRAM buffer ────────────────────────────────────────────────────────────

class PsramBuffer
{
public:
    PsramBuffer() = default;
    ~PsramBuffer()
    {
        if (data_)
            heap_caps_free(data_);
    }
    PsramBuffer(const PsramBuffer &) = delete;
    PsramBuffer &operator=(const PsramBuffer &) = delete;

    bool ensure(size_t needed)
    {
        if (needed <= capacity_)
            return true;
        if (needed > kMaxImageBytes)
            return false;

        size_t new_cap = capacity_ ? capacity_ : kInitialBufferBytes;
        while (new_cap < needed)
            new_cap *= 2;
        if (new_cap > kMaxImageBytes)
            new_cap = kMaxImageBytes;

        void *grown = heap_caps_realloc(data_, new_cap,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown)
            return false;
        data_ = static_cast<uint8_t *>(grown);
        capacity_ = new_cap;
        return true;
    }

    bool assign(const uint8_t *src, size_t len)
    {
        if (!src || len == 0 || !ensure(len))
            return false;
        memcpy(data_, src, len);
        size_ = len;
        return true;
    }

    // Takes ownership of a PSRAM allocation (as returned by thumbnail_cache).
    bool adopt(uint8_t *data, size_t len)
    {
        if (!data || len == 0)
            return false;
        if (data_)
            heap_caps_free(data_);
        data_ = data;
        size_ = len;
        capacity_ = len;
        return true;
    }

    void advance(size_t len) { size_ += len; }
    uint8_t *data() { return data_; }
    size_t size() const { return size_; }

private:
    uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};

// ── Semaphore guard ─────────────────────────────────────────────────────────

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

// ── JPEG decode callbacks ───────────────────────────────────────────────────

struct JpegIo
{
    const uint8_t *src;
    size_t src_len;
    size_t src_pos;
    uint8_t *dst; // LVGL RGB888 (BGR byte order)
    uint16_t out_w;
};

size_t tjpgd_in_cb(JDEC *jd, uint8_t *buf, size_t n)
{
    auto *io = static_cast<JpegIo *>(jd->device);
    size_t avail = io->src_len - io->src_pos;
    n = (n < avail) ? n : avail;
    if (buf)
        memcpy(buf, io->src + io->src_pos, n);
    io->src_pos += n;
    return n;
}

int tjpgd_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    static int s_rect_count = 0;
    if ((++s_rect_count & 7) == 0)
        taskYIELD();

    auto *io = static_cast<JpegIo *>(jd->device);
    const auto *src = static_cast<const uint8_t *>(bitmap);
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

// Largest downscale that still covers the requested box. Cover-cropped images
// therefore never need upscaling on the device.
uint8_t choose_scale(uint16_t src_w, uint16_t src_h, uint16_t target_w,
                     uint16_t target_h)
{
    if (target_w == 0 || target_h == 0 || src_w == 0 || src_h == 0)
        return 0;

    uint8_t scale = 0;
    while (scale < kMaxJpegScale)
    {
        uint16_t next_w = static_cast<uint16_t>(src_w >> (scale + 1));
        uint16_t next_h = static_cast<uint16_t>(src_h >> (scale + 1));
        if (next_w < target_w || next_h < target_h || next_w == 0 || next_h == 0)
            break;
        ++scale;
    }
    return scale;
}

bool decode_jpeg_buffer(const uint8_t *jpeg, size_t jpeg_len, uint16_t target_w,
                        uint16_t target_h, lv_image_dsc_t **out_dsc,
                        uint8_t **out_px)
{
    if (!jpeg || jpeg_len == 0 || !out_dsc || !out_px)
        return false;

    // Prefer internal RAM, but the work buffer is only ever touched by the CPU
    // and tjpgd is happy with PSRAM. Falling back keeps thumbnails decoding when
    // the internal heap is fragmented rather than failing every cache hit.
    auto *work = static_cast<uint8_t *>(
        heap_caps_malloc(kTjpgdWorkBytes, MALLOC_CAP_INTERNAL));
    if (!work)
    {
        work = static_cast<uint8_t *>(
            heap_caps_malloc(kTjpgdWorkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!work)
    {
        ESP_LOGE(TAG, "OOM for TJpgD work buffer");
        return false;
    }

    JpegIo io{jpeg, jpeg_len, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, kTjpgdWorkBytes, &io);
    if (res != JDR_OK)
    {
        heap_caps_free(work);
        ESP_LOGE(TAG, "jd_prepare error=%d", (int)res);
        return false;
    }

    uint8_t scale = choose_scale(jd.width, jd.height, target_w, target_h);
    uint16_t decoded_w = static_cast<uint16_t>(jd.width >> scale);
    uint16_t decoded_h = static_cast<uint16_t>(jd.height >> scale);
    jd.scale = scale;

    if (decoded_w == 0 || decoded_h == 0)
    {
        heap_caps_free(work);
        ESP_LOGW(TAG, "JPEG has no pixels");
        return false;
    }

    size_t pixel_bytes = static_cast<size_t>(decoded_w) * decoded_h * 3;
    if (pixel_bytes > kMaxDecodedBytes)
    {
        heap_caps_free(work);
        ESP_LOGW(TAG, "decoded image too large (%ux%u)", decoded_w, decoded_h);
        return false;
    }

    auto *px = static_cast<uint8_t *>(
        heap_caps_malloc(pixel_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!px)
    {
        heap_caps_free(work);
        ESP_LOGE(TAG, "OOM for %u-byte image", (unsigned)pixel_bytes);
        return false;
    }

    io.dst = px;
    io.out_w = decoded_w;
    res = jd_decomp(&jd, tjpgd_out_cb, scale);
    heap_caps_free(work);

    if (res != JDR_OK)
    {
        heap_caps_free(px);
        ESP_LOGE(TAG, "jd_decomp error=%d", (int)res);
        return false;
    }

    auto *dsc = new lv_image_dsc_t{};
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB888;
    dsc->header.w = decoded_w;
    dsc->header.h = decoded_h;
    dsc->header.stride = decoded_w * 3;
    dsc->data_size = pixel_bytes;
    dsc->data = px;

    *out_dsc = dsc;
    *out_px = px;
    return true;
}

// ── HTTP download ───────────────────────────────────────────────────────────

bool download_jpeg(const std::string &url, PsramBuffer &out)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 1024;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.max_redirection_count = 5;
    cfg.user_agent = "KitchenAssistant/1.0";

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "http init failed: %.80s", url.c_str());
        return false;
    }

    bool ok = false;
    do
    {
        if (esp_http_client_open(client, 0) != ESP_OK)
        {
            ESP_LOGE(TAG, "http open failed: %.80s", url.c_str());
            break;
        }

        int64_t content_length = esp_http_client_fetch_headers(client);
        if (content_length > static_cast<int64_t>(kMaxImageBytes))
        {
            ESP_LOGW(TAG, "image too large (%lld bytes): %.80s",
                     (long long)content_length, url.c_str());
            break;
        }

        int status = esp_http_client_get_status_code(client);
        if (status != 200)
        {
            ESP_LOGE(TAG, "http status %d: %.80s", status, url.c_str());
            break;
        }

        uint8_t chunk[1024];
        bool read_failed = false;
        while (true)
        {
            int read_len = esp_http_client_read(
                client, reinterpret_cast<char *>(chunk), sizeof(chunk));
            if (read_len < 0)
            {
                ESP_LOGE(TAG, "http read error: %.80s", url.c_str());
                read_failed = true;
                break;
            }
            if (read_len == 0)
                break;

            size_t len = static_cast<size_t>(read_len);
            if (!out.ensure(out.size() + len))
            {
                ESP_LOGW(TAG, "image exceeds %u-byte budget: %.80s",
                         (unsigned)kMaxImageBytes, url.c_str());
                read_failed = true;
                break;
            }
            memcpy(out.data() + out.size(), chunk, len);
            out.advance(len);
        }

        if (read_failed || out.size() == 0)
            break;

        ok = true;
    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

std::string url_encode(const std::string &in)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~')
        {
            out += static_cast<char>(c);
        }
        else
        {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

bool is_allrecipes_url(const std::string &url)
{
    static const std::string kHost = "allrecipes.com";

    size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return false;

    size_t start = scheme + 3;
    size_t end = url.find('/', start);
    std::string host = url.substr(
        start, end == std::string::npos ? std::string::npos : end - start);

    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host.resize(colon);

    if (host == kHost)
        return true;
    return host.size() > kHost.size() &&
           host[host.size() - kHost.size() - 1] == '.' &&
           host.compare(host.size() - kHost.size(), kHost.size(), kHost) == 0;
}

} // namespace

// ── URL rewriting ───────────────────────────────────────────────────────────

// Declared in thumbnail_manager.h, so it must have external linkage — keep it
// outside the anonymous namespace above.
std::string thumbnail_url(const std::string &url, uint16_t width,
                          uint16_t height)
{
    static const std::string kObject = "/storage/v1/object/public/";
    static const std::string kRender = "/storage/v1/render/image/public/";
    static const std::string kWsrv = "https://wsrv.nl/";

    if (width == 0 || height == 0)
        return url;

    // wsrv is always the last hop. Skip an already-proxied URL so a thumbnail
    // that is queued twice does not get wrapped again.
    if (url.rfind(kWsrv, 0) == 0)
        return url;

    std::string source = url;

    size_t object_at = url.find(kObject);
    if (object_at != std::string::npos)
    {
        // Ask the Storage renderer for the target size first so the proxy only
        // has to pull a few KB instead of the original (often multi-MB) file.
        // Rendering does not help with the decode problem on its own: Supabase
        // emits progressive JPEGs, which tjpgd rejects with JDR_FMT3. The wsrv
        // hop below re-encodes the small render to baseline.
        source = url.substr(0, object_at) + kRender +
                 url.substr(object_at + kObject.size());
        source += (source.find('?') == std::string::npos) ? '?' : '&';
        source += "width=" + std::to_string(width) + "&height=" +
                  std::to_string(height) + "&resize=cover";
    }
    else if (url.find(kRender) == std::string::npos &&
             is_allrecipes_url(url))
    {
        // allrecipes.com rejects wsrv's fetches with 403. DuckDuckGo's image
        // proxy is allowed to fetch the same URL, so put it in front of wsrv.
        source = "https://external-content.duckduckgo.com/iu/?u=" +
                 url_encode(url) + "&f=1&nofb=1";
    }

    // wsrv resizes server-side and re-encodes to baseline JPEG. Several recipe
    // sources serve progressive JPEGs, and originals are often hundreds of KB;
    // the proxy turns both into a few-KB, decodable thumbnail.
    std::string out = kWsrv + "?url=" + url_encode(source);
    out += "&w=" + std::to_string(width) + "&h=" + std::to_string(height);
    out += "&fit=cover&output=jpg&q=70";
    return out;
}

// ── Default thumbnail size ──────────────────────────────────────────────────

uint16_t s_thumb_max_w = 112;
uint16_t s_thumb_max_h = 112;

std::atomic<uint32_t> s_thumb_generation{0};

// ── Init ────────────────────────────────────────────────────────────────────

void thumbnail_manager_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                            bool thumbEnableCache)
{
    s_thumb_max_w = thumbMaxWidth;
    s_thumb_max_h = thumbMaxHeight;
    s_thumb_cache = thumbEnableCache;

    // Two concurrent downloads keep the queue moving without starving TLS.
    s_http_concurrency_sem = xSemaphoreCreateCounting(2, 2);
    configASSERT(s_http_concurrency_sem);

    s_thumb_queue = xQueueCreate(THUMB_QUEUE_DEPTH, sizeof(ThumbContext *));
    configASSERT(s_thumb_queue);

    if (s_thumb_cache)
    {
        s_cache_ready = thumbnail_cache::init();
        if (!s_cache_ready)
            ESP_LOGW(TAG, "Thumbnail cache unavailable; downloading without cache");
    }

    TaskHandle_t worker_handle = nullptr;
    xTaskCreatePinnedToCoreWithCaps(
        thumb_worker_task, "thumb_worker", 8192, nullptr,
        1, // lowest non-idle — below draw workers (prio 2)
        &worker_handle,
        1, // core 1 keeps HTTP off the LVGL core
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    diag_task_register(worker_handle, DIAG_TASK_THUMB);

    ESP_LOGI(TAG, "Thumbnail manager initialised (%ux%u, cache=%d)",
             thumbMaxWidth, thumbMaxHeight, s_thumb_cache ? 1 : 0);
}

// ── Queue API ───────────────────────────────────────────────────────────────

void thumb_queue_push(ThumbContext *ctx)
{
    ctx->generation = s_thumb_generation.load();
    diag_bc(DIAG_THUMB_PUSH, (uint16_t)ctx->generation);
    // DIAGNOSTIC (temporary): headroom at every thumbnail download, so the
    // post-mortem shows whether the internal heap was already spent by then.
    diag_heap_log("thumb push");
    ESP_LOGI(TAG, "Queue push: gen=%u url=%.60s", ctx->generation, ctx->url.c_str());
    if (xQueueSend(s_thumb_queue, &ctx, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Thumb queue full, dropping: %s", ctx->url.c_str());

        // The callback holds ctx as user_data; remove it before freeing.
        lv_lock();
        if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
            lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb,
                                                  ctx);
        lv_unlock();

        delete ctx;
    }
}

void thumb_queue_cancel_all()
{
    s_thumb_generation.fetch_add(1, std::memory_order_relaxed);
    diag_bc(DIAG_THUMB_CANCEL, (uint16_t)s_thumb_generation.load());
    ESP_LOGI(TAG, "Thumb generation bumped to %u", s_thumb_generation.load());
}

// ── Thumb lifecycle callbacks ───────────────────────────────────────────────

void thumb_obj_deleted_cb(lv_event_t *e)
{
    auto *ctx = static_cast<ThumbContext *>(lv_event_get_user_data(e));
    if (!ctx)
        return;
    ctx->cancelled.store(true);
    ctx->thumb = nullptr;
}

void free_thumb_data_cb(lv_event_t *e)
{
    auto *d = static_cast<ThumbDataCtx *>(lv_event_get_user_data(e));
    if (!d || d->freed)
    {
        if (d && d->freed)
            ESP_LOGW(TAG, "free_thumb_data_cb: double-free prevented");
        return;
    }
    d->freed = true;

    if (d->dsc)
    {
        if (d->dsc->data)
        {
            heap_caps_free((void *)d->dsc->data);
            d->dsc->data = nullptr;
        }
        delete d->dsc;
    }
    d->dsc = nullptr;
    d->px = nullptr;
    delete d;
}

// ── Main fetch-and-decode ───────────────────────────────────────────────────

bool fetch_and_decode_jpeg(const std::string &url, uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc, uint8_t **out_px,
                           bool useCache)
{
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
    {
        ESP_LOGW(TAG, "Unsupported image URL: %.80s", url.c_str());
        return false;
    }

    PsramBuffer jpeg;
    bool loaded = false;

    if (useCache && s_cache_ready)
    {
        uint8_t *cached = nullptr;
        size_t cached_len = 0;
        diag_bc(DIAG_THUMB_CACHE_GET_ENTER, 0);
        const bool hit = thumbnail_cache::get(url, W, H, &cached, &cached_len);
        diag_bc(DIAG_THUMB_CACHE_GET_DONE, hit ? 1 : 0);
        if (hit && jpeg.adopt(cached, cached_len))
        {
            loaded = true;
            ESP_LOGI(TAG, "cache hit %ux%u: %.60s", W, H, url.c_str());
        }
        else
        {
            if (cached)
                heap_caps_free(cached);
            ESP_LOGI(TAG, "cache miss %ux%u: %.60s", W, H, url.c_str());
        }
    }

    if (!loaded)
    {
        // Hold an HTTP slot only for the download. Cache hits and decoding must
        // not consume one, otherwise a page of nine images queues up behind two
        // slots even when every thumbnail is already on disk.
        {
            // DIAGNOSTIC (temporary): bracket the HTTPS transfer. WiFi goes
            // through esp-hosted over SDIO to the C6, so a watchdog stall whose
            // last line is "[net] download start" points at that path (or TLS)
            // rather than at the SPIFFS cache.
            ESP_LOGI(TAG, "[net] download start %ux%u: %.60s", W, H, url.c_str());
            const int64_t dl_started_us = esp_timer_get_time();
            diag_bc(DIAG_THUMB_NET_ENTER, 0);

            SemaphoreGuard semGuard(s_http_concurrency_sem);
            if (!semGuard.acquired())
            {
                ESP_LOGE(TAG, "HTTP semaphore unavailable: %.60s", url.c_str());
                return false;
            }

            const bool dl_ok = download_jpeg(url, jpeg);
            diag_bc(DIAG_THUMB_NET_DONE, dl_ok ? 1 : 0);
            ESP_LOGI(TAG, "[net] download %s in %lld ms (%zu B)",
                     dl_ok ? "ok" : "failed",
                     (long long)((esp_timer_get_time() - dl_started_us) / 1000),
                     jpeg.size());
            if (!dl_ok)
                return false;
        }

        if (useCache && s_cache_ready)
        {
            diag_bc(DIAG_THUMB_CACHE_PUT, 0);
            thumbnail_cache::put(url, W, H, jpeg.data(), jpeg.size());
        }
    }

    diag_bc(DIAG_THUMB_DECODE_ENTER, 0);
    const bool decoded = decode_jpeg_buffer(jpeg.data(), jpeg.size(), W, H,
                                            out_dsc, out_px);
    diag_bc(DIAG_THUMB_DECODE_DONE, decoded ? 1 : 0);
    return decoded;
}

// ── Worker task ─────────────────────────────────────────────────────────────

void thumb_worker_task(void *)
{
    ThumbContext *ctx = nullptr;

    diag_bc(DIAG_THUMB_WAIT, 0); // entering the queue wait; no job in flight

    while (true)
    {
        if (xQueueReceive(s_thumb_queue, &ctx, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;

        diag_bc(DIAG_THUMB_RECV, (uint16_t)ctx->generation);

        if (ctx->generation != s_thumb_generation.load())
        {
            diag_bc(DIAG_THUMB_STALE, (uint16_t)ctx->generation);
            lv_lock();
            if (!ctx->cancelled.load() && ctx->thumb &&
                lv_obj_is_valid(ctx->thumb))
                lv_obj_remove_event_cb_with_user_data(
                    ctx->thumb, thumb_obj_deleted_cb, ctx);
            lv_unlock();

            delete ctx;
            continue;
        }

        vTaskDelay(1);

        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;
        uint16_t w = ctx->maxW ? ctx->maxW : s_thumb_max_w;
        uint16_t h = ctx->maxH ? ctx->maxH : s_thumb_max_h;
        diag_bc(DIAG_THUMB_FETCH_ENTER, 0);
        bool ok = fetch_and_decode_jpeg(ctx->url, w, h, &dsc, &px,
                                        ctx->cacheAllowed);

        diag_bc(DIAG_THUMB_APPLY_ENTER, ok ? 1 : 0);
        lv_lock();

        // The list may have been rebuilt while the fetch ran.
        if (ctx->generation != s_thumb_generation.load())
        {
            if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
                lv_obj_remove_event_cb_with_user_data(
                    ctx->thumb, thumb_obj_deleted_cb, ctx);
            if (ok)
            {
                heap_caps_free(px);
                delete dsc;
            }
            lv_unlock();
            diag_bc(DIAG_THUMB_APPLY_DONE, 0);
            delete ctx;
            continue;
        }

        if (ok && !ctx->cancelled.load() && ctx->thumb &&
            lv_obj_is_valid(ctx->thumb))
        {
            lv_image_set_src(ctx->thumb, dsc);
            lv_obj_set_style_clip_corner(ctx->thumb, true, 0);
            lv_obj_set_style_bg_opa(ctx->thumb, LV_OPA_TRANSP, 0);
            lv_obj_remove_event_cb_with_user_data(ctx->thumb, thumb_obj_deleted_cb,
                                                  ctx);
            lv_obj_add_event_cb(ctx->thumb, free_thumb_data_cb, LV_EVENT_DELETE,
                                new ThumbDataCtx{dsc, px});
        }
        else
        {
            if (ok)
            {
                heap_caps_free(px);
                delete dsc;
            }
            if (ctx->thumb && lv_obj_is_valid(ctx->thumb))
                lv_obj_remove_event_cb_with_user_data(
                    ctx->thumb, thumb_obj_deleted_cb, ctx);
        }

        lv_unlock();
        diag_bc(DIAG_THUMB_APPLY_DONE, 0);
        delete ctx;

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
