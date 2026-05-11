#include "quick_panel.h"
#include "display_sleep.h"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "QuickPanel";
QuickPanel g_quick_panel;

// ═══════════════════════════════════════════════════════════════════════
//  Internals
// ═══════════════════════════════════════════════════════════════════════

void QuickPanel::setY(int32_t y)
{
    lv_obj_set_y(_panel, y);
}

void QuickPanel::animateTo(int32_t target_y, bool hide_when_done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_user_data(&a, this);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v)
                        { static_cast<QuickPanel *>(obj)->setY(v); });
    lv_anim_set_values(&a, lv_obj_get_y(_panel), target_y);
    lv_anim_set_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (hide_when_done)
        lv_anim_set_completed_cb(&a, onAnimDone);
    lv_anim_start(&a);

    lv_opa_t from_opa = lv_obj_get_style_bg_opa(_scrim, LV_PART_MAIN);
    lv_opa_t to_opa = hide_when_done ? LV_OPA_TRANSP : LV_OPA_30;
    lv_anim_t s;
    lv_anim_init(&s);
    lv_anim_set_var(&s, _scrim);
    lv_anim_set_exec_cb(&s, [](void *obj, int32_t v)
                        { lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), (lv_opa_t)v, LV_PART_MAIN); });
    lv_anim_set_values(&s, (int32_t)from_opa, (int32_t)to_opa);
    lv_anim_set_duration(&s, 260);
    lv_anim_start(&s);
}

