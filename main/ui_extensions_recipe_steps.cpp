#include "ui_extensions_recipe_steps.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "RecipeStepsAggregationService.h"
#include "RecipeDetailService.h"
#include "StepProgressBar.h"

void UIExtensionsRecipeSteps::createRecipeStepsTask()
{
    xTaskCreatePinnedToCoreWithCaps(
        [](void *param)
        {
            auto recipeSuggestion = recipeDetailService.getSelectedRecipe();
            ESP_LOGI("actions", "Creating recipe steps for URL: %s", recipeSuggestion.url.c_str());
            Recipe recipe;
            auto success = recipeStepsAggregationService.getRecipe(recipeSuggestion.url, recipe);

            if (success)
            {
                ESP_LOGI("GetRecipeTask", "=== Recipe: %s ===", recipe.title.c_str());
                ESP_LOGI("GetRecipeTask", "  Description : %s", recipe.description.c_str());
                ESP_LOGI("GetRecipeTask", "  Prep: %s  Cook: %s  Servings: %s",
                         recipe.prepTime.c_str(), recipe.cookTime.c_str(), recipe.servings.c_str());
                ESP_LOGI("GetRecipeTask", "  Ingredients (%d):", (int)recipe.ingredients.size());
                for (const auto &ing : recipe.ingredients)
                    ESP_LOGI("GetRecipeTask", "    - %s %s %s%s",
                             ing.quantity.c_str(), ing.unit.c_str(), ing.name.c_str(),
                             ing.notes.empty() ? "" : (" (" + ing.notes + ")").c_str());

                ESP_LOGI("GetRecipeTask", "  Phases (%d):", (int)recipe.aggregatedSteps.size());

                std::vector<const char *> labels;
                labels.reserve(recipe.aggregatedSteps.size());

                for (const auto &phase : recipe.aggregatedSteps)
                {
                    labels.push_back(phase.title.c_str());

                    ESP_LOGI("GetRecipeTask", "  [%s] — %d ingredients, %d images",
                             phase.title.c_str(),
                             (int)phase.ingredients.size(),
                             (int)phase.imageRefs.size());
                    ESP_LOGI("GetRecipeTask", "    Method: %.120s...", phase.method.c_str());
                }

                lv_lock();
                lv_obj_clear_flag(objects.step_progress, LV_OBJ_FLAG_HIDDEN);
                spb_init(objects.step_progress__spb_root, recipe.aggregatedSteps.size(), labels.data());
                spb_set_step(objects.step_progress__spb_root, recipe.aggregatedSteps.size(), 1);
                lv_label_set_text(objects.phase_recipe_title, recipe.title.c_str());
                lv_unlock();
            }
            else
            {
                ESP_LOGE("GetRecipeTask", "getRecipe failed for: %s", recipeSuggestion.url.c_str());
            }

            lv_lock();
            lv_obj_add_flag(objects.phase_detail_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_unlock();

            ESP_LOGI("GetRecipeTask", "Stack high-water mark: %d bytes remaining",
                     (int)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

            vTaskDelete(NULL);
        },
        "GetRecipeTask",
        20480, // 20 KB — revisit after checking high-water mark
        nullptr,
        5,
        NULL, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}