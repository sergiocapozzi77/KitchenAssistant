#include "filters_ui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "styles.h"
#include "esp_log.h"
#include "ui.h"
#include <vector>
#include <string>
#include <sstream>

static const char *TAG = "Filters";

// Filter state
static filter_state_t current_filters = {
    .meal_type = NULL,
    .total_time = NULL,
    .diet = NULL,
    .difficulty = NULL,
    .cuisine = NULL,
    .calories = NULL};

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

// LVGL dropdown objects
static lv_obj_t *meal_type_dropdown = NULL;
static lv_obj_t *total_time_dropdown = NULL;
static lv_obj_t *diet_dropdown = NULL;
static lv_obj_t *difficulty_dropdown = NULL;
static lv_obj_t *cuisine_dropdown = NULL;
static lv_obj_t *calories_dropdown = NULL;
static lv_obj_t *keywords_textarea = NULL;

// Helper function to create dropdown option string
static char *create_options_string(filter_option_t options[], int count, const char *label)
{
    static char buffer[1024]; // Static buffer for simplicity
    buffer[0] = '\0';

    // Start with "None" option
    strcat(buffer, label);

    // Add all other options
    for (int i = 0; i < count; i++)
    {
        strcat(buffer, "\n");
        strcat(buffer, options[i].display);
    }

    return buffer;
}

static void update_keywords_from_textarea()
{
    if (!keywords_textarea)
        return;
    const char *text = lv_textarea_get_text(keywords_textarea);
    if (!text)
        return;
    std::string input(text);
    current_filters.keywords.clear();
    std::string token;
    for (size_t i = 0; i < input.length(); ++i)
    {
        char c = input[i];
        if (c == ',' || c == ' ' || c == '\t' || c == '\n')
        {
            if (!token.empty())
            {
                current_filters.keywords.push_back(token);
                token.clear();
            }
        }
        else
        {
            token += c;
        }
    }
    if (!token.empty())
    {
        current_filters.keywords.push_back(token);
    }
}

static void keywords_textarea_event_handler(lv_event_t *e)
{
    update_keywords_from_textarea();
}

// Dropdown event handler
static void dropdown_event_handler(lv_event_t *e)
{
    lv_obj_t *dropdown = (lv_obj_t *)lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(dropdown);

    // Get the options array based on which dropdown
    filter_option_t *options = NULL;
    int count = 0;
    char **target_field = NULL;

    if (dropdown == meal_type_dropdown)
    {
        options = meal_type_options;
        count = meal_type_count;
        target_field = &current_filters.meal_type;
    }
    else if (dropdown == total_time_dropdown)
    {
        options = total_time_options;
        count = total_time_count;
        target_field = &current_filters.total_time;
    }
    else if (dropdown == diet_dropdown)
    {
        options = diet_options;
        count = diet_count;
        target_field = &current_filters.diet;
    }
    else if (dropdown == difficulty_dropdown)
    {
        options = difficulty_options;
        count = difficulty_count;
        target_field = &current_filters.difficulty;
    }
    else if (dropdown == cuisine_dropdown)
    {
        options = cuisine_options;
        count = cuisine_count;
        target_field = &current_filters.cuisine;
    }
    else if (dropdown == calories_dropdown)
    {
        options = calories_options;
        count = calories_count;
        target_field = &current_filters.calories;
    }

    if (options && target_field)
    {
        if (selected == 0 || selected > count)
        {
            *target_field = NULL;
        }
        else
        {
            *target_field = (char *)options[selected - 1].value;
        }
    }
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

// Create the main filter panel
void create_filter_panel()
{
    // Get dropdown objects from the objects array
    meal_type_dropdown = objects.products_filters_panel__meal_type_dropdown;
    total_time_dropdown = objects.products_filters_panel__total_time_dropdown;
    diet_dropdown = objects.products_filters_panel__diet_dropdown;
    difficulty_dropdown = objects.products_filters_panel__difficulty_dropdown;
    cuisine_dropdown = objects.products_filters_panel__cuisine_dropdown;
    calories_dropdown = objects.products_filters_panel__calories_dropdown;
    keywords_textarea = objects.products_filters_panel__keywords_text;
    if (keywords_textarea)
        update_keywords_from_textarea();

    // Verify dropdowns were created
    if (!meal_type_dropdown || !total_time_dropdown || !diet_dropdown ||
        !difficulty_dropdown || !cuisine_dropdown || !calories_dropdown)
    {
        ESP_LOGE(TAG, "Failed to get dropdown objects from user widget");
        return;
    }

    // Set default selection to none (first option)
    lv_dropdown_set_selected(meal_type_dropdown, 0);
    lv_dropdown_set_selected(total_time_dropdown, 0);
    lv_dropdown_set_selected(diet_dropdown, 0);
    lv_dropdown_set_selected(difficulty_dropdown, 0);
    lv_dropdown_set_selected(cuisine_dropdown, 0);
    lv_dropdown_set_selected(calories_dropdown, 0);

    // Add event handlers
    lv_obj_add_event_cb(meal_type_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(total_time_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(diet_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(difficulty_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(cuisine_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(calories_dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    if (keywords_textarea)
    {
        lv_obj_add_event_cb(keywords_textarea, keywords_textarea_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

// Public function to get current filter state
filter_state_t *get_filter_state(void)
{
    return &current_filters;
}
