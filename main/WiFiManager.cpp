#include "WiFiManager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_heap_caps.h"

#include <cstring>
#include <algorithm>

static const char *TAG = "WiFiManager";
WiFiManager wifiManager;

// ─────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────

void WiFiManager::init(const std::string &ssid, const std::string &password)
{

    m_ssid = ssid;
    m_password = password;

    m_scanMutex = xSemaphoreCreateMutex();
    m_cmdQueue = xQueueCreate(5, sizeof(Cmd *));

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::eventHandler, this);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiManager::eventHandler, this);

    esp_timer_create_args_t t = {
        .callback = reconnectTimerCb,
        .arg = this,
        .name = "wifi_retry"};
    esp_timer_create(&t, &m_timer);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // Start worker task
    xTaskCreate(wifiTask, "wifi_task", 4096, this, 5, nullptr);

    // Trigger initial connection via queue
    connectToNetwork(ssid, password);
}

// ─────────────────────────────────────────────
// Public API (SAFE now)
// ─────────────────────────────────────────────

bool WiFiManager::connectToNetwork(const std::string &ssid, const std::string &password)
{
    auto *cmd = new Cmd{CmdType::Connect, ssid, password};
    if (xQueueSend(m_cmdQueue, &cmd, 0) != pdTRUE)
    {
        delete cmd;
        return false;
    }
    return true;
}

bool WiFiManager::startScan()
{
    auto *cmd = new Cmd{CmdType::StartScan, "", ""};
    if (xQueueSend(m_cmdQueue, &cmd, 0) != pdTRUE)
    {
        delete cmd;
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────
// Worker task (ONLY place using esp_wifi_*)
// ─────────────────────────────────────────────

void WiFiManager::wifiTask(void *arg)
{
    auto *self = static_cast<WiFiManager *>(arg);

    while (true)
    {
        Cmd *cmd = nullptr;
        if (xQueueReceive(self->m_cmdQueue, &cmd, portMAX_DELAY))
        {
            switch (cmd->type)
            {
            case CmdType::Connect:
            {
                ESP_LOGI(TAG, "Connecting to %s %s", cmd->ssid.c_str(), cmd->password.c_str());

                self->m_ssid = cmd->ssid;
                self->m_password = cmd->password;
                self->m_sntpSynced = false;
                self->m_retryDelayMs = 1000;

                self->applyConfig();

                esp_wifi_disconnect();
                esp_wifi_connect();

                self->m_state = State::Connecting;
                break;
            }

            case CmdType::StartScan:
            {
                if (self->m_state == State::Scanning)
                    break;

                ESP_LOGI(TAG, "Starting scan");

                self->m_state = State::Scanning;
                self->m_scanComplete = false;

                esp_wifi_disconnect();

                wifi_scan_config_t cfg = {};
                esp_wifi_scan_start(&cfg, false);
                break;
            }
            }

            delete cmd;
        }
    }
}

// ─────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────

void WiFiManager::applyConfig()
{
    wifi_config_t cfg = {};

    strncpy((char *)cfg.sta.ssid, m_ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = 0;

    strncpy((char *)cfg.sta.password, m_password.c_str(), sizeof(cfg.sta.password) - 1);
    cfg.sta.password[sizeof(cfg.sta.password) - 1] = 0;

    cfg.sta.threshold.authmode =
        m_password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

// ─────────────────────────────────────────────
// Events (LIGHTWEIGHT only)
// ─────────────────────────────────────────────

void WiFiManager::eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    auto *self = static_cast<WiFiManager *>(arg);

    if (base == WIFI_EVENT)
    {
        if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            self->m_sntpSynced = false;

            if (self->m_state != State::Scanning)
            {
                self->scheduleReconnect();
            }
        }
        else if (id == WIFI_EVENT_SCAN_DONE)
        {
            uint16_t count = 20;

            auto *recs = (wifi_ap_record_t *)heap_caps_malloc(
                sizeof(wifi_ap_record_t) * count,
                MALLOC_CAP_8BIT);

            if (recs)
            {
                if (esp_wifi_scan_get_ap_records(&count, recs) == ESP_OK)
                {
                    if (xSemaphoreTake(self->m_scanMutex, pdMS_TO_TICKS(1000)))
                    {
                        self->m_scanResults.assign(recs, recs + count);
                        xSemaphoreGive(self->m_scanMutex);
                    }
                }
                free(recs);
            }

            self->m_scanComplete = true;
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        self->m_state = State::Connected;
        self->m_retryDelayMs = 1000;

        if (!self->m_sntpStarted)
        {
            self->startSNTP();
            self->m_sntpStarted = true;
        }
    }
}

// ─────────────────────────────────────────────
// Reconnect
// ─────────────────────────────────────────────

void WiFiManager::scheduleReconnect()
{
    esp_timer_stop(m_timer);
    esp_timer_start_once(m_timer, m_retryDelayMs * 1000ULL);

    m_retryDelayMs = std::min(m_retryDelayMs * 2, kMaxRetryMs);
}

void WiFiManager::reconnectTimerCb(void *arg)
{
    auto *self = static_cast<WiFiManager *>(arg);

    if (self->m_state == State::Scanning)
        return;

    esp_wifi_connect();
}

// ─────────────────────────────────────────────
// SNTP
// ─────────────────────────────────────────────

void WiFiManager::startSNTP()
{
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntpCallback);
    esp_sntp_init();
}

void WiFiManager::sntpCallback(struct timeval *)
{
    wifiManager.m_sntpSynced = true;
}

// ─────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────

bool WiFiManager::isConnected() const
{
    return m_state == State::Connected;
}

bool WiFiManager::isSntpSynced() const
{
    return m_sntpSynced;
}

std::string WiFiManager::getSSID() const
{
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        return std::string((char *)ap.ssid);

    return "";
}

bool WiFiManager::isScanComplete() const
{
    return m_scanComplete;
}

int WiFiManager::getScanCount()
{
    if (xSemaphoreTake(m_scanMutex, pdMS_TO_TICKS(100)))
    {
        int count = static_cast<int>(m_scanResults.size());
        xSemaphoreGive(m_scanMutex);
        return count;
    }

    return 0;
}

// Scan getters unchanged from before...

bool WiFiManager::getScanResult(int i, std::string &ssid, int8_t &rssi, wifi_auth_mode_t &auth)
{
    if (xSemaphoreTake(m_scanMutex, pdMS_TO_TICKS(200)))
    {
        if (i >= 0 && i < (int)m_scanResults.size())
        {
            auto &r = m_scanResults[i];
            ssid = (char *)r.ssid;
            rssi = r.rssi;
            auth = r.authmode;

            xSemaphoreGive(m_scanMutex);
            return true;
        }
        xSemaphoreGive(m_scanMutex);
    }
    return false;
}
