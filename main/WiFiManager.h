#pragma once

#include <string>
#include <vector>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"

class WiFiManager
{
public:
    void init(const std::string &ssid, const std::string &password);

    bool connectToNetwork(const std::string &ssid, const std::string &password);

    bool isConnected() const;
    bool isSntpSynced() const;
    std::string getSSID() const;

    // Scan
    bool startScan();
    bool isScanComplete() const;
    int getScanCount();
    bool getScanResult(int index, std::string &ssid, int8_t &rssi, wifi_auth_mode_t &auth);

private:
    enum class State
    {
        Idle,
        Connecting,
        Connected,
        Scanning
    };

    enum class CmdType
    {
        Connect,
        StartScan
    };

    struct Cmd
    {
        CmdType type;
        std::string ssid;
        std::string password;
    };

    // Tasks
    static void wifiTask(void *arg);

    // Internal
    void applyConfig();
    void scheduleReconnect();

    static void reconnectTimerCb(void *arg);
    static void eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data);

    // SNTP
    void startSNTP();
    static void sntpCallback(struct timeval *tv);

private:
    std::string m_ssid;
    std::string m_password;

    std::atomic<State> m_state{State::Idle};
    std::atomic<bool> m_scanComplete{false};
    std::atomic<bool> m_sntpSynced{false};

    std::vector<wifi_ap_record_t> m_scanResults;
    SemaphoreHandle_t m_scanMutex = nullptr;

    QueueHandle_t m_cmdQueue = nullptr;
    esp_timer_handle_t m_timer = nullptr;

    uint32_t m_retryDelayMs = 1000;
    static constexpr uint32_t kMaxRetryMs = 30000;

    bool m_sntpStarted = false;
};

extern WiFiManager wifiManager;