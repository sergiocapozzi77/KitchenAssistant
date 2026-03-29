#pragma once

#include <string>
#include <vector>
#include <random>
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
    RecipeGialloZafferanoService();

    /**
     * Identical signature to RecipeGoodFoodService::getRecipeSuggestions.
     *
     * @param ingredients   Pantry items — shuffled per call to vary results
     * @param mealType      e.g. "primo", "secondo", "dolce", "antipasto"
     * @param keywords      Freeform search terms prepended to ingredients
     * @param difficulty    "easy" | "medium" | "hard"  (mapped → "facile"/"media"/"difficile")
     * @param totalTime     Not used as a URL filter (GZ has no time param in search);
     *                      retained so card-level time can still be surfaced
     * @param diet          "vegetarian" | "vegan"  (mapped → "vegetariana"/"vegana")
     * @param cuisine       Not used — GZ is implicitly Italian cuisine
     * @param ratings       Not used as a URL filter (no ratings query param on GZ search)
     * @param calories      Not used as a URL filter
     * @param page          1-based page index
     */
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
    std::string urlEncode(const std::string &s);
    int parseMinutes(const std::string &timeInput);

    // ── HTML parsing helpers ─────────────────────────────────────────────────

    /**
     * Returns the substring between the first occurrence of `open` (after
     * `fromPos`) and the subsequent `close`.  Returns "" if not found.
     */
    std::string extractBetween(const std::string &html,
                               const std::string &open,
                               const std::string &close,
                               size_t fromPos = 0,
                               size_t *endPos = nullptr) const;

    /**
     * Extracts the value of a named attribute from a single HTML tag string,
     * e.g. extractAttr("<img src=\"foo.jpg\" alt=\"x\">", "src") → "foo.jpg"
     */
    std::string extractAttr(const std::string &tag, const std::string &attr) const;

    /**
     * Strips all HTML tags from a fragment and returns plain text.
     */
    std::string stripTags(const std::string &html) const;

    /**
     * Splits the raw HTML into per-article chunks.
     * GZ search results are wrapped in <article ...> ... </article> blocks.
     */
    std::vector<std::string> splitArticles(const std::string &html) const;

    /**
     * Maps English difficulty tokens to GZ's Italian URL values.
     *  "easy"   → "facile"
     *  "medium" → "media"
     *  "hard"   → "difficile"
     * Unknown strings are passed through unchanged.
     */
    std::string mapDifficulty(const std::string &difficulty) const;

    /**
     * Maps English diet tokens to GZ's Italian URL values.
     *  "vegetarian" → "vegetariana"
     *  "vegan"      → "vegana"
     */
    std::string mapDiet(const std::string &diet) const;
};

extern RecipeGialloZafferanoService recipeGialloZafferanoService;
