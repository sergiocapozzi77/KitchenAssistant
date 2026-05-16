#include "display_sleep.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h" // for bsp_display_backlight_on/off

static const char *TAG = "DisplaySleep";

DisplaySleep g_display_sleep;

void DisplaySleep::init(uint32_t timeout_ms)
{
    _timeout_ms = timeout_ms;
    _timer = lv_timer_create(onTimerExpired, timeout_ms, this);
    lv_timer_set_repeat_count(_timer, -1); // infinite — we own the lifecycle
    lv_timer_pause(_timer);
    lv_timer_reset(_timer);
}

void DisplaySleep::sleep()
{
    if (_sleeping)
        return;
    _sleeping = true;
    _explicit_sleep = false; // inactivity timeout — normal wake-on-touch
    lv_timer_pause(_timer);

    bsp_display_backlight_off();

    ESP_LOGI(TAG, "sleeping");
}

void DisplaySleep::sleepWithoutWake()
{
    if (_sleeping)
        return;
    _sleeping = true;
    _explicit_sleep = true; // user pressed Sleep button
    lv_timer_pause(_timer);

    bsp_display_backlight_off();

    // Clear the flag after 500 ms so the user can wake by touching again
    lv_timer_t *t = lv_timer_create(onWakeUnblock, 500, this);
    lv_timer_set_repeat_count(t, 1);

    ESP_LOGI(TAG, "sleeping (explicit — wake suppressed for 500ms)");
}

void DisplaySleep::wake()
{
    if (!_sleeping)
        return;
    _sleeping = false;

    bsp_display_backlight_on();
    // lv_obj_invalidate(lv_scr_act());

    resetTimer();
    ESP_LOGI(TAG, "awake");
}

void DisplaySleep::onTouch()
{
    if (_sleeping)
    {
        if (_explicit_sleep)
            return; // still within the post-button wake-suppression window
        wake();
        return;
    }
    resetTimer();
}

void DisplaySleep::resetTimer()
{
    lv_timer_reset(_timer);
    lv_timer_resume(_timer);
}

void DisplaySleep::onTimerExpired(lv_timer_t *t)
{
    static_cast<DisplaySleep *>(lv_timer_get_user_data(t))->sleep();
}

void DisplaySleep::onWakeUnblock(lv_timer_t *t)
{
    static_cast<DisplaySleep *>(lv_timer_get_user_data(t))->_explicit_sleep = false;
}