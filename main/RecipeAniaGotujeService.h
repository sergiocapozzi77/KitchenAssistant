#pragma once

#include <string>
#include <vector>
#include <random>
#include "AppwriteHttpClient.h"
#include "models.h"

class RecipeAniaGotujeService
{
public:
    // Constructor now requires Appwrite configuration
    RecipeAniaGotujeService();

    // Original public method – behaviour unchanged from caller's perspective
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
    std::mt19937 _rng; // kept for potential future use, but not used in search

    // Helper to parse the function response
    std::vector<RecipeSuggestion> parseSuggestionsResponse(const std::string &envelope);
};

extern RecipeAniaGotujeService recipeAniaGotujeService;