#include "actions.h"
#include "lvgl.h"
#include "ui.h"
#include "styles.h"
#include "esp_log.h"
#include "vars.h"
#include "images.h"
#include "RecipeGoodFoodService.h"

void set_tab_icon(lv_obj_t *tabview, uint32_t index, const void *img_src)
{
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_t *btn = lv_obj_get_child(tab_btns, index);

    if (!btn)
        return;

    // Remove existing children (label)
    uint32_t child_cnt = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < child_cnt; i++)
    {
        lv_obj_del(lv_obj_get_child(btn, 0));
    }

    // Add image
    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, img_src);
    lv_obj_center(img);
}

void action_screen_loading(lv_event_t *e)
{
    // This function can be used to perform actions when the loading screen is shown
    ESP_LOGI("actions", "Loading screen shown");
    set_tab_icon(objects.tabview, 0, &img_shopping);
    set_tab_icon(objects.tabview, 1, &img_chef);

    lv_obj_add_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
}

void fetchRecipesTask(void *param);

void action_generate_recipe_click(lv_event_t *e)
{
    ESP_LOGI("actions", "Generate Recipe button clicked");

    // Somewhere in initTasks() or after WiFi connects:
    xTaskCreate(fetchRecipesTask, "FetchRecipes", 16384, nullptr, 5, nullptr);
}

void fetchRecipesTask(void *param)
{
    std::vector<std::string> ingredients = {
        "chicken",
        "lemon",
        "garlic"};

    std::vector<std::string> keywords = {
        "healthy",
        "quick"};

    auto suggestions = recipeGoodFoodService.getRecipeSuggestions(
        ingredients,
        "main-course", // dishType
        keywords,
        "easy",      // difficulty (pass "" to skip the filter)
        "30-minutes" // totalTime  (pass "" to skip the filter)
    );

    ESP_LOGI("App", "Got %d recipe suggestions", suggestions.size());

    for (const auto &r : suggestions)
    {
        ESP_LOGI("App",
                 "  [%s] %s (%s) -> %s",
                 r.difficulty.c_str(),
                 r.name.c_str(),
                 r.totalTime.c_str(),
                 r.url.c_str());
    }

    // TODO: pass results to UI, e.g.:
    // populateRecipeList(objects.recipe_list, suggestions);

    vTaskDelete(nullptr);
}
