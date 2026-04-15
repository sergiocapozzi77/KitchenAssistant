#pragma once

#include <string>
#include <vector>
#include <random>
#include "AppwriteHttpClient.h"
#include "models.h"

class RecipeService
{
public:
    // Constructor uses global Appwrite client (from AppwriteClientInstance)
    RecipeService();

    /**
     * Fetch recipe suggestions from the specified source.
     *
     * @param source Recipe source identifier ("goodfood", "giallozafferano", "aniagotuje")
     * @param ingredients List of ingredient names to search for
     * @param mealType Optional meal type filter
     * @param keywords Optional keyword list
     * @param difficulty Optional difficulty filter
     * @param totalTime Optional total time filter
     * @param diet Optional diet filter
     * @param cuisine Optional cuisine filter
     * @param ratings Optional ratings filter (currently unused)
     * @param calories Optional calories filter (currently unused)
     * @param page Page number (1-based)
     * @return Vector of recipe suggestions
     */
    std::vector<RecipeSuggestion> getRecipeSuggestions(
        const std::string &source,
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
    std::mt19937 _rng; // kept for potential future use

    // Helper to parse the function response
    std::vector<RecipeSuggestion> parseSuggestionsResponse(const std::string &source, const std::string &envelope);

    // Map UI source value to canonical source value used in payload and parsing
    static std::string mapSourceToCanonical(const std::string &uiSource);
};

// Global instance
extern RecipeService recipeService;