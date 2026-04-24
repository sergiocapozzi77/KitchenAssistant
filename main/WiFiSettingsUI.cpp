#include "WiFiSettingsUI.h"
#include "WiFiManager.h"
#include "ui_extensions.h"
#include "ui.h"      // objects
#include "screens.h" // objects_t
#include "styles.h"  // add_style_main_button
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <cstdio>

#define BASE_DIR CONFIG_BSP_SPIFFS_MOUNT_POINT

static const char *TAG = "wifi_ui";

// ── Static member definitions ──

lv_obj_t *WiFiSettingsUI::s_screen = nullptr;
lv_obj_t *WiFiSettingsUI::s_prev_screen = nullptr;
lv_obj_t *WiFiSettingsUI::s_status_lbl = nullptr;
lv_obj_t *WiFiSettingsUI::s_scanning_lbl = nullptr;
lv_obj_t *WiFiSettingsUI::s_list = nullptr;
lv_obj_t *WiFiSettingsUI::s_password_panel = nullptr;
lv_obj_t *WiFiSettingsUI::s_password_ta = nullptr;
lv_obj_t *WiFiSettingsUI::s_keyboard = nullptr;
lv_timer_t *WiFiSettingsUI::s_close_timer = nullptr;
lv_timer_t *WiFiSettingsUI::s_pop_timer = nullptr;
bool WiFiSettingsUI::s_is_active = false;
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
        const char *path = BASE_DIR "/wifi_creds.json";

        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "%s", json_str);
            fclose(f);
            ESP_LOGI(TAG, "Saved WiFi credentials to SPIFFS");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", path);
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

        if (s_screen && s_is_active)
            closeScreen();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to connect to %s", ctx->ssid.c_str());

        if (s_screen && s_is_active)
        {
            if (s_password_panel)
            {
                if (s_keyboard && lv_obj_is_valid(s_keyboard))
                    lv_keyboard_set_textarea(s_keyboard, nullptr);
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
        }
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
    for (int i = 0; i < 300; i++)
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
        ESP_LOGW(TAG, "Connection to %s timed out after 30s", ctx->ssid.c_str());
    }

    lv_async_call(connectResultCb, ctx);
    vTaskDelete(NULL);
}

// ── Initiate connection to a network ──

