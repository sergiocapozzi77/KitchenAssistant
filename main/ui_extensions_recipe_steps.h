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

public:
    static void createRecipeStepsTask();
    static void populatePhaseIngredients(const Recipe &recipe, int phaseIndex = 0);
    static void populatePhaseMethod(const Recipe &recipe, int phaseIndex = 0);

    // Phase navigation
    static void setCurrentRecipe(const Recipe &recipe);
    static bool hasCurrentRecipe() { return s_hasRecipe; }
    static int getCurrentPhaseIndex() { return s_currentPhaseIndex; }
    static int getPhaseCount();
    static void navigateToPhase(int index);
    static void navigateNext();
    static void navigatePrev();
    static void updateUIForCurrentPhase();
    static void updatePhaseNavigationButtons();
};

#endif // UI_EXTENSIONS_RECIPE_STEPS_H