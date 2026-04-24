#include "spiffs_cleanup.h"
#include "esp_log.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

static const char *TAG = "SPIFFS_CLEANUP";

#define BASE_DIR CONFIG_BSP_SPIFFS_MOUNT_POINT

bool spiffs_cleanup::delete_wifi_creds()
{
    const char *path = BASE_DIR "/wifi_creds.json";
    if (access(path, F_OK) != 0)
    {
        ESP_LOGI(TAG, "No wifi_creds.json to delete");
        return true;
    }
    if (unlink(path) == 0)
    {
        ESP_LOGI(TAG, "Deleted wifi_creds.json");
        return true;
    }
    ESP_LOGE(TAG, "Failed to delete wifi_creds.json (errno=%d)", errno);
    return false;
}

bool spiffs_cleanup::delete_thumbnail_cache()
{
    bool all_ok = true;

    // Delete index file first (silently ignore if absent)
    const char *index = BASE_DIR "/thumb_index.json";
    if (access(index, F_OK) == 0 && unlink(index) != 0)
    {
        ESP_LOGE(TAG, "Failed to delete thumb_index.json (errno=%d)", errno);
        all_ok = false;
    }
    else
    {
        ESP_LOGI(TAG, "Deleted thumb_index.json");
    }

    // Delete all .jpg files in the SPIFFS root
    DIR *dir = opendir(BASE_DIR);
    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to open " BASE_DIR " (errno=%d)", errno);
        return false;
    }

    struct dirent *entry;
    int deleted = 0;
    while ((entry = readdir(dir)) != nullptr)
    {
        // Only match hex-hash .jpg files (16 hex chars + ".jpg")
        size_t len = strlen(entry->d_name);
        if (len == 20 && strcmp(entry->d_name + 16, ".jpg") == 0)
        {
            // Verify it's all hex before the extension
            bool is_hex = true;
            for (size_t i = 0; i < 16; i++)
            {
                if (!((entry->d_name[i] >= '0' && entry->d_name[i] <= '9') ||
                      (entry->d_name[i] >= 'a' && entry->d_name[i] <= 'f') ||
                      (entry->d_name[i] >= 'A' && entry->d_name[i] <= 'F')))
                {
                    is_hex = false;
                    break;
                }
            }
            if (!is_hex)
                continue;

            char full_path[64];
            snprintf(full_path, sizeof(full_path), BASE_DIR "/%s", entry->d_name);
            if (unlink(full_path) == 0)
            {
                deleted++;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to delete %s (errno=%d)", full_path, errno);
                all_ok = false;
            }
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Deleted %d cached thumbnail(s)", deleted);
    return all_ok;
}
