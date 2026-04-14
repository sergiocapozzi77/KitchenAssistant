#pragma once

#include <string>
#include <vector>
#include <random>
#include "cJSON.h"
#include "models.h"
#include "AppwriteHttpClient.h"

class RecipeGoodFoodService
{
public:
    // Uses global Appwrite client (from AppwriteClientInstance)
    RecipeGoodFoodService();

    std::vector<RecipeSuggestion> getRecipeSuggestions(
        const std::vector<std::string> &ingredients,
        const std::string &mealType = "",
        const std::vector<std::string> &keywords = {},
        const std::string &difficulty = "",
        const std::string &totalTime = "",
        const std::string &diet = "",
        const std::string &cuisine = "",
        const std::string &ratings = "",
        const std::string &calories = "",
        int page = 1);

private:
    AppwriteHttpClient _httpClient;
    std::mt19937 _rng;

    std::vector<RecipeSuggestion> parseSuggestionsResponse(const std::string &envelope);
};

// Global instance
extern RecipeGoodFoodService recipeGoodFoodService;
