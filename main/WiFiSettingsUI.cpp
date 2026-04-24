#include "WiFiSettingsUI.h"
#include "WiFiManager.h"
#include "ui_extensions.h"
#include "ui.h"           // objects
#include "screens.h"      // objects_t
#include "styles.h"       // add_style_main_button
#include "actions.h"      // keywords_textarea_focused_cb, keywords_textarea_defocused_cb
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <cstdio>

static const char *TAG = "wifi_ui";

// ── Static member definitions ──

lv_obj_t *WiFiSettingsUI::s_dialog = nullptr;
lv_obj_t *WiFiSettingsUI::s_status_lbl = nullptr;
lv_obj_t *WiFiSettingsUI::s_scanning_lbl = nullptr;
lv_obj_t *WiFiSettingsUI::s_list = nullptr;
lv_obj_t *WiFiSettingsUI::s_password_panel = nullptr;
lv_obj_t *WiFiSettingsUI::s_password_ta = nullptr;
std::string WiFiSettingsUI::s_pending_ssid;

// ── Helpers ──

const char *WiFiSettingsUI::signalStr(uint8_t rssi)
{
    if (rssi < 50)
        return "Excellent";
    if (rssi < 67)
        return "Good";
    if (rssi < 80)
        return "Fair";
    return "Weak";
}

const char *WiFiSettingsUI::authStr(int mode)
{
    switch (mode)
    {
    case WIFI_AUTH_OPEN:
        return "Open";
    default:
        return "Secured";
    }
}

void WiFiSettingsUI::updateStatusLabel()
{
    if (!s_status_lbl)
        return;
    std::string txt = "Connected: ";
    if (wifiManager.isConnected())
        txt += wifiManager.getSSID();
    else
        txt += "Not connected";
    lv_label_set_text(s_status_lbl, txt.c_str());
}

void WiFiSettingsUI::saveCredentials(const std::string &ssid, const std::string &password)
{
    cJSON *json = cJSON_CreateObject();
    if (!json)
        return;

    cJSON_AddStringToObject(json, "ssid", ssid.c_str());
    cJSON_AddStringToObject(json, "password", password.c_str());

    char *json_str = cJSON_Print(json);
    if (json_str)
    {
        FILE *f = fopen("/spiffs/wifi_creds.json", "w");
        if (f)
        {
            fprintf(f, "%s", json_str);
            fclose(f);
            ESP_LOGI(TAG, "Saved WiFi credentials to SPIFFS");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to open wifi_creds.json for writing");
        }
        free(json_str);
    }
    cJSON_Delete(json);
}

// ── Connection result callback (runs on LVGL main task) ──

