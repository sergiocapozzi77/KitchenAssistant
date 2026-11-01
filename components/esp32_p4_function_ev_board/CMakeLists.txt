
idf_component_register(
    SRCS "esp32_p4_function_ev_board.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    # ESP-IDF v6 no longer re-exports the split drivers from the legacy
    # `driver` component, so name the ones used by the BSP directly.
    # `usb` (USB Host) is no longer an IDF component either; it is pulled in
    # from the registry via idf_component.yml.
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_sdmmc
    PRIV_REQUIRES esp_lcd spiffs fatfs
)
