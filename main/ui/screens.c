#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 1280);
    lv_obj_add_event_cb(obj, action_screen_loading, LV_EVENT_SCREEN_LOAD_START, (void *)0);
    {
        lv_obj_t *parent_obj = obj;
        {
            // tabview
            lv_obj_t *obj = lv_tabview_create(parent_obj);
            objects.tabview = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
            lv_tabview_set_tab_bar_position(obj, LV_DIR_TOP);
            lv_tabview_set_tab_bar_size(obj, 60);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "\uF015 Products");
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                            lv_obj_set_style_layout(obj, LV_LAYOUT_GRID, LV_PART_MAIN | LV_STATE_DEFAULT);
                            {
                                static lv_coord_t dsc[] = {80, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
                                lv_obj_set_style_grid_row_dsc_array(obj, dsc, LV_PART_MAIN | LV_STATE_DEFAULT);
                            }
                            {
                                static lv_coord_t dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
                                lv_obj_set_style_grid_column_dsc_array(obj, dsc, LV_PART_MAIN | LV_STATE_DEFAULT);
                            }
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // productsHeader_pnl
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    objects.products_header_pnl = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                                    lv_obj_set_style_grid_cell_row_pos(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_y_align(obj, LV_GRID_ALIGN_STRETCH, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // product_filter_dropdown
                                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                                            objects.product_filter_dropdown = obj;
                                            lv_obj_set_pos(obj, 452, -5);
                                            lv_obj_set_size(obj, 149, LV_SIZE_CONTENT);
                                            lv_dropdown_set_options(obj, "Show All\nExpirying");
                                            lv_dropdown_set_selected(obj, 0);
                                            lv_obj_add_event_cb(obj, action_product_filter_change, LV_EVENT_VALUE_CHANGED, (void *)0);
                                            add_style_drop_down_with_shadow(obj);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // product_sort_dropdown
                                            lv_obj_t *obj = lv_dropdown_create(parent_obj);
                                            objects.product_sort_dropdown = obj;
                                            lv_obj_set_pos(obj, 617, -5);
                                            lv_obj_set_size(obj, 146, LV_SIZE_CONTENT);
                                            lv_dropdown_set_options(obj, "Sort A-Z\nBy Expiry");
                                            lv_dropdown_set_selected(obj, 0);
                                            lv_obj_add_event_cb(obj, action_product_sort_value_changed, LV_EVENT_VALUE_CHANGED, (void *)0);
                                            add_style_drop_down_with_shadow(obj);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // products_reload_btn
                                            lv_obj_t *obj = lv_button_create(parent_obj);
                                            objects.products_reload_btn = obj;
                                            lv_obj_set_pos(obj, -6, -7);
                                            lv_obj_set_size(obj, 50, 50);
                                            lv_obj_add_event_cb(obj, action_products_reload_click, LV_EVENT_CLICKED, (void *)0);
                                            add_style_main_button(obj);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "\uF021");
                                                }
                                            }
                                        }
                                    }
                                }
                                {
                                    // products_list
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    objects.products_list = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
                                    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
                                    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
                                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_ANY | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_ANY | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_row_pos(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_y_align(obj, LV_GRID_ALIGN_STRETCH, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // createRecipe_pnl
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    objects.create_recipe_pnl = obj;
                                    lv_obj_set_pos(obj, -15, -147);
                                    lv_obj_set_size(obj, LV_PCT(100), 347);
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                                    lv_obj_set_style_radius(obj, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_color(obj, lv_color_hex(theme_colors[active_theme_index][2]), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_y_align(obj, LV_GRID_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_row_pos(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_left(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_right(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_bottom(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            // productSelected_lbl
                                            lv_obj_t *obj = lv_label_create(parent_obj);
                                            objects.product_selected_lbl = obj;
                                            lv_obj_set_pos(obj, 3, 6);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Product selected");
                                        }
                                        {
                                            // products_filters_panel
                                            lv_obj_t *obj = lv_obj_create(parent_obj);
                                            objects.products_filters_panel = obj;
                                            lv_obj_set_pos(obj, -9, 55);
                                            lv_obj_set_size(obj, 746, 195);
                                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            create_user_widget_filters_panel(obj, 10);
                                        }
                                        {
                                            lv_obj_t *obj = lv_button_create(parent_obj);
                                            lv_obj_set_pos(obj, 5, 261);
                                            lv_obj_set_size(obj, 350, 50);
                                            add_style_main_button(obj);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "AI RECIPE");
                                                }
                                            }
                                        }
                                        {
                                            // generate_recipe_btn
                                            lv_obj_t *obj = lv_button_create(parent_obj);
                                            objects.generate_recipe_btn = obj;
                                            lv_obj_set_pos(obj, 378, 261);
                                            lv_obj_set_size(obj, 350, 50);
                                            lv_obj_add_event_cb(obj, action_generate_recipe_click, LV_EVENT_CLICKED, (void *)0);
                                            add_style_main_button(obj);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "RECIPE");
                                                }
                                            }
                                        }
                                    }
                                }
                                {
                                    lv_obj_t *obj = lv_keyboard_create(parent_obj);
                                    objects.obj3 = obj;
                                    lv_obj_set_pos(obj, 0, 14294);
                                    lv_obj_set_size(obj, 800, 299);
                                    lv_obj_set_style_align(obj, LV_ALIGN_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_row_pos(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            // product_edit_modal
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            objects.product_edit_modal = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
                            lv_obj_set_style_bg_opa(obj, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff646464), LV_PART_MAIN | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // product_edit
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    objects.product_edit = obj;
                                    lv_obj_set_pos(obj, 50, 240);
                                    lv_obj_set_size(obj, 700, 800);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    create_user_widget_product_edit(obj, 21);
                                }
                            }
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Recipes");
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_obj_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                            lv_obj_set_style_layout(obj, LV_LAYOUT_GRID, LV_PART_MAIN | LV_STATE_DEFAULT);
                            {
                                static lv_coord_t dsc[] = {80, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
                                lv_obj_set_style_grid_row_dsc_array(obj, dsc, LV_PART_MAIN | LV_STATE_DEFAULT);
                            }
                            {
                                static lv_coord_t dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
                                lv_obj_set_style_grid_column_dsc_array(obj, dsc, LV_PART_MAIN | LV_STATE_DEFAULT);
                            }
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // recipes_header_pnl
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    objects.recipes_header_pnl = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                                    lv_obj_set_style_grid_cell_row_pos(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_y_align(obj, LV_GRID_ALIGN_STRETCH, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t *parent_obj = obj;
                                        {
                                            lv_obj_t *obj = lv_button_create(parent_obj);
                                            lv_obj_set_pos(obj, 681, -12);
                                            lv_obj_set_size(obj, 71, 55);
                                            add_style_flat_button(obj);
                                            lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    objects.obj0 = obj;
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff0d0c0c), LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "\uF078");
                                                }
                                            }
                                        }
                                        {
                                            // recipe_suggestion_prev_btn
                                            lv_obj_t *obj = lv_button_create(parent_obj);
                                            objects.recipe_suggestion_prev_btn = obj;
                                            lv_obj_set_pos(obj, 45, -7);
                                            lv_obj_set_size(obj, 156, 50);
                                            lv_obj_add_event_cb(obj, action_recipe_suggestion_prev, LV_EVENT_CLICKED, (void *)0);
                                            add_style_main_button(obj);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "PREV");
                                                }
                                            }
                                        }
                                        {
                                            // recipe_suggestion_next_btn
                                            lv_obj_t *obj = lv_button_create(parent_obj);
                                            objects.recipe_suggestion_next_btn = obj;
                                            lv_obj_set_pos(obj, 211, -6);
                                            lv_obj_set_size(obj, 156, 50);
                                            lv_obj_add_event_cb(obj, action_recipe_suggestion_next, LV_EVENT_CLICKED, (void *)0);
                                            add_style_main_button(obj);
                                            {
                                                lv_obj_t *parent_obj = obj;
                                                {
                                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                                    lv_obj_set_pos(obj, 0, 0);
                                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                                                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                                    lv_label_set_text(obj, "NEXT");
                                                }
                                            }
                                        }
                                    }
                                }
                                {
                                    // recipes_list
                                    lv_obj_t *obj = lv_obj_create(parent_obj);
                                    objects.recipes_list = obj;
                                    lv_obj_set_pos(obj, 0, 0);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
                                    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
                                    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
                                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_ANY | LV_STATE_DEFAULT);
                                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_ANY | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_row_pos(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_grid_cell_y_align(obj, LV_GRID_ALIGN_STRETCH, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_margin_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                    }
                }
            }
        }
        {
            // spinner
            lv_obj_t *obj = lv_spinner_create(parent_obj);
            objects.spinner = obj;
            lv_obj_set_pos(obj, 360, 600);
            lv_obj_set_size(obj, 80, 80);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
        {
            // snackbar
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.snackbar = obj;
            lv_obj_set_pos(obj, 31, 1132);
            lv_obj_set_size(obj, 743, 95);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff343434), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 240, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    objects.obj1 = obj;
                    lv_obj_set_pos(obj, 602, 3);
                    lv_obj_set_size(obj, 100, 50);
                    lv_obj_add_event_cb(obj, action_snack_bar_hide_clicked, LV_EVENT_CLICKED, (void *)0);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff777777), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(obj, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "HIDE");
                        }
                    }
                }
                {
                    // snackbar_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.snackbar_text = obj;
                    lv_obj_set_pos(obj, 51, 13);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "This is a test message");
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj2 = obj;
                    lv_obj_set_pos(obj, 0, 13);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "\uF06E");
                }
            }
        }
    }
    lv_keyboard_set_textarea(objects.obj3, ((lv_obj_t **)&objects)[10]);
    
    tick_screen_main();
}