void WiFiSettingsUI::connectResultCb(void *arg)
{
    WifiConnectCtx *ctx = static_cast<WifiConnectCtx *>(arg);
    if (!ctx)
        return;

    if (wifiManager.isConnected())
    {
        ESP_LOGI(TAG, "Connected to %s", ctx->ssid.c_str());
        saveCredentials(ctx->ssid, ctx->password);

        if (objects.current_wifi_lbl && lv_obj_is_valid(objects.current_wifi_lbl))
        {
            lv_label_set_text(objects.current_wifi_lbl, ctx->ssid.c_str());
        }
        showSnackbar(("Connected to " + ctx->ssid).c_str(), 3000);
        destroyDialog();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to connect to %s", ctx->ssid.c_str());

        if (s_password_panel)
        {
            lv_obj_del(s_password_panel);
            s_password_panel = nullptr;
            s_password_ta = nullptr;
        }
        if (s_list)
        {
            lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_scanning_lbl)
        {
            lv_label_set_text(s_scanning_lbl, "Connection failed. Try again.");
            lv_obj_clear_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        updateStatusLabel();
        showSnackbar(("Failed to connect to " + ctx->ssid).c_str(), 5000);
    }

    delete ctx;
}

// ── Connection monitor task (background, polls connection state) ──

void WiFiSettingsUI::monitorConnectionTask(void *arg)
{
    WifiConnectCtx *ctx = static_cast<WifiConnectCtx *>(arg);
    if (!ctx)
    {
        vTaskDelete(NULL);
        return;
    }

    bool connected = false;
    for (int i = 0; i < 150; i++)
    {
        if (wifiManager.isConnected())
        {
            connected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!connected)
    {
        ESP_LOGW(TAG, "Connection to %s timed out after 15s", ctx->ssid.c_str());
    }

    lv_async_call(connectResultCb, ctx);
    vTaskDelete(NULL);
}

// ── Initiate connection to a network ──

void WiFiSettingsUI::doConnect(const std::string &ssid, const std::string &password)
{
    if (!wifiManager.connectToNetwork(ssid, password))
    {
        showSnackbar("Failed to start connection", 5000);
        return;
    }

    if (s_status_lbl)
    {
        lv_label_set_text(s_status_lbl, ("Connecting to " + ssid + "...").c_str());
    }
    if (s_scanning_lbl)
    {
        lv_label_set_text(s_scanning_lbl, "Connecting...");
        lv_obj_clear_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    WifiConnectCtx *ctx = new WifiConnectCtx{ssid, password};
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        monitorConnectionTask, "WifiConnect",
        4096, ctx, 3, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        delete ctx;
        showSnackbar("Failed to create connection task", 5000);
    }
}

// ── Password dialog callbacks ──

void WiFiSettingsUI::onPasswordConnectClick(lv_event_t *e)
{
    if (!s_password_ta || !lv_obj_is_valid(s_password_ta))
        return;

    const char *password = lv_textarea_get_text(s_password_ta);
    if (!password)
        password = "";

    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_password_panel)
    {
        lv_obj_del(s_password_panel);
        s_password_panel = nullptr;
        s_password_ta = nullptr;
    }

    doConnect(s_pending_ssid, password);
}

void WiFiSettingsUI::onPasswordCancelClick(lv_event_t *e)
{
    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_password_panel)
    {
        lv_obj_del(s_password_panel);
        s_password_panel = nullptr;
        s_password_ta = nullptr;
    }

    if (s_list)
        lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    updateStatusLabel();
    if (s_scanning_lbl)
    {
        lv_label_set_text(s_scanning_lbl, "");
        lv_obj_add_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Show password input dialog ──

void WiFiSettingsUI::showPasswordDialog(const std::string &ssid)
{
    if (!s_dialog)
        return;

    s_pending_ssid = ssid;

    if (s_list)
        lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    if (s_scanning_lbl)
        lv_obj_add_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);

    if (s_status_lbl)
    {
        lv_label_set_text(s_status_lbl, ("Password for: " + ssid).c_str());
    }
    lv_obj_t *scr = lv_scr_act();
    s_password_panel = lv_obj_create(scr);
    lv_obj_set_size(s_password_panel, 460, 280);
    lv_obj_center(s_password_panel);
    lv_obj_set_style_bg_color(s_password_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(s_password_panel, 12, 0);
    lv_obj_set_style_shadow_width(s_password_panel, 16, 0);
    lv_obj_set_style_shadow_color(s_password_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_password_panel, 60, 0);
    lv_obj_set_style_border_width(s_password_panel, 1, 0);
    lv_obj_set_style_border_color(s_password_panel, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_flex_flow(s_password_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_password_panel, 20, 0);
    lv_obj_set_style_pad_row(s_password_panel, 16, 0);

    // Title
    lv_obj_t *title = lv_label_create(s_password_panel);
    lv_label_set_text(title, ("Password for\n" + ssid).c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, lv_pct(100));

    // Password textarea
    s_password_ta = lv_textarea_create(s_password_panel);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_textarea_set_placeholder_text(s_password_ta, "Enter password...");
    lv_obj_set_width(s_password_ta, lv_pct(100));
    lv_obj_set_height(s_password_ta, 48);
    lv_obj_set_style_border_width(s_password_ta, 1, 0);
    lv_obj_set_style_border_color(s_password_ta, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(s_password_ta, 6, 0);
    lv_obj_set_style_pad_all(s_password_ta, 10, 0);

    // Register with the shared keyboard
    lv_obj_add_event_cb(s_password_ta, keywords_textarea_focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(s_password_ta, keywords_textarea_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);

    // Button row
    lv_obj_t *btn_row = lv_obj_create(s_password_panel);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_bg_opa(btn_row, 0, 0);

    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_height(cancel_btn, 44);
    lv_obj_set_flex_grow(cancel_btn, 1);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_border_width(cancel_btn, 0, 0);
    lv_obj_add_event_cb(cancel_btn, onPasswordCancelClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    // Spacer
    lv_obj_t *spacer = lv_obj_create(btn_row);
    lv_obj_set_width(spacer, 12);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_bg_opa(spacer, 0, 0);

    // Connect button
    lv_obj_t *connect_btn = lv_btn_create(btn_row);
    lv_obj_set_height(connect_btn, 44);
    lv_obj_set_flex_grow(connect_btn, 1);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x27AE60), 0);
    lv_obj_set_style_radius(connect_btn, 6, 0);
    lv_obj_set_style_border_width(connect_btn, 0, 0);
    lv_obj_add_event_cb(connect_btn, onPasswordConnectClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_center(connect_lbl);

    // Auto-focus the textarea to trigger the keyboard
    lv_textarea_set_cursor_pos(s_password_ta, 0);
    lv_obj_send_event(s_password_ta, LV_EVENT_FOCUSED, nullptr);
}

// ── Network list population ──

void WiFiSettingsUI::populateNetworkList()
{
    if (!s_list)
        return;

    lv_obj_clean(s_list);

    int count = wifiManager.getScanCount();
    if (count == 0)
    {
        lv_label_set_text(s_scanning_lbl, "No networks found");
        return;
    }

    lv_obj_add_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < count; i++)
    {
        std::string ssid;
        uint8_t rssi;
        wifi_auth_mode_t auth;

        if (!wifiManager.getScanResult(i, ssid, rssi, auth))
            continue;
        if (ssid.empty())
            continue;

        // Network row button
        lv_obj_t *row = lv_btn_create(s_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 56);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xF5F6FA), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xE8EAF0), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        int *idx = new int(i);
        lv_obj_add_event_cb(row, onNetworkClick, LV_EVENT_CLICKED, idx);
        lv_obj_add_event_cb(row, [](lv_event_t *e)
                            { delete static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, idx);

        // SSID label
        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, ssid.c_str());
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 16, 0);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(ssid_lbl, 1);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0x888888), 0);

        // Signal + auth label
        std::string info = std::string(signalStr(rssi)) + " | " + authStr(auth);
        lv_obj_t *info_lbl = lv_label_create(row);
        lv_label_set_text(info_lbl, info.c_str());
        lv_obj_align(info_lbl, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
    }
}

