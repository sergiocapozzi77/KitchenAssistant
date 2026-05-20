#pragma once
#include "lvgl.h"

class QuickPanel
{
public:
    void init();
    void show();
    void hide();
    bool isVisible() const { return _visible; }
    void updateBattery(int pct, bool charging = false);
    void updateWifi(bool connected, const char *ssid = nullptr);

    // Call from touch_read_cb before passing event to LVGL.
    // Returns true if the touch was consumed by gesture logic.
    bool handleTouch(int32_t x, int32_t y, bool pressed);

private:
    // ── layout ───────────────────────────────────────────────────────
    static constexpr int32_t SCREEN_W = 800;
    static constexpr int32_t SCREEN_H = 1280;
    static constexpr int32_t PANEL_H = 340;
    static constexpr int32_t EDGE_ZONE = 40;      // top px that start swipe tracking
    static constexpr int32_t SHOW_THRESHOLD = 70; // downward dy needed to open
    static constexpr int32_t SHOW_VELOCITY = 200; // min px/s to distinguish flick from slow drag
    static constexpr int32_t DRAG_START_DY = 12;  // upward dy to enter drag mode
    static constexpr int32_t HIDE_COMMIT = 90;    // upward dy to commit close

    // ── widgets ──────────────────────────────────────────────────────
    lv_obj_t *_scrim = nullptr;  // full-screen dim layer
    lv_obj_t *_panel = nullptr;  // the shade itself
    lv_obj_t *_handle = nullptr; // drag pill
    lv_obj_t *_bat_label = nullptr;
    lv_obj_t *_wifi_label = nullptr;

    // ── state ────────────────────────────────────────────────────────
    bool _visible = false;

    // swipe-down-to-show tracking
    bool _show_track = false;
    bool _show_consuming = false;  // consume all events after show() until release
    int32_t _show_start_y = 0;
    uint32_t _show_start_tick = 0; // tick when tracking started (velocity check)

    // drag-up-to-dismiss tracking
    bool _drag_track = false;
    bool _drag_committed = false;
    int32_t _drag_start_y = 0;
    int32_t _panel_base_y = 0;

    // ── brightness ───────────────────────────────────────────────────
    uint8_t _brightness = 80;
    void loadBrightness();
    static void saveBrightness(uint8_t val);

    // ── internals ────────────────────────────────────────────────────
    void buildUI();
    void setY(int32_t y);
    void animateTo(int32_t target_y, bool hide_when_done);

    static void onAnimDone(lv_anim_t *a);
    static void onSleepClick(lv_event_t *e);
};

extern QuickPanel g_quick_panel;