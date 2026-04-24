#pragma once

#include <string>
#include <lvgl.h>

class WiFiSettingsUI
{
public:
    /// Show the WiFi settings dialog, or destroy it if already shown
    static void toggleDialog();

private:
    // ── Dialog state ──
    static lv_obj_t *s_dialog;
    static lv_obj_t *s_status_lbl;
    static lv_obj_t *s_scanning_lbl;
    static lv_obj_t *s_list;
    static lv_obj_t *s_password_panel;
    static lv_obj_t *s_password_ta;
    static std::string s_pending_ssid;

    // ── Context structs for async tasks ──
    struct WifiConnectCtx
    {
        std::string ssid;
        std::string password;
    };
    struct ScanCtx
    {
    };

    // ── Helpers ──
    static const char *signalStr(uint8_t rssi);
    static const char *authStr(int mode);
    static void updateStatusLabel();
    static void saveCredentials(const std::string &ssid, const std::string &password);

    // ── Connection ──
    static void doConnect(const std::string &ssid, const std::string &password);
    static void monitorConnectionTask(void *arg);
    static void connectResultCb(void *arg);

    // ── Scanning ──
    static void startScanAsync();
    static void scanTask(void *arg);
    static void scanCompleteCb(void *arg);

    // ── UI construction ──
    static void populateNetworkList();
    static void showPasswordDialog(const std::string &ssid);
    static void destroyDialog();

    // ── LVGL event callbacks ──
    static void onCloseClick(lv_event_t *e);
    static void onRescanClick(lv_event_t *e);
    static void onNetworkClick(lv_event_t *e);
    static void onPasswordConnectClick(lv_event_t *e);
    static void onPasswordCancelClick(lv_event_t *e);
};