void QuickPanel::onAnimDone(lv_anim_t *a)
{
    auto *self = static_cast<QuickPanel *>(lv_anim_get_user_data(a));
    lv_obj_add_flag(self->_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(self->_scrim, LV_OBJ_FLAG_HIDDEN);
    self->_visible = false;
}

void QuickPanel::onSleepClick(lv_event_t *e)
{
    auto *self = static_cast<QuickPanel *>(lv_event_get_user_data(e));
    lv_async_call([](void *d)
                  {
        auto *qp = static_cast<QuickPanel *>(d);
        qp->hide();
        g_display_sleep.sleepWithoutWake(); }, self);
}

// ═══════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════

void QuickPanel::init() { buildUI(); }

void QuickPanel::show()
{
    if (_visible)
        return;
    _visible = true;
    lv_obj_clear_flag(_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(_scrim, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_y(_panel, -PANEL_H);
    animateTo(0, false);
    ESP_LOGI(TAG, "show");
}

void QuickPanel::hide()
{
    if (!_visible)
        return;
    _drag_track = false;
    _drag_committed = false;
    animateTo(-PANEL_H, true);
    ESP_LOGI(TAG, "hide");
}

void QuickPanel::updateBattery(int pct, bool charging)
{
    if (!_bat_label)
        return;
    const char *icon;
    if (charging)
        icon = LV_SYMBOL_CHARGE;
    else if (pct > 75)
        icon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 50)
        icon = LV_SYMBOL_BATTERY_3;
    else if (pct > 25)
        icon = LV_SYMBOL_BATTERY_2;
    else if (pct > 10)
        icon = LV_SYMBOL_BATTERY_1;
    else
        icon = LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(_bat_label, "%s  %d%%", icon, pct);
}

void QuickPanel::updateWifi(bool connected, const char *ssid)
{
    if (!_wifi_label)
        return;
    if (connected && ssid)
        lv_label_set_text_fmt(_wifi_label, LV_SYMBOL_WIFI "  %s", ssid);
    else if (connected)
        lv_label_set_text(_wifi_label, LV_SYMBOL_WIFI "  Connected");
    else
        lv_label_set_text(_wifi_label, LV_SYMBOL_WIFI "  No network");
    lv_obj_set_style_text_color(
        _wifi_label,
        connected ? lv_color_hex(0x1A73E8) : lv_color_hex(0xAAAAAA),
        LV_PART_MAIN | LV_STATE_DEFAULT);
}

// ═══════════════════════════════════════════════════════════════════════
//  Gesture handling
// ═══════════════════════════════════════════════════════════════════════

bool QuickPanel::handleTouch(int32_t x, int32_t y, bool pressed)
{

    // ── finger lifted ────────────────────────────────────────────────
    if (!pressed)
    {
        bool was_consuming = _show_consuming;
        bool was_committed = _drag_committed;

        _show_track = false;
        _show_consuming = false;
        _drag_track = false;
        _drag_committed = false;

        if (was_committed)
        {
            // Commit hide or snap back open
            if (lv_obj_get_y(_panel) < -HIDE_COMMIT)
            {
                hide();
            }
            else
            {
                animateTo(0, false);
            }
            return true;
        }

        // Consume the lift if we were mid-swipe-down gesture so LVGL
        // never sees a PRESSED→RELEASED pair on the scrim
        return was_consuming;
    }

    // ── still consuming after a swipe-to-show — eat everything ──────
    if (_show_consuming)
        return true;

    // ── drag to dismiss (panel already open) ─────────────────────────
    if (_visible)
    {
        int32_t panel_y = lv_obj_get_y(_panel);

        if (!_drag_track)
        {
            if (y >= panel_y && y <= panel_y + PANEL_H)
            {
                _drag_track = true;
                _drag_committed = false;
                _drag_start_y = y;
                _panel_base_y = panel_y;
            }
            // Touch outside panel area — was scrim click, now ignored
            return false;
        }

        int32_t delta = y - _drag_start_y;

        if (!_drag_committed && delta < -DRAG_START_DY)
        {
            _drag_committed = true;
        }

        if (_drag_committed)
        {
            int32_t new_y = std::min((int32_t)0, _panel_base_y + delta);
            setY(new_y);
            float ratio = 1.0f + (float)new_y / (float)PANEL_H;
            lv_obj_set_style_bg_opa(
                _scrim, (lv_opa_t)(ratio * LV_OPA_30), LV_PART_MAIN);
            return true;
        }

        return false;
    }

    // ── swipe-down from top edge to show ─────────────────────────────
    if (!_show_track)
    {
        if (y < EDGE_ZONE)
        {
            _show_track = true;
            _show_start_y = y;
            // Don't consume — let LVGL process the PRESS so taps on
            // top-edge UI elements (tabs, etc.) still work.
            return false;
        }
        return false;
    }

    // Mid-gesture: consume to keep the movement away from LVGL
    if (y - _show_start_y >= SHOW_THRESHOLD)
    {
        _show_track = false;
        _show_consuming = true;
        show();
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  UI — white theme
// ═══════════════════════════════════════════════════════════════════════

void QuickPanel::buildUI()
{
    lv_obj_t *layer = lv_layer_top();

    // ── scrim ─────────────────────────────────────────────────────────
    _scrim = lv_obj_create(layer);
    lv_obj_remove_style_all(_scrim);
    lv_obj_set_size(_scrim, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(_scrim, 0, 0);
    lv_obj_set_style_bg_color(_scrim, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scrim, LV_OPA_30, LV_PART_MAIN);
    lv_obj_add_flag(_scrim, LV_OBJ_FLAG_HIDDEN);

    // ── panel ─────────────────────────────────────────────────────────
    _panel = lv_obj_create(layer);
    lv_obj_remove_style_all(_panel);
    lv_obj_set_size(_panel, SCREEN_W, PANEL_H);
    lv_obj_set_pos(_panel, 0, -PANEL_H);
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_panel, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(_panel, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_panel, 28, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_panel, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel, 0, LV_PART_MAIN);
    lv_obj_set_layout(_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        _panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);

    // ── drag handle pill ──────────────────────────────────────────────
    lv_obj_t *handle_wrap = lv_obj_create(_panel);
    lv_obj_remove_style_all(handle_wrap);
    lv_obj_set_size(handle_wrap, LV_PCT(100), 22);
    lv_obj_clear_flag(handle_wrap, LV_OBJ_FLAG_SCROLLABLE);

    _handle = lv_obj_create(handle_wrap);
    lv_obj_remove_style_all(_handle);
    lv_obj_set_size(_handle, 48, 4);
    lv_obj_align(_handle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_handle, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_handle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_handle, 2, LV_PART_MAIN);

    // ── status bar ────────────────────────────────────────────────────
    lv_obj_t *status = lv_obj_create(_panel);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, LV_PCT(100), 36);
    lv_obj_set_layout(status, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    _bat_label = lv_label_create(status);
    lv_label_set_text(_bat_label, LV_SYMBOL_BATTERY_FULL "  --%");
    lv_obj_set_style_text_color(
        _bat_label, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);

    _wifi_label = lv_label_create(status);
    lv_label_set_text(_wifi_label, LV_SYMBOL_WIFI "  --");
    lv_obj_set_style_text_color(
        _wifi_label, lv_color_hex(0x1A73E8), LV_PART_MAIN | LV_STATE_DEFAULT);

    // ── divider ───────────────────────────────────────────────────────
    lv_obj_t *div = lv_obj_create(_panel);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(0xE8E8E8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_margin_top(div, 10, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(div, 14, LV_PART_MAIN);

    // ── quick tiles ───────────────────────────────────────────────────
    lv_obj_t *tiles = lv_obj_create(_panel);
    lv_obj_remove_style_all(tiles);
    lv_obj_set_size(tiles, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(tiles, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        tiles, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tiles, 16, LV_PART_MAIN);
    lv_obj_clear_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);

    auto makeTile = [&](const char *sym, const char *label_text,
                        lv_event_cb_t cb, uint32_t accent_hex)
    {
        lv_obj_t *tile = lv_button_create(tiles);
        lv_obj_set_size(tile, 110, 90);
        lv_obj_set_style_bg_color(
            tile, lv_color_hex(0xF5F5F7), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(
            tile, lv_color_hex(0xEAEAEC), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(
            tile, lv_color_hex(accent_hex), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_border_opa(tile, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
        lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, 6, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        if (cb)
            lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, this);

        lv_obj_t *ico = lv_label_create(tile);
        lv_label_set_text(ico, sym);
        lv_obj_set_style_text_color(
            ico, lv_color_hex(accent_hex), LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *lbl = lv_label_create(tile);
        lv_label_set_text(lbl, label_text);
        lv_obj_set_style_text_color(
            lbl, lv_color_hex(0x666666), LV_PART_MAIN | LV_STATE_DEFAULT);
    };

    makeTile(LV_SYMBOL_POWER, "Sleep", onSleepClick, 0xE53935);
    makeTile(LV_SYMBOL_HOME, "Home", nullptr, 0x1A73E8);
    makeTile(LV_SYMBOL_SETTINGS, "Settings", nullptr, 0x757575);

    // ── brightness row ────────────────────────────────────────────────
    lv_obj_t *bright_row = lv_obj_create(_panel);
    lv_obj_remove_style_all(bright_row);
    lv_obj_set_size(bright_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(bright_row, 18, LV_PART_MAIN);
    lv_obj_set_layout(bright_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bright_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        bright_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bright_row, 14, LV_PART_MAIN);
    lv_obj_clear_flag(bright_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sun_label = lv_label_create(bright_row);
    lv_label_set_text(sun_label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(
        sun_label, lv_color_hex(0xF9A825), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *slider = lv_slider_create(bright_row);
    lv_obj_set_size(slider, SCREEN_W - 28 * 2 - 36 - 14, 6);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0xE0E0E0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0xF9A825), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(
        slider, lv_color_hex(0xCCCCCC), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, lv_color_hex(0x000000), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_10, LV_PART_KNOB);

    lv_obj_add_event_cb(slider, [](lv_event_t *e)
                        {
        int32_t val = lv_slider_get_value(lv_event_get_target_obj(e));
        // ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, val * 40);
        // ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGI("QuickPanel", "Brightness -> %" PRId32, val); }, LV_EVENT_VALUE_CHANGED, nullptr);
}