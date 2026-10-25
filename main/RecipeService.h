#pragma once

#include <string>
#include <vector>

#include "models.h"

class SupabaseClient;

class RecipeService
{
public:
    RecipeService();

    bool fetchSummaries(std::vector<RecipeSummary> &out);
    bool fetchForCookbook(const std::string &cookbookId, std::vector<Recipe> &out);

private:
    SupabaseClient &_client;
};

extern RecipeService recipeService;
