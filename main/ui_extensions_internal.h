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

// Thumbnail fetch/decode
struct ThumbContext
{
    lv_obj_t *thumb; // nulled under lv_lock if object deleted before task finishes
    std::string url;
    uint32_t generation;
};

struct ThumbWorkerCtx
{
    std::vector<ThumbContext *> items;
};

struct ThumbDataCtx
{
    lv_image_dsc_t *dsc;
    uint8_t *px;
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
                           lv_image_dsc_t **out_dsc, uint8_t **out_px);
void thumb_worker_task(void *arg);

// Common event callbacks
void row_click_cb(lv_event_t *e);
void free_thumb_data_cb(lv_event_t *e);
void thumb_obj_deleted_cb(lv_event_t *e);

// Helper functions
int days_until_expiry(const std::string &isoDate);
lv_color_t get_expiry_color(int days);

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