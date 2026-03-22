
#include "RecipeSuggestionsManager.h"
#include "RecipeGoodFoodService.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_extensions.h"
#include "ui.h"

RecipeSuggestionsManager recipeSuggestionsManager;

void fetchRecipesTask(void *param);
void showRecipesTask(void *param);

RecipeSuggestionsManager::RecipeSuggestionsManager()
{
}

void RecipeSuggestionsManager::reset()
{
    std::lock_guard<std::mutex> lock(_suggestionMutex);
    allSuggestions.clear();

    currentPage = 1;
}

void RecipeSuggestionsManager::loadCurrentPage()
{
    int start = (currentPage - 1) * pageSize;
    int end = start + pageSize;

    std::lock_guard<std::mutex> lock(_suggestionMutex);
    // If start is beyond the end of the vector, return an empty page
    bool hasRange =
        start >= 0 &&
        end >= 0 &&
        start < allSuggestions.size() &&
        end <= allSuggestions.size() &&
        start < end;

    if (hasRange)
    {
        ESP_LOGI("ShowRecipesTask", "Recipes available, showing them");
        xTaskCreate(showRecipesTask, "ShowRecipes", 16384, this, 5, nullptr);
    }
    else
    {
        ESP_LOGI("ShowRecipesTask", "No recipes available, fetching them");
        xTaskCreate(fetchRecipesTask, "FetchRecipes", 16384, this, 5, nullptr);
    }
}

void RecipeSuggestionsManager::assignSuggestions(const std::vector<RecipeSuggestion> &suggestions)
{
    std::lock_guard<std::mutex> lock(_suggestionMutex);
    allSuggestions = suggestions;
}

void RecipeSuggestionsManager::appendSuggestions(const std::vector<RecipeSuggestion> &suggestions)
{
    std::lock_guard<std::mutex> lock(_suggestionMutex);
    allSuggestions.insert(allSuggestions.end(), suggestions.begin(), suggestions.end());
}

std::vector<RecipeSuggestion> RecipeSuggestionsManager::getSuggestions()
{
    std::lock_guard<std::mutex> lock(_suggestionMutex);
    return allSuggestions;
}

int RecipeSuggestionsManager::getSuggestionSize()
{
    std::lock_guard<std::mutex> lock(_suggestionMutex);
    return allSuggestions.size();
}

void fetchRecipesTask(void *param)
{
    RecipeSuggestionsManager *manager = (RecipeSuggestionsManager *)param;
    std::vector<std::string> ingredients = {
        "chicken",
        "lemon",
        "garlic"};

    std::vector<std::string> keywords = {
        "healthy",
        "quick"};

    manager->appendSuggestions(recipeGoodFoodService.getRecipeSuggestions(
        ingredients,
        "main-course", // dishType
        keywords,
        "easy",       // difficulty (pass "" to skip the filter)
        "30-minutes", // totalTime  (pass "" to skip the filter)
        1));

    ESP_LOGI("ShowRecipesTask", "Got %d recipe suggestions", manager->getSuggestionSize());

    std::vector<RecipeSuggestion> suggestions = manager->getSuggestions();
    for (const auto &r : suggestions)
    {
        ESP_LOGI("ShowRecipesTask",
                 "  [%s] %s (%s) -> %s",
                 r.difficulty.c_str(),
                 r.name.c_str(),
                 r.totalTime.c_str(),
                 r.url.c_str());
    }

    // xTaskCreate(showRecipesTask, "ShowRecipes", 16384, manager, 5, nullptr);
    populateRecipeList(objects.recipes_list, suggestions);

    vTaskDelete(nullptr);
}

void showRecipesTask(void *param)
{
    RecipeSuggestionsManager *manager = (RecipeSuggestionsManager *)param;
    ESP_LOGI("ShowRecipesTask", "Updating UI with fetched recipes...");
    std::vector<RecipeSuggestion> suggestions = manager->getSuggestions();

    int start = (manager->currentPage - 1) * manager->pageSize;
    int end = start + manager->pageSize;
    // If start is beyond the end of the vector, return an empty page
    bool hasRange =
        start >= 0 &&
        end >= 0 &&
        start < suggestions.size() &&
        end <= suggestions.size() &&
        start < end;

    if (!hasRange)
    {
        ESP_LOGI("ShowRecipesTask", "Unable to show recipes");
        return;
    }

    std::vector<RecipeSuggestion> pageItems(
        suggestions.begin() + start,
        suggestions.begin() + std::min(end, (int)suggestions.size()));

    ESP_LOGI("ShowRecipesTask", "Passing %d recipes to UI", pageItems.size());
    populateRecipeList(objects.recipes_list, pageItems);

    vTaskDelete(nullptr);
}