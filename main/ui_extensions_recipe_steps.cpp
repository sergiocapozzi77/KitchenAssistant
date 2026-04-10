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
#include "styles.h"
#include "secrets.h"

// Helper to convert RecipeIngredient to display string
static std::string ingredientToDisplayText(const RecipeIngredient &ing)
{
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
    return display;
}

// Appwrite Storage constants for recipe phase images
static const std::string STORAGE_ENDPOINT = "https://fra.cloud.appwrite.io/v1";
static const std::string STORAGE_BUCKET_ID = "69cff07c002843354236";

static std::string makeStorageUrl(const std::string &fileId)
{
    std::string url = STORAGE_ENDPOINT + "/storage/buckets/" + STORAGE_BUCKET_ID + "/files/" + fileId + "/view";
    return url;
}

// Static member definitions
Recipe UIExtensionsRecipeSteps::s_currentRecipe{};
int UIExtensionsRecipeSteps::s_currentPhaseIndex = 0;
bool UIExtensionsRecipeSteps::s_hasRecipe = false;

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
                    ESP_LOGI("GetRecipeTask", "    Method steps: %d", (int)phase.method.size());
                    if (!phase.method.empty())
                    {
                        ESP_LOGI("GetRecipeTask", "      - %.120s...", phase.method[0].c_str());
                    }
                }

                setCurrentRecipe(recipe);

                lv_lock();
                lv_obj_clear_flag(objects.step_progress, LV_OBJ_FLAG_HIDDEN);
                spb_init(objects.step_progress__spb_root, recipe.aggregatedSteps.size(), labels.data());
                spb_set_step(objects.step_progress__spb_root, recipe.aggregatedSteps.size(), 1);
                lv_label_set_text(objects.phase_recipe_title, recipe.title.c_str());
                UIExtensionsRecipeSteps::updateUIForCurrentPhase();

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

void UIExtensionsRecipeSteps::populatePhaseTitle(const Recipe &recipe, int phaseIndex)
{
    if (!objects.recipe_phase_title_txt || !lv_obj_is_valid(objects.recipe_phase_title_txt))
        return;

    if (phaseIndex < 0 || phaseIndex >= (int)recipe.aggregatedSteps.size())
    {
        ESP_LOGE("UIExtensionsRecipeSteps", "Invalid phase index %d, phases=%d", phaseIndex, (int)recipe.aggregatedSteps.size());
        return;
    }

    ESP_LOGI("UIExtensionsRecipeSteps", "Populating phase title, phases=%d, index=%d", (int)recipe.aggregatedSteps.size(), phaseIndex);
    if (!recipe.aggregatedSteps.empty())
    {
        ESP_LOGI("UIExtensionsRecipeSteps", "Phase %d '%s' has %d images",
                 phaseIndex, recipe.aggregatedSteps[phaseIndex].title.c_str(),
                 (int)recipe.aggregatedSteps[phaseIndex].imageRefs.size());
    }

    if (!recipe.aggregatedSteps.empty())
    {
        const auto &phase = recipe.aggregatedSteps[phaseIndex];
        lv_label_set_text(objects.recipe_phase_title_txt, phase.title.c_str());
    }
}

