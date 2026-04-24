#pragma once

#include <string>
#include <vector>
#include "esp_event.h"
#include "esp_wifi.h"

class WiFiManager
{
public:
    WiFiManager() = default;
    ~WiFiManager() = default;

    void init(const std::string &ssid, const std::string &password);
    static bool isConnected();
    void waitForConnection();

    static bool isSntpSynced();
    std::string getSSID() const;

    // WiFi scanning (returns true if scan started successfully)
    bool startScan();
    bool isScanComplete();
    int getScanCount();
    bool getScanResult(int index, std::string &ssid, uint8_t &rssi, wifi_auth_mode_t &authMode);

    // Manual connect to a specific network
    bool connectToNetwork(const std::string &ssid, const std::string &password);

private:
    static volatile bool sntp_synced;
    std::string current_ssid;
    static void sntpSyncCallback(struct timeval *tv);
    static void eventHandler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data);

    static void startSNTP();

    static volatile bool wifi_connected;
    static bool sntp_initialized;

    // Scan state
    static volatile bool scan_complete;
    static volatile bool scanning;
    static std::vector<wifi_ap_record_t> scan_results;
};

extern WiFiManager wifiManager;
