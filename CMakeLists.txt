# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)
project(kitchen_assistant)

# LVGL: Optimize for size (rendering is already fast, size matters more)
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Os -ffunction-sections -fdata-sections)

# LCD driver: Optimize for size (only runs during init, not performance-critical)
idf_component_get_property(lcd_lib esp_lcd_jd9365 COMPONENT_LIB)
target_compile_options(${lcd_lib} PRIVATE -Os)