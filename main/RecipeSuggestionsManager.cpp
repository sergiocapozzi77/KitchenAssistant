
#include "RecipeSuggestionsManager.h"
#include "esp_heap_caps.h"
#include "RecipeGoodFoodService.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_extensions.h"
#include "ui.h"
#include "filters_ui.h"
#include "ProductsManager.h"

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

void RecipeSuggestionsManager::loadNextPage()
{
    currentPage++;
    loadCurrentPage();
}

void RecipeSuggestionsManager::loadPrevPage()
{
    if (currentPage > 1)
    {
        currentPage--;
    }
    else
    {
        return;
    }

    loadCurrentPage();
}

void RecipeSuggestionsManager::loadCurrentPage()
{
    int start = (currentPage - 1) * pageSize;
    int end = start + pageSize;

    ESP_LOGI("ShowRecipesTask", "Current page is %d", currentPage);
    ESP_LOGI("ShowRecipesTask", "Showing recipes from %d to %d", start, end);

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
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(showRecipesTask, "ShowRecipes", 16384, this, 5, nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
            ESP_LOGE("ShowRecipesTask", "Failed to create ShowRecipes task");
    }
    else
    {
        ESP_LOGI("ShowRecipesTask", "No recipes available, fetching them");
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(fetchRecipesTask, "FetchRecipes", 16384, this, 5, nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
            ESP_LOGE("ShowRecipesTask", "Failed to create FetchRecipes task");
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

void RecipeSuggestionsManager::showCurrentPageRecipes()
{
    std::vector<RecipeSuggestion> suggestions = getSuggestions();

    int start = (currentPage - 1) * pageSize;
    int end = start + pageSize;
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
}

void fetchRecipesTask(void *param)
{
    lv_lock();
    lv_obj_clear_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();

    RecipeSuggestionsManager *manager = (RecipeSuggestionsManager *)param;

    filter_state_t *filterState = get_filter_state();

    std::vector<Product> selectedProducts = productsManager.getSelectedProducts();
    std::vector<std::string> ingredients;
    for (const auto &product : selectedProducts) {
        ingredients.push_back(product.name);
    }

    std::vector<std::string> keywords = {};

    manager->appendSuggestions(recipeGoodFoodService.getRecipeSuggestions(
        ingredients,
        filterState->meal_type,
        keywords,
        filterState->difficulty, // difficulty (pass "" to skip the filter)
        filterState->total_time, // totalTime  (pass "" to skip the filter)
        filterState->diet,       // diet
        filterState->cuisine,    // cuisine
        "",                      // ratings
        "",                      // calories
        1));

    lv_lock();
    lv_obj_add_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();

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

    manager->showCurrentPageRecipes();

    vTaskDelete(nullptr);
}

void showRecipesTask(void *param)
{
    RecipeSuggestionsManager *manager = (RecipeSuggestionsManager *)param;
    ESP_LOGI("ShowRecipesTask", "Updating UI with fetched recipes...");
    manager->showCurrentPageRecipes();

    vTaskDelete(nullptr);
}