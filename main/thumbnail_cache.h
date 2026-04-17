#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace thumbnail_cache
{

    /**
     * @brief Initialize the thumbnail cache system
     *
     * Creates the IO task with internal RAM stack and loads the index from SPIFFS.
     * Must be called before any get() or put() operations.
     *
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Retrieve a cached thumbnail
     *
     * Blocks until the index is loaded, then performs a fast RAM lookup.
     * If found, delegates file read to the IO task and blocks until complete.
     *
     * @param url Original image URL
     * @param width Requested thumbnail width
     * @param height Requested thumbnail height
     * @param out_jpeg Output buffer for JPEG data (cleared on failure)
     * @return true if found and successfully read, false otherwise
     */
    bool get(const std::string &url, uint16_t width, uint16_t height,
             std::vector<uint8_t> &out_jpeg);

    /**
     * @brief Store a thumbnail in the cache
     *
     * Copies the JPEG data to SPIRAM and queues it for async write.
     * Returns immediately without blocking on flash I/O.
     *
     * The index is saved to flash either:
     * - After INDEX_SAVE_BATCH_SIZE consecutive puts (default: 5)
     * - After INDEX_SAVE_INTERVAL_MS has elapsed (default: 30 seconds)
     *
     * This batching strategy eliminates UI flickering caused by frequent flash writes.
     *
     * @param url Original image URL
     * @param width Thumbnail width
     * @param height Thumbnail height
     * @param jpeg_data JPEG buffer (copied, caller retains ownership)
     * @param jpeg_len JPEG size in bytes
     * @return true if successfully queued, false on error
     */
    bool put(const std::string &url, uint16_t width, uint16_t height,
             const uint8_t *jpeg_data, size_t jpeg_len);

    /**
     * @brief Force immediate index save
     *
     * Queues a flush operation to write the index to flash immediately.
     * Useful before shutdown or when you want to ensure persistence.
     * Non-blocking — returns immediately after queueing.
     *
     * Typical usage:
     * - Call from a shutdown handler
     * - After a batch of thumbnail downloads completes
     * - Before entering deep sleep
     */
    void flush();

    /**
     * @brief Get the current number of cached items
     *
     * @return Number of entries in the cache
     */
    size_t size();

    /**
     * @brief Prune cache to a maximum number of items
     *
     * Currently unused — pruning happens automatically during put()
     * when MAX_CACHE_ITEMS is exceeded.
     *
     * @param max_items Maximum number of items to keep
     */
    void prune(size_t max_items);

} // namespace thumbnail_cache