#pragma once

#include "HttpClientHelper.h"
#include "models.h"
#include <string>

class RecipeDetailService
{
public:
    explicit RecipeDetailService(const HttpClientHelper &client,
                                 const std::string &functionId)
        : _client(client), _functionId(functionId) {}

    // Calls the Appwrite function and populates recipe.ingredients,
    // recipe.methodSteps, recipe.prepTime, recipe.cookTime, recipe.servings,
    // recipe.difficulty, and recipe.name (if empty).
    // Returns true on success.
    bool fetchDetails(RecipeSuggestion &recipe);

    RecipeSuggestion getSelectedRecipe() const { return selectedRecipe; }
    void setSelectedRecipe(const RecipeSuggestion &recipe) { selectedRecipe = recipe; }

private:
    RecipeSuggestion selectedRecipe;
    HttpClientHelper _client;
    std::string _functionId;

    // Parses the two-layer Appwrite response:
    //   outer: { responseBody: "<json>", responseStatusCode: 200, ... }
    //   inner: { success: true, recipe: { ... } }
    bool parseResponse(const std::string &raw, RecipeSuggestion &recipe);
};

extern RecipeDetailService recipeDetailService;