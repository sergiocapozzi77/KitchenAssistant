#pragma once

#include <string>
#include <vector>
#include <random>
#include "AppwriteHttpClient.h"
#include "models.h" // RecipeSuggestion definition shared with RecipeGoodFoodService

/**
 * RecipeGialloZafferanoService
 *
 * Mirrors the public interface of RecipeGoodFoodService but targets
 * https://www.giallozafferano.it — an Italian recipe site with traditional
 * server-rendered HTML instead of Next.js __NEXT_DATA__ JSON.
 *
 * Filter parameters are mapped from English → Italian URL conventions.
 * Parsing is HTML-based; individual recipe cards are delimited by <article> tags.
 */
class RecipeGialloZafferanoService
{
public:
    // Uses global Appwrite client (from AppwriteClientInstance) and
    // APPWRITE_FUNCTION_ID from secrets.h
    RecipeGialloZafferanoService();

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
extern RecipeGialloZafferanoService recipeGialloZafferanoService;
