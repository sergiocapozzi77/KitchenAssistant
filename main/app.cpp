#include "app.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lv_demos.h"
#include "ui.h"
#include "WiFiManager.h"
#include "ProductService.h"
#include "ui_extensions.h"

static const char *TAG = "APP";

// // Configuration
// #define SLEEP_TIMEOUT_MS 60000
// #define QUEUE_SIZE 8
// #define WAKE_GPIO GPIO_NUM_17
// #define BACKLIGHT_GPIO GPIO_NUM_45
// #define LCD_SLEEP_DELAY_MS 120

// RTC_DATA_ATTR bool woke_from_touch = false;

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
}

void Application::initTasks()
{
    // barcode_reader = new BarcodeReader(barcode_queue);
    // ESP_ERROR_CHECK(barcode_reader->init());

    // product_fetcher = new ProductFetcher(
    //     barcode_queue,
    //     product_queue,
    //     &product_cache);
    // ESP_ERROR_CHECK(product_fetcher->start());

    // Task to fetch products expiring today or tomorrow
    xTaskCreate(Application::fetchProductsTask, "FetchProducts", 8096, this, 5, &fetchTaskHandle);

    // ESP_LOGI(TAG, "Tasks started");
}

void Application::initHardware()
{
    // WiFi
    wifiManager.init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = false,
        }};
    cfg.lvgl_port_cfg.task_stack = 24576;
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Initialize EEZ Studio UI once
    bsp_display_lock(0);
    ui_init();
    bsp_display_unlock();

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// Main Loop
// ============================================================

void Application::mainLoop()
{
    while (true)
    {
        bsp_display_lock(0);
        ui_tick();
        bsp_display_unlock();

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

// Task to fetch products expiring today or tomorrow
void Application::fetchProductsTask(void *param)
{
    Application *self = (Application *)param;
    while (!wifiManager.isConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "WiFi connected. Fetching products...");

    // Fetch products
    int out;
    auto products = productService.getProductsRetry({}, out);
    if (products.empty())
    {
        ESP_LOGI(TAG, "No products found");
        //  LVGLManager::updateStatusLabel("No products expiring soon");
        self->fetchTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "Fetched %d products", products.size());
        // LVGLManager::updateStatusLabel("Fetched " + std::to_string(products.size()) + " expiring products");
        populateProductList(objects.products_list, products);
    }

    self->fetchTaskHandle = NULL;
    vTaskDelete(NULL);
}