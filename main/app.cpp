#include "app.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_task_wdt.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "lv_demos.h"
#include "ui.h"
#include "WiFiManager.h"
#include "ProductService.h"
#include "ui_extensions.h"
#include "ProductsManager.h"
#include "FavouritesManager.h"
// #include "thumbnail_cache.h"  // DISABLED
#include "cJSON.h"
#include <cstdio>
#include "display_sleep.h"
#include "quick_panel.h"
#include "battery.h"

static const char *TAG = "APP";
static BatteryMonitor s_battery;

#define BASE_DIR CONFIG_BSP_SPIFFS_MOUNT_POINT

// // Configuration
// #define SLEEP_TIMEOUT_MS 60000
// #define QUEUE_SIZE 8
// #define WAKE_GPIO GPIO_NUM_17
// #define BACKLIGHT_GPIO GPIO_NUM_45
// #define LCD_SLEEP_DELAY_MS 120

// RTC_DATA_ATTR bool woke_from_touch = false;

// Original BSP touch read callback — called inside our wrapper
static lv_indev_read_cb_t s_bsp_touch_read_cb = nullptr;

static void wrappedTouchReadCb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (s_bsp_touch_read_cb)
        s_bsp_touch_read_cb(indev, data);

    bool pressed = (data->state == LV_INDEV_STATE_PRESSED);
    bool was_sleeping = g_display_sleep.isSleeping();

    if (pressed) // ← only on actual touch
        g_display_sleep.onTouch();

    bool consumed = g_quick_panel.handleTouch(
        data->point.x, data->point.y, pressed);

    if (was_sleeping || g_display_sleep.isSleeping() || consumed)
        data->state = LV_INDEV_STATE_RELEASED;
}

void Application::initNVS()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing truncated NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

void Application::initQueues()
{
    // barcode_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);
    // product_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);

    // if (!barcode_queue || !product_queue)
    // {
    //     ESP_LOGE(TAG, "Queue creation failed");
    //     abort();
    // }
    // ESP_LOGI(TAG, "Queues created");
    ui_extensions_init(112, 112, true);
}

static void combinedFetchTask(void *param)
{
    showSpinner();

    while (!wifiManager.isConnected())
        vTaskDelay(pdMS_TO_TICKS(1000));

    while (!wifiManager.isSntpSynced())
        vTaskDelay(pdMS_TO_TICKS(500));

    lv_lock();
    if (objects.current_wifi_lbl && lv_obj_is_valid(objects.current_wifi_lbl))
        lv_label_set_text(objects.current_wifi_lbl, wifiManager.getSSID().c_str());
    lv_unlock();

    ESP_LOGI(TAG, "WiFi connected. Starting combined data fetch...");

    // Run product and favourite fetches sequentially in one task
    productsManager.fetchProductsSync();
    hideSpinner();

    favouritesManager.fetchFavourites();

    // Refresh favourites UI if on the favourites tab
    if (objects.tabview && lv_obj_is_valid(objects.tabview) && lv_tabview_get_tab_act(objects.tabview) == 2)
    {
        showCurrentPageFavourites(true);
    }

    vTaskDelete(NULL);
}

