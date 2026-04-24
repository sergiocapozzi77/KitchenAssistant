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

volatile bool WiFiManager::scan_complete = false;
volatile bool WiFiManager::scanning = false;
volatile bool WiFiManager::suppress_reconnect = false;
std::vector<wifi_ap_record_t> WiFiManager::scan_results;

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

    current_ssid = ssid;

    ESP_LOGI(TAG, "WiFi initialization complete");
}

std::string WiFiManager::getSSID() const
{
    return current_ssid;
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

        case WIFI_EVENT_SCAN_DONE:
        {
            ESP_LOGI(TAG, "WiFi scan completed");
            scanning = false;

            // Fetch scan results — use a reasonable buffer directly
            uint16_t ap_count = 30;
            wifi_ap_record_t *records = (wifi_ap_record_t *)heap_caps_malloc(
                ap_count * sizeof(wifi_ap_record_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (records)
            {
                esp_err_t err = esp_wifi_scan_get_ap_records(&ap_count, records);
                if (err == ESP_OK)
                {
                    ESP_LOGI(TAG, "Found %d APs", ap_count);
                    if (ap_count > 0)
                        scan_results.assign(records, records + ap_count);
                    else
                        scan_results.clear();
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to get scan results: %d", err);
                    scan_results.clear();
                }
                heap_caps_free(records);
            }

            scan_complete = true;

            // Reconnect after scan (the disconnecting for scan suppressed auto-reconnect)
            WiFiManager *self = static_cast<WiFiManager *>(arg);
            if (self && !self->current_ssid.empty())
            {
                esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK)
                    ESP_LOGW(TAG, "Reconnect after scan: %d", ret);
            }
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *event =
                (wifi_event_sta_disconnected_t *)event_data;

            ESP_LOGW(TAG,
                     "Disconnected, reason: %d",
                     event->reason);

            wifi_connected = false;
            sntp_synced = false;

            // Suppress auto-reconnect during active scan or manual connect
            if (scanning || suppress_reconnect)
            {
                ESP_LOGI(TAG, "Auto-reconnect suppressed");
                suppress_reconnect = false;  // clear for next real disconnect
                break;
            }

            // Don't auto-reconnect on auth/permanent failures (wrong credentials,
            // AP rejection, etc.) — only retry for transient signal losses.
            if (event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                event->reason == WIFI_REASON_AUTH_FAIL ||
                event->reason == WIFI_REASON_ASSOC_FAIL ||
                event->reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                event->reason == WIFI_REASON_CONNECTION_FAIL)
            {
                ESP_LOGW(TAG, "Auth/permanent failure (reason %d) — not reconnecting", event->reason);
                break;
            }

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

// ============================================================
// WiFi Scanning
// ============================================================

bool WiFiManager::startScan()
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    scan_results.clear();
    scan_complete = false;
    scanning = true;

    // Disconnect to stop auto-reconnect from interfering with scan
    esp_wifi_disconnect();

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WiFi scan: %d", ret);
        scanning = false;
        return false;
    }
    return true;
}

bool WiFiManager::isScanComplete()
{
    return scan_complete;
}

int WiFiManager::getScanCount()
{
    return (int)scan_results.size();
}

bool WiFiManager::getScanResult(int index, std::string &ssid, uint8_t &rssi, wifi_auth_mode_t &authMode)
{
    if (index < 0 || index >= (int)scan_results.size())
        return false;

    const wifi_ap_record_t &record = scan_results[index];
    ssid = std::string((const char *)record.ssid);
    rssi = (uint8_t)(-record.rssi); // Store as positive dBm (e.g., -67dBm → 67)
    authMode = record.authmode;
    return true;
}

// ============================================================
// Manual Connect
// ============================================================

bool WiFiManager::connectToNetwork(const std::string &ssid, const std::string &password)
{
    if (ssid.empty())
        return false;

    ESP_LOGI(TAG, "Connecting to network: %s", ssid.c_str());

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.pmf_cfg.capable = false;
    wifi_config.sta.pmf_cfg.required = false;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set WiFi config: %d", ret);
        return false;
    }

    current_ssid = ssid;

    // Suppress auto-reconnect so the event handler doesn't race our connect
    suppress_reconnect = true;

    // Disconnect from any previous network so the new config takes effect cleanly
    esp_wifi_disconnect();

    // Reset connection state and trigger connection
    wifi_connected = false;
    sntp_synced = false;

    ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect: %d", ret);
        return false;
    }

    ESP_LOGI(TAG, "Connection initiated to %s", ssid.c_str());
    return true;
}