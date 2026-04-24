#pragma once

#include <string>
#include <lvgl.h>

class WiFiSettingsUI
{
public:
    /// Show the WiFi settings screen, or close it if already shown
    static void toggleDialog();

private:
    // ── Screen state ──
    static lv_obj_t *s_screen;
    static lv_obj_t *s_prev_screen;
    static lv_obj_t *s_status_lbl;
    static lv_obj_t *s_scanning_lbl;
    static lv_obj_t *s_list;
    static lv_obj_t *s_password_panel;
    static lv_obj_t *s_password_ta;
    static lv_obj_t *s_keyboard;
    static lv_timer_t *s_close_timer;
    static bool s_is_active;
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

    // ── Screen lifecycle ──
    static void openScreen();
    static void closeScreen();
    static void closeAnimCb(lv_timer_t *t);
    static void destroyScreen();

    // ── Incremental network list population ──
    static lv_timer_t *s_pop_timer;
    static void populateTimerCb(lv_timer_t *t);

    // ── UI construction ──
    static void populateNetworkList();
    static void showPasswordDialog(const std::string &ssid);

    // ── LVGL event callbacks ──
    static void onCloseClick(lv_event_t *e);
    static void onRescanClick(lv_event_t *e);
    static void onNetworkClick(lv_event_t *e);
    static void onPasswordConnectClick(lv_event_t *e);
    static void onPasswordCancelClick(lv_event_t *e);
    static void onPasswordFocused(lv_event_t *e);
};
