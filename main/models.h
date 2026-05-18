#pragma once
#include <string>
#include <vector>

struct RecipeSuggestion
{
    std::string name;
    std::string url;
    std::string description;
    std::string imageUrl;
    std::string imageUrlBig; // only used for AI-generated recipes, larger header image
    std::string recipeSource;
    std::string author;
    std::string difficulty; // From "skillLevel"
    std::string totalTime;  // From "time"
    double ratingValue = 0.0;
    int ratingCount = 0;
    bool isPremium = false;
    std::string contentType;
    std::string id;

    // Full recipe details (populated on demand)
    std::vector<std::string> ingredients;
    std::vector<std::string> methodSteps;
    std::string prepTime;
    std::string cookTime;
    std::string servings;
    bool detailsFetched = false;
};

struct Product
{
    std::string name;
    int quantity = 0;
    std::string category;
    std::string rowId;
    std::string expiry;
    std::string barcode;
    bool frozen = false;
};

struct Favorite
{
    std::string id;   // Appwrite document ID ($id)
    std::string url;  // Recipe URL (unique identifier)
    std::string name; // Recipe name
    std::string description;
    std::string imageUrl;     // Recipe image URL
    std::string imageUrlBig;  // Larger header image URL for AI-generated recipes
    std::string difficulty;   // From "skillLevel"
    std::string totalTime;    // From "time"
    std::string recipeSource; // e.g., "ai-deepseek", "bbcgoodfood", etc.
    std::vector<std::string> ingredients;
    std::vector<std::string> methodSteps;
    std::vector<std::string> cookbookIds;
};

struct Cookbook
{
    std::string id;   // Appwrite $id
    std::string name;
};
