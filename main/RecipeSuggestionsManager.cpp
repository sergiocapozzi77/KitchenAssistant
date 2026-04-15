
#include "RecipeSuggestionsManager.h"
#include "esp_heap_caps.h"
#include "RecipeService.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_extensions.h"
#include "ui.h"
#include "filters_ui.h"
#include "ProductsManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

RecipeSuggestionsManager recipeSuggestionsManager;

void fetchRecipesTask(void *param);
void showRecipesTask(void *param);

RecipeSuggestionsManager::RecipeSuggestionsManager()
{
}

void RecipeSuggestionsManager::reset()
{
    {
        std::lock_guard<std::mutex> lock(_suggestionMutex);
        allSuggestions.clear();
        currentPage = 1;
    }
    updatePaginationButtons();
}

void RecipeSuggestionsManager::loadNextPage()
{
    currentPage++;
    loadCurrentPage();
    updatePaginationButtons();
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
    updatePaginationButtons();
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
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(showRecipesTask, "ShowRecipes", 32768, this, 5, nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
            ESP_LOGE("ShowRecipesTask", "Failed to create ShowRecipes task");
    }
    else
    {
        ESP_LOGI("ShowRecipesTask", "No recipes available, fetching them");
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(fetchRecipesTask, "FetchRecipes", 32768, this, 5, nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
            ESP_LOGE("ShowRecipesTask", "Failed to create FetchRecipes task");
    }
}

void RecipeSuggestionsManager::assignSuggestions(const std::vector<RecipeSuggestion> &suggestions)
{
    {
        std::lock_guard<std::mutex> lock(_suggestionMutex);
        allSuggestions = suggestions;
    }
    updatePaginationButtons();
}

void RecipeSuggestionsManager::appendSuggestions(const std::vector<RecipeSuggestion> &suggestions)
{
    {
        std::lock_guard<std::mutex> lock(_suggestionMutex);
        allSuggestions.insert(allSuggestions.end(), suggestions.begin(), suggestions.end());
    }
    updatePaginationButtons();
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

void RecipeSuggestionsManager::showCurrentPageRecipes(bool force)
{
    if (lv_obj_get_child_count(objects.recipes_list) > 0 && !force)
    {
        updatePaginationButtons();
        return; // Don't repopulate if already populated (e.g. when switching tabs)
    }

    std::vector<RecipeSuggestion> suggestions = getSuggestions();

    int start = (currentPage - 1) * pageSize;
    int end = std::min(start + pageSize, (int)suggestions.size());
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
        updatePaginationButtons();
        return;
    }

    std::vector<RecipeSuggestion> pageItems(
        suggestions.begin() + start,
        suggestions.begin() + std::min(end, (int)suggestions.size()));

    ESP_LOGI("ShowRecipesTask", "Passing %d recipes to UI", pageItems.size());
    populateRecipeList(objects.recipes_list, pageItems);
    updatePaginationButtons();
}

int RecipeSuggestionsManager::getTotalPages() const
{
    std::lock_guard<std::mutex> lock(_suggestionMutex);
    if (allSuggestions.empty()) return 0;
    return (allSuggestions.size() + pageSize - 1) / pageSize;
}

void RecipeSuggestionsManager::updatePaginationButtons()
{
    lv_lock();
    if (!objects.recipe_suggestion_prev_btn || !lv_obj_is_valid(objects.recipe_suggestion_prev_btn) ||
        !objects.recipe_suggestion_next_btn || !lv_obj_is_valid(objects.recipe_suggestion_next_btn))
    {
        lv_unlock();
        return;
    }

    // Update previous button
    if (currentPage > 1)
    {
        lv_obj_clear_state(objects.recipe_suggestion_prev_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(objects.recipe_suggestion_prev_btn, LV_STATE_DISABLED);
    }

    // Update next button
    int totalPages = getTotalPages();
    if (totalPages == 0 || currentPage < totalPages)
    {
        lv_obj_clear_state(objects.recipe_suggestion_next_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(objects.recipe_suggestion_next_btn, LV_STATE_DISABLED);
    }
    lv_unlock();
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
    ingredients.reserve(selectedProducts.size());
    ESP_LOGI("ShowRecipesTask", "Free heap: %lu", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI("ShowRecipesTask", "Selected products: %d", (int)selectedProducts.size());

    heap_caps_check_integrity_all(true);
    ESP_LOGI("ShowRecipesTask", "Heap OK before building ingredients");

    if (lv_obj_has_state(objects.products_filters_panel__poducts_selected_cb, LV_STATE_CHECKED))
    {
        // checkbox is checked

        for (const auto &product : selectedProducts)
        {
            ingredients.push_back(product.name);
        }
    }

    heap_caps_check_integrity_all(true);
    ESP_LOGI("ShowRecipesTask", "Heap OK after building ingredients");

    std::vector<std::string> keywords = filterState->keywords;

    ESP_LOGI("ShowRecipesTask", "Largest free block: %d bytes", (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Convert filter state to strings safely (NULL -> "")
    std::string mealType = filterState->meal_type ? filterState->meal_type : "";
    std::string difficulty = filterState->difficulty ? filterState->difficulty : "";
    std::string totalTime = filterState->total_time ? filterState->total_time : "";
    std::string diet = filterState->diet ? filterState->diet : "";
    std::string cuisine = filterState->cuisine ? filterState->cuisine : "";
    std::string source = filterState->source ? filterState->source : "";

    ESP_LOGI("ShowRecipesTask", "Filters - mealType: '%s', difficulty: '%s', totalTime: '%s', diet: '%s', cuisine: '%s', source: '%s'",
             mealType.c_str(), difficulty.c_str(), totalTime.c_str(), diet.c_str(), cuisine.c_str(), source.c_str());

    ESP_LOGI("ShowRecipesTask", "Fetching recipes with %d ingredients and %d keywords...",
             (int)ingredients.size(), (int)keywords.size());

    std::vector<RecipeSuggestion> suggestions;
    std::string sourceStr = filterState->source ? filterState->source : "goodfood";
    suggestions = recipeService.getRecipeSuggestions(
        sourceStr,
        ingredients,
        mealType,
        keywords,
        difficulty,
        totalTime,
        diet,
        cuisine,
        "", // ratings
        "", // calories
        1);

    manager->appendSuggestions(suggestions);

    lv_lock();
    lv_obj_add_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();

    ESP_LOGI("ShowRecipesTask", "Got %d recipe suggestions", manager->getSuggestionSize());

    for (const auto &r : suggestions)
    {
        ESP_LOGI("ShowRecipesTask",
                 "  [%s] %s (%s) -> %s",
                 r.difficulty.c_str(),
                 r.name.c_str(),
                 r.totalTime.c_str(),
                 r.url.c_str());
    }

    manager->showCurrentPageRecipes(true);

    vTaskDelete(nullptr);
}

void showRecipesTask(void *param)
{
    RecipeSuggestionsManager *manager = (RecipeSuggestionsManager *)param;
    ESP_LOGI("ShowRecipesTask", "Updating UI with fetched recipes...");
    manager->showCurrentPageRecipes(true);

    vTaskDelete(nullptr);
}