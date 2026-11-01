#include "thumbnail_cache.h"
#include "diag.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

static const char *TAG = "THUMBCACHE";

#define CACHE_BASE_DIR CONFIG_BSP_SPIFFS_MOUNT_POINT
#define INDEX_FILE CONFIG_BSP_SPIFFS_MOUNT_POINT "/thumb_index.json"
#define INDEX_FILE_TMP INDEX_FILE ".tmp"
// Bump when cached bytes can no longer be trusted (URL encoding, image format).
// An index with a different version is wiped on boot, so stale entries never
// serve a progressive JPEG that tjpgd cannot decode.
// v2: cache keys are now 16 hex chars instead of 8.
#define CACHE_INDEX_VERSION 2
#define MAX_CACHE_ITEMS 100
// Deep enough for a full 3×3 page of PUTs plus the matching GETs; at 8 the
// queue rejected writes while a page was loading, so those images were never
// cached and got downloaded again on the next visit.
#define IO_QUEUE_DEPTH 24
// save_index_internal() builds the whole index with cJSON on this stack. At
// 8 KB the deep cJSON/number-formatting call chain overflowed right when the
// first timed save ran, which is the Core 0 panic seen ~30 s after boot.
#define IO_TASK_STACK 20480
#define MAX_CACHE_FILE_BYTES (2 * 1024 * 1024)

// Save index every N puts or every INDEX_SAVE_INTERVAL_MS, whichever comes first.
// Keep the batch small: one 3×3 cookbook page is nine images and must survive a
// reboot even if the 30 s timer has not fired yet.
#define INDEX_SAVE_BATCH_SIZE 4
#define INDEX_SAVE_INTERVAL_MS 30000 // 30 seconds

namespace thumbnail_cache
{

    // The cache index holds up to MAX_CACHE_ITEMS entries whose URLs are the
    // largest thing in it (a few hundred bytes each for signed image URLs).
    // Keep them in PSRAM so the index does not permanently eat the internal
    // heap that TLS, DMA and task stacks need. Falls back to internal memory if
    // PSRAM is exhausted; the build has C++ exceptions disabled, so a total
    // allocation failure aborts exactly like the default std::allocator would.
    template <typename T>
    struct PsramAllocator
    {
        using value_type = T;

        PsramAllocator() noexcept = default;
        template <typename U>
        PsramAllocator(const PsramAllocator<U> &) noexcept {}

