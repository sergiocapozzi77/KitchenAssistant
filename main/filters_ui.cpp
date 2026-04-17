#include "filters_ui.h"
#include <string.h>
#include "esp_log.h"
#include <vector>
#include <string>
#include "ui.h"

static const char *TAG = "Filters";

// ===================== STATE =====================

static filter_state_t current_filters = {
    .meal_type = NULL,
    .total_time = NULL,
    .diet = NULL,
    .difficulty = NULL,
    .cuisine = NULL,
    .calories = NULL,
    .source = NULL};

// ===================== OPTIONS =====================

typedef struct
{
    const char *display;
    const char *value;
} filter_option_t;

// Define all filter options
static filter_option_t meal_type_options[] = {
    {"Afternoon tea", "afternoon-tea"},
    {"Breakfast", "breakfast"},
    {"Brunch", "brunch"},
    {"Buffet", "buffet"},
    {"Canapes", "canapes"},
    {"Condiment", "condiment"},
    {"Dessert", "dessert"},
    {"Dinner", "dinner"},
    {"Fish Course", "fish-course"},
    {"Lunch", "lunch"},
    {"Main course", "main-course"},
    {"Pasta", "pasta"},
    {"Side dish", "side-dish"},
    {"Snack", "snack"},
    {"Soup", "soup"},
    {"Starter", "starter"},
    {"Supper", "supper"},
    {"Treat", "treat"},
    {"Vegetable", "vegetable"}};
static const int meal_type_count = sizeof(meal_type_options) / sizeof(meal_type_options[0]);

// Total time options
static filter_option_t total_time_options[] = {
    {"Under 15 minutes", "lt-900"},
    {"Under 30 minutes", "lt-1800"},
    {"Under 45 minutes", "lt-2700"},
    {"Under 1 hour", "lt-3600"},
    {"1 hour or more", "gte-3600"}};
static const int total_time_count = sizeof(total_time_options) / sizeof(total_time_options[0]);

// Diet options
static filter_option_t diet_options[] = {
    {"Healthy", "healthy"},
    {"Gluten-free", "gluten-free"},
    {"Vegetarian", "vegetarian"},
    {"Egg-free", "egg-free"},
    {"Nut-free", "nut-free"},
    {"Dairy-free", "dairy-free"},
    {"High-protein", "high-protein"},
    {"Vegan", "vegan"},
    {"Low sugar", "low-sugar"},
    {"High-fibre", "high-fibre"},
    {"Low calorie", "low-calorie"},
    {"Keto", "keto"},
    {"Low fat", "low-fat"},
    {"Low carb", "low-carb"}};
static const int diet_count = sizeof(diet_options) / sizeof(diet_options[0]);

// Difficulty options
static filter_option_t difficulty_options[] = {
    {"Easy", "easy"},
    {"More effort", "more-effort"},
    {"A challenge", "a-challenge"}};
static const int difficulty_count = sizeof(difficulty_options) / sizeof(difficulty_options[0]);

// Cuisine options
static filter_option_t cuisine_options[] = {
    {"African", "african"},
    {"American", "american"},
    {"Asian", "asian"},
    {"Australian", "australian"},
    {"Azerbaijan", "azerbaijan"},
    {"Brazilian", "brazilian"},
    {"British", "british"},
    {"Cajun & Creole", "cajun-creole"},
    {"Caribbean", "caribbean"},
    {"Chinese", "chinese"},
    {"Eastern European", "eastern-european"},
    {"Egyptian", "egyptian"},
    {"English", "english"},
    {"French", "french"},
    {"German", "german"},
    {"Greek", "greek"},
    {"Indian", "indian"},
    {"Indonesian", "indonesian"},
    {"Irish", "irish"},
    {"Italian", "italian"},
    {"Japanese", "japanese"},
    {"Korean", "korean"},
    {"Latin American", "latin-american"},
    {"Mediterranean", "mediterranean"},
    {"Mexican", "mexican"},
    {"Middle Eastern", "middle-eastern"},
    {"Moroccan", "moroccan"},
    {"North African", "north-african"},
    {"Portuguese", "portuguese"},
    {"Scandinavian", "scandinavian"},
    {"Scottish", "scottish"},
    {"Southern & Soul", "southern-soul"},
    {"Spanish", "spanish"},
    {"Swedish", "swedish"},
    {"Thai", "thai"},
    {"Tunisian", "tunisian"},
    {"Turkish", "turkish"},
    {"Vietnamese", "vietnamese"}};
static const int cuisine_count = sizeof(cuisine_options) / sizeof(cuisine_options[0]);

// Calories options
static filter_option_t calories_options[] = {
    {"Up to 250 kcal", "lt-250"},
    {"Up to 500 kcal", "lt-500"},
    {"Up to 750 kcal", "lt-750"},
    {"Up to 1000 kcal", "lt-1000"},
    {"Up to 1250 kcal", "lt-1250"},
    {"Up to 1500 kcal", "lt-1500"}};
static const int calories_count = sizeof(calories_options) / sizeof(calories_options[0]);

