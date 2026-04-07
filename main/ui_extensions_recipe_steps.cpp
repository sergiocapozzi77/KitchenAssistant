#include "ui_extensions_recipe_steps.h"
#include "ui.h"
#include "fonts.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "RecipeStepsAggregationService.h"
#include "RecipeDetailService.h"
#include "StepProgressBar.h"
#include "ui_extensions_internal.h"

// Static member definitions
Recipe UIExtensionsRecipeSteps::s_currentRecipe{};
int UIExtensionsRecipeSteps::s_currentPhaseIndex = 0;
bool UIExtensionsRecipeSteps::s_hasRecipe = false;

struct IngredientCheckboxContext
{
    lv_obj_t *label;
    lv_obj_t *line;
};

static void free_ingredient_checkbox_ctx_cb(lv_event_t *e)
{
    delete (IngredientCheckboxContext *)lv_event_get_user_data(e);
}

static void ingredient_checkbox_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!checkbox)
        return;

    IngredientCheckboxContext *ctx = static_cast<IngredientCheckboxContext *>(lv_event_get_user_data(e));
    if (!ctx || !ctx->label)
        return;

    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
    {
        // Apply strikethrough: gray color and text decoration
        lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_decor(ctx->label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
    }
    else
    {
        // Restore normal
        lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_decor(ctx->label, LV_TEXT_DECOR_NONE, 0);
    }
}

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
                         recipeStepsAggregationService.decodeDuration(recipe.prepTime).c_str(),
                         recipeStepsAggregationService.decodeDuration(recipe.cookTime).c_str(),
                         recipe.servings.c_str());
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

                setCurrentRecipe(recipe);

                lv_lock();
                lv_obj_clear_flag(objects.step_progress, LV_OBJ_FLAG_HIDDEN);
                spb_init(objects.step_progress__spb_root, recipe.aggregatedSteps.size(), labels.data());
                spb_set_step(objects.step_progress__spb_root, recipe.aggregatedSteps.size(), 1);
                lv_label_set_text(objects.phase_recipe_title, recipe.title.c_str());
                UIExtensionsRecipeSteps::populatePhaseIngredients(recipe, 0);
                UIExtensionsRecipeSteps::populatePhaseMethod(recipe, 0);
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

