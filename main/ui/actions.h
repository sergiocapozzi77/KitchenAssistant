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

// Spawns a background delete and refreshes the list on success.
void request_product_delete(const std::string &id);
