#include "thumbnail_cache.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
#include <chrono>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>

static const char *TAG = "THUMBCACHE";

#define CACHE_BASE_DIR CONFIG_BSP_SPIFFS_MOUNT_POINT
#define INDEX_FILE CONFIG_BSP_SPIFFS_MOUNT_POINT "/thumb_index.json"
#define MAX_CACHE_ITEMS 30

namespace thumbnail_cache
{

    struct CacheEntry
    {
        std::string hash;
        std::string url;
        uint16_t width;
        uint16_t height;
        uint64_t timestamp; // milliseconds since epoch
        size_t file_size;
    };

    static std::vector<CacheEntry> s_cache;
    static bool s_initialized = false;
    static SemaphoreHandle_t s_cache_mutex = NULL;

    // Simple hash combining string and dimensions
    static std::string make_hash(const std::string &url, uint16_t width, uint16_t height)
    {
        std::ostringstream oss;
        oss << url << "|" << width << "|" << height;
        std::string combined = oss.str();
        std::hash<std::string> hasher;
        size_t h = hasher(combined);
        oss.str("");
        oss << std::hex << std::setfill('0') << std::setw(16) << h;
        return oss.str();
    }

    // static bool ensure_cache_dir()
    // {
    //     struct stat st;
    //     ESP_LOGI(TAG, "Ensuring cache directory: %s", CACHE_BASE_DIR);
    //     if (stat(CACHE_BASE_DIR, &st) == 0)
    //     {
    //         ESP_LOGI(TAG, "stat succeeded, is_dir=%d", S_ISDIR(st.st_mode));
    //         return S_ISDIR(st.st_mode);
    //     }
    //     ESP_LOGI(TAG, "stat failed, errno=%d", errno);
    //     // Create directory
    //     if (mkdir(CACHE_BASE_DIR, 0755) != 0)
    //     {
    //         ESP_LOGE(TAG, "Failed to create cache directory, errno=%d", errno);
    //         return false;
    //     }
    //     ESP_LOGI(TAG, "Created cache directory");
    //     return true;
    // }

    static bool load_index()
    {
        s_cache.clear();
        FILE *f = fopen(INDEX_FILE, "r");
        if (!f)
        {
            ESP_LOGW(TAG, "No index file found, starting fresh");
            return true; // not an error
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<char> buf(len + 1);
        size_t read = fread(buf.data(), 1, len, f);
        fclose(f);
        if (read != (size_t)len)
        {
            ESP_LOGE(TAG, "Failed to read index file");
            return false;
        }
        buf[len] = 0;
        cJSON *root = cJSON_Parse(buf.data());
        if (!root)
        {
            ESP_LOGE(TAG, "Failed to parse index JSON");
            return false;
        }
        if (!cJSON_IsArray(root))
        {
            ESP_LOGE(TAG, "Index root is not an array");
            cJSON_Delete(root);
            return false;
        }
        int count = cJSON_GetArraySize(root);
        for (int i = 0; i < count; ++i)
        {
            cJSON *item = cJSON_GetArrayItem(root, i);
            if (!cJSON_IsObject(item))
                continue;
            cJSON *jhash = cJSON_GetObjectItem(item, "hash");
            cJSON *jurl = cJSON_GetObjectItem(item, "url");
            cJSON *jwidth = cJSON_GetObjectItem(item, "width");
            cJSON *jheight = cJSON_GetObjectItem(item, "height");
            cJSON *jtimestamp = cJSON_GetObjectItem(item, "timestamp");
            cJSON *jsize = cJSON_GetObjectItem(item, "file_size");
            if (!jhash || !jurl || !jwidth || !jheight || !jtimestamp || !jsize)
                continue;
            CacheEntry entry;
            entry.hash = jhash->valuestring;
            entry.url = jurl->valuestring;
            entry.width = (uint16_t)jwidth->valueint;
            entry.height = (uint16_t)jheight->valueint;
            entry.timestamp = (uint64_t)cJSON_GetNumberValue(jtimestamp);
            entry.file_size = (size_t)cJSON_GetNumberValue(jsize);
            // Verify file exists
            std::string path = std::string(CACHE_BASE_DIR) + "/" + entry.hash + ".jpg";
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && st.st_size == entry.file_size)
            {
                s_cache.push_back(entry);
            }
            else
            {
                ESP_LOGW(TAG, "Cached file missing or size mismatch: %s", entry.hash.c_str());
                // Remove orphaned file (if exists)
                unlink(path.c_str());
            }
        }
        cJSON_Delete(root);
        ESP_LOGI(TAG, "Loaded %zu cache entries", s_cache.size());
        return true;
    }

