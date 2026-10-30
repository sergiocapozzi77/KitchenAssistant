#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"

// Queue a direct HTTP thumbnail for an already-sized lv_image object.
// Returns false if thumbnails are disabled or the request is invalid.
// The caller must hold the LVGL lock.
bool push_thumbnail(lv_obj_t *image, const std::string &url, uint16_t width,
                    uint16_t height, bool cacheAllowed);