void tick_screen_main() {
    tick_user_widget_filters_panel(10);
    tick_user_widget_product_edit(21);
}

void create_user_widget_product_edit(lv_obj_t *parent_obj, int startWidgetIndex) {
    (void)startWidgetIndex;
    lv_obj_t *obj = parent_obj;
    {
        lv_obj_t *parent_obj = obj;
        {
            // product_edit_panel
            lv_obj_t *obj = lv_obj_create(parent_obj);
            ((lv_obj_t **)&objects)[startWidgetIndex + 0] = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
            lv_obj_add_event_cb(obj, action_edit_product_panel_clicked, LV_EVENT_CLICKED, (void *)0);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // product_edit_close_btn
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 1] = obj;
                    lv_obj_set_pos(obj, 598, -5);
                    lv_obj_set_size(obj, 60, 60);
                    lv_obj_add_event_cb(obj, action_product_edit_close, LV_EVENT_CLICKED, (void *)0);
                    add_style_main_button(obj);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "\uF00D");
                        }
                    }
                }
                {
                    // product_edit_title_lbl
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 2] = obj;
                    lv_obj_set_pos(obj, 20, 25);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff495057), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Edit Product");
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 3] = obj;
                    lv_obj_set_pos(obj, 20, 100);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff495057), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Product Name");
                }
                {
                    // product_edit_name_ta
                    lv_obj_t *obj = lv_textarea_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 4] = obj;
                    lv_obj_set_pos(obj, 20, 135);
                    lv_obj_set_size(obj, 627, 42);
                    lv_textarea_set_max_length(obj, 128);
                    lv_textarea_set_placeholder_text(obj, "Enter product name...");
                    lv_textarea_set_one_line(obj, true);
                    lv_textarea_set_password_mode(obj, false);
                    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                    lv_obj_set_style_text_font(obj, &ui_font_ext_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 5] = obj;
                    lv_obj_set_pos(obj, 20, 230);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff495057), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Expiry Date");
                }
                {
                    // product_edit_expiry_ta
                    lv_obj_t *obj = lv_textarea_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 6] = obj;
                    lv_obj_set_pos(obj, 20, 265);
                    lv_obj_set_size(obj, 568, 48);
                    lv_textarea_set_max_length(obj, 128);
                    lv_textarea_set_placeholder_text(obj, "YYYY-MM-DD");
                    lv_textarea_set_one_line(obj, true);
                    lv_textarea_set_password_mode(obj, false);
                    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 7] = obj;
                    lv_obj_set_pos(obj, 20, 360);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff495057), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Category");
                }
                {
                    // product_edit_category_dd
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 8] = obj;
                    lv_obj_set_pos(obj, 20, 395);
                    lv_obj_set_size(obj, 627, 60);
                    lv_dropdown_set_options(obj, "Baby\nBakery\nBeverages\nBreakfast & Cereal\nCondiments & Dressing\nCooking & Baking\nDairy\nDeli\nFrozen Foods\nGrains\nPasta & Sides\nHealth & Personal Care\nHousehold & Cleaning\nMeat\nPet Supplies\nProduce\nSeafood\nSnacks\nSoups & Canned Food\nWine, Beer & Spirit\nOther");
                    lv_dropdown_set_dir(obj, LV_DIR_BOTTOM);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_RIGHT);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // product_edit_save_btn
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 9] = obj;
                    lv_obj_set_pos(obj, -3, 686);
                    lv_obj_set_size(obj, 310, 70);
                    lv_obj_add_event_cb(obj, action_product_edit_save, LV_EVENT_CLICKED, (void *)0);
                    add_style_main_button(obj);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "SAVE");
                        }
                    }
                }
                {
                    // product_edit_cancel_btn
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 10] = obj;
                    lv_obj_set_pos(obj, 328, 686);
                    lv_obj_set_size(obj, 330, 70);
                    lv_obj_add_event_cb(obj, action_product_edit_close, LV_EVENT_CLICKED, (void *)0);
                    add_style_main_button(obj);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "CANCEL");
                        }
                    }
                }
                {
                    // product_edit_selectdate
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 11] = obj;
                    lv_obj_set_pos(obj, 599, 265);
                    lv_obj_set_size(obj, 48, 48);
                    lv_obj_add_event_cb(obj, action_product_edit_calendar, LV_EVENT_CLICKED, (void *)0);
                    add_style_main_button(obj);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text(obj, "\uF304");
                        }
                    }
                }
                {
                    // calendar_editproduct
                    lv_obj_t *obj = lv_calendar_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 12] = obj;
                    lv_obj_set_pos(obj, 20, 313);
                    lv_obj_set_size(obj, 428, 352);
                    lv_calendar_add_header_arrow(obj);
                    lv_calendar_set_today_date(obj, 2022, 11, 1);
                    lv_calendar_set_month_shown(obj, 2022, 11);
                    lv_obj_add_event_cb(obj, action_product_calendar_value_changed, LV_EVENT_VALUE_CHANGED, (void *)0);
                    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}

