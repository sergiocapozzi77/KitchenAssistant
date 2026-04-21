#pragma once

#include <string>
#include <vector>
#include "models.h"
#include "HttpClientHelper.h"

class DeepSeekAIService
{
public:
    DeepSeekAIService();

    /**
     * Fetch AI-generated recipe suggestions from DeepSeek.
     *
     * @param ingredients List of ingredient names to search for
     * @param mealType Optional meal type filter
     * @param keywords Optional keyword list
     * @param difficulty Optional difficulty filter
     * @param totalTime Optional total time filter
     * @param diet Optional diet filter
     * @param cuisine Optional cuisine filter
     * @param ratings Optional ratings filter (currently unused)
     * @param calories Optional calories filter (currently unused)
     * @param page Page number (1-based) - ignored for AI, always returns 5 recipes
     * @return Vector of recipe suggestions
     */
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
    HttpClientHelper _httpClient;
    std::string _apiKey;
    std::string _endpoint;

    // Helper to build the prompt for DeepSeek
    std::string buildPrompt(
        const std::vector<std::string> &ingredients,
        const std::string &mealType,
        const std::vector<std::string> &keywords,
        const std::string &difficulty,
        const std::string &totalTime,
        const std::string &diet,
        const std::string &cuisine,
        const std::string &ratings,
        const std::string &calories) const;

    // Helper to parse DeepSeek JSON response into RecipeSuggestion objects
    std::vector<RecipeSuggestion> parseAIResponse(const std::string &jsonResponse) const;
};

// Global instance
extern DeepSeekAIService deepSeekAIService;