// Source options
static filter_option_t source_options[] = {
    {"BBC Good Food", "bbcgoodfood"},
    {"GialloZafferano.it", "giallozafferanoit"},
    {"AniaGotuje.pl", "aniagotuje"},
    {"AllRecipes", "allrecipes"},
    // {"Food52", "food52"},
    // {"Serious Eats", "seriouseats"},
    // {"Smitten Kitchen", "smittenkitchen"},
    // {"Epicurious", "epicurious"},
    // {"Taste of Home", "tasteofhome"},
    // {"Food Network", "foodnetwork"},
    // {"Delish", "delish"},
    // {"Simply Recipes", "simplyrecipes"},
    // {"The Spruce Eats", "thespruceeats"},
    // {"EatingWell", "eatingwell"},
    // {"Bon Appétit", "bonappetit"}
};
static const int source_count = sizeof(source_options) / sizeof(source_options[0]);

// LVGL dropdown objects

// ===================== PANEL =====================

static std::vector<filter_panel_t *> panels;
static bool is_syncing = false;

// ===================== HELPERS =====================

static int find_index(const char *value, filter_option_t options[], int count)
{
    if (!value)
        return 0;
    for (int i = 0; i < count; i++)
    {
        if (strcmp(value, options[i].value) == 0)
            return i + 1;
    }
    return 0;
}

static void sync_panel(filter_panel_t *panel)
{
    if (is_syncing)
        return;
    is_syncing = true;

    lv_dropdown_set_selected(panel->meal_type_dropdown,
                             find_index(current_filters.meal_type, meal_type_options, meal_type_count));

    lv_dropdown_set_selected(panel->total_time_dropdown,
                             find_index(current_filters.total_time, total_time_options, total_time_count));

    lv_dropdown_set_selected(panel->diet_dropdown,
                             find_index(current_filters.diet, diet_options, diet_count));

    lv_dropdown_set_selected(panel->difficulty_dropdown,
                             find_index(current_filters.difficulty, difficulty_options, difficulty_count));

    lv_dropdown_set_selected(panel->cuisine_dropdown,
                             find_index(current_filters.cuisine, cuisine_options, cuisine_count));

    lv_dropdown_set_selected(panel->calories_dropdown,
                             find_index(current_filters.calories, calories_options, calories_count));

    int idx = find_index(current_filters.source, source_options, source_count);
    lv_dropdown_set_selected(panel->source_dropdown, idx >= 0 ? idx : 1);

    is_syncing = false;
}

static void sync_all_panels(filter_panel_t *origin_panel)
{
    for (auto panel : panels)
    {
        if (panel == origin_panel)
            continue; // Skip the one the user just touched
        sync_panel(panel);
    }
}

static void update_keywords(filter_panel_t *panel)
{
    if (!panel->keywords_textarea)
        return;

    const char *text = lv_textarea_get_text(panel->keywords_textarea);
    if (!text)
        return;

    current_filters.keywords.clear();

    std::string input(text);
    std::string token;

    for (char c : input)
    {
        if (c == ',' || c == ' ' || c == '\n')
        {
            if (!token.empty())
            {
                current_filters.keywords.push_back(token);
                token.clear();
            }
        }
        else
            token += c;
    }

    if (!token.empty())
        current_filters.keywords.push_back(token);
}

// ===================== EVENTS =====================

static void dropdown_event_handler(lv_event_t *e)
{
    if (is_syncing)
        return;

    filter_panel_t *panel = (filter_panel_t *)lv_event_get_user_data(e);
    lv_obj_t *dropdown = (lv_obj_t *)lv_event_get_target(e);

    uint16_t selected = lv_dropdown_get_selected(dropdown);

    filter_option_t *options = NULL;
    int count = 0;
    char **target = NULL;

    if (dropdown == panel->meal_type_dropdown)
    {
        options = meal_type_options;
        count = meal_type_count;
        target = &current_filters.meal_type;
    }
    else if (dropdown == panel->total_time_dropdown)
    {
        options = total_time_options;
        count = total_time_count;
        target = &current_filters.total_time;
    }
    else if (dropdown == panel->diet_dropdown)
    {
        options = diet_options;
        count = diet_count;
        target = &current_filters.diet;
    }
    else if (dropdown == panel->difficulty_dropdown)
    {
        options = difficulty_options;
        count = difficulty_count;
        target = &current_filters.difficulty;
    }
    else if (dropdown == panel->cuisine_dropdown)
    {
        options = cuisine_options;
        count = cuisine_count;
        target = &current_filters.cuisine;
    }
    else if (dropdown == panel->calories_dropdown)
    {
        options = calories_options;
        count = calories_count;
        target = &current_filters.calories;
    }
    else if (dropdown == panel->source_dropdown)
    {
        options = source_options;
        count = source_count;
        target = &current_filters.source;
    }

    if (target)
    {
        if (selected == 0 || selected > count)
            *target = NULL;
        else
            *target = (char *)options[selected - 1].value;
    }

    sync_all_panels(panel);
}

