idf_component_register(
    SRCS
        main.cpp
        app.cpp
        diag.cpp
        SupabaseClient.cpp
        ProductService.cpp
        ProductsManager.cpp
        RecipeService.cpp
        RecipesManager.cpp
        CookbookService.cpp
        CookbooksManager.cpp
        WiFiSettingsUI.cpp
        WiFiManager.cpp
        ui_extensions.cpp
        ui_extensions_products.cpp
        ui_extensions_cookbooks.cpp
        thumbnail_cache.cpp
        thumbnail_manager.cpp
        spiffs_cleanup.cpp
        display_sleep.cpp
        quick_panel.cpp
        battery.cpp
        ui_theme.cpp
        ui/screens.cpp
        ui/images.c
        ui/actions.cpp
        ui/ui.c
        ui/ui_image_shopping.c
        ui/ui_image_chef.c
        ui/ui_image_restaurant.c
        ui/ui_font_ext_font_montserrat_18.c
        ui/ui_font_ext_font_montserrat_36.c
        ui/ui_font_ext_font_montserrat_26.c
        ui/ui_image_snowflake.c
        ui/ui_image_favourite_add.c
        ui/ui_image_favourite_remove.c
        ui/ui_image_favourite.c
        ui/ui_image_settings.c
        ui/ui_image_baby.c
        ui/ui_image_meat.c
        ui/ui_image_produce.c
        ui/ui_image_condiment.c
        ui/ui_image_bakery.c
        ui/ui_image_dairy.c
        ui/ui_image_wine.c
        ui/ui_image_other.c
        ui/ui_image_snacks.c
    INCLUDE_DIRS
    .
    ui)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)

# Diagnostic: route every esp_cache_msync() call through __wrap_esp_cache_msync
# in diag.cpp so the next post-mortem shows whether the freeze happened inside
# a cache writeback (and how long those interrupts-off windows get). The
# esp_restart entries are wrapped for the same reason: an ESP_RST_SW boot is a
# deliberate software reset, and their wrappers record the calling PC.
#
# xt_unhandled_exception is the port entry for synchronous faults; its wrapper
# copies the full trap frame, the faulting task identity and a window of its
# stack into RTC RAM before the panic handler can wedge. The three panic.c
# entry points are wrapped to record how far the panic path got before a reset
# (RTC WDT armed, TIMG WDTs disabled, system panic handler reached).
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_cache_msync"
    "-Wl,--wrap=esp_restart"
    "-Wl,--wrap=esp_restart_noos"
    "-Wl,--wrap=esp_panic_handler_increment_entry_count"
    "-Wl,--wrap=xt_unhandled_exception"
    "-Wl,--wrap=esp_panic_handler_enable_rtc_wdt"
    "-Wl,--wrap=esp_panic_handler_disable_timg_wdts"
    "-Wl,--wrap=esp_panic_handler"
    # The blend guard needs to see every LVGL draw buffer from the moment it is
    # created, and every software blend before it stores pixels. See blendGuard
    # in diag.cpp.
    "-Wl,--wrap=lv_draw_buf_init"
    "-Wl,--wrap=lv_draw_buf_create"
    "-Wl,--wrap=lv_draw_buf_create_ex"
    "-Wl,--wrap=lv_draw_buf_destroy"
    "-Wl,--wrap=lv_draw_sw_blend")
