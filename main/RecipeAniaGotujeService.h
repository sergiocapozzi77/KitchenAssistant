#pragma once

#include <string>
#include <vector>
#include <random>
#include "models.h" // Assuming you have this struct defined elsewhere

class RecipeAniaGotujeService
{
public:
    RecipeAniaGotujeService();

    std::vector<RecipeSuggestion> getRecipeSuggestions(
        const std::vector<std::string> &ingredients,
        const std::string &mealType,
        const std::vector<std::string> &keywords,
        const std::string &difficulty,
        const std::string &totalTime,
        const std::string &diet,
        const std::string &cuisine,
        const std::string &ratings,
        const std::string &calories,
        int page);

private:
    std::mt19937 _rng;

    std::vector<RecipeSuggestion> fetchPage(
        const std::string &query,
        const std::string &mealType,
        const std::string &difficulty,
        const std::string &diet,
        int page);

    std::string httpGet(const std::string &url, int &status);

    // Parse recipes from __NUXT__ JSON (primary method)
    std::vector<RecipeSuggestion> parseNuxtJson(const std::string &html) const;

    // Fallback HTML card parser
    std::vector<RecipeSuggestion> parseHtmlCards(const std::string &html) const;

    // Utility functions
    std::string urlEncode(const std::string &s);
    std::string extractBetween(const std::string &html, const std::string &open,
                               const std::string &close, size_t fromPos = 0,
                               size_t *endPos = nullptr) const;
    std::string extractAttr(const std::string &tag, const std::string &attr) const;
    std::string stripTags(const std::string &html) const;
    int parseMinutes(const std::string &timeInput);
    float parseFloat(const std::string &str) const;

    // Filter mapping (not used directly as site lacks filters)
    std::string mapDifficulty(const std::string &d) const;
    std::string mapDiet(const std::string &diet) const;
    std::string mapMealType(const std::string &mealType) const;
};

// Global singleton
extern RecipeAniaGotujeService recipeAniaGotujeService;