#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include <string>
#include "models.h"
#include "lvgl.h"
#include "thumbnail_manager.h"

// ── Lifecycle ────────────────────────────────────────────────────────────────
// Call once at startup before any screen that shows thumbnails
void ui_extensions_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                         bool thumbEnableCache);

// ── Product list ─────────────────────────────────────────────────────────────
void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products);
void setProductSearchFilter(const std::string &filter);
void close_product_edit_modal();

// ── Recipe / Favourites ──────────────────────────────────────────────────────
void populateRecipeList(lv_obj_t *root, const std::vector<RecipeSuggestion> &recipes);
void populateFavouritesList(lv_obj_t *root, const std::vector<Favorite> &favourites);
void showCurrentPageFavourites(bool force = false);
void populateCookbooksList(lv_obj_t *root, const std::vector<Cookbook> &cookbooks);
void showCurrentPageCookbooks(bool force = false);
void showRecipeDetailScreen(const RecipeSuggestion &recipe);

// ── UI helpers ───────────────────────────────────────────────────────────────
void showSnackbar(const char *message, int duration_ms);
void showSpinner();
void hideSpinner();
