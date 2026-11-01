#pragma once

#include <string>
#include <vector>

struct Product
{
    std::string id;
    std::string name;
    std::string category;
    std::string expiry;  // YYYY-MM-DD, empty when null
    std::string barcode;
    int quantity = 0;
    bool frozen = false;
};

struct Cookbook
{
    std::string id;
    std::string name;
    std::string coverImageUrl; // derived from the summary list
    int recipeCount = 0;       // derived from the summary list
};

struct RecipeSummary
{
    std::string id;
    std::string imageUrl;
    std::vector<std::string> cookbookIds;
};

struct Ingredient
{
    std::string name;
    std::string quantity;
    std::string unit;
    std::string rawIngredient;
};

struct IngredientSection
{
    std::string name;
    std::vector<Ingredient> items;
};

struct InstructionSection
{
    std::string name;
    std::vector<std::string> steps;
};

struct Recipe
{
    std::string id;
    std::string name;
    std::string url;
    std::string imageUrl;
    std::string imageUrlBig;
    std::string description;
    std::string cuisine;
    std::string difficulty;
    std::string totalTime;
    std::string prepTime;
    std::string cookTime;
    std::string recipeSource;
    int serving = 0;
    std::vector<std::string> cookbookIds;
    std::vector<IngredientSection> ingredients;
    std::vector<InstructionSection> methodSteps;
};
