#pragma once

#include <string>
#include <vector>
#include <random>
#include "esp_http_client.h"
#include "cJSON.h"
#include "models.h"

class RecipeGoodFoodService
{
public:
    RecipeGoodFoodService();
    /*
#include "RecipeGoodFoodService.h"

void fetchRecipesTask(void *param)
{
    std::vector<std::string> ingredients = {
        "chicken",
        "lemon",
        "garlic"
    };

    std::vector<std::string> keywords = {
        "healthy",
        "quick"
    };

    auto suggestions = recipeGoodFoodService.getRecipeSuggestions(
        ingredients,
        "main-course",   // dishType
        keywords,
        "easy",          // difficulty (pass "" to skip the filter)
        "30-minutes"     // totalTime  (pass "" to skip the filter)
    );

    ESP_LOGI("App", "Got %d recipe suggestions", suggestions.size());

    for (const auto &r : suggestions)
    {
        ESP_LOGI("App",
                 "  [%s] %s (%d min) -> %s",
                 r.difficulty.c_str(),
                 r.name.c_str(),
                 r.prepTime,
                 r.url.c_str());
    }

    // TODO: pass results to UI, e.g.:
    // populateRecipeList(objects.recipe_list, suggestions);

    vTaskDelete(nullptr);
}

// Somewhere in initTasks() or after WiFi connects:
xTaskCreate(fetchRecipesTask, "FetchRecipes", 16384, nullptr, 5, nullptr);                                                                                                                                                                                    }
                                                                                                                                                                                                               // Somewhere in initTasks() or after WiFi connects:                                                                                                                                                          xTaskCreate(fetchRecipesTask, "FetchRecipes", 16384, nullptr, 5, nullptr);
    */

    std::vector<RecipeSuggestion> getRecipeSuggestions(
        const std::vector<std::string> &ingredients,
        const std::string &dishType,
        const std::vector<std::string> &keywords,
        const std::string &difficulty,
        const std::string &totalTime);

private:
    std::vector<RecipeSuggestion> fetchPage(
        const std::string &query,
        const std::string &dishType,
        const std::string &difficulty,
        const std::string &totalTime,
        int page);

    std::string httpGet(const std::string &url, int &status);
    std::string urlEncode(const std::string &s);
    std::string extractNextData(const std::string &html);
    int parseMinutes(const std::string &timeInput);

    std::mt19937 _rng;
};

extern RecipeGoodFoodService recipeGoodFoodService;
