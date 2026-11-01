# WHOLE_ARCHIVE: lv_mem_custom.cpp supplies LVGL's allocator hooks
# (lv_malloc_core, lv_mem_init, ...). The generated link line scans libmain.a
# once but liblvgl__lvgl.a several times, and the LVGL copy scanned after
# libmain.a pulls lv_init.c.obj / lv_mem.c.obj, which reference those hooks.
# Without --whole-archive those references go unresolved at the final link.
idf_component_register(
    SRCS
        main.cpp
        app.cpp
        lv_mem_custom.cpp
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
    ui
    WHOLE_ARCHIVE)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)