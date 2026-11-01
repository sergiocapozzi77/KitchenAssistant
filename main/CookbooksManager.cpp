#include "CookbooksManager.h"

#include "CookbookService.h"
#include "RecipesManager.h"

#include "esp_log.h"

#include <utility>

static const char *TAG = "COOKBOOK_MGR";

bool CookbooksManager::fetchSync()
{
    std::vector<Cookbook> fetched;
    if (!cookbookService.fetchCookbooks(fetched))
        return false;

    if (!recipesManager.summariesLoaded() && !recipesManager.fetchSummariesSync())
    {
        ESP_LOGE(TAG, "Recipe summaries unavailable; keeping cached cookbooks");
        return false;
    }

    for (auto &cookbook : fetched)
    {
        cookbook.recipeCount = recipesManager.recipeCountFor(cookbook.id);
        cookbook.coverImageUrl = recipesManager.coverFor(cookbook.id);
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _cookbooks = std::move(fetched);
    _loaded = true;
    ESP_LOGI(TAG, "Cookbook cache updated (%u rows)", (unsigned)_cookbooks.size());
    return true;
}

bool CookbooksManager::refreshSync()
{
    recipesManager.clear();
    return fetchSync();
}

bool CookbooksManager::loaded() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _loaded;
}

std::vector<Cookbook> CookbooksManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _cookbooks;
}

bool CookbooksManager::getCookbook(const std::string &id, Cookbook &out) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &cookbook : _cookbooks)
    {
        if (cookbook.id == id)
        {
            out = cookbook;
            return true;
        }
    }
    return false;
}

int CookbooksManager::recipeCountFor(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &cookbook : _cookbooks)
    {
        if (cookbook.id == id)
            return cookbook.recipeCount;
    }
    return 0;
}

CookbooksManager cookbooksManager;
