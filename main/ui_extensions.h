#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"
#include "models.h"

enum class ProductFilter
{
    All,
    ExpiringSoon,
    Expired
};

enum class ProductSort
{
    Name,
    Expiry
};

// Call once at startup; initialises the thumbnail worker only.
void ui_extensions_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                        bool thumbEnableCache);

// ── Inventory ───────────────────────────────────────────────────────────────
void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products);
void setProductSearchFilter(const std::string &filter);
void setProductFilter(ProductFilter filter);
void setProductSort(ProductSort sort);

const char *product_category_options(); // newline-separated, for lv_dropdown
int product_category_index(const std::string &category);
std::string product_category_at(int index);

void open_product_edit_modal(const Product &product);
void close_product_edit_modal();
bool get_product_modal_values(Product &out);
bool product_modal_id(std::string &out);
void adjust_product_modal_quantity(int delta);

// ── Cookbooks and recipes (Checkpoint B) ────────────────────────────────────
void populateCookbooksUi();
void showCookbookDetail(const std::string &cookbookId);
void populateCookbookRecipesUi(const std::string &cookbookId);
void retryCookbookDetail();
void showRecipeDetailScreen(const Recipe &recipe);
// Shows the failure state when a cookbook fetch could not be started.
void showCookbookDetailFailure(const std::string &cookbookId);
// Clears the open cookbook id so an in-flight fetch result is discarded.
void leaveCookbookDetail();
// The cookbook whose detail screen is open; empty after leaving it.
std::string openCookbookId();

// ── Overlays ────────────────────────────────────────────────────────────────
void showSnackbar(const char *message, int duration_ms);
void showSpinner();
void hideSpinner();
