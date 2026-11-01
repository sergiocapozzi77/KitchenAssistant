#include "display_sleep.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"                    // for bsp_display_backlight_on/off
#include "bsp/esp32_p4_function_ev_board.h" // for BSP_LCD_TOUCH_INT

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

void DisplaySleep::sleepUntilTouch()
{
    if (_sleeping)
        return;
    _sleeping = true;
    _explicit_sleep = false;
    lv_timer_pause(_timer);

    bsp_display_backlight_off();
    ESP_LOGI(TAG, "sleeping (light sleep, wake on touch)");

    if (BSP_LCD_TOUCH_INT == GPIO_NUM_NC)
    {
        // No touch interrupt on this panel — behave like a normal display
        // sleep; the touch read callback will wake us.
        ESP_LOGW(TAG, "no touch interrupt wired, display sleep only");
        return;
    }

    // Let the finger lift before arming the wake source, otherwise the touch
    // that pressed the button would immediately wake the chip again.
    vTaskDelay(pdMS_TO_TICKS(300));

    // The GSL3680 asserts INT LOW while the panel is touched.
    esp_err_t err = gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK)
        err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK)
    {
        // Without a wake source light sleep would never return, so stay in
        // display sleep and let the touch input callback wake us instead.
        ESP_LOGE(TAG, "touch wake setup failed: %s", esp_err_to_name(err));
        return;
    }

    esp_light_sleep_start();

    wake();
    ESP_LOGI(TAG, "woke from light sleep");
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