#pragma once

namespace spiffs_cleanup
{

    /**
     * @brief Delete saved WiFi credentials from SPIFFS
     *
     * Removes /spiffs/wifi_creds.json so the device will
     * present the WiFi setup screen on next boot.
     *
     * @return true if deleted or file didn't exist, false on error
     */
    bool delete_wifi_creds();

    /**
     * @brief Delete the entire thumbnail cache
     *
     * Removes /spiffs/thumb_index.json and all hash-named .jpg files
     * from /spiffs/. After this the thumbnail_cache will start fresh.
     *
     * Note: The thumbnail_cache IO task may be mid-write when this runs.
     * Call thumbnail_cache::flush() first, then call this, then
     * expect stale GET requests to fail (harmlessly).
     *
     * @return true on success, false if any file could not be removed
     */
    bool delete_thumbnail_cache();

} // namespace spiffs_cleanup
