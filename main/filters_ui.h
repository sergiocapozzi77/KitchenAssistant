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

// Filter option structure
typedef struct
{
    const char *display; // Display text for the option
    const char *value;   // Value to use in API calls
} filter_option_t;

// Function to create the filter panel
/**
 * @brief Create the main filter panel with all dropdowns
 */
void create_filter_panel();

// Function to get current filter state
/**
 * @brief Get the current filter state
 * @return Pointer to the current filter state structure
 */
filter_state_t *get_filter_state(void);

#endif /* FILTERS_H */