// ── Scan completion callback ──

void WiFiSettingsUI::scanCompleteCb(void *arg)
{
    delete static_cast<ScanCtx *>(arg);
    populateNetworkList();
}

// ── Scan task (background, retries if WiFi not ready) ──

void WiFiSettingsUI::scanTask(void *arg)
{
    (void)arg;

    bool started = false;
    for (int attempt = 0; attempt < 5; attempt++)
    {
        if (wifiManager.startScan())
        {
            started = true;
            break;
        }
        ESP_LOGW(TAG, "Scan attempt %d failed, retrying in 2s...", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (!started)
    {
        ESP_LOGE(TAG, "All scan attempts failed");
        lv_async_call([](void *)
                      {
            if (s_scanning_lbl)
                lv_label_set_text(s_scanning_lbl, "Scan failed. Tap Rescan."); }, nullptr);
        vTaskDelete(NULL);
        return;
    }

    int wait_count = 0;
    while (!wifiManager.isScanComplete())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
        if (wait_count > 200)
        {
            ESP_LOGE(TAG, "Scan timed out");
            break;
        }
    }

    lv_async_call(scanCompleteCb, new ScanCtx());
    vTaskDelete(NULL);
}

void WiFiSettingsUI::startScanAsync()
{
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        scanTask, "WifiScan",
        4096, nullptr, 3, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create WiFi scan task");
        if (s_scanning_lbl)
            lv_label_set_text(s_scanning_lbl, "Scan failed");
    }
}

// ── Network row click handler ──

