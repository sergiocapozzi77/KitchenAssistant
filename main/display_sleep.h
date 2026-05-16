#pragma once
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "lvgl.h"

class DisplaySleep
{
public:
    void init(uint32_t timeout_ms = 30000);

    void sleep();
    void wake();
    void sleepWithoutWake(); ///< Like sleep() but suppresses wake from lingering touch samples
    void onTouch();          // call from touch_read_cb on every press
    bool isSleeping() const { return _sleeping; }

private:
    lv_timer_t *_timer = nullptr;

    esp_lcd_panel_handle_t _panel = nullptr;
    gpio_num_t _bl_gpio = GPIO_NUM_NC;
    bool _sleeping = false;
    bool _explicit_sleep = false;
    uint32_t _timeout_ms = 0;
    void resetTimer();

    static void onTimerExpired(lv_timer_t *t);
    static void onWakeUnblock(lv_timer_t *t);
};

extern DisplaySleep g_display_sleep;