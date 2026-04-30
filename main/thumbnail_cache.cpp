#include "thumbnail_cache.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <atomic>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

static const char *TAG = "THUMBCACHE";

#define CACHE_BASE_DIR CONFIG_BSP_SPIFFS_MOUNT_POINT
#define INDEX_FILE CONFIG_BSP_SPIFFS_MOUNT_POINT "/thumb_index.json"
#define MAX_CACHE_ITEMS 100
#define IO_QUEUE_DEPTH 8
#define IO_TASK_STACK 8192

// Save index every N puts or every INDEX_SAVE_INTERVAL_MS, whichever comes first
#define INDEX_SAVE_BATCH_SIZE 10
#define INDEX_SAVE_INTERVAL_MS 30000 // 30 seconds

namespace thumbnail_cache
{

    // ─── Types ───────────────────────────────────────────────────────────────────

    struct CacheEntry
    {
        std::string hash;
        std::string url;
        uint16_t width;
        uint16_t height;
        uint64_t timestamp; // µs from esp_timer_get_time() — monotonic, always valid
        size_t file_size;
    };

    enum class JobType : uint8_t
    {
        PUT,
        GET,
        FLUSH_INDEX
    };

    // ─── GET context ─────────────────────────────────────────────────────────────
    //
    // Heap-allocated so it outlives the get() stack frame in the timeout case.
    // Ownership rules:
    //   • get() allocates it and sends it to the IO task via IoJob::ctx.
    //   • If get() times out it sets cancelled=true and walks away — the IO task
    //     then owns the struct and is responsible for freeing it.
    //   • If get() succeeds (semaphore taken in time) it reads the result and
    //     frees the struct itself; the IO task must NOT touch it after giving the
    //     semaphore.
    //
    // The cancelled flag is std::atomic<bool> so the write in get() and the read
    // in cache_io_task are race-free on dual-core ESP32-P4.
    //
    struct GetContext
    {
        std::vector<uint8_t> *out_jpeg; // caller's buffer — only valid while !cancelled
        bool result;
        std::atomic<bool> cancelled;
        SemaphoreHandle_t done_sem;

        GetContext() : out_jpeg(nullptr), result(false), cancelled(false), done_sem(nullptr) {}
    };

    // ─── IO job ──────────────────────────────────────────────────────────────────
    //
    // Passed by value through FreeRTOS queue — must be POD / trivially copyable.
    // No std::string or std::vector members allowed here.
    //
    struct IoJob
    {
        JobType type;
        char hash[17]; // 16 hex chars + NUL

        // PUT fields
        uint8_t *jpeg_data; // SPIRAM buffer; ownership transferred to IO task
        size_t jpeg_len;
        char url[512];
        uint16_t width;
        uint16_t height;

        // GET fields — see GetContext above
        GetContext *ctx; // heap-allocated; ownership rules described in GetContext
    };

    // ─── Static state ─────────────────────────────────────────────────────────────

    static std::vector<CacheEntry> s_cache;
    static SemaphoreHandle_t s_mutex = NULL;
    static QueueHandle_t s_io_queue = NULL;
    static SemaphoreHandle_t s_ready = NULL; // given after index loaded
    static bool s_initialized = false;
    static bool s_index_dirty = false;
    static uint32_t s_puts_since_save = 0;
    static int64_t s_last_save_time = 0;

    // ─── Hash ─────────────────────────────────────────────────────────────────────

    static std::string make_hash(const std::string &url, uint16_t w, uint16_t h)
    {
        std::ostringstream oss;
        oss << url << "|" << w << "|" << h;
        std::hash<std::string> hasher;
        size_t hv = hasher(oss.str());
        oss.str("");
        // size_t is 32-bit on ESP32 — pad to 8 hex chars
        oss << std::hex << std::setfill('0') << std::setw(sizeof(size_t) * 2) << hv;
        return oss.str();
    }

