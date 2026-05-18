#include "CookbookManager.h"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "CookbookManager";

CookbookManager cookbookManager;

void CookbookManager::fetchCookbooks()
{
    ESP_LOGI(TAG, "Fetching cookbooks from Appwrite...");
    std::vector<Cookbook> newCookbooks = _cookbookService.getCookbooks();
    {
        std::lock_guard<std::mutex> lock(_cookbooksMutex);
        _cookbooks = newCookbooks;
    }
    ESP_LOGI(TAG, "Fetched %zu cookbooks", newCookbooks.size());
}

std::vector<Cookbook> CookbookManager::getCookbooks() const
{
    std::lock_guard<std::mutex> lock(_cookbooksMutex);
    return _cookbooks;
}

void CookbookManager::addCookbook(const Cookbook &cookbook)
{
    std::lock_guard<std::mutex> lock(_cookbooksMutex);
    _cookbooks.push_back(cookbook);
    ESP_LOGI(TAG, "Added cookbook to cache: %s (%s)", cookbook.name.c_str(), cookbook.id.c_str());
}

void CookbookManager::removeCookbook(const std::string &id)
{
    std::lock_guard<std::mutex> lock(_cookbooksMutex);
    auto it = std::remove_if(_cookbooks.begin(), _cookbooks.end(),
        [&id](const Cookbook &cb) { return cb.id == id; });
    if (it != _cookbooks.end())
    {
        _cookbooks.erase(it, _cookbooks.end());
        ESP_LOGI(TAG, "Removed cookbook from cache: %s", id.c_str());
    }
}