void WiFiSettingsUI::doConnect(const std::string &ssid, const std::string &password)
{
    ESP_LOGI(TAG, "doConnect ssid=%s pwd_len=%zu", ssid.c_str(), password.length());

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
    if (!s_is_active)
        return;
    if (!s_password_ta || !lv_obj_is_valid(s_password_ta))
        return;

    const char *password_chars = lv_textarea_get_text(s_password_ta);
    std::string password = password_chars ? password_chars : "";
    ESP_LOGI(TAG, "PWD after textarea copy (len=%zu): %s", password.length(), password.c_str());

    if (s_keyboard && lv_obj_is_valid(s_keyboard))
    {
        lv_keyboard_set_textarea(s_keyboard, nullptr);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
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
    if (!s_is_active)
        return;

    if (s_keyboard && lv_obj_is_valid(s_keyboard))
    {
        lv_keyboard_set_textarea(s_keyboard, nullptr);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_password_panel)
    {
        lv_obj_del(s_password_panel);
        s_password_panel = nullptr;
        s_password_ta = nullptr;
    }

    if (s_list)
        lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    if (s_screen && s_is_active)
        updateStatusLabel();
    if (s_scanning_lbl)
    {
        lv_label_set_text(s_scanning_lbl, "");
        lv_obj_add_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Keyboard focus callbacks ──

void WiFiSettingsUI::onPasswordFocused(lv_event_t *e)
{
    lv_obj_t *ta = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (s_keyboard && lv_obj_is_valid(s_keyboard))
    {
        lv_keyboard_set_textarea(s_keyboard, ta);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Show password input dialog ──

void WiFiSettingsUI::showPasswordDialog(const std::string &ssid)
{
    if (!s_screen || !s_is_active)
        return;

    // Guard against double-invocation (e.g. double-tap on a network)
    if (s_password_panel)
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
    lv_obj_t *scr = s_screen ? s_screen : lv_scr_act();
    s_password_panel = lv_obj_create(scr);
    lv_obj_set_size(s_password_panel, 460, 280);
    lv_obj_align(s_password_panel, LV_ALIGN_TOP_MID, 0, 350);
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
    lv_obj_clear_flag(s_password_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(s_password_panel);
    lv_label_set_text(title, ("Password for\n" + ssid).c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
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
    lv_obj_set_style_text_font(s_password_ta, &lv_font_montserrat_20, 0);

    // Register with our local keyboard
    lv_obj_add_event_cb(s_password_ta, onPasswordFocused, LV_EVENT_FOCUSED, nullptr);

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
    add_style_main_button(cancel_btn);
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
    add_style_main_button(connect_btn);
    lv_obj_add_event_cb(connect_btn, onPasswordConnectClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_center(connect_lbl);

    // Associate keyboard with textarea and show it
    // (use direct setup instead of synthetic LV_EVENT_FOCUSED to avoid
    //  triggering unexpected internal callback chains on animated screens)
    lv_textarea_set_cursor_pos(s_password_ta, 0);
    if (s_keyboard)
    {
        lv_keyboard_set_textarea(s_keyboard, s_password_ta);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Network list population ──

void WiFiSettingsUI::populateNetworkList()
{
    if (!s_list)
        return;

    lv_obj_clean(s_list);

    // Cancel any in-flight population timer from a previous scan
    if (s_pop_timer)
    {
        lv_timer_del(s_pop_timer);
        s_pop_timer = nullptr;
    }

    int count = wifiManager.getScanCount();
    if (count == 0)
    {
        lv_label_set_text(s_scanning_lbl, "No networks found");
        return;
    }

    lv_obj_add_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);

    // Add items incrementally so the LVGL task stays responsive
    int *ctx = new int(0);
    s_pop_timer = lv_timer_create(populateTimerCb, 10, ctx);
    lv_timer_set_repeat_count(s_pop_timer, 1);
}

void WiFiSettingsUI::populateTimerCb(lv_timer_t *t)
{
    if (!s_list || !s_is_active)
    {
        s_pop_timer = nullptr;
        delete static_cast<int *>(lv_timer_get_user_data(t));
        return; // timer auto-deletes (repeat_count was 1)
    }

    int *idx = static_cast<int *>(lv_timer_get_user_data(t));
    int count = wifiManager.getScanCount();
    if (!idx || count == 0)
    {
        delete idx;
        s_pop_timer = nullptr;
        return;
    }

    // Add up to 3 items this tick
    int batch = 0;
    for (; batch < 3 && *idx < count; (*idx)++, batch++)
    {
        std::string ssid;
        int8_t rssi;
        wifi_auth_mode_t auth;

        if (!wifiManager.getScanResult(*idx, ssid, rssi, auth))
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

        int *btn_idx = new int(*idx);
        lv_obj_add_event_cb(row, onNetworkClick, LV_EVENT_CLICKED, btn_idx);
        lv_obj_add_event_cb(row, [](lv_event_t *e)
                            { delete static_cast<int *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, btn_idx);

        // SSID label
        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, ssid.c_str());
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 16, 0);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(ssid_lbl, 1);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_20, 0);

        // Signal + auth label
        std::string info = std::string(signalStr(rssi)) + " | " + authStr(auth);
        lv_obj_t *info_lbl = lv_label_create(row);
        lv_label_set_text(info_lbl, info.c_str());
        lv_obj_align(info_lbl, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_20, 0);
    }

    if (*idx < count)
    {
        // More items remain — re-arm the timer for the next batch
        s_pop_timer = lv_timer_create(populateTimerCb, 10, lv_timer_get_user_data(t));
        lv_timer_set_repeat_count(s_pop_timer, 1);
    }
    else
    {
        delete idx;
        s_pop_timer = nullptr;
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
    if (!s_is_active)
        return;
    int *idx = static_cast<int *>(lv_event_get_user_data(e));
    if (!idx)
        return;

    std::string ssid;
    int8_t rssi;
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

// ── Screen lifecycle ──

void WiFiSettingsUI::destroyScreen()
{
    if (s_keyboard && lv_obj_is_valid(s_keyboard))
    {
        lv_keyboard_set_textarea(s_keyboard, nullptr);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    // Cancel deferred cleanup timer if it still exists
    if (s_close_timer)
    {
        lv_timer_del(s_close_timer);
        s_close_timer = nullptr;
    }

    // Cancel any in-flight population timer
    if (s_pop_timer)
    {
        delete static_cast<int *>(lv_timer_get_user_data(s_pop_timer));
        lv_timer_del(s_pop_timer);
        s_pop_timer = nullptr;
    }

    // Delete the screen (LVGL recursively deletes children)
    if (s_screen)
    {
        lv_obj_del(s_screen);
    }

    s_screen = nullptr;
    s_status_lbl = nullptr;
    s_scanning_lbl = nullptr;
    s_list = nullptr;
    s_password_panel = nullptr;
    s_password_ta = nullptr;
    s_keyboard = nullptr;
    s_prev_screen = nullptr;
    s_pending_ssid.clear();
    s_is_active = false;
}

void WiFiSettingsUI::closeAnimCb(lv_timer_t *t)
{
    destroyScreen();
    s_close_timer = nullptr;
}

void WiFiSettingsUI::closeScreen()
{
    if (!s_screen || !s_is_active)
        return;

    s_is_active = false;

    if (s_keyboard && lv_obj_is_valid(s_keyboard))
    {
        lv_keyboard_set_textarea(s_keyboard, nullptr);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    // Navigate back with slide-out-to-right animation
    lv_obj_t *target = (s_prev_screen && lv_obj_is_valid(s_prev_screen))
                           ? s_prev_screen
                           : lv_scr_act();
    lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);

    // Deferred cleanup: wait for animation to finish, then destroy
    s_close_timer = lv_timer_create(closeAnimCb, 300, nullptr);
    lv_timer_set_repeat_count(s_close_timer, 1);
}

void WiFiSettingsUI::openScreen()
{
    // Cancel any stale close timer (safety)
    if (s_close_timer)
    {
        lv_timer_del(s_close_timer);
        s_close_timer = nullptr;
    }

    // Create as a proper screen (parent=0)
    s_screen = lv_obj_create(0);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_size(s_screen, 800, 1280);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    // Capture previous screen before transition
    s_prev_screen = lv_scr_act();

    // Mark active before building UI (callbacks may check this)
    s_is_active = true;

    // Start animation immediately so the user sees movement
    // while the remaining widgets are built.
    lv_scr_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

    // ── Header ──
    lv_obj_t *header = lv_obj_create(s_screen);
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
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_26, 0);

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
    s_status_lbl = lv_label_create(s_screen);
    lv_obj_set_width(s_status_lbl, lv_pct(100));
    lv_obj_set_style_pad_all(s_status_lbl, 14, 0);
    lv_obj_set_style_pad_left(s_status_lbl, 20, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_24, 0);

    // ── Scanning indicator ──
    s_scanning_lbl = lv_label_create(s_screen);
    lv_obj_set_width(s_scanning_lbl, lv_pct(100));
    lv_obj_set_style_pad_all(s_scanning_lbl, 6, 0);
    lv_obj_set_style_pad_left(s_scanning_lbl, 20, 0);
    lv_obj_set_style_text_color(s_scanning_lbl, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_scanning_lbl, "Scanning for networks...");
    lv_obj_set_style_text_font(s_scanning_lbl, &lv_font_montserrat_22, 0);

    // ── Scrollable network list ──
    s_list = lv_obj_create(s_screen);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_list, 10, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_bg_opa(s_list, 0, 0);

    // ── Rescan button ──
    lv_obj_t *rescan_btn = lv_btn_create(s_screen);
    lv_obj_set_width(rescan_btn, lv_pct(80));
    lv_obj_set_x(rescan_btn, 80); // center horizontally (800 - 80*2 = 640 content width)
    lv_obj_set_height(rescan_btn, 48);
    lv_obj_add_event_cb(rescan_btn, onRescanClick, LV_EVENT_CLICKED, nullptr);
    add_style_main_button(rescan_btn);
    lv_obj_set_style_margin_bottom(rescan_btn, 20, 0);

    lv_obj_t *rescan_lbl = lv_label_create(rescan_btn);
    lv_label_set_text(rescan_lbl, "Rescan");
    lv_obj_center(rescan_lbl);
    lv_obj_set_style_text_color(rescan_lbl, lv_color_hex(0x000000), 0);

    // ── Keyboard (hidden by default, shown when password is needed) ──
    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_keyboard, lv_pct(100), 299);
    lv_obj_set_pos(s_keyboard, 0, 1280 - 299);
    updateStatusLabel();
    startScanAsync();
}

void WiFiSettingsUI::onCloseClick(lv_event_t *e)
{
    closeScreen();
}

void WiFiSettingsUI::onRescanClick(lv_event_t *e)
{
    if (!s_list || !s_scanning_lbl || !s_is_active)
        return;

    // Cancel any in-flight population from the previous scan
    if (s_pop_timer)
    {
        delete static_cast<int *>(lv_timer_get_user_data(s_pop_timer));
        lv_timer_del(s_pop_timer);
        s_pop_timer = nullptr;
    }

    lv_obj_clean(s_list);
    lv_label_set_text(s_scanning_lbl, "Scanning for networks...");
    lv_obj_clear_flag(s_scanning_lbl, LV_OBJ_FLAG_HIDDEN);
    startScanAsync();
}

// ── Public API ──

void WiFiSettingsUI::toggleDialog()
{
    if (s_screen)
    {
        if (s_is_active)
            closeScreen();
        // else: mid-close animation, ignore
        return;
    }
    openScreen();
}
