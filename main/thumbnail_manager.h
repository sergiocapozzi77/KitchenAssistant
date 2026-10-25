#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "lvgl.h"

// ── Structs ─────────────────────────────────────────────────────────────────

struct ThumbContext
{
    lv_obj_t *thumb = nullptr;
    std::string url;
    uint32_t generation = 0;
    std::atomic<bool> cancelled{false};
    uint16_t maxW = 0; // 0 = use global default set at thumbnail_manager_init
    uint16_t maxH = 0;
    bool cacheAllowed = true;
};

struct ThumbDataCtx
{
    lv_image_dsc_t *dsc = nullptr;
    uint8_t *px = nullptr;
    bool freed = false; // sentinel — set true before freeing, checked to detect double-free
};

// ── Thumbnail manager lifecycle ─────────────────────────────────────────────
// Initialises the queue, the SPIFFS thumbnail cache and the worker task.
// Must run after SPIFFS is mounted.
void thumbnail_manager_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                            bool thumbEnableCache);

// Cancel all pending thumbnails (call when navigating away from a list screen)
void thumb_queue_cancel_all();

// Push a thumbnail context to the worker queue (passes ownership)
void thumb_queue_push(ThumbContext *ctx);

// ── Image download and decode ───────────────────────────────────────────────
// Rewrites Supabase Storage public-object URLs to a sized render endpoint, then
// passes every source through the wsrv.nl image proxy so the result is a
// baseline JPEG (which tjpgd can decode). Allrecipes URLs go through
// DuckDuckGo's image proxy first because Allrecipes returns 403 to wsrv.
std::string thumbnail_url(const std::string &url, uint16_t width,
                          uint16_t height);

// Downloads the URL directly (2 MB guard), decodes with tjpgd at a scale that
// still covers W×H, and returns a variable image descriptor owned by the caller.
bool fetch_and_decode_jpeg(const std::string &url, uint16_t W, uint16_t H,
                           lv_image_dsc_t **out_dsc, uint8_t **out_px,
                           bool useCache = true);

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
