#pragma once

#include <string>
#include <vector>
#include "models.h"
#include "filters_ui.h"

class RecipeAIDetailService
{
public:
    RecipeAIDetailService();

    /**
     * Fetch full recipe details from DeepSeek AI for a given recipe suggestion.
     * Populates recipe.ingredients, recipe.methodSteps, and other fields.
     *
     * @param recipe Recipe suggestion to expand (name, description, etc.)
     * @param ingredients List of available ingredient names (from selected products)
     * @param filterState Current filter state (meal type, difficulty, etc.)
     * @return true on success, false on failure
     */
    bool fetchDetails(RecipeSuggestion &recipe,
                      const std::vector<std::string> &ingredients,
                      const filter_state_t *filterState);

private:
    std::string _apiKey;
    std::string _endpoint;

    // Helper to build the prompt for DeepSeek
    std::string buildPrompt(const RecipeSuggestion &recipe,
                            const std::vector<std::string> &ingredients,
                            const filter_state_t *filterState) const;

    // Helper to parse DeepSeek JSON response into recipe details
    bool parseAIResponse(const std::string &jsonResponse, RecipeSuggestion &recipe) const;

    // Helper to perform HTTP POST to DeepSeek API
    static std::string deepSeekHttpPost(const std::string &url, const std::string &body, int &status, int timeout_ms = 120000);
};

// Global instance
extern RecipeAIDetailService recipeAIDetailService;