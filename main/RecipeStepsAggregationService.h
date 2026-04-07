#pragma once

#include <string>
#include <vector>
#include "AppwriteHttpClient.h" // your existing HTTP client
#include "secrets.h"            // endpoint, projectId, apiKey, functionId
#include "cJSON.h"

// ─────────────────────────────────────────────────────────────────────────────
// Data structures
// ─────────────────────────────────────────────────────────────────────────────

struct RecipeIngredient
{
    std::string quantity; // "250", "q.b.", "½"
    std::string unit;     // "g", "ml", "" if none
    std::string name;     // "Farina 00"
    std::string notes;    // "semi-piccante", "" if none
};

struct RecipeImageRef
{
    std::string fileId;   // Appwrite Storage file ID
    std::string fileName; // e.g. "recipe_1234_img_1.jpg"
};

struct RecipePhase
{
    std::string title;                         // "Prepare the dough"
    std::vector<std::string> method;           // combined instruction text
    std::vector<RecipeIngredient> ingredients; // ingredients used in this phase
    std::vector<RecipeImageRef> imageRefs;     // uploaded images for this phase
};

struct Recipe
{
    std::string documentId; // Appwrite document ID
    std::string title;
    std::string description;
    std::string prepTime;
    std::string cookTime;
    std::string servings;
    std::vector<RecipePhase> aggregatedSteps; // grouped phases with images
};

// ─────────────────────────────────────────────────────────────────────────────
// Service
// ─────────────────────────────────────────────────────────────────────────────

class RecipeStepsAggregationService
{
public:
    RecipeStepsAggregationService();

    /**
     * Fetch a recipe from a URL. First checks if the recipe is already stored
     * in the Appwrite database by querying the sourceUrl column.
     * If found, returns the cached recipe immediately.
     * Otherwise, invokes the Appwrite function to scrape, process and store
     * the recipe from the URL.
     * On success, populates outRecipe and returns true.
     * On failure, logs the error and returns false.
     */
    bool getRecipe(const std::string &url, Recipe &outRecipe,
                   int maxWidth = 320, int maxHeight = 240);

    /**
     * Fetch a recipe from the Appwrite database by searching for the sourceUrl column.
     * If a matching recipe is found, populates outRecipe and returns true.
     * If not found or error, returns false.
     */
    bool getRecipeFromDatabase(const std::string &url, Recipe &outRecipe);

private:
    const std::string apiKey = APPWRITE_API_KEY;
    const std::string endpoint = "https://fra.cloud.appwrite.io/v1";
    const std::string projectId = "6954045e003c75c1c3bf";
    const std::string DatabaseId = "695404ac0021bf7d9707";
    const std::string CollectionId = "products";
    const std::string BarcodeCollectionId = "barcodes";
    const std::string RecipesCollectionId = RECIPES_COLLECTION_ID;
    const std::string RecipeDataColumn = "data"; // column in Recipes collection that holds the recipe JSON
    const std::string SourceUrlColumn = "sourceUrl";
    const std::string functionId = APPWRITE_FUNCTION_ID;
    AppwriteHttpClient _httpClient;
    // Debug helper: log all keys in a cJSON object
    void logAllKeys(cJSON *obj, const char *label);
    // JSON helpers
    static std::string safeString(cJSON *obj, const char *key);
    static void parseIngredient(cJSON *item, RecipeIngredient &out);
    static void parseIngredients(cJSON *arr, std::vector<RecipeIngredient> &out);
    static void parsePhase(cJSON *item, RecipePhase &out);
    bool parseRecipeResponse(const std::string &body, Recipe &out);
    cJSON *pollExecution(const std::string &executionId);
    std::string executeFunction(
        const std::string &url,
        int maxWidth,
        int maxHeight,
        bool async,
        int &statusOut);

public:
    /**
     * Decode ISO 8601 duration strings to human-readable format.
     * Examples:
     *   "PT5M"       → "5 mins"
     *   "PT2H"       → "2 hrs"
     *   "PT2H30M"    → "2 hrs 30 mins"
     *   "PT1H30M45S" → "1 hr 30 mins"
     */
    static std::string decodeDuration(const std::string &isoDuration);
};

extern RecipeStepsAggregationService recipeStepsAggregationService;