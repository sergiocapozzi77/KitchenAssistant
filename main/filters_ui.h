#ifndef FILTERS_H
#define FILTERS_H

#include "lvgl.h"
#include <vector>
#include <string>

void log_filter_state(void);
// Filter structure to store selected values
typedef struct
{
    char *meal_type;  // Meal type (single-select)
    char *total_time; // Total time (single-select)
    char *diet;       // Diet (single-select)
    char *difficulty; // Difficulty (single-select)
    char *cuisine;    // Cuisine (single-select)
    char *calories;   // Calories (single-select)
    char *source;     // Recipe source (single-select)
    std::vector<std::string> keywords;
} filter_state_t;

typedef struct
{
    lv_obj_t *meal_type_dropdown;
    lv_obj_t *total_time_dropdown;
    lv_obj_t *diet_dropdown;
    lv_obj_t *difficulty_dropdown;
    lv_obj_t *cuisine_dropdown;
    lv_obj_t *calories_dropdown;
    lv_obj_t *source_dropdown;
    lv_obj_t *keywords_textarea;
    lv_obj_t *products_selected_cb;
} filter_panel_t;

// Filter option structure

// Function to create the filter panel
/**
 * @brief Create the main filter panel with all dropdowns
 */
void create_filter_panel(filter_panel_t *panel);

void init_products_filter_panel(filter_panel_t *panel);
void init_recipes_filter_panel(filter_panel_t *panel);

// Function to get current filter state
/**
 * @brief Get the current filter state
 * @return Pointer to the current filter state structure
 */
filter_state_t *get_filter_state(void);

#endif /* FILTERS_H */