    // ─── Index helpers (called ONLY from cache_io_task — internal RAM stack) ──────

    static bool load_index_internal()
    {
        s_cache.clear();
        FILE *f = fopen(INDEX_FILE, "r");
        if (!f)
        {
            ESP_LOGW(TAG, "No index file found, starting fresh");
            return true;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<char> buf(len + 1);
        size_t rd = fread(buf.data(), 1, len, f);
        fclose(f);
        if (rd != (size_t)len)
        {
            ESP_LOGE(TAG, "Index read failed");
            return false;
        }
        buf[len] = '\0';

        cJSON *root = cJSON_Parse(buf.data());
        if (!root || !cJSON_IsArray(root))
        {
            ESP_LOGE(TAG, "Index JSON invalid");
            if (root)
                cJSON_Delete(root);
            return false;
        }
        int count = cJSON_GetArraySize(root);
        for (int i = 0; i < count; ++i)
        {
            cJSON *item = cJSON_GetArrayItem(root, i);
            if (!cJSON_IsObject(item))
                continue;
            cJSON *jh = cJSON_GetObjectItem(item, "hash");
            cJSON *ju = cJSON_GetObjectItem(item, "url");
            cJSON *jw = cJSON_GetObjectItem(item, "width");
            cJSON *jht = cJSON_GetObjectItem(item, "height");
            cJSON *jts = cJSON_GetObjectItem(item, "timestamp");
            cJSON *jsz = cJSON_GetObjectItem(item, "file_size");
            if (!jh || !ju || !jw || !jht || !jts || !jsz)
                continue;

            CacheEntry e;
            e.hash = jh->valuestring;
            e.url = ju->valuestring;
            e.width = (uint16_t)jw->valueint;
            e.height = (uint16_t)jht->valueint;
            e.timestamp = (uint64_t)cJSON_GetNumberValue(jts);
            e.file_size = (size_t)cJSON_GetNumberValue(jsz);

            std::string path = std::string(CACHE_BASE_DIR) + "/" + e.hash + ".jpg";
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && (size_t)st.st_size == e.file_size)
            {
                s_cache.push_back(e);
            }
            else
            {
                ESP_LOGW(TAG, "Orphaned file, removing: %s", e.hash.c_str());
                unlink(path.c_str());
            }
        }
        cJSON_Delete(root);
        ESP_LOGI(TAG, "Loaded %zu cache entries", s_cache.size());
        return true;
    }