static void textarea_event_handler(lv_event_t *e)
{
    if (is_syncing)
        return;

    is_syncing = true;

    filter_panel_t *origin_panel = (filter_panel_t *)lv_event_get_user_data(e);
    const char *text = lv_textarea_get_text(origin_panel->keywords_textarea);

    // Sync textarea content to all other panels
    for (auto panel : panels)
    {
        if (panel == origin_panel || !panel->keywords_textarea)
            continue;

        lv_textarea_set_text(panel->keywords_textarea, text);
    }

    update_keywords(origin_panel);
    is_syncing = false;
}

static void checkbox_event_handler(lv_event_t *e)
{
    if (is_syncing)
        return;

    is_syncing = true;

    filter_panel_t *origin_panel = (filter_panel_t *)lv_event_get_user_data(e);
    bool is_checked = lv_obj_has_state(origin_panel->products_selected_cb, LV_STATE_CHECKED);

    // Sync checkbox state to all other panels
    for (auto panel : panels)
    {
        if (panel == origin_panel || !panel->products_selected_cb)
            continue;

        if (is_checked)
            lv_obj_add_state(panel->products_selected_cb, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(panel->products_selected_cb, LV_STATE_CHECKED);
    }

    is_syncing = false;
}

// ===================== PUBLIC =====================

void create_filter_panel(filter_panel_t *panel)
{
    panels.push_back(panel);

    lv_obj_add_event_cb(panel->meal_type_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    lv_obj_add_event_cb(panel->total_time_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    lv_obj_add_event_cb(panel->diet_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    lv_obj_add_event_cb(panel->difficulty_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    lv_obj_add_event_cb(panel->cuisine_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    lv_obj_add_event_cb(panel->calories_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    lv_obj_add_event_cb(panel->source_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, panel);

    if (panel->keywords_textarea)
    {
        lv_obj_add_event_cb(panel->keywords_textarea, textarea_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    }

    if (panel->products_selected_cb)
    {
        lv_obj_add_event_cb(panel->products_selected_cb, checkbox_event_handler, LV_EVENT_VALUE_CHANGED, panel);
    }

    sync_panel(panel);
}

// Log initialization (can be called from init or after creation)
void log_filter_state(void)
{
    ESP_LOGI(TAG, "Filter state:");
    ESP_LOGI(TAG, "  meal_type: %s", current_filters.meal_type ? current_filters.meal_type : "NULL");
    ESP_LOGI(TAG, "  total_time: %s", current_filters.total_time ? current_filters.total_time : "NULL");
    ESP_LOGI(TAG, "  diet: %s", current_filters.diet ? current_filters.diet : "NULL");
    ESP_LOGI(TAG, "  difficulty: %s", current_filters.difficulty ? current_filters.difficulty : "NULL");
    ESP_LOGI(TAG, "  cuisine: %s", current_filters.cuisine ? current_filters.cuisine : "NULL");
    ESP_LOGI(TAG, "  calories: %s", current_filters.calories ? current_filters.calories : "NULL");
    ESP_LOGI(TAG, "  keywords: %d", current_filters.keywords.size());
    for (size_t i = 0; i < current_filters.keywords.size(); ++i)
    {
        ESP_LOGI(TAG, "    - %s", current_filters.keywords[i].c_str());
    }
}

filter_state_t *get_filter_state(void)
{
    return &current_filters;
}

void init_products_filter_panel(filter_panel_t *panel)
{
    panel->meal_type_dropdown = objects.products_filters_panel__meal_type_dropdown;
    panel->total_time_dropdown = objects.products_filters_panel__total_time_dropdown;
    panel->diet_dropdown = objects.products_filters_panel__diet_dropdown;
    panel->difficulty_dropdown = objects.products_filters_panel__difficulty_dropdown;
    panel->cuisine_dropdown = objects.products_filters_panel__cuisine_dropdown;
    panel->calories_dropdown = objects.products_filters_panel__calories_dropdown;
    panel->source_dropdown = objects.products_filters_panel__source_dropdown;
    panel->keywords_textarea = objects.products_filters_panel__keywords_text;
    panel->products_selected_cb = objects.products_filters_panel__poducts_selected_cb;
}

void init_recipes_filter_panel(filter_panel_t *panel)
{
    panel->meal_type_dropdown = objects.recipes_filters_panel__meal_type_dropdown;
    panel->total_time_dropdown = objects.recipes_filters_panel__total_time_dropdown;
    panel->diet_dropdown = objects.recipes_filters_panel__diet_dropdown;
    panel->difficulty_dropdown = objects.recipes_filters_panel__difficulty_dropdown;
    panel->cuisine_dropdown = objects.recipes_filters_panel__cuisine_dropdown;
    panel->calories_dropdown = objects.recipes_filters_panel__calories_dropdown;
    panel->source_dropdown = objects.recipes_filters_panel__source_dropdown;
    panel->keywords_textarea = objects.recipes_filters_panel__keywords_text;
    panel->products_selected_cb = objects.recipes_filters_panel__poducts_selected_cb;
}