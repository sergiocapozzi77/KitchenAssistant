#include "WiFiManager.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include "esp_task_wdt.h"
WiFiManager wifiManager;

static const char *TAG = "WiFiManager";

volatile bool WiFiManager::wifi_connected = false;
bool WiFiManager::sntp_initialized = false;
volatile bool WiFiManager::sntp_synced = false;

// ============================================================
// Public API
// ============================================================

void WiFiManager::init(const std::string &ssid,
                       const std::string &password)
{
    ESP_LOGI(TAG, "Initializing WiFi... SSID: %s Password: %s", ssid.c_str(), password.c_str());
    // --- Network stack ---
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif); // Add this assertion like AppSettings does

    // --- WiFi init ---
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_LOGI(TAG, "Initializing WiFi, init");
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set UK country (channels 1–13)
    wifi_country_t country = {
        .cc = "GB",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO};
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    ESP_LOGI(TAG, "Initializing WiFi, events");
    // Register events
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WiFiManager::eventHandler,
        this));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WiFiManager::eventHandler,
        this));

    ESP_LOGI(TAG, "Initializing WiFi, setmode");
    // --- Set mode and START first (like AppSettings) ---
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_LOGI(TAG, "Initializing WiFi, start");
    ESP_ERROR_CHECK(esp_wifi_start());

    // --- THEN set configuration (matches AppSettings pattern) ---
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    // wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = false;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Initializing WiFi, setconfig");
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Initializing WiFi, connect");
    // Explicitly connect (like AppSettings does in wifiConnectTask)
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "WiFi initialization complete");
}

void WiFiManager::waitForConnection()
{
    while (!isConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool WiFiManager::isConnected()
{
    return wifi_connected;
}

// ============================================================
// Event Handler
// ============================================================

void WiFiManager::eventHandler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            // REMOVE THIS CASE - we call esp_wifi_connect() explicitly in init()
            // ESP_LOGI(TAG, "Connecting to WiFi...");
            // esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *event =
                (wifi_event_sta_disconnected_t *)event_data;

            ESP_LOGW(TAG,
                     "Disconnected, reason: %d",
                     event->reason);

            wifi_connected = false;

            // Small backoff before reconnect
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
            break;
        }

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG,
                 "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        wifi_connected = true;

        if (!sntp_initialized)
        {
            startSNTP();
            sntp_initialized = true;
        }
    }
}
// ============================================================
// SNTP
// ============================================================

void WiFiManager::startSNTP()
{
    ESP_LOGI(TAG, "Initializing SNTP...");

    // UK timezone
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntpSyncCallback);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started");
}

void WiFiManager::sntpSyncCallback(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP synced, time: %lld", (long long)tv->tv_sec);
    sntp_synced = true;
}

bool WiFiManager::isSntpSynced()
{
    return sntp_synced;
}