void tick_user_widget_product_edit(int startWidgetIndex) {
    (void)startWidgetIndex;
}

void create_user_widget_filters_panel(lv_obj_t *parent_obj, int startWidgetIndex) {
    (void)startWidgetIndex;
    lv_obj_t *obj = parent_obj;
    {
        lv_obj_t *parent_obj = obj;
        {
            // filters_panel
            lv_obj_t *obj = lv_obj_create(parent_obj);
            ((lv_obj_t **)&objects)[startWidgetIndex + 1] = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 746, 200);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // keywords_text
                    lv_obj_t *obj = lv_textarea_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 0] = obj;
                    lv_obj_set_pos(obj, 3, -3);
                    lv_obj_set_size(obj, 460, 42);
                    lv_textarea_set_max_length(obj, 128);
                    lv_textarea_set_placeholder_text(obj, "Keywords...");
                    lv_textarea_set_one_line(obj, true);
                    lv_textarea_set_password_mode(obj, false);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_text_area_with_shadow(obj);
                }
                {
                    // meal_type_dropdown
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 2] = obj;
                    lv_obj_set_pos(obj, 3, 60);
                    lv_obj_set_size(obj, 220, 40);
                    lv_dropdown_set_options(obj, "Meal Type\nAfternoon tea\nBreakfast\nBrunch\nBuffet\nCanapes\nCondiment\nDessert\nDinner\nFish Course\nLunch\nMain course\nPasta\nSide dish\nSnack\nSoup\nStarter\nSupper\nTreat\nVegetable");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_UP);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_drop_down_with_shadow(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // total_time_dropdown
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 3] = obj;
                    lv_obj_set_pos(obj, 243, 60);
                    lv_obj_set_size(obj, 220, 40);
                    lv_dropdown_set_options(obj, "Total Time\nUnder 15 minutes\nUnder 30 minutes\nUnder 45 minutes\nUnder 1 hour\n1 hour or more");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_UP);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_drop_down_with_shadow(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // diet_dropdown
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 4] = obj;
                    lv_obj_set_pos(obj, 483, 60);
                    lv_obj_set_size(obj, 220, 40);
                    lv_dropdown_set_options(obj, "Diets\nHealthy\nGluten-free\nVegetarian\nEgg-free\nNut-free\nDairy-free\nHigh-protein\nVegan\nLow sugar\nHigh-fibre\nLow calorie\nKeto\nLow fat\nLow carb");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_UP);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_drop_down_with_shadow(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // difficulty_dropdown
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 5] = obj;
                    lv_obj_set_pos(obj, 3, 120);
                    lv_obj_set_size(obj, 220, 40);
                    lv_dropdown_set_options(obj, "Difficulty\nEasy\nMore effort\nA challenge");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_UP);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_drop_down_with_shadow(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // cuisine_dropdown
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 6] = obj;
                    lv_obj_set_pos(obj, 243, 120);
                    lv_obj_set_size(obj, 220, 40);
                    lv_dropdown_set_options(obj, "Cuisine\nAfrican\nAmerican\nAsian\nAustralian\nAzerbaijan\nBrazilian\nBritish\nCajun & Creole\nCaribbean\nChinese\nEastern European\nEgyptian\nEnglish\nFrench\nGerman\nGreek\nIndian\nIndonesian\nIrish\nItalian\nJapanese\nKorean\nLatin American\nMediterranean\nMexican\nMiddle Eastern\nMoroccan\nNorth African\nPortuguese\nScandinavian\nScottish\nSouthern & Soul\nSpanish\nSwedish\nThai\nTunisian\nTurkish\nVietnamese");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_UP);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_drop_down_with_shadow(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // calories_dropdown
                    lv_obj_t *obj = lv_dropdown_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 7] = obj;
                    lv_obj_set_pos(obj, 483, 120);
                    lv_obj_set_size(obj, 220, 40);
                    lv_dropdown_set_options(obj, "Calories\nUp to 250 kcal\nUp to 500 kcal\nUp to 750 kcal\nUp to 1000 kcal\nUp to 1250 kcal\nUp to 1500 kcal");
                    lv_dropdown_set_dir(obj, LV_DIR_TOP);
                    lv_dropdown_set_symbol(obj, LV_SYMBOL_UP);
                    lv_dropdown_set_selected(obj, 0);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    add_style_drop_down_with_shadow(obj);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
            }
        }
    }
}

