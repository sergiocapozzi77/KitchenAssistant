/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lv_demos.h"
#include "ui.h"

void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = false,
        }};
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Initialize EEZ Studio UI once
    bsp_display_lock(0);
    ui_init();
    bsp_display_unlock();

    // Continuous update loop
    while (1)
    {
        bsp_display_lock(0);
        ui_tick();
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz update rate
    }
}
