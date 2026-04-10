#pragma once

#include <string>
#include <vector>

namespace thumbnail_cache {

/**
 * @brief Initialize the thumbnail cache subsystem.
 * Must be called after SPIFFS is mounted.
 * @return true on success, false on failure.
 */
bool init();

/**
 * @brief Try to retrieve a cached JPEG thumbnail.
 * @param url Original image URL
 * @param width Requested max width
 * @param height Requested max height
 * @param out_jpeg Output buffer (will be cleared and filled on success)
 * @return true if found and loaded, false otherwise.
 */
bool get(const std::string& url, uint16_t width, uint16_t height, std::vector<uint8_t>& out_jpeg);

/**
 * @brief Store a JPEG thumbnail in the cache.
 * @param url Original image URL
 * @param width Requested max width
 * @param height Requested max height
 * @param jpeg_data Raw JPEG bytes (must be valid JPEG)
 * @param jpeg_len Length of JPEG data
 * @return true on success, false on failure.
 */
bool put(const std::string& url, uint16_t width, uint16_t height, const uint8_t* jpeg_data, size_t jpeg_len);

/**
 * @brief Ensure cache size does not exceed max_items by removing oldest entries.
 * @param max_items Maximum number of cached thumbnails to keep.
 */
void prune(size_t max_items);

/**
 * @brief Get current number of cached thumbnails.
 */
size_t size();

} // namespace thumbnail_cache