void tick_user_widget_filters_panel(int startWidgetIndex) {
    (void)startWidgetIndex;
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
    { "ext_font_montserrat_18", &ui_font_ext_font_montserrat_18 },
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;
void change_color_theme(uint32_t theme_index) {
    active_theme_index = theme_index;
    
    {
        lv_obj_set_style_border_color(objects.create_recipe_pnl, lv_color_hex(theme_colors[theme_index][2]), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_style_set_border_color(get_style_drop_down_with_shadow_MAIN_DEFAULT(), lv_color_hex(theme_colors[theme_index][2]));
    lv_style_set_bg_color(get_style_main_button_MAIN_DEFAULT(), lv_color_hex(theme_colors[theme_index][0]));
    lv_style_set_bg_grad_color(get_style_main_button_MAIN_DEFAULT(), lv_color_hex(theme_colors[theme_index][1]));
    lv_style_set_border_color(get_style_text_area_with_shadow_MAIN_DEFAULT(), lv_color_hex(theme_colors[theme_index][2]));
    lv_obj_invalidate(objects.main);
}
uint32_t theme_colors[1][3] = {
    { 0xff007aff, 0xff0159b7, 0xffb5b5b5 },
};

//
//
//

void create_screens() {

// Set default LVGL theme
    lv_display_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    create_screen_main();
}