void UIExtensionsRecipeSteps::populatePhaseImages(const Recipe &recipe, int phaseIndex)
{
    if (!objects.recipe_phase_imgs || !lv_obj_is_valid(objects.recipe_phase_imgs))
        return;

    if (phaseIndex < 0 || phaseIndex >= (int)recipe.aggregatedSteps.size())
    {
        ESP_LOGE("UIExtensionsRecipeSteps", "Invalid phase index %d, phases=%d", phaseIndex, (int)recipe.aggregatedSteps.size());
        return;
    }

    ESP_LOGI("UIExtensionsRecipeSteps", "Populating phase images, phases=%d, index=%d", (int)recipe.aggregatedSteps.size(), phaseIndex);
    s_thumb_generation++;

    if (!recipe.aggregatedSteps.empty())
    {
        ESP_LOGI("UIExtensionsRecipeSteps", "Phase %d '%s' has %d images",
                 phaseIndex, recipe.aggregatedSteps[phaseIndex].title.c_str(),
                 (int)recipe.aggregatedSteps[phaseIndex].imageRefs.size());
    }

    // Clear container and prepare layout
    lv_obj_clean(objects.recipe_phase_imgs);
    lv_obj_clear_flag(objects.recipe_phase_imgs, LV_OBJ_FLAG_HIDDEN);
    // lv_obj_set_flex_flow(objects.recipe_phase_imgs, LV_FLEX_FLOW_ROW_WRAP);
    // lv_obj_set_flex_align(objects.recipe_phase_imgs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // lv_obj_set_style_pad_all(objects.recipe_phase_imgs, 8, 0);
    // lv_obj_set_style_pad_column(objects.recipe_phase_imgs, 8, 0);
    // lv_obj_set_style_pad_row(objects.recipe_phase_imgs, 8, 0);

    std::vector<ThumbContext *> pending_thumbs;
    int thumbWidth = 300;
    int thumbHeight = 200;

    if (!recipe.aggregatedSteps.empty())
    {
        const auto &phase = recipe.aggregatedSteps[phaseIndex];
        if (!phase.imageRefs.empty())
        {
            for (const auto &imgRef : phase.imageRefs)
            {
                // Create placeholder image
                lv_obj_t *thumb = lv_image_create(objects.recipe_phase_imgs);
                lv_obj_set_size(thumb, thumbWidth, thumbHeight);             // thumbnail size
                lv_obj_set_style_bg_color(thumb, lv_color_hex(0xDEE2E6), 0); // grey until loaded
                lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(thumb, 8, 0);
                lv_obj_set_style_border_width(thumb, 0, 0);
                //  lv_image_set_inner_align(thumb, LV_IMAGE_ALIGN_COVER);

                ESP_LOGI("UIExtensionsRecipeSteps", "Scheduling image fetch: %s", imgRef.url.c_str());
                ThumbContext *tctx = new ThumbContext{thumb, imgRef.url, s_thumb_generation};
                pending_thumbs.push_back(tctx);
            }
        }
        else
        {
            lv_obj_add_flag(objects.recipe_phase_imgs, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        lv_obj_add_flag(objects.recipe_phase_imgs, LV_OBJ_FLAG_HIDDEN);
    }

    // Spawn thumbnail worker task if there are images to fetch
    if (!pending_thumbs.empty())
    {
        ThumbWorkerCtx *wctx = new ThumbWorkerCtx{pending_thumbs};
        wctx->maxWidth = 0;
        wctx->maxHeight = thumbHeight;

        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
            thumb_worker_task, "phase_img_worker", 8192, wctx, 5, NULL, 1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
        {
            ESP_LOGE("UIExtensionsRecipeSteps", "Failed to create thumb worker task");
            for (auto *tctx : pending_thumbs)
                delete tctx;
            delete wctx;
        }
    }
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

    if (!recipe.aggregatedSteps.empty())
    {
        const auto &phase = recipe.aggregatedSteps[phaseIndex];
        if (!phase.ingredients.empty())
        {
            // Convert RecipeIngredient structs to display strings
            std::vector<std::string> displayTexts;
            for (const auto &ing : phase.ingredients)
            {
                displayTexts.push_back(ingredientToDisplayText(ing));
            }
            // Use shared function to populate UI
            populateIngredientsUI(objects.recipe_phase_ingredients, displayTexts);
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
            int step_num = 1;
            for (const auto &step : phase.method)
            {
                lv_obj_t *step_card = lv_obj_create(objects.recipe_phase_method);
                lv_obj_set_width(step_card, lv_pct(100));
                lv_obj_set_height(step_card, LV_SIZE_CONTENT);
                lv_obj_clear_flag(step_card, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_flex_flow(step_card, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(step_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_all(step_card, 12, 0);
                lv_obj_set_style_pad_column(step_card, 12, 0);
                lv_obj_set_style_border_width(step_card, 1, 0);
                lv_obj_set_style_border_color(step_card, lv_color_hex(0xE9ECEF), 0);
                lv_obj_set_style_radius(step_card, 0, 0); // No rounded corners
                lv_obj_set_style_bg_color(step_card, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(step_card, LV_OPA_COVER, 0);

                // Step number circle
                lv_obj_t *num_cont = lv_obj_create(step_card);
                lv_obj_set_size(num_cont, 32, 32);
                lv_obj_clear_flag(num_cont, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_radius(num_cont, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(num_cont, lv_color_hex(theme_colors[active_theme_index][0]), 0);
                lv_obj_set_style_bg_opa(num_cont, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(num_cont, 0, 0);
                lv_obj_set_style_pad_all(num_cont, 0, 0);
                lv_obj_set_flex_flow(num_cont, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(num_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t *num_lbl = lv_label_create(num_cont);
                char nbuf[8];
                snprintf(nbuf, sizeof(nbuf), "%d", step_num++);
                lv_label_set_text(num_lbl, nbuf);
                lv_obj_set_style_text_color(num_lbl, lv_color_white(), 0);
                lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_16, 0);

                lv_obj_t *text_lbl = lv_label_create(step_card);
                lv_label_set_text(text_lbl, step.c_str());
                lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(text_lbl, 1);
                lv_obj_set_style_text_font(text_lbl, &ui_font_ext_font_montserrat_18, 0);
                lv_obj_set_style_text_color(text_lbl, lv_color_hex(0x212529), 0);
                lv_obj_set_style_text_line_space(text_lbl, 6, 0); // Line spacing for readability
                lv_obj_set_style_pad_all(text_lbl, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
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
    if (!s_hasRecipe)
        return 0;
    return (int)s_currentRecipe.aggregatedSteps.size();
}

void UIExtensionsRecipeSteps::navigateToPhase(int index)
{
    if (!s_hasRecipe)
        return;
    if (index < 0 || index >= (int)s_currentRecipe.aggregatedSteps.size())
        return;

    lv_lock();
    s_currentPhaseIndex = index;
    updateUIForCurrentPhase();
    updatePhaseNavigationButtons();
    lv_unlock();
}

void UIExtensionsRecipeSteps::navigateNext()
{
    if (!s_hasRecipe)
        return;
    int next = s_currentPhaseIndex + 1;
    if (next < (int)s_currentRecipe.aggregatedSteps.size())
        navigateToPhase(next);
}

void UIExtensionsRecipeSteps::navigatePrev()
{
    if (!s_hasRecipe)
        return;
    int prev = s_currentPhaseIndex - 1;
    if (prev >= 0)
        navigateToPhase(prev);
}

void UIExtensionsRecipeSteps::updateUIForCurrentPhase()
{
    if (!s_hasRecipe)
        return;

    // Update step progress bar
    if (objects.step_progress__spb_root && lv_obj_is_valid(objects.step_progress__spb_root))
    {
        spb_set_step(objects.step_progress__spb_root, s_currentRecipe.aggregatedSteps.size(), s_currentPhaseIndex + 1);
    }

    // Update phase title (optional)
    // lv_label_set_text(objects.phase_recipe_title, s_currentRecipe.title.c_str());

    // Repopulate ingredients and method for current phase
    populatePhaseTitle(s_currentRecipe, s_currentPhaseIndex);
    populatePhaseImages(s_currentRecipe, s_currentPhaseIndex);
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