void Application::initTasks()
{
    StackType_t *stack = (StackType_t *)heap_caps_malloc(16384 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (!stack)
    {
        ESP_LOGE(TAG, "Failed to allocate PSRAM stack for combinedFetch task");
        return;
    }
    ESP_LOGI(TAG, "Allocated combinedFetch task stack in PSRAM");

    static StaticTask_t taskBuf;
    xTaskCreateStatic(combinedFetchTask, "combinedFetch", 16384, nullptr, 2, stack, &taskBuf);
}

void Application::initHardware()
{
    // Mount SPIFFS early so we can load saved WiFi credentials
    esp_err_t spiffs_ret = bsp_spiffs_mount();
    if (spiffs_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (ret %d)", spiffs_ret);
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS mounted");
    }

    // Load saved WiFi credentials from SPIFFS only (no Kconfig fallback)
    m_wifiCredsFound = false;
    std::string wifi_ssid, wifi_password;

    if (spiffs_ret == ESP_OK)
    {
        const char *path = BASE_DIR "/wifi_creds.json";

        FILE *f = fopen(path, "r");
        if (f)
        {
            // Read file into buffer
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (fsize > 0 && fsize < 4096)
            {
                std::string buf(fsize, '\0');
                if (fread(buf.data(), 1, fsize, f) == (size_t)fsize)
                {
                    cJSON *json = cJSON_Parse(buf.c_str());
                    if (json)
                    {
                        cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
                        cJSON *pass_item = cJSON_GetObjectItem(json, "password");
                        if (cJSON_IsString(ssid_item) && cJSON_IsString(pass_item))
                        {
                            wifi_ssid = ssid_item->valuestring;
                            wifi_password = pass_item->valuestring;
                            m_wifiCredsFound = true;
                            ESP_LOGI(TAG, "Loaded WiFi credentials for SSID: %s", wifi_ssid.c_str());
                        }
                        cJSON_Delete(json);
                    }
                }
            }
            fclose(f);
        }
        else
        {
            ESP_LOGI(TAG, "No saved WiFi credentials found — will prompt user in settings");
        }

        // // DISABLED: thumbnail cache
        // if (!thumbnail_cache::init())
        // {
        //     ESP_LOGW(TAG, "Thumbnail cache init failed (continuing without cache)");
        // }
    }

    // Initialize WiFi driver (always, regardless of saved credentials)
    wifiManager.init();

    // Connect only if we have saved credentials
    if (m_wifiCredsFound)
    {
        wifiManager.connectToNetwork(wifi_ssid, wifi_password);
    }

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = false,
        }};
    cfg.lvgl_port_cfg.task_stack = 32768; // Allocate LVGL port task stack in PSRAM
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Initialize EEZ Studio UI once
    bsp_display_lock(0);

    // ── wrap the BSP touch indev ──────────────────────────────────────
    lv_indev_t *cur = lv_indev_get_next(nullptr);
    while (cur)
    {
        if (lv_indev_get_type(cur) == LV_INDEV_TYPE_POINTER)
        {
            s_bsp_touch_read_cb = lv_indev_get_read_cb(cur);
            lv_indev_set_read_cb(cur, wrappedTouchReadCb);
            ESP_LOGI(TAG, "Touch callback wrapped");
            break;
        }
        cur = lv_indev_get_next(cur);
    }

    // ── init sleep + quick panel ──────────────────────────────────────
    g_display_sleep.init(60 * 10 * 1000); // 10min inactivity timeout
    g_quick_panel.init();
    s_battery.init();

    ui_init();
    bsp_display_unlock();

    // If no saved WiFi credentials, navigate to the settings tab so the user
    // can configure WiFi
    if (!m_wifiCredsFound)
    {
        lv_async_call([](void *)
                      {
            if (objects.tabview && lv_obj_is_valid(objects.tabview)) {
                lv_tabview_set_active(objects.tabview, 3, LV_ANIM_OFF);
            } }, nullptr);
    }

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// Main Loop
// ============================================================

void Application::mainLoop()
{
    uint32_t last_info_update = 0;
    while (true)
    {
        bsp_display_lock(0);
        ui_tick();
        bsp_display_unlock();

        uint32_t now = lv_tick_get();
        if (now - last_info_update >= 2000)
        {
            last_info_update = now;

            bool connected = wifiManager.isConnected();
            g_quick_panel.updateWifi(connected,
                                     connected ? wifiManager.getSSID().c_str() : nullptr);

            g_quick_panel.updateBattery(s_battery.readPercent());
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz update rate
    }
}

// ============================================================
// Run
// ============================================================

void Application::run()
{
    ESP_LOGI(TAG, "Booting application...");

    initNVS();
    initHardware();
    initQueues();
    initTasks();

    mainLoop();
}