        T *allocate(std::size_t n)
        {
            void *ptr = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!ptr)
                ptr = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_DEFAULT);
            if (!ptr)
            {
                ESP_LOGE(TAG, "OOM allocating thumbnail cache string (%u B)",
                         (unsigned)(n * sizeof(T)));
                abort();
            }
            return static_cast<T *>(ptr);
        }

        void deallocate(T *ptr, std::size_t) noexcept { heap_caps_free(ptr); }

        template <typename U>
        bool operator==(const PsramAllocator<U> &) const noexcept { return true; }
        template <typename U>
        bool operator!=(const PsramAllocator<U> &) const noexcept { return false; }
    };

    using PsramString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

    // ─── Types ───────────────────────────────────────────────────────────────────

    struct CacheEntry
    {
        std::string hash;
        PsramString url;
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
    // Heap-allocated and handed to the IO task. get() waits for done_sem without
    // a timeout, so the IO task may safely write into the caller's output target;
    // ownership of the context passes back to get() when it returns. Exactly one
    // of out_jpeg / out_psram is set: the vector overload fills internal memory,
    // the raw overload returns an owned PSRAM allocation.
    //
    struct GetContext
    {
        std::vector<uint8_t> *out_jpeg;
        uint8_t **out_psram;
        size_t *out_psram_len;
        bool result;
        SemaphoreHandle_t done_sem;

        GetContext() : out_jpeg(nullptr), out_psram(nullptr), out_psram_len(nullptr),
                       result(false), done_sem(nullptr) {}
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
        std::ostringstream key;
        key << url << "|" << w << "|" << h;
        const std::string text = key.str();

        // Two independent 32-bit hashes give a 16-char filename, matching the
        // length is_cache_filename() expects (an 8-char name used to slip past
        // every purge path, so stale files were never cleaned up).
        std::hash<std::string> hasher;
        uint32_t a = (uint32_t)hasher(text);
        uint32_t b = (uint32_t)hasher(text + "#thumb");

        std::ostringstream oss;
        oss << std::hex << std::setfill('0')
            << std::setw(8) << a << std::setw(8) << b;
        return oss.str();
    }

    // ─── Index helpers (called ONLY from cache_io_task — internal RAM stack) ──────

    static void purge_all_internal(); // defined below

    // Drops the index and every cached file. Used when the on-disk format is
    // older than CACHE_INDEX_VERSION so no stale entry survives the upgrade.
    static void reset_cache_internal()
    {
        unlink(INDEX_FILE);
        unlink(INDEX_FILE_TMP);
        purge_all_internal();
        // Nothing needs saving: the index file is gone and the cache is empty.
        // Leaving the index dirty would trigger a pointless write at the first
        // timed-save tick.
        s_index_dirty = false;
        s_puts_since_save = 0;
        ESP_LOGW(TAG, "Cleared thumbnail cache for index format v%d",
                 CACHE_INDEX_VERSION);
    }

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
        ESP_LOGI(TAG, "[io] index file size: %ld bytes", len);
        if (len <= 0 || len > MAX_CACHE_FILE_BYTES)
        {
            ESP_LOGE(TAG, "Index has invalid length (%ld)", len);
            fclose(f);
            unlink(INDEX_FILE);
            return true;
        }
        std::vector<char> buf(len + 1);
        size_t rd = fread(buf.data(), 1, len, f);
        fclose(f);
        if (rd != (size_t)len)
        {
            ESP_LOGE(TAG, "Index read failed");
            unlink(INDEX_FILE);
            return true;
        }
        buf[len] = '\0';
        ESP_LOGI(TAG, "[io] index read %zu bytes", rd);

        cJSON *root = cJSON_Parse(buf.data());
        if (!root)
        {
            ESP_LOGE(TAG, "Index JSON invalid, discarding it");
            unlink(INDEX_FILE);
            return true;
        }

        cJSON *entries = root;
        if (cJSON_IsObject(root))
        {
            cJSON *version = cJSON_GetObjectItem(root, "version");
            entries = cJSON_GetObjectItem(root, "entries");
            if (!cJSON_IsNumber(version) ||
                version->valueint != CACHE_INDEX_VERSION ||
                !cJSON_IsArray(entries))
            {
                cJSON_Delete(root);
                reset_cache_internal();
                return true;
            }
        }
        else if (cJSON_IsArray(root))
        {
            // Written before the index carried a format version: start over.
            cJSON_Delete(root);
            reset_cache_internal();
            return true;
        }
        else
        {
            ESP_LOGE(TAG, "Index JSON has an unknown shape, discarding it");
            cJSON_Delete(root);
            unlink(INDEX_FILE);
            return true;
        }

        int count = cJSON_GetArraySize(entries);
        ESP_LOGI(TAG, "[io] index parsed, %d entries", count);
        for (int i = 0; i < count; ++i)
        {
            cJSON *item = cJSON_GetArrayItem(entries, i);
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
            // esp_timer_get_time() restarts at zero on every boot, so stored
            // timestamps from an older session would look newer than anything
            // added now and fresh entries would be evicted first. Index order
            // is insertion order, so use it as the age instead.
            e.timestamp = (uint64_t)i;
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

            // Yield between SPIFFS I/O operations so the IDLE task can feed the
            // watchdog. Each stat/unlink disables the flash cache, and doing many
            // in a row without yielding can starve IDLE0 on CPU 0.
            vTaskDelay(1);
        }
        cJSON_Delete(root);
        ESP_LOGI(TAG, "Loaded %zu cache entries", s_cache.size());
        return true;
    }

    static bool save_index_internal()
    {
        // DIAGNOSTIC (temporary): bracket the index write so a watchdog reset
        // that lands inside it leaves "[io] index save start" as the last line.
        const int64_t save_started_us = esp_timer_get_time();
        diag_bc(DIAG_IO_IDX_BUILD, (uint16_t)s_cache.size());

        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "version", CACHE_INDEX_VERSION);
        cJSON *entries = cJSON_CreateArray();
        for (const auto &e : s_cache)
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "hash", e.hash.c_str());
            cJSON_AddStringToObject(item, "url", e.url.c_str());
            cJSON_AddNumberToObject(item, "width", e.width);
            cJSON_AddNumberToObject(item, "height", e.height);
            cJSON_AddNumberToObject(item, "timestamp", (double)e.timestamp);
            cJSON_AddNumberToObject(item, "file_size", (double)e.file_size);
            cJSON_AddItemToArray(entries, item);
        }
        cJSON_AddItemToObject(root, "entries", entries);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json)
            return false;

        const size_t len = strlen(json);
        size_t fs_total = 0, fs_used = 0;
        esp_spiffs_info(CONFIG_BSP_SPIFFS_PARTITION_LABEL, &fs_total, &fs_used);
        ESP_LOGI(TAG, "[io] index save start: %zu entries, %zu B json, fs %zu/%zu B used",
                 s_cache.size(), len, fs_used, fs_total);

        // Write to a temporary file first: truncating the live index on a full
        // partition would leave the cache with no way to find its own files.
        diag_bc(DIAG_IO_IDX_OPEN, 0);
        FILE *f = fopen(INDEX_FILE_TMP, "w");
        if (!f)
        {
            free(json);
            return false;
        }
        size_t written = fwrite(json, 1, len, f);
        diag_bc(DIAG_IO_IDX_WRITE, (uint16_t)(written / 1024));
        bool flush_ok = (fflush(f) == 0);
        diag_bc(DIAG_IO_IDX_FLUSH, flush_ok ? 1 : 0);
        bool close_ok = (fclose(f) == 0);
        diag_bc(DIAG_IO_IDX_CLOSE, close_ok ? 1 : 0);
        free(json);

        if (written != len || !flush_ok || !close_ok)
        {
            ESP_LOGE(TAG, "Index write failed (%zu/%zu, errno=%d)", written, len, errno);
            unlink(INDEX_FILE_TMP);
            return false;
        }

        diag_bc(DIAG_IO_IDX_UNLINK, 0);
        unlink(INDEX_FILE);
        if (rename(INDEX_FILE_TMP, INDEX_FILE) != 0)
        {
            ESP_LOGE(TAG, "Index rename failed (errno=%d)", errno);
            unlink(INDEX_FILE_TMP);
            return false;
        }
        diag_bc(DIAG_IO_IDX_RENAME, 0);

        s_index_dirty = false;
        s_puts_since_save = 0;
        s_last_save_time = esp_timer_get_time();
        ESP_LOGI(TAG, "Index saved (%zu entries) in %lld ms, cache_io stack free: %u bytes",
                 s_cache.size(),
                 (long long)((esp_timer_get_time() - save_started_us) / 1000),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        diag_bc(DIAG_IO_IDX_DONE, 0);
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

    // Cache files are named <8 or 16 hex chars>.jpg so they can be told apart
    // from other files on the SPIFFS partition (WiFi credentials, the index, ...).
    // 8-char names are the pre-v2 format; still recognising them lets the version
    // reset delete them instead of leaving them to fill the partition forever.
    static bool is_cache_filename(const char *name)
    {
        if (!name)
            return false;
        size_t len = strlen(name);
        if (len < 4 + 8 || len > 4 + 16)
            return false;
        size_t stem = len - 4;
        if (strcmp(name + stem, ".jpg") != 0)
            return false;
        for (size_t i = 0; i < stem; ++i)
        {
            char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        }
        return true;
    }

    // The budget is based on the file sizes we track, not on esp_spiffs_info():
    // SPIFFS keeps deleted pages around until garbage collection, which makes
    // its "used" figure a poor guide for how large a write will fit. Keeping a
    // quarter of the partition free leaves room for GC, the index and credentials.
    static size_t cache_budget_internal()
    {
        size_t total = 0;
        size_t used = 0;
        if (esp_spiffs_info(CONFIG_BSP_SPIFFS_PARTITION_LABEL, &total, &used) == ESP_OK &&
            total > 0)
        {
            return (total / 4) * 3;
        }
        return 5 * 1024 * 1024;
    }

    // SPIFFS can fill up well before MAX_CACHE_ITEMS if the cached images are
    // large, so evict oldest entries until this write fits the byte budget.
    static void ensure_space_internal(size_t needed)
    {
        const size_t budget = cache_budget_internal();

        size_t cached_bytes = 0;
        for (const auto &e : s_cache)
            cached_bytes += e.file_size;

        if (cached_bytes + needed <= budget)
            return;

        std::sort(s_cache.begin(), s_cache.end(),
                  [](const CacheEntry &a, const CacheEntry &b)
                  { return a.timestamp < b.timestamp; });

        while (!s_cache.empty() && cached_bytes + needed > budget)
        {
            const CacheEntry victim = s_cache.front();
            std::string path = std::string(CACHE_BASE_DIR) + "/" + victim.hash + ".jpg";
            if (unlink(path.c_str()) != 0 && errno != ENOENT)
                ESP_LOGW(TAG, "unlink failed for %s (errno=%d)", victim.hash.c_str(), errno);
            ESP_LOGI(TAG, "Evicted for space: %s (%zu B)", victim.hash.c_str(),
                     victim.file_size);
            cached_bytes = (cached_bytes > victim.file_size)
                               ? cached_bytes - victim.file_size
                               : 0;
            s_cache.erase(s_cache.begin());
            s_index_dirty = true;
            vTaskDelay(1); // keep IDLE0 alive across a burst of evictions
        }
    }

    // Deletes cache files that exist on disk but are missing from the index.
    // They are leftovers from an index that failed to save and are unreachable,
    // yet they keep occupying the partition.
    static void purge_orphans_internal()
    {
        DIR *dir = opendir(CACHE_BASE_DIR);
        if (!dir)
        {
            ESP_LOGW(TAG, "[io] purge dir open failed");
            return;
        }
        ESP_LOGI(TAG, "[io] purge dir open");

        std::vector<std::string> orphans;
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (!is_cache_filename(ent->d_name))
                continue;
            std::string hash(ent->d_name, strlen(ent->d_name) - 4);
            bool indexed = std::any_of(s_cache.begin(), s_cache.end(),
                                       [&hash](const CacheEntry &e)
                                       { return e.hash == hash; });
            if (!indexed)
                orphans.emplace_back(ent->d_name);
        }
        closedir(dir);
        ESP_LOGI(TAG, "[io] purge scan done, %zu orphans", orphans.size());

        for (const auto &name : orphans)
        {
            std::string path = std::string(CACHE_BASE_DIR) + "/" + name;
            if (unlink(path.c_str()) == 0)
                ESP_LOGW(TAG, "Removed orphaned cache file: %s", name.c_str());
            vTaskDelay(1);
        }
        ESP_LOGI(TAG, "[io] purge unlink done");
    }

    // Drops every cache file so a full partition can recover, regardless of what
    // the index thinks is on disk.
    static void purge_all_internal()
    {
        std::vector<std::string> names;
        DIR *dir = opendir(CACHE_BASE_DIR);
        if (dir)
        {
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr)
                if (is_cache_filename(ent->d_name))
                    names.emplace_back(ent->d_name);
            closedir(dir);
        }

        for (const auto &name : names)
        {
            std::string path = std::string(CACHE_BASE_DIR) + "/" + name;
            unlink(path.c_str());
            // Each unlink disables the flash cache; yield so IDLE0 on this core
            // can feed the task watchdog during a large purge.
            vTaskDelay(1);
        }
        s_cache.clear();
        s_index_dirty = true;
        ESP_LOGW(TAG, "Purged thumbnail cache (%zu files)", names.size());
    }

    // ─── IO task ─────────────────────────────────────────────────────────────────
    //
    // THIS IS THE ONLY TASK THAT MAY CALL fopen / fread / fwrite / unlink ON SPIFFS.
    // Its stack is in internal DRAM (MALLOC_CAP_INTERNAL) so it remains accessible
    // when the flash cache is disabled during SPIFFS page writes.
    //
    static void cache_io_task(void *arg)
    {
        ESP_LOGI(TAG, "[io] task started (core %d)", (int)xPortGetCoreID());

        // Load index here — safe because this task has an internal-RAM stack
        int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        diag_bc(DIAG_IO_LOAD_ENTER, 0);
        load_index_internal();
        diag_bc(DIAG_IO_LOAD_DONE, 0);
        int64_t t1 = esp_timer_get_time();
        diag_bc(DIAG_IO_PURGE_ENTER, 0);
        purge_orphans_internal();
        diag_bc(DIAG_IO_PURGE_DONE, 0);
        int64_t t2 = esp_timer_get_time();
        s_last_save_time = esp_timer_get_time();
        xSemaphoreGive(s_mutex);

        ESP_LOGI(TAG, "[io] index load took %lld ms, purge took %lld ms",
                 (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000));
        ESP_LOGI(TAG, "cache_io stack free after index load: %u bytes",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));

        // Signal init() that the index is ready; get()/put() may now proceed
        xSemaphoreGive(s_ready);
        ESP_LOGI(TAG, "[io] ready");

        IoJob job;
        TickType_t check_interval = pdMS_TO_TICKS(5000); // check every 5 seconds for timed saves

        while (true)
        {
            // DIAGNOSTIC (temporary): marks the moment this task parked on the
            // queue. If the last IO_WAIT is shortly before a freeze, the flash
            // operations that ran just before it are the suspect.
            diag_bc(DIAG_IO_WAIT, 0);

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

            diag_bc(DIAG_IO_JOB, (uint16_t)job.type);

            // ── PUT ──────────────────────────────────────────────────────────────
            if (job.type == JobType::PUT)
            {
                // DIAGNOSTIC (temporary): the PUT window covers ensure_space's
                // unlinks, the file write and any batched index save — the
                // flash operations suspected of stalling the tick interrupts.
                diag_bc(DIAG_IO_PUT_ENTER, (uint16_t)(job.jpeg_len / 1024));
                const int64_t put_started_us = esp_timer_get_time();
                size_t fs_total = 0, fs_used = 0;
                esp_spiffs_info(CONFIG_BSP_SPIFFS_PARTITION_LABEL, &fs_total, &fs_used);
                ESP_LOGI(TAG, "[io] PUT start: %s (%zu B), fs %zu/%zu B used",
                         job.hash, job.jpeg_len, fs_used, fs_total);

                std::string path = std::string(CACHE_BASE_DIR) + "/" + job.hash + ".jpg";
                bool stored = false;

                // Two attempts: the second starts from an empty cache, so a
                // partition that is full for any reason still recovers.
                for (int attempt = 0; attempt < 2 && !stored; ++attempt)
                {
                    diag_bc(DIAG_IO_PUT_SPACE, (uint16_t)attempt);
                    xSemaphoreTake(s_mutex, portMAX_DELAY);
                    if (attempt == 0)
                        ensure_space_internal(job.jpeg_len);
                    else
                        purge_all_internal();
                    xSemaphoreGive(s_mutex);

                    FILE *f = fopen(path.c_str(), "wb");
                    if (!f)
                    {
                        ESP_LOGE(TAG, "fopen failed for write: %s (errno=%d)",
                                 path.c_str(), errno);
                        continue;
                    }
                    diag_bc(DIAG_IO_PUT_OPEN, 0);

                    size_t written = fwrite(job.jpeg_data, 1, job.jpeg_len, f);
                    diag_bc(DIAG_IO_PUT_WRITE, (uint16_t)(written / 1024));
                    bool flush_ok = (fflush(f) == 0);
                    bool close_ok = (fclose(f) == 0);
                    diag_bc(DIAG_IO_PUT_CLOSE, 0);
                    if (written != job.jpeg_len || !flush_ok || !close_ok)
                    {
                        ESP_LOGE(TAG,
                                 "Write incomplete (%zu/%zu, errno=%d), removing: %s",
                                 written, job.jpeg_len, errno, job.hash);
                        unlink(path.c_str());
                        continue;
                    }

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

                    ESP_LOGI(TAG, "Cached: %s (%zu B) in %lld ms [dirty=%d, puts=%u]",
                             job.hash, job.jpeg_len,
                             (long long)((esp_timer_get_time() - put_started_us) / 1000),
                             s_index_dirty, s_puts_since_save);

                    if (should_flush)
                    {
                        diag_bc(DIAG_IO_PUT_INDEX, (uint16_t)s_puts_since_save);
                        save_index_internal();
                    }

                    xSemaphoreGive(s_mutex);
                    diag_bc(DIAG_IO_PUT_DONE, 0);
                    stored = true;
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
                long file_len = 0;
                std::vector<uint8_t> local_buf;
                uint8_t *psram_buf = nullptr;

                FILE *f = fopen(path.c_str(), "rb");
                diag_bc(DIAG_IO_GET_OPEN, f ? 1 : 0);
                if (f)
                {
                    fseek(f, 0, SEEK_END);
                    long len = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (len > 0 && len <= MAX_CACHE_FILE_BYTES)
                    {
                        file_len = len;
                        if (ctx->out_psram)
                        {
                            // Cache reads go straight to PSRAM so a hit does not
                            // add to internal-heap fragmentation.
                            psram_buf = static_cast<uint8_t *>(heap_caps_malloc(
                                (size_t)len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                            if (psram_buf)
                            {
                                size_t rd = fread(psram_buf, 1, (size_t)len, f);
                                ok = (rd == (size_t)len);
                            }
                            else
                            {
                                ESP_LOGE(TAG, "OOM for cached JPEG (%ld B)", len);
                            }
                        }
                        else
                        {
                            local_buf.resize((size_t)len);
                            size_t rd = fread(local_buf.data(), 1, (size_t)len, f);
                            ok = (rd == (size_t)len);
                        }
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Cached file has invalid length (%ld): %s",
                                 len, job.hash);
                    }
                    fclose(f);
                    if (!ok)
                    {
                        local_buf.clear();
                        if (psram_buf)
                        {
                            heap_caps_free(psram_buf);
                            psram_buf = nullptr;
                        }
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "fopen failed for read: %s (errno=%d)", path.c_str(), errno);
                }
                diag_bc(DIAG_IO_GET_READ, ok ? 1 : 0);

                // get() blocks until this semaphore is given, so ctx (and the
                // caller's output target) are guaranteed to still be alive here.
                // Deliberately no LRU update on reads: only PUT ages entries, so
                // a cache hit never dirties the index.
                if (ok)
                {
                    if (ctx->out_psram)
                    {
                        *ctx->out_psram = psram_buf;
                        *ctx->out_psram_len = (size_t)file_len;
                    }
                    else
                    {
                        *ctx->out_jpeg = std::move(local_buf);
                    }
                }
                ctx->result = ok;

                // After this line we must NOT touch ctx — get() owns it now.
                diag_bc(DIAG_IO_GET_DONE, 0);
                xSemaphoreGive(ctx->done_sem);

                if (ok)
                    ESP_LOGI(TAG, "Cache hit: %s", job.hash);
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
        TaskHandle_t io_handle = nullptr;
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
            cache_io_task, "cache_io", IO_TASK_STACK, NULL,
            2, // priority — lower than thumb_worker so writes don't block UI
            &io_handle,
            0, // pin to core 0 alongside SPIFFS driver
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create cache IO task");
            return false;
        }

        diag_task_register(io_handle, DIAG_TASK_IO);

        s_initialized = true;
        ESP_LOGI(TAG, "Thumbnail cache initializing (waiting for index load)...");
        return true;
    }

    // Shared body of both public get() overloads; exactly one output target is
    // set by the caller.
    static bool get_impl(const std::string &url, uint16_t width, uint16_t height,
                         std::vector<uint8_t> *out_jpeg, uint8_t **out_psram,
                         size_t *out_psram_len)
    {
        if (!s_initialized)
            return false;

        // Block until the IO task has finished loading the index
        xSemaphoreTake(s_ready, portMAX_DELAY);
        xSemaphoreGive(s_ready); // put it back so subsequent calls don't block

        std::string hash = make_hash(url, width, height);

        // Index lookup is a RAM-only operation — safe from any task
        diag_bc(DIAG_IO_GET_LOOKUP, 0);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        bool exists = std::any_of(s_cache.begin(), s_cache.end(),
                                  [&hash](const CacheEntry &e)
                                  { return e.hash == hash; });
        xSemaphoreGive(s_mutex);
        diag_bc(DIAG_IO_GET_LOOKUP_DONE, exists ? 1 : 0);

        if (!exists)
            return false;

        // Heap-allocated because the IO task writes its result through this
        // context after get() has queued the job.
        GetContext *ctx = new GetContext{};
        ctx->out_jpeg = out_jpeg;
        ctx->out_psram = out_psram;
        ctx->out_psram_len = out_psram_len;
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

        // Wait until the IO task has read the file. Waiting without a timeout is
        // required for memory safety: returning early would destroy the caller's
        // output target while the IO task can still be writing into it.
        diag_bc(DIAG_IO_GET_WAIT, 0);
        xSemaphoreTake(ctx->done_sem, portMAX_DELAY);

        // Semaphore taken — IO task has finished and will not touch ctx again.
        bool result = ctx->result;
        vSemaphoreDelete(ctx->done_sem);
        delete ctx;
        return result;
    }

    bool get(const std::string &url, uint16_t width, uint16_t height,
             std::vector<uint8_t> &out_jpeg)
    {
        return get_impl(url, width, height, &out_jpeg, nullptr, nullptr);
    }

    bool get(const std::string &url, uint16_t width, uint16_t height,
             uint8_t **out_data, size_t *out_len)
    {
        if (!out_data || !out_len)
            return false;
        *out_data = nullptr;
        *out_len = 0;
        return get_impl(url, width, height, nullptr, out_data, out_len);
    }

    bool put(const std::string &url, uint16_t width, uint16_t height,
             const uint8_t *jpeg_data, size_t jpeg_len)
    {
        if (!s_initialized)
            return false;

        ESP_LOGI(TAG, "Queueing cache PUT for %s (%zu B)", url.c_str(), jpeg_len);

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