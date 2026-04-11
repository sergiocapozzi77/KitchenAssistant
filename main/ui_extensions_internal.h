#pragma once

#include <vector>
#include <string>
#include "lvgl.h"
#include "esp_log.h"
#include "tjpgd.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "models.h"

// Forward declarations of internal functions
void init_styles();

// Shimmer animation functions
void stop_shimmer_animation(lv_obj_t *shimmer_bar);
void start_shimmer_animation(lv_obj_t *shimmer_bar, lv_obj_t *parent);
lv_obj_t *create_shimmer_overlay(lv_obj_t *parent);

// Thumbnail fetch/decode
struct ThumbContext
{
    lv_obj_t *thumb; // nulled under lv_lock if object deleted before task finishes
    lv_obj_t *shimmer; // shimmer overlay object, null if not present
    std::string url;
    uint32_t generation;
};

struct ThumbWorkerCtx
{
    std::vector<ThumbContext *> items;
    int maxWidth = 112;
    int maxHeight = 112;
    bool enableCache = true;
    uint32_t generation = 0;
};

struct ThumbDataCtx
{
    lv_image_dsc_t *dsc;
    uint8_t *px;
};

struct IngredientCheckboxContext
{
    lv_obj_t *label;
    lv_obj_t *line;
};

struct JpegIo
{
    const uint8_t *src;
    size_t src_len;
    size_t src_pos;
    uint8_t *dst; // RGB888 output
    uint16_t out_w;
};

bool fetch_and_decode_jpeg(const std::string &url, uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc, uint8_t **out_px,
                           bool useCache = true);
void thumb_worker_task(void *arg);

// Common event callbacks
void row_click_cb(lv_event_t *e);
void free_thumb_data_cb(lv_event_t *e);
void thumb_obj_deleted_cb(lv_event_t *e);
void ingredient_checkbox_cb(lv_event_t *e);
void free_ingredient_checkbox_ctx_cb(lv_event_t *e);

// Helper functions
int days_until_expiry(const std::string &isoDate, bool frozen);
lv_color_t get_expiry_color(int days);

// Ingredients UI helpers
void setupIngredientsContainer(lv_obj_t *container);
lv_obj_t *createIngredientRow(lv_obj_t *parent, const std::string &displayText);
void populateIngredientsUI(lv_obj_t *container, const std::vector<std::string> &displayTexts);

// Global variables (declared extern, defined in ui_extensions.cpp)
extern uint32_t s_thumb_generation;
extern lv_style_t style_card;
extern lv_style_t style_header;
extern lv_style_t style_row;
extern lv_style_t style_qty_cont;
extern lv_style_t style_qty_btn;
extern lv_style_t style_del_btn;
extern lv_style_t style_expiry_badge;
extern lv_style_t style_checkbox_indicator;
extern bool styles_initialized;