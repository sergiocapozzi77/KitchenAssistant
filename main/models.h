#pragma once
#include <string>

struct RecipeSuggestion
{
    std::string name;
    std::string url;
    std::string description;
    std::string imageUrl;
    std::string recipeSource;
    std::string author;
    std::string difficulty; // From "skillLevel"
    std::string totalTime;  // From "time"
    double ratingValue = 0.0;
    int ratingCount = 0;
    bool isPremium = false;
    std::string contentType;
    std::string id;
};

struct Product
{
    std::string name;
    int quantity = 0;
    std::string category;
    std::string rowId;
    std::string expiry;
};