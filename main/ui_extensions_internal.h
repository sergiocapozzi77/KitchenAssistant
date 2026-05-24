#pragma once

#include <vector>
#include <string>
#include "lvgl.h"
#include "models.h"
#include "thumbnail_manager.h"

// Forward declarations of internal functions
void init_styles();

struct IngredientCheckboxContext
{
    lv_obj_t *label;
    lv_obj_t *line;
};

// Common event callbacks
void row_click_cb(lv_event_t *e);
void ingredient_checkbox_cb(lv_event_t *e);
void free_ingredient_checkbox_ctx_cb(lv_event_t *e);

// Helper functions
int days_until_expiry(const std::string &isoDate, bool frozen);
lv_color_t get_expiry_color(int days);

// Ingredients UI helpers
void setupIngredientsContainer(lv_obj_t *container);
lv_obj_t *createIngredientRow(lv_obj_t *parent, const std::string &displayText);
void populateIngredientsUI(lv_obj_t *container, const std::vector<std::string> &displayTexts);

// Recipe card helpers
void make_children_bubble(lv_obj_t *obj);
lv_obj_t *createRecipeCard(lv_obj_t *parent, const RecipeSuggestion &recipe);
lv_obj_t *createRecipeCard(lv_obj_t *parent, const Favorite &fav);

// Global variables (declared extern, defined in ui_extensions.cpp)
extern lv_style_t style_card;
extern lv_style_t style_header;
extern lv_style_t style_row;
extern lv_style_t style_qty_cont;
extern lv_style_t style_qty_btn;
extern lv_style_t style_del_btn;
extern lv_style_t style_expiry_badge;
extern lv_style_t style_checkbox_indicator;
extern bool styles_initialized;

// Cookbook navigation helpers
void cleanupCookbookDrill();
void ensure_back_button();

// Cookbook navigation state
enum class FavouritesViewMode {
    ALL_FAVOURITES,
    COOKBOOK_LIST,
    COOKBOOK_DRILL
};
extern FavouritesViewMode g_favouritesViewMode;
extern std::string g_activeCookbookId;
extern std::string g_activeCookbookName;
