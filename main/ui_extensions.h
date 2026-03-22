#pragma once

#include <vector>
#include <string>
#include "models.h"
#include "lvgl.h"

void populateProductList(lv_obj_t *root, const std::vector<Product> &products);
void populateRecipeList(lv_obj_t *root, const std::vector<RecipeSuggestion> &recipes);