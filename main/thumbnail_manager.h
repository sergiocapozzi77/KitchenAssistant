#pragma once

#include <vector>
#include <string>
#include <atomic>
#include "lvgl.h"
#include "esp_err.h"

// ── Structs ─────────────────────────────────────────────────────────────────

struct ThumbContext
{
    lv_obj_t *thumb;
    lv_obj_t *shimmer;
    std::string url;
    uint32_t generation;
    std::atomic<bool> cancelled{false};
    uint16_t maxW = 0; // 0 = use global default set at thumbnail_manager_init
    uint16_t maxH = 0;
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

// ── Thumbnail manager lifecycle ─────────────────────────────────────────────
// Initialises the thumbnail queue, HTTP semaphore, Leonardo cache, and worker task
void thumbnail_manager_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                             bool thumbEnableCache);

// Cancel all pending thumbnails (call when navigating away from a list screen)
void thumb_queue_cancel_all();

// Push a thumbnail context to the worker queue (passes ownership)
void thumb_queue_push(ThumbContext *ctx);

// ── JPEG decode ─────────────────────────────────────────────────────────────
bool fetch_and_decode_jpeg(const std::string &url, uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc, uint8_t **out_px,
                           bool useCache = true);

// ── Leonardo URL cache ──────────────────────────────────────────────────────
// Returns cached Leonardo-generated image URL for a given recipe URL + dimensions,
// or empty string if not found.
std::string get_leonardo_cached_url(const std::string &url, uint16_t w, uint16_t h);

// ── Shimmer ─────────────────────────────────────────────────────────────────
lv_obj_t *create_shimmer_overlay(lv_obj_t *parent);
void start_shimmer_animation(lv_obj_t *shimmer_bar, lv_obj_t *parent);
void stop_shimmer_animation(lv_obj_t *shimmer_bar);
void stop_and_delete_shimmer(lv_obj_t *shimmer);

// Stop ALL shimmer animations globally (safe to call during screen transitions)
void stop_all_shimmer_animations();

// ── Event callbacks (for use with lv_event_t) ───────────────────────────────
void thumb_obj_deleted_cb(lv_event_t *e);
void free_thumb_data_cb(lv_event_t *e);

// ── Worker task ─────────────────────────────────────────────────────────────
void thumb_worker_task(void *arg);

// ── Default thumbnail size (set at init, read by card builders) ──────────────
extern uint16_t s_thumb_max_w;
extern uint16_t s_thumb_max_h;

// ── Global generation counter ───────────────────────────────────────────────
extern std::atomic<uint32_t> s_thumb_generation;
