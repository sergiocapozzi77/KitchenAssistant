#pragma once

#include <string>
#include "models.h"
#include "cJSON.h"

class RecipeDetailService
{
public:
    // Fetches and parses full recipe details (ingredients + method) into recipe.
    // Returns true on success. Must be called from a FreeRTOS task (not LVGL thread).
    bool fetchDetails(RecipeSuggestion &recipe);
    RecipeSuggestion getSelectedRecipe() const { return selectedRecipe; }

private:
    RecipeSuggestion selectedRecipe;
    // Stream HTML and extract __NEXT_DATA__, JSON-LD, and __POST_CONTENT__ blocks.
    bool fetchHtmlAndExtract(const std::string &url,
                             std::string &nextData,
                             std::string &jsonLd,
                             std::string &postContent);

    bool parseNextData(const std::string &json, RecipeSuggestion &recipe);
    bool parseJsonLd(const std::string &json, RecipeSuggestion &recipe);
    bool parsePostContent(const std::string &json, RecipeSuggestion &recipe);

    // Shared helper: parse {ingredients:[sections], method:[sections]} node
    void cIngredientsMethod(cJSON *node, RecipeSuggestion &recipe);

    std::string parseIso8601Duration(const std::string &iso);
    std::string sanitizeText(const std::string &text);
};

extern RecipeDetailService recipeDetailService;