void UIExtensionsRecipeSteps::populatePhaseIngredients(const Recipe &recipe, int phaseIndex)
{
    if (!objects.recipe_phase_ingredients || !lv_obj_is_valid(objects.recipe_phase_ingredients))
        return;

    if (phaseIndex < 0 || phaseIndex >= (int)recipe.aggregatedSteps.size())
    {
        ESP_LOGE("UIExtensionsRecipeSteps", "Invalid phase index %d, phases=%d", phaseIndex, (int)recipe.aggregatedSteps.size());
        return;
    }

    ESP_LOGI("UIExtensionsRecipeSteps", "Populating phase ingredients, phases=%d, index=%d", (int)recipe.aggregatedSteps.size(), phaseIndex);
    if (!recipe.aggregatedSteps.empty())
    {
        ESP_LOGI("UIExtensionsRecipeSteps", "Phase %d '%s' has %d ingredients",
                 phaseIndex, recipe.aggregatedSteps[phaseIndex].title.c_str(),
                 (int)recipe.aggregatedSteps[phaseIndex].ingredients.size());
    }

    // lv_lock(); // Caller must hold lv_lock
    lv_obj_clean(objects.recipe_phase_ingredients);

    // Set up container for two-column layout
    lv_obj_set_flex_flow(objects.recipe_phase_ingredients, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(objects.recipe_phase_ingredients, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(objects.recipe_phase_ingredients, 12, 0);
    lv_obj_set_style_pad_row(objects.recipe_phase_ingredients, 12, 0);

    if (!recipe.aggregatedSteps.empty())
    {
        const auto &phase = recipe.aggregatedSteps[phaseIndex];
        if (!phase.ingredients.empty())
        {
            for (const auto &ing : phase.ingredients)
            {
                // Build display string
                std::string display;
                if (!ing.quantity.empty())
                {
                    display += ing.quantity;
                    if (!ing.unit.empty())
                        display += " " + ing.unit;
                    display += " ";
                }
                display += ing.name;
                if (!ing.notes.empty())
                    display += " (" + ing.notes + ")";

                lv_obj_t *row = lv_obj_create(objects.recipe_phase_ingredients);
                lv_obj_set_width(row, lv_pct(48));
                lv_obj_set_height(row, LV_SIZE_CONTENT);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_top(row, 8, 0);
                lv_obj_set_style_pad_bottom(row, 8, 0);
                lv_obj_set_style_pad_left(row, 0, 0);
                lv_obj_set_style_pad_right(row, 0, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_column(row, 8, 0);

                // Checkbox for ingredient
                lv_obj_t *checkbox = lv_checkbox_create(row);
                lv_checkbox_set_text(checkbox, "");
                lv_obj_set_style_pad_right(checkbox, 8, 0);
                lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR);
                lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);
                // Set explicit size for checkbox indicator (larger for recipe details)
                lv_obj_set_style_width(checkbox, 32, LV_PART_INDICATOR);
                lv_obj_set_style_height(checkbox, 32, LV_PART_INDICATOR);

                // Ingredient label - bigger font
                lv_obj_t *lbl = lv_label_create(row);
                lv_label_set_text(lbl, display.c_str());
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(lbl, 1);
                lv_obj_set_style_text_font(lbl, &ui_font_ext_font_montserrat_18, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);

                // Make row clickable to toggle checkbox
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, checkbox);

                // Context to connect checkbox with label
                IngredientCheckboxContext *ctx = new IngredientCheckboxContext{lbl, nullptr};
                lv_obj_add_event_cb(checkbox, ingredient_checkbox_cb, LV_EVENT_VALUE_CHANGED, ctx);
                lv_obj_add_event_cb(checkbox, free_ingredient_checkbox_ctx_cb, LV_EVENT_DELETE, ctx);

                vTaskDelay(pdMS_TO_TICKS(10)); // Yield to LVGL to render incrementally
            }
        }
        else
        {
            lv_obj_t *lbl = lv_label_create(objects.recipe_phase_ingredients);
            lv_label_set_text(lbl, "No ingredients for this phase.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }
    }
    else
    {
        lv_obj_t *lbl = lv_label_create(objects.recipe_phase_ingredients);
        lv_label_set_text(lbl, "No phases available.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    }

    // lv_unlock(); // Caller must hold lv_lock
}

void UIExtensionsRecipeSteps::populatePhaseMethod(const Recipe &recipe, int phaseIndex)
{
    if (!objects.recipe_phase_method || !lv_obj_is_valid(objects.recipe_phase_method))
        return;

    if (phaseIndex < 0 || phaseIndex >= (int)recipe.aggregatedSteps.size())
    {
        ESP_LOGE("UIExtensionsRecipeSteps", "Invalid phase index %d, phases=%d", phaseIndex, (int)recipe.aggregatedSteps.size());
        return;
    }

    ESP_LOGI("UIExtensionsRecipeSteps", "Populating phase method, phases=%d, index=%d", (int)recipe.aggregatedSteps.size(), phaseIndex);
    if (!recipe.aggregatedSteps.empty())
    {
        ESP_LOGI("UIExtensionsRecipeSteps", "Phase %d '%s' method length: %d",
                 phaseIndex, recipe.aggregatedSteps[phaseIndex].title.c_str(),
                 (int)recipe.aggregatedSteps[phaseIndex].method.size());
    }

    // lv_lock(); // Caller must hold lv_lock
    lv_obj_clean(objects.recipe_phase_method);

    // Set up container for method text
    lv_obj_set_flex_flow(objects.recipe_phase_method, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(objects.recipe_phase_method, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(objects.recipe_phase_method, 0, 0);

    if (!recipe.aggregatedSteps.empty())
    {
        const auto &phase = recipe.aggregatedSteps[phaseIndex];
        if (!phase.method.empty())
        {
            // Create a card similar to method steps in recipe detail
            lv_obj_t *method_card = lv_obj_create(objects.recipe_phase_method);
            lv_obj_set_width(method_card, lv_pct(100));
            lv_obj_set_height(method_card, LV_SIZE_CONTENT);
            lv_obj_clear_flag(method_card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(method_card, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(method_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_all(method_card, 12, 0);
            lv_obj_set_style_border_width(method_card, 1, 0);
            lv_obj_set_style_border_color(method_card, lv_color_hex(0xE9ECEF), 0);
            lv_obj_set_style_radius(method_card, 0, 0); // No rounded corners
            lv_obj_set_style_bg_color(method_card, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(method_card, LV_OPA_COVER, 0);

            // Method label inside card
            lv_obj_t *lbl = lv_label_create(method_card);
            lv_label_set_text(lbl, phase.method.c_str());
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl, lv_pct(100));
            lv_obj_set_style_text_font(lbl, &ui_font_ext_font_montserrat_18, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);
            lv_obj_set_style_text_line_space(lbl, 6, 0); // Line spacing for readability
            lv_obj_set_style_pad_top(lbl, 0, 0);
            lv_obj_set_style_pad_bottom(lbl, 0, 0);
            lv_obj_set_style_pad_left(lbl, 0, 0);
            lv_obj_set_style_pad_right(lbl, 0, 0);
        }
        else
        {
            lv_obj_t *lbl = lv_label_create(objects.recipe_phase_method);
            lv_label_set_text(lbl, "No method description for this phase.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }
    }
    else
    {
        lv_obj_t *lbl = lv_label_create(objects.recipe_phase_method);
        lv_label_set_text(lbl, "No phases available.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    }

    // lv_unlock(); // Caller must hold lv_lock
}

// ============================================================================
// Phase navigation
// ============================================================================

void UIExtensionsRecipeSteps::setCurrentRecipe(const Recipe &recipe)
{
    lv_lock();
    s_currentRecipe = recipe;
    s_currentPhaseIndex = 0;
    s_hasRecipe = true;
    updatePhaseNavigationButtons();
    lv_unlock();
}

int UIExtensionsRecipeSteps::getPhaseCount()
{
    if (!s_hasRecipe) return 0;
    return (int)s_currentRecipe.aggregatedSteps.size();
}

void UIExtensionsRecipeSteps::navigateToPhase(int index)
{
    if (!s_hasRecipe) return;
    if (index < 0 || index >= (int)s_currentRecipe.aggregatedSteps.size()) return;

    lv_lock();
    s_currentPhaseIndex = index;
    updateUIForCurrentPhase();
    updatePhaseNavigationButtons();
    lv_unlock();
}

void UIExtensionsRecipeSteps::navigateNext()
{
    if (!s_hasRecipe) return;
    int next = s_currentPhaseIndex + 1;
    if (next < (int)s_currentRecipe.aggregatedSteps.size())
        navigateToPhase(next);
}

void UIExtensionsRecipeSteps::navigatePrev()
{
    if (!s_hasRecipe) return;
    int prev = s_currentPhaseIndex - 1;
    if (prev >= 0)
        navigateToPhase(prev);
}

void UIExtensionsRecipeSteps::updateUIForCurrentPhase()
{
    if (!s_hasRecipe) return;

    // Update step progress bar
    if (objects.step_progress__spb_root && lv_obj_is_valid(objects.step_progress__spb_root))
    {
        spb_set_step(objects.step_progress__spb_root, s_currentRecipe.aggregatedSteps.size(), s_currentPhaseIndex + 1);
    }

    // Update phase title (optional)
    // lv_label_set_text(objects.phase_recipe_title, s_currentRecipe.title.c_str());

    // Repopulate ingredients and method for current phase
    populatePhaseIngredients(s_currentRecipe, s_currentPhaseIndex);
    populatePhaseMethod(s_currentRecipe, s_currentPhaseIndex);
}

void UIExtensionsRecipeSteps::updatePhaseNavigationButtons()
{
    // Enable/disable next button
    if (objects.recipe_phase_next && lv_obj_is_valid(objects.recipe_phase_next))
    {
        if (!s_hasRecipe)
            lv_obj_add_state(objects.recipe_phase_next, LV_STATE_DISABLED);
        else if (s_currentPhaseIndex >= (int)s_currentRecipe.aggregatedSteps.size() - 1)
            lv_obj_add_state(objects.recipe_phase_next, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(objects.recipe_phase_next, LV_STATE_DISABLED);
    }

    // Enable/disable prev button
    if (objects.recipe_phase_prev && lv_obj_is_valid(objects.recipe_phase_prev))
    {
        if (!s_hasRecipe)
            lv_obj_add_state(objects.recipe_phase_prev, LV_STATE_DISABLED);
        else if (s_currentPhaseIndex <= 0)
            lv_obj_add_state(objects.recipe_phase_prev, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(objects.recipe_phase_prev, LV_STATE_DISABLED);
    }
}