    static bool save_index_internal()
    {
        cJSON *root = cJSON_CreateArray();
        for (const auto &e : s_cache)
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "hash", e.hash.c_str());
            cJSON_AddStringToObject(item, "url", e.url.c_str());
            cJSON_AddNumberToObject(item, "width", e.width);
            cJSON_AddNumberToObject(item, "height", e.height);
            cJSON_AddNumberToObject(item, "timestamp", (double)e.timestamp);
            cJSON_AddNumberToObject(item, "file_size", (double)e.file_size);
            cJSON_AddItemToArray(root, item);
        }
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json)
            return false;

        FILE *f = fopen(INDEX_FILE, "w");
        if (!f)
        {
            free(json);
            return false;
        }
        fwrite(json, 1, strlen(json), f);
        fclose(f);
        free(json);

        s_index_dirty = false;
        s_puts_since_save = 0;
        s_last_save_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Index saved (%zu entries)", s_cache.size());
        return true;
    }

    static void prune_internal(size_t max_items)
    {
        if (s_cache.size() <= max_items)
            return;
        std::sort(s_cache.begin(), s_cache.end(),
                  [](const CacheEntry &a, const CacheEntry &b)
                  { return a.timestamp < b.timestamp; });
        while (s_cache.size() > max_items)
        {
            std::string path = std::string(CACHE_BASE_DIR) + "/" + s_cache.front().hash + ".jpg";
            unlink(path.c_str());
            ESP_LOGI(TAG, "Evicted: %s", s_cache.front().hash.c_str());
            s_cache.erase(s_cache.begin());
        }
        s_index_dirty = true;
    }

    // ─── IO task ─────────────────────────────────────────────────────────────────
    //
    // THIS IS THE ONLY TASK THAT MAY CALL fopen / fread / fwrite / unlink ON SPIFFS.
    // Its stack is in internal DRAM (MALLOC_CAP_INTERNAL) so it remains accessible
    // when the flash cache is disabled during SPIFFS page writes.
    //
    static void cache_io_task(void *arg)
    {
        // Load index here — safe because this task has an internal-RAM stack
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        load_index_internal();
        xSemaphoreGive(s_mutex);

        // Signal init() that the index is ready; get()/put() may now proceed
        xSemaphoreGive(s_ready);

        IoJob job;
        TickType_t check_interval = pdMS_TO_TICKS(5000); // check every 5 seconds for timed saves

        while (true)
        {
            // Wait for job with timeout so we can periodically flush the index
            if (xQueueReceive(s_io_queue, &job, check_interval) != pdTRUE)
            {
                // Timeout — flush if dirty and the interval has elapsed
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                bool should_save = s_index_dirty &&
                                   ((esp_timer_get_time() - s_last_save_time) > (INDEX_SAVE_INTERVAL_MS * 1000LL));
                xSemaphoreGive(s_mutex);

                if (should_save)
                {
                    xSemaphoreTake(s_mutex, portMAX_DELAY);
                    save_index_internal();
                    xSemaphoreGive(s_mutex);
                }
                continue;
            }

            // ── PUT ──────────────────────────────────────────────────────────────
            if (job.type == JobType::PUT)
            {
                std::string path = std::string(CACHE_BASE_DIR) + "/" + job.hash + ".jpg";
                FILE *f = fopen(path.c_str(), "wb");
                if (f)
                {
                    size_t written = fwrite(job.jpeg_data, 1, job.jpeg_len, f);
                    fclose(f);
                    if (written == job.jpeg_len)
                    {
                        xSemaphoreTake(s_mutex, portMAX_DELAY);

                        // Remove stale entry if a re-cache of the same key arrives
                        auto it = std::find_if(s_cache.begin(), s_cache.end(),
                                               [&job](const CacheEntry &e)
                                               { return e.hash == job.hash; });
                        if (it != s_cache.end())
                            s_cache.erase(it);

                        CacheEntry e;
                        e.hash = job.hash;
                        e.url = job.url;
                        e.width = job.width;
                        e.height = job.height;
                        e.timestamp = (uint64_t)esp_timer_get_time();
                        e.file_size = job.jpeg_len;
                        s_cache.push_back(e);

                        prune_internal(MAX_CACHE_ITEMS);
                        s_index_dirty = true;
                        s_puts_since_save++;

                        bool should_flush = (s_puts_since_save >= INDEX_SAVE_BATCH_SIZE);

                        ESP_LOGI(TAG, "Cached: %s (%zu B) [dirty=%d, puts=%u]",
                                 job.hash, job.jpeg_len, s_index_dirty, s_puts_since_save);

                        if (should_flush)
                            save_index_internal();

                        xSemaphoreGive(s_mutex);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Write incomplete, removing: %s", job.hash);
                        unlink(path.c_str());
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "fopen failed for write: %s (errno=%d)", path.c_str(), errno);
                }
                // PUT transfers ownership of jpeg_data to us — always free it
                heap_caps_free(job.jpeg_data);
            }

            // ── GET ──────────────────────────────────────────────────────────────
            else if (job.type == JobType::GET)
            {
                GetContext *ctx = job.ctx;

                std::string path = std::string(CACHE_BASE_DIR) + "/" + job.hash + ".jpg";
                bool ok = false;
                std::vector<uint8_t> local_buf; // read into local first, copy only if not cancelled

                FILE *f = fopen(path.c_str(), "rb");
                if (f)
                {
                    fseek(f, 0, SEEK_END);
                    long len = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    local_buf.resize(len);
                    size_t rd = fread(local_buf.data(), 1, len, f);
                    fclose(f);
                    ok = (rd == (size_t)len);
                    if (!ok)
                        local_buf.clear();
                }
                else
                {
                    ESP_LOGE(TAG, "fopen failed for read: %s (errno=%d)", path.c_str(), errno);
                }

                // Check cancelled *after* the slow fread, before touching ctx->out_jpeg.
                // cancelled is std::atomic<bool> — safe on dual-core without a mutex.
                if (ctx->cancelled.load(std::memory_order_acquire))
                {
                    // get() already returned to the caller — the stack frame is gone.
                    // We own ctx; clean it up and move on.
                    ESP_LOGW(TAG, "GET timed out, discarding result for %s", job.hash);
                    vSemaphoreDelete(ctx->done_sem);
                    delete ctx;
                }
                else
                {
                    // get() is still blocking on the semaphore — safe to write results.
                    // NOTE: we deliberately do NOT update the LRU timestamp here.
                    // Reads are FIFO-neutral: only writes (PUT) age the entries.
                    // This keeps the index clean — a cache hit never dirties the index.
                    if (ok)
                        *ctx->out_jpeg = std::move(local_buf);
                    ctx->result = ok;

                    // Signal get() that the result is ready.
                    // After this line we must NOT touch ctx — get() owns it now.
                    xSemaphoreGive(ctx->done_sem);

                    if (ok)
                        ESP_LOGI(TAG, "Cache hit: %s", job.hash);
                }
            }

            // ── FLUSH_INDEX ──────────────────────────────────────────────────────
            else if (job.type == JobType::FLUSH_INDEX)
            {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (s_index_dirty)
                    save_index_internal();
                xSemaphoreGive(s_mutex);
            }
        }
    }

    // ─── Public API ───────────────────────────────────────────────────────────────

    bool init()
    {
        if (s_initialized)
            return true;

        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex)
        {
            ESP_LOGE(TAG, "Failed to create mutex");
            return false;
        }

        // Binary semaphore used to block get()/put() until the index is loaded
        s_ready = xSemaphoreCreateBinary();
        if (!s_ready)
        {
            ESP_LOGE(TAG, "Failed to create ready semaphore");
            return false;
        }

        s_io_queue = xQueueCreate(IO_QUEUE_DEPTH, sizeof(IoJob));
        if (!s_io_queue)
        {
            ESP_LOGE(TAG, "Failed to create IO queue");
            return false;
        }

        // CRITICAL: MALLOC_CAP_INTERNAL keeps the stack in DRAM.
        // Any task that calls fopen/fwrite/fread on SPIFFS must have an internal
        // stack — PSRAM stacks panic when the flash cache is disabled during writes.
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
            cache_io_task, "cache_io", IO_TASK_STACK, NULL,
            4, // priority — lower than thumb_worker so writes don't block UI
            NULL,
            0, // pin to core 0 alongside SPIFFS driver
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create cache IO task");
            return false;
        }

        s_initialized = true;
        ESP_LOGI(TAG, "Thumbnail cache initializing (waiting for index load)...");
        return true;
    }

    bool get(const std::string &url, uint16_t width, uint16_t height,
             std::vector<uint8_t> &out_jpeg)
    {
        if (!s_initialized)
            return false;

        // Block until the IO task has finished loading the index
        xSemaphoreTake(s_ready, portMAX_DELAY);
        xSemaphoreGive(s_ready); // put it back so subsequent calls don't block

        std::string hash = make_hash(url, width, height);

        // Index lookup is a RAM-only operation — safe from any task
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        bool exists = std::any_of(s_cache.begin(), s_cache.end(),
                                  [&hash](const CacheEntry &e)
                                  { return e.hash == hash; });
        xSemaphoreGive(s_mutex);

        if (!exists)
            return false;

        // Allocate the GET context on the heap so it outlives our stack frame
        // in the (unlikely) case that the IO task doesn't process it before our timeout.
        GetContext *ctx = new GetContext{};
        ctx->out_jpeg = &out_jpeg;
        ctx->done_sem = xSemaphoreCreateBinary();

        if (!ctx->done_sem)
        {
            delete ctx;
            return false;
        }

        IoJob job{};
        job.type = JobType::GET;
        job.ctx = ctx;
        strncpy(job.hash, hash.c_str(), 16);
        job.hash[16] = '\0';

        if (xQueueSend(s_io_queue, &job, pdMS_TO_TICKS(2000)) != pdTRUE)
        {
            ESP_LOGE(TAG, "IO queue full on GET");
            vSemaphoreDelete(ctx->done_sem);
            delete ctx;
            return false;
        }

        // Wait for the IO task to signal completion
        if (xSemaphoreTake(ctx->done_sem, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            // Timeout — signal the IO task to discard this job's results and
            // free ctx itself (we must not touch ctx after this store).
            ESP_LOGW(TAG, "Cache IO timeout for %s", hash.c_str());
            ctx->cancelled.store(true, std::memory_order_release);
            // ctx and done_sem are now owned by the IO task — do not free here.
            return false;
        }

        // Semaphore taken — IO task has finished and will not touch ctx again.
        bool result = ctx->result;
        vSemaphoreDelete(ctx->done_sem);
        delete ctx;
        return result;
    }

    bool put(const std::string &url, uint16_t width, uint16_t height,
             const uint8_t *jpeg_data, size_t jpeg_len)
    {
        if (!s_initialized)
            return false;

        // Block until index is loaded — we don't want to write before we know
        // what's already cached
        xSemaphoreTake(s_ready, portMAX_DELAY);
        xSemaphoreGive(s_ready);

        // Copy the JPEG into a SPIRAM buffer.
        // Ownership is transferred to the IO task which will free it after writing.
        uint8_t *copy = (uint8_t *)heap_caps_malloc(jpeg_len,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!copy)
        {
            ESP_LOGE(TAG, "SPIRAM alloc failed for put (%zu B)", jpeg_len);
            return false;
        }
        memcpy(copy, jpeg_data, jpeg_len);

        std::string hash = make_hash(url, width, height);

        IoJob job{};
        job.type = JobType::PUT;
        job.jpeg_data = copy; // IO task takes ownership
        job.jpeg_len = jpeg_len;
        job.width = width;
        job.height = height;
        strncpy(job.hash, hash.c_str(), 16);
        job.hash[16] = '\0';
        strncpy(job.url, url.c_str(), sizeof(job.url) - 1);
        job.url[sizeof(job.url) - 1] = '\0';

        if (xQueueSend(s_io_queue, &job, pdMS_TO_TICKS(2000)) != pdTRUE)
        {
            ESP_LOGE(TAG, "IO queue full, dropping cache write for %s", hash.c_str());
            heap_caps_free(copy); // we still own it if send fails
            return false;
        }

        // Fire-and-forget — actual fwrite happens asynchronously in cache_io_task
        return true;
    }

    void flush()
    {
        if (!s_initialized)
            return;

        IoJob job{};
        job.type = JobType::FLUSH_INDEX;

        if (xQueueSend(s_io_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE)
            ESP_LOGW(TAG, "Failed to queue index flush");
        else
            ESP_LOGI(TAG, "Index flush queued");
    }

    size_t size()
    {
        if (!s_initialized || !s_mutex)
            return 0;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        size_t count = s_cache.size();
        xSemaphoreGive(s_mutex);
        return count;
    }

    void prune(size_t max_items)
    {
        // Pruning involves unlink() + fwrite (index) so it must run in the IO task.
        // This public overload is a no-op because prune_internal() is already called
        // automatically inside the IO task on every PUT.
        (void)max_items;
    }

} // namespace thumbnail_cache