    static bool save_index()
    {
        cJSON *root = cJSON_CreateArray();
        for (const auto &entry : s_cache)
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "hash", entry.hash.c_str());
            cJSON_AddStringToObject(item, "url", entry.url.c_str());
            cJSON_AddNumberToObject(item, "width", entry.width);
            cJSON_AddNumberToObject(item, "height", entry.height);
            cJSON_AddNumberToObject(item, "timestamp", (double)entry.timestamp);
            cJSON_AddNumberToObject(item, "file_size", (double)entry.file_size);
            cJSON_AddItemToArray(root, item);
        }
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json)
        {
            ESP_LOGE(TAG, "Failed to serialize index JSON");
            return false;
        }
        FILE *f = fopen(INDEX_FILE, "w");
        if (!f)
        {
            ESP_LOGE(TAG, "Failed to open index file for writing");
            free(json);
            return false;
        }
        fwrite(json, 1, strlen(json), f);
        fclose(f);
        free(json);
        return true;
    }

    bool init()
    {
        if (s_initialized)
            return true;
        // if (!ensure_cache_dir())
        // {
        //     return false;
        // }
        if (!load_index())
        {
            return false;
        }
        s_cache_mutex = xSemaphoreCreateMutex();
        if (s_cache_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create cache mutex");
            return false;
        }
        s_initialized = true;
        ESP_LOGI(TAG, "Thumbnail cache initialized with %zu entries", s_cache.size());
        return true;
    }

    bool get(const std::string &url, uint16_t width, uint16_t height, std::vector<uint8_t> &out_jpeg)
    {
        if (!s_initialized)
            return false;
        if (s_cache_mutex == NULL)
            return false;
        if (xSemaphoreTake(s_cache_mutex, portMAX_DELAY) != pdTRUE)
            return false;

        std::string hash = make_hash(url, width, height);
        auto it = std::find_if(s_cache.begin(), s_cache.end(), [&hash](const CacheEntry &e)
                               { return e.hash == hash; });
        if (it == s_cache.end())
        {
            xSemaphoreGive(s_cache_mutex);
            return false;
        }
        std::string path = std::string(CACHE_BASE_DIR) + "/" + hash + ".jpg";
        FILE *f = fopen(path.c_str(), "rb");
        if (!f)
        {
            ESP_LOGE(TAG, "Cached file missing: %s", hash.c_str());
            // Remove from index
            s_cache.erase(it);
            save_index(); // save_index also needs lock, but we already hold it
            xSemaphoreGive(s_cache_mutex);
            return false;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        out_jpeg.resize(len);
        size_t read = fread(out_jpeg.data(), 1, len, f);
        fclose(f);
        if (read != (size_t)len)
        {
            ESP_LOGE(TAG, "Failed to read cached file");
            out_jpeg.clear();
            xSemaphoreGive(s_cache_mutex);
            return false;
        }
        // Update timestamp (LRU)
        it->timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        // Re-sort? We'll sort on prune.
        ESP_LOGI(TAG, "Cache hit: %s", hash.c_str());
        xSemaphoreGive(s_cache_mutex);
        return true;
    }

    static void prune_locked(size_t max_items)
    {
        if (s_cache.size() <= max_items)
            return;
        // Sort by timestamp ascending (oldest first)
        std::sort(s_cache.begin(), s_cache.end(),
                  [](const CacheEntry &a, const CacheEntry &b)
                  { return a.timestamp < b.timestamp; });
        while (s_cache.size() > max_items)
        {
            const CacheEntry &oldest = s_cache.front();
            std::string path = std::string(CACHE_BASE_DIR) + "/" + oldest.hash + ".jpg";
            unlink(path.c_str());
            s_cache.erase(s_cache.begin());
            ESP_LOGI(TAG, "Pruned old cache entry: %s", oldest.hash.c_str());
        }
        save_index();
    }

    bool put(const std::string &url, uint16_t width, uint16_t height, const uint8_t *jpeg_data, size_t jpeg_len)
    {
        if (!s_initialized)
            return false;
        if (s_cache_mutex == NULL)
            return false;
        if (xSemaphoreTake(s_cache_mutex, portMAX_DELAY) != pdTRUE)
            return false;

        std::string hash = make_hash(url, width, height);
        // Remove existing entry if any
        auto it = std::find_if(s_cache.begin(), s_cache.end(), [&hash](const CacheEntry &e)
                               { return e.hash == hash; });
        if (it != s_cache.end())
        {
            s_cache.erase(it);
        }
        // Write file
        std::string path = std::string(CACHE_BASE_DIR) + "/" + hash + ".jpg";
        FILE *f = fopen(path.c_str(), "wb");
        if (!f)
        {
            ESP_LOGE(TAG, "Failed to open cache file for writing: %s", path.c_str());
            xSemaphoreGive(s_cache_mutex);
            return false;
        }
        size_t written = fwrite(jpeg_data, 1, jpeg_len, f);
        fclose(f);
        if (written != jpeg_len)
        {
            ESP_LOGE(TAG, "Failed to write cache file");
            unlink(path.c_str());
            xSemaphoreGive(s_cache_mutex);
            return false;
        }
        // Add entry
        CacheEntry entry;
        entry.hash = hash;
        entry.url = url;
        entry.width = width;
        entry.height = height;
        entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        entry.file_size = jpeg_len;
        s_cache.push_back(entry);
        // Prune if needed (default max MAX_CACHE_ITEMS)
        prune_locked(MAX_CACHE_ITEMS);
        // Save index
        if (!save_index())
        {
            ESP_LOGE(TAG, "Failed to save index after put");
            // Still return true? The file is saved.
        }
        ESP_LOGI(TAG, "Cached thumbnail: %s (%zu bytes)", hash.c_str(), jpeg_len);
        xSemaphoreGive(s_cache_mutex);
        return true;
    }

    void prune(size_t max_items)
    {
        if (!s_initialized || s_cache_mutex == NULL)
            return;
        if (xSemaphoreTake(s_cache_mutex, portMAX_DELAY) != pdTRUE)
            return;
        prune_locked(max_items);
        xSemaphoreGive(s_cache_mutex);
    }

    size_t size()
    {
        if (!s_initialized || s_cache_mutex == NULL)
            return 0;
        if (xSemaphoreTake(s_cache_mutex, portMAX_DELAY) != pdTRUE)
            return 0;
        size_t count = s_cache.size();
        xSemaphoreGive(s_cache_mutex);
        return count;
    }

} // namespace thumbnail_cache