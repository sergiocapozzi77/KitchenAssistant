#include "filters_ui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "styles.h"
#include "esp_log.h"

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

// Create a single dropdown
static lv_obj_t *create_dropdown(lv_obj_t *parent, const char *label, filter_option_t options[], int count)
{
    // Create dropdown
    lv_obj_t *dropdown = lv_dropdown_create(parent);
    lv_obj_set_size(dropdown, 220, LV_SIZE_CONTENT);
    lv_dropdown_set_dir(dropdown, LV_DIR_TOP);
    lv_dropdown_set_symbol(dropdown, LV_SYMBOL_UP);
    add_style_drop_down_with_shadow(dropdown);

    // Set options
    char *options_str = create_options_string(options, count, label);
    lv_dropdown_set_options(dropdown, options_str);

    // Set default selection to none
    lv_dropdown_set_selected(dropdown, 0);

    // Add event handler
    lv_obj_add_event_cb(dropdown, dropdown_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    return dropdown;
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
}

// Create the main filter panel
void create_filter_panel(lv_obj_t *parent)
{
    // Configure parent as grid: 3 columns, 2 rows (already defined in screens.c, but we redefine for consistency)
    lv_obj_set_style_layout(parent, LV_LAYOUT_GRID, 0);

    // Define grid columns: 3 equal fractional units
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_style_grid_column_dsc_array(parent, col_dsc, 0);

    // Define grid rows: 2 equal fractional units (matching screens.c)
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_style_grid_row_dsc_array(parent, row_dsc, 0);

    // Set padding
    lv_obj_set_style_pad_all(parent, 10, 0);

    // Create dropdowns and place them in grid cells
    // Row 0
    meal_type_dropdown = create_dropdown(parent, "Meal Type", meal_type_options, meal_type_count);
    lv_obj_set_style_grid_cell_column_pos(meal_type_dropdown, 0, 0);
    lv_obj_set_style_grid_cell_row_pos(meal_type_dropdown, 0, 0);
    lv_obj_set_style_grid_cell_y_align(meal_type_dropdown, LV_GRID_ALIGN_STRETCH, 0);

    total_time_dropdown = create_dropdown(parent, "Total Time", total_time_options, total_time_count);
    lv_obj_set_style_grid_cell_column_pos(total_time_dropdown, 1, 0);
    lv_obj_set_style_grid_cell_row_pos(total_time_dropdown, 0, 0);
    lv_obj_set_style_grid_cell_y_align(total_time_dropdown, LV_GRID_ALIGN_STRETCH, 0);

    diet_dropdown = create_dropdown(parent, "Diets", diet_options, diet_count);
    lv_obj_set_style_grid_cell_column_pos(diet_dropdown, 2, 0);
    lv_obj_set_style_grid_cell_row_pos(diet_dropdown, 0, 0);
    lv_obj_set_style_grid_cell_y_align(diet_dropdown, LV_GRID_ALIGN_STRETCH, 0);

    // Row 1
    difficulty_dropdown = create_dropdown(parent, "Difficulty", difficulty_options, difficulty_count);
    lv_obj_set_style_grid_cell_column_pos(difficulty_dropdown, 0, 0);
    lv_obj_set_style_grid_cell_row_pos(difficulty_dropdown, 1, 0);
    lv_obj_set_style_grid_cell_y_align(difficulty_dropdown, LV_GRID_ALIGN_STRETCH, 0);

    cuisine_dropdown = create_dropdown(parent, "Cuisine", cuisine_options, cuisine_count);
    lv_obj_set_style_grid_cell_column_pos(cuisine_dropdown, 1, 0);
    lv_obj_set_style_grid_cell_row_pos(cuisine_dropdown, 1, 0);
    lv_obj_set_style_grid_cell_y_align(cuisine_dropdown, LV_GRID_ALIGN_STRETCH, 0);

    calories_dropdown = create_dropdown(parent, "Calories", calories_options, calories_count);
    lv_obj_set_style_grid_cell_column_pos(calories_dropdown, 2, 0);
    lv_obj_set_style_grid_cell_row_pos(calories_dropdown, 1, 0);
    lv_obj_set_style_grid_cell_y_align(calories_dropdown, LV_GRID_ALIGN_STRETCH, 0);
}

// Public function to get current filter state
filter_state_t *get_filter_state(void)
{
    return &current_filters;
}
