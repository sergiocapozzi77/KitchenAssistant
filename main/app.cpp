#include "app.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_freertos_hooks.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "ui.h"
#include "WiFiManager.h"
#include "ui_extensions.h"
#include "ProductsManager.h"
#include "cJSON.h"
#include "display_sleep.h"
#include "quick_panel.h"
#include "battery.h"
#include "diag.h"

static const char *TAG = "APP";
static BatteryMonitor s_battery;

// ── DIAGNOSTIC (temporary) ───────────────────────────────────────────────────
//
// Tick probes, heartbeat tasks and the RTC breadcrumb/monitor machinery live in
// diag.cpp. It records the previous run's last activity in RTC RAM and dumps it
// on the next boot, and it logs live from an esp_timer callback if one or both
// core's FreeRTOS ticks stop while esp_timer still advances. Remove it together
// with its call sites once the watchdog resets are understood.

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
    // DIAGNOSTIC (temporary): cache back on, with phase logging in cache_io.
    // Tests whether the boot-time SPIFFS index load/purge triggers the silent
    // INT-WDT reset; the last "[io] ..." line before a reset names the phase.
    ui_extensions_init(112, 112, true);
}

static void combinedFetchTask(void *param)
{
    // DIAGNOSTIC (temporary): the main task never reached APP_LOOP in the runs
    // that froze right after "[io] ready"; these crumbs show how far this task
    // got while the main task waited behind it.
    diag_bc(DIAG_APP_FETCH_ENTER, 0);

    lv_lock();
    showSpinner();
    lv_unlock();
    diag_bc(DIAG_APP_FETCH_UI, 0);

    uint16_t waited_s = 0;
    while (!wifiManager.isConnected())
    {
        if ((waited_s % 10) == 0)
            diag_bc(DIAG_APP_FETCH_WAIT, waited_s);
        waited_s++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (!wifiManager.isSntpSynced())
        vTaskDelay(pdMS_TO_TICKS(500));

    lv_lock();
    if (objects.current_wifi_lbl && lv_obj_is_valid(objects.current_wifi_lbl))
        lv_label_set_text(objects.current_wifi_lbl, wifiManager.getSSID().c_str());
    lv_unlock();

    ESP_LOGI(TAG, "WiFi connected. Fetching products...");

    productsManager.fetchProductsSync();
    lv_lock();
    hideSpinner();
    lv_unlock();

    diag_bc(DIAG_APP_FETCH_DONE, 0);
    vTaskDelete(NULL);
}

void Application::initTasks()
{
    diag_bc(DIAG_APP_TASKS_ENTER, 0);

    StackType_t *stack = (StackType_t *)heap_caps_malloc(16384 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (!stack)
    {
        ESP_LOGE(TAG, "Failed to allocate PSRAM stack for combinedFetch task");
        return;
    }
    ESP_LOGI(TAG, "Allocated combinedFetch task stack in PSRAM");
    diag_bc(DIAG_APP_TASKS_ALLOC, 0);

    static StaticTask_t taskBuf;
    TaskHandle_t handle = xTaskCreateStatic(combinedFetchTask, "combinedFetch", 16384,
                                            nullptr, 2, stack, &taskBuf);
    diag_bc(DIAG_APP_TASKS_CREATED, (uint16_t)(handle != nullptr));
    diag_task_register(handle, DIAG_TASK_FETCH);
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
        // Tear-free output comes from CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR, which
        // makes esp_lvgl_port use the panel's two frame buffers instead of these.
        // Kept true so partial double buffering still applies if that is disabled.
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = false,
        }};
    cfg.lvgl_port_cfg.task_stack = 32768; // Allocate LVGL port task stack in PSRAM
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Build the hand-written UI once
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

    // DIAGNOSTIC (temporary): a periodic LVGL timer. If its last beat in the
    // RTC log is far behind the esp_timer monitor's, the LVGL task stopped
    // while the rest of the system kept running.
    lv_timer_create([](lv_timer_t *)
                    { diag_lvgl_beat(); },
                    250, nullptr);

    bsp_display_unlock();

    // If no saved WiFi credentials, navigate to the settings tab so the user
    // can configure WiFi (overrides the Cookbooks startup tab).
    if (!m_wifiCredsFound)
        lv_async_call([](void *) { show_main_settings_tab(); }, nullptr);

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// Main Loop
// ============================================================

void Application::mainLoop()
{
    uint32_t last_info_update = 0;
    uint32_t loop_count = 0;

    while (true)
    {
        // DIAGNOSTIC (temporary): proof the main task keeps running (~1 s beat).
        if ((++loop_count % 100) == 0)
            diag_bc(DIAG_APP_MAINLOOP, (uint16_t)(loop_count / 100));

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

    // DIAGNOSTIC (temporary): dumps the previous run's RTC log and starts the
    // tick probes, heartbeats and liveness monitor before any flash access, so
    // a stall during NVS/SPIFFS init is visible too.
    diag_init();

    initNVS();
    initHardware();
    diag_bc(DIAG_APP_HW_READY, 0);

    initQueues();
    diag_bc(DIAG_APP_QUEUES_READY, 0);

    initTasks();
    diag_bc(DIAG_APP_LOOP, 0);

    mainLoop();
}
