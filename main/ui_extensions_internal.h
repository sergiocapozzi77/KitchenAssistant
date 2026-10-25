#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"

// Queue a direct HTTP thumbnail for an already-sized lv_image object.
// The caller must hold the LVGL lock.
void push_thumbnail(lv_obj_t *image, const std::string &url, uint16_t width,
                    uint16_t height, bool cacheAllowed);
