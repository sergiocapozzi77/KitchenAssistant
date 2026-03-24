
#pragma once
#include <vector>
#include "models.h"
#include <mutex>

class RecipeSuggestionsManager
{
public:
    RecipeSuggestionsManager();
    void assignSuggestions(const std::vector<RecipeSuggestion> &suggestions);
    void loadCurrentPage();
    void appendSuggestions(const std::vector<RecipeSuggestion> &suggestions);
    void reset();
    void loadNextPage();
    void loadPrevPage();

    int getSuggestionSize();
    std::vector<RecipeSuggestion> getSuggestions();
    int currentPage = 1;
    const int pageSize = 6;
    void showCurrentPageRecipes();

private:
    std::vector<RecipeSuggestion> allSuggestions;
    mutable std::mutex _suggestionMutex;
};

extern RecipeSuggestionsManager recipeSuggestionsManager;