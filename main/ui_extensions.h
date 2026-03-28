#pragma once

#include <vector>
#include <string>
#include "models.h"
#include "lvgl.h"

void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products);
void populateRecipeList(lv_obj_t *root, const std::vector<RecipeSuggestion> &recipes);
void showRecipeDetailScreen(const RecipeSuggestion &recipe);
void showSnackbar(const char *message, int duration_ms);
extern uint32_t s_thumb_generation;
void showSpinner();
void hideSpinner();
void close_product_edit_modal();