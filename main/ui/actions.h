#pragma once

#include <string>

#include "lvgl.h"

void action_products_reload(lv_event_t *e);
void action_product_search_open(lv_event_t *e);
void action_product_search_changed(lv_event_t *e);
void action_product_filter_toggle(lv_event_t *e);
void action_product_sort_toggle(lv_event_t *e);
void action_product_filter_chip(lv_event_t *e);
void action_product_sort_chip(lv_event_t *e);
void action_product_edit_save(lv_event_t *e);
void action_product_edit_cancel(lv_event_t *e);
void action_product_edit_delete(lv_event_t *e);
void action_product_qty_minus(lv_event_t *e);
void action_product_qty_plus(lv_event_t *e);
void action_settings_wifi(lv_event_t *e);

// Cookbooks and recipes
void action_cookbooks_reload(lv_event_t *e);
void action_cookbook_open(lv_event_t *e);
void action_cookbook_back(lv_event_t *e);
void action_cookbook_retry(lv_event_t *e);
void action_recipe_open(lv_event_t *e);
void action_recipe_back(lv_event_t *e);

// Loads the cookbook grid on first use; force clears every recipe cache.
void request_cookbooks_load();
void request_cookbook_recipes(const std::string &cookbookId);

// Spawns a background delete and refreshes the list on success.
void request_product_delete(const std::string &id);