void WiFiSettingsUI::onNetworkClick(lv_event_t *e)
{
    int *idx = static_cast<int *>(lv_event_get_user_data(e));
    if (!idx)
        return;

    std::string ssid;
    uint8_t rssi;
    wifi_auth_mode_t auth;

    if (!wifiManager.getScanResult(*idx, ssid, rssi, auth))
        return;
    if (ssid.empty())
        return;

    if (auth == WIFI_AUTH_OPEN)
    {
        doConnect(ssid, "");
    }
    else
    {
        showPasswordDialog(ssid);
    }
}

// ── Dialog lifecycle ──

void WiFiSettingsUI::destroyDialog()
{
    if (objects.keywords_keyboard && lv_obj_is_valid(objects.keywords_keyboard))
    {
        lv_obj_add_flag(objects.keywords_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    // Delete the main dialog (LVGL recursively deletes children)
    if (s_dialog)
    {
        lv_obj_del(s_dialog);
    }

    s_dialog = nullptr;
    s_status_lbl = nullptr;
    s_scanning_lbl = nullptr;
    s_list = nullptr;
    s_password_panel = nullptr;
    s_password_ta = nullptr;
    s_pending_ssid.clear();
}

void WiFiSettingsUI::onCloseClick(lv_event_t *e)
{
    destroyDialog();
}

void WiFiSettingsUI::onRescanClick(lv_event_t *e)
{
    if (!s_list || !s_scanning_lbl)
        return;

    lv_obj_clean(s_list);
    lv_label_set_text(s_scanning_lbl, "Scanning for networks...");
    lv_obj_clear_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);
    startScanAsync();
}

// ── Public API ──

void WiFiSettingsUI::toggleDialog()
{
    if (s_dialog)
    {
        destroyDialog();
        return;
    }

    lv_obj_t *scr = lv_scr_act();

    // ── Dialog container ──
    s_dialog = lv_obj_create(scr);
    lv_obj_set_size(s_dialog, 660, 600);
    lv_obj_center(s_dialog);
    lv_obj_set_style_bg_color(s_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(s_dialog, 16, 0);
    lv_obj_set_style_shadow_width(s_dialog, 24, 0);
    lv_obj_set_style_shadow_color(s_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_dialog, 80, 0);
    lv_obj_set_style_border_width(s_dialog, 0, 0);
    lv_obj_set_flex_flow(s_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_dialog, 0, 0);

    // ── Header ──
    lv_obj_t *header = lv_obj_create(s_dialog);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 55);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, "WiFi Settings");
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x000000), 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 38, 38);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(close_btn, onCloseClick, LV_EVENT_CLICKED, nullptr);
    add_style_main_button(close_btn);

    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "X");
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0x000000), 0);

    // ── Status ──
    s_status_lbl = lv_label_create(s_dialog);
    lv_obj_set_width(s_status_lbl, lv_pct(100));
    lv_obj_set_style_pad_all(s_status_lbl, 14, 0);
    lv_obj_set_style_pad_left(s_status_lbl, 20, 0);

    // ── Scanning indicator ──
    s_scanning_lbl = lv_label_create(s_dialog);
    lv_obj_set_width(s_scanning_lbl, lv_pct(100));
    lv_obj_set_style_pad_all(s_scanning_lbl, 6, 0);
    lv_obj_set_style_pad_left(s_scanning_lbl, 20, 0);
    lv_obj_set_style_text_color(s_scanning_lbl, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_scanning_lbl, "Scanning for networks...");

    // ── Scrollable network list ──
    s_list = lv_obj_create(s_dialog);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_list, 10, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_bg_opa(s_list, 0, 0);

    // ── Rescan button ──
    lv_obj_t *rescan_btn = lv_btn_create(s_dialog);
    lv_obj_set_width(rescan_btn, lv_pct(100));
    lv_obj_set_height(rescan_btn, 48);
    lv_obj_add_event_cb(rescan_btn, onRescanClick, LV_EVENT_CLICKED, nullptr);
    add_style_main_button(rescan_btn);

    lv_obj_t *rescan_lbl = lv_label_create(rescan_btn);
    lv_label_set_text(rescan_lbl, "Rescan");
    lv_obj_center(rescan_lbl);
    lv_obj_set_style_text_color(rescan_lbl, lv_color_hex(0x000000), 0);

    updateStatusLabel();
    startScanAsync();
}
