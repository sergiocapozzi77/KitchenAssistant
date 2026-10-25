#include "RecipesManager.h"

#include "RecipeService.h"

#include "esp_log.h"

#include <utility>

static const char *TAG = "RECIPE_MGR";

bool RecipesManager::fetchSummariesSync()
{
    std::vector<RecipeSummary> fetched;
    if (!recipeService.fetchSummaries(fetched))
    {
        ESP_LOGE(TAG, "Summary fetch failed; keeping cached summaries");
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _summaries = std::move(fetched);
    _summariesLoaded = true;
    ESP_LOGI(TAG, "Summary cache updated (%u rows)", (unsigned)_summaries.size());
    return true;
}

bool RecipesManager::fetchForCookbookSync(const std::string &cookbookId)
{
    std::vector<Recipe> fetched;
    bool ok = recipeService.fetchForCookbook(cookbookId, fetched);

    std::lock_guard<std::mutex> lock(_mutex);
    CookbookCache &cache = _cookbooks[cookbookId];
    if (ok)
    {
        cache.loaded = true;
        cache.failed = false;
        cache.recipes = std::move(fetched);
        ESP_LOGI(TAG, "Cookbook cache updated (%.36s, %u rows)", cookbookId.c_str(),
                 (unsigned)cache.recipes.size());
    }
    else
    {
        cache.loaded = false;
        cache.failed = true;
        cache.recipes.clear();
        ESP_LOGE(TAG, "Cookbook fetch failed (%.36s)", cookbookId.c_str());
    }
    return ok;
}

bool RecipesManager::summariesLoaded() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _summariesLoaded;
}

std::vector<RecipeSummary> RecipesManager::getSummaries() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _summaries;
}

bool RecipesManager::hasCookbook(const std::string &cookbookId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _cookbooks.find(cookbookId);
    return it != _cookbooks.end() && it->second.loaded && !it->second.failed;
}

bool RecipesManager::cookbookFailed(const std::string &cookbookId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _cookbooks.find(cookbookId);
    return it != _cookbooks.end() && it->second.failed;
}

std::vector<Recipe> RecipesManager::getCookbookRecipes(
    const std::string &cookbookId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _cookbooks.find(cookbookId);
    if (it == _cookbooks.end() || !it->second.loaded)
        return {};
    return it->second.recipes;
}

bool RecipesManager::getRecipe(const std::string &recipeId, Recipe &out) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &entry : _cookbooks)
    {
        if (!entry.second.loaded)
            continue;
        for (const auto &recipe : entry.second.recipes)
        {
            if (recipe.id == recipeId)
            {
                out = recipe;
                return true;
            }
        }
    }
    return false;
}

int RecipesManager::recipeCountFor(const std::string &cookbookId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    int count = 0;
    for (const auto &summary : _summaries)
    {
        for (const auto &id : summary.cookbookIds)
        {
            if (id == cookbookId)
            {
                ++count;
                break;
            }
        }
    }
    return count;
}

std::string RecipesManager::coverFor(const std::string &cookbookId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &summary : _summaries)
    {
        for (const auto &id : summary.cookbookIds)
        {
            if (id == cookbookId)
                return summary.imageUrl;
        }
    }
    return "";
}

void RecipesManager::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _summaries.clear();
    _summariesLoaded = false;
    _cookbooks.clear();
    ESP_LOGI(TAG, "Recipe caches cleared");
}

RecipesManager recipesManager;
