#ifndef UI_EXTENSIONS_RECIPE_STEPS_H
#define UI_EXTENSIONS_RECIPE_STEPS_H

#include "RecipeStepsAggregationService.h"
#include "RecipeDetailService.h"
#include "lvgl.h"
#include <vector>
#include <string>

class UIExtensionsRecipeSteps
{
private:
    static Recipe s_currentRecipe;
    static int s_currentPhaseIndex;
    static bool s_hasRecipe;
    static float s_scalingFactor;

public:
    static void createRecipeStepsTask();
    static void populatePhaseTitle(const Recipe &recipe, int phaseIndex);
    static void populatePhaseIngredients(const Recipe &recipe, int phaseIndex = 0);
    static void populatePhaseMethod(const Recipe &recipe, int phaseIndex = 0);
    static void populatePhaseImages(const Recipe &recipe, int phaseIndex = 0);

    // Phase navigation
    static void setCurrentRecipe(const Recipe &recipe);
    static void clearCurrentRecipe();
    static bool hasCurrentRecipe() { return s_hasRecipe; }
    static int getCurrentPhaseIndex() { return s_currentPhaseIndex; }
    static int getPhaseCount();
    static void navigateToPhase(int index);
    static void navigateNext();
    static void navigatePrev();
    static void updateUIForCurrentPhase();
    static void updatePhaseNavigationButtons();
    // Scaling
    static void setScalingFactor(float factor);
    static float getScalingFactor() { return s_scalingFactor; }
    static void applyScalingFactor(float factor);
    static void updateIngredientsWithScaling();
};

#endif // UI_EXTENSIONS_RECIPE_STEPS_H