idf_component_register(
    SRCS 
        main.cpp
        app.cpp
        ProductService.cpp
        WifiManager.cpp
        ui_extensions.cpp
        ui/screens.c
        ui/styles.c
        ui/images.c
        ui/ui.c
    INCLUDE_DIRS 
    . 
    ui)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)