idf_component_register(
    SRCS 
        main.cpp
        app.cpp
        ProductService.cpp
        ProductsManager.cpp
        RecipeGoodFoodService.cpp
        WifiManager.cpp
        ui_extensions.cpp
        ui/screens.c
        ui/styles.c
        ui/images.c
        ui/actions.cpp
        ui/ui.c
        ui/ui_image_shopping.c
        ui/ui_image_chef.c
    INCLUDE_DIRS 
    . 
    ui)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)