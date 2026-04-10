#include "FavouritesManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "WiFiManager.h"
#include <algorithm>
#include "lvgl.h"
#include "ui.h"
#include "ui_extensions.h"

static const char *TAG = "FavouritesManager";

FavouritesManager favouritesManager;

// Public API: Fetch favourites from Appwrite (blocking)
void FavouritesManager::fetchFavourites()
{
    ESP_LOGI(TAG, "Fetching favourites from Appwrite...");

    std::vector<Favorite> newFavourites = _favouriteService.getFavourites();

    {
        std::lock_guard<std::mutex> lock(_favouritesMutex);
        _favourites = newFavourites;
    }

    ESP_LOGI(TAG, "Fetched %zu favourites", newFavourites.size());
}

// Public API: Get all favourites (thread-safe copy)
std::vector<Favorite> FavouritesManager::getFavourites() const
{
    std::lock_guard<std::mutex> lock(_favouritesMutex);
    return _favourites; // Return copy for thread safety
}

// Public API: Check if URL is favourited
bool FavouritesManager::isFavouriteUrl(const std::string &url) const
{
    std::lock_guard<std::mutex> lock(_favouritesMutex);

    auto it = std::find_if(_favourites.begin(), _favourites.end(),
                           [&url](const Favorite &fav)
                           { return fav.url == url; });

    return it != _favourites.end();
}

// Public API: Add favourite to cache (does not sync to Appwrite)
void FavouritesManager::addFavourite(const Favorite &favourite)
{
    std::lock_guard<std::mutex> lock(_favouritesMutex);

    // Check if already exists
    auto it = std::find_if(_favourites.begin(), _favourites.end(),
                           [&favourite](const Favorite &fav)
                           { return fav.url == favourite.url; });

    if (it == _favourites.end())
    {
        _favourites.push_back(favourite);
        ESP_LOGI(TAG, "Added favourite to cache: %s", favourite.url.c_str());
    }
    else
    {
        ESP_LOGI(TAG, "Favourite already in cache: %s", favourite.url.c_str());
    }
}

// Public API: Remove favourite from cache (does not sync to Appwrite)
void FavouritesManager::removeFavourite(const std::string &url)
{
    std::lock_guard<std::mutex> lock(_favouritesMutex);

    auto it = std::remove_if(_favourites.begin(), _favourites.end(),
                             [&url](const Favorite &fav)
                             { return fav.url == url; });

    if (it != _favourites.end())
    {
        _favourites.erase(it, _favourites.end());
        ESP_LOGI(TAG, "Removed favourite from cache: %s", url.c_str());
    }
    else
    {
        ESP_LOGI(TAG, "Favourite not found in cache: %s", url.c_str());
    }
}

// Public API: Start background task to fetch favourites
void FavouritesManager::startBackgroundFetch()
{
    // Create background task similar to products fetch
    xTaskCreate(
        fetchFavouritesTask,
        "fetchFavourites",
        8192,
        this,
        1,
        nullptr);
}

// Background task implementation
void FavouritesManager::fetchFavouritesTask(void *param)
{
    FavouritesManager *manager = static_cast<FavouritesManager *>(param);
    if (!manager)
    {
        ESP_LOGE(TAG, "Invalid manager pointer in fetch task");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Background favourites fetch task started");

    // Wait for WiFi to be connected (same as products fetch)
    while (!wifiManager.isConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Wait for SNTP sync (optional for favourites)
    while (!wifiManager.isSntpSynced())
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Fetch favourites
    manager->fetchFavourites();

    ESP_LOGI(TAG, "Background favourites fetch task completed");

    if (lv_tabview_get_tab_act(objects.tabview) == 2)
    {
        populateFavouritesList(objects.favourites_list, manager->getFavourites());
    }

    vTaskDelete(nullptr);
}