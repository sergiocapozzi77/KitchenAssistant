#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "models.h"

class RecipesManager
{
public:
    bool fetchSummariesSync();
    bool fetchForCookbookSync(const std::string &cookbookId);

    bool summariesLoaded() const;
    std::vector<RecipeSummary> getSummaries() const;

    bool hasCookbook(const std::string &cookbookId) const;
    bool cookbookFailed(const std::string &cookbookId) const;
    std::vector<Recipe> getCookbookRecipes(const std::string &cookbookId) const;
    // Number of loaded recipes for a cookbook (0 while loading or on failure).
    size_t loadedRecipeCount(const std::string &cookbookId) const;
    // Copies only the requested slice, so paging does not duplicate the whole
    // cached cookbook (recipes carry every ingredient and method string).
    std::vector<Recipe> getCookbookRecipePage(const std::string &cookbookId,
                                              size_t first, size_t count) const;
    bool getRecipe(const std::string &recipeId, Recipe &out) const;

    int recipeCountFor(const std::string &cookbookId) const;
    std::string coverFor(const std::string &cookbookId) const;

    void clear();

private:
    struct CookbookCache
    {
        bool loaded = false;
        bool failed = false;
        std::vector<Recipe> recipes;
    };

    mutable std::mutex _mutex;
    std::vector<RecipeSummary> _summaries;
    bool _summariesLoaded = false;
    std::map<std::string, CookbookCache> _cookbooks;
};

extern RecipesManager recipesManager;
