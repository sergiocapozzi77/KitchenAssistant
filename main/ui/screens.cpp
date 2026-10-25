#include "screens.h"

#include "actions.h"
#include "ui_theme.h"

#include <cstdint>
#include <string>

objects_t objects = {};

static lv_obj_t *make_screen()
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_PAGE_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

static lv_obj_t *make_row(lv_obj_t *parent, int height)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static void style_tab_bar_for(lv_obj_t *tabview)
{
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_COLOR_CARD_BORDER), 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);

    uint32_t active = lv_tabview_get_tab_active(tabview);
    uint32_t count = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < count; ++i)
    {
        lv_obj_t *button = lv_obj_get_child(bar, i);
        bool is_active = (i == active);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_SURFACE_HIGHEST), 0);
        lv_obj_set_style_bg_opa(button, is_active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        if (label)
        {
            lv_obj_set_style_text_font(label, ui_theme::font_small, 0);
            lv_obj_set_style_text_color(
                label, lv_color_hex(is_active ? UI_COLOR_ACCENT : UI_COLOR_TEXT_MUTED), 0);
        }
    }
}

static void tab_changed_cb(lv_event_t *event)
{
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    style_tab_bar_for(target);

    if (target == objects.tabview && objects.tabview &&
        lv_tabview_get_tab_active(objects.tabview) == 1)
        request_cookbooks_load();
}

static void textarea_focus_cb(lv_event_t *e)
{
    if (!objects.keyboard)
        return;
    lv_obj_t *textarea = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_keyboard_set_textarea(objects.keyboard, textarea);
    lv_obj_clear_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(objects.keyboard);
}

static void keyboard_done_cb(lv_event_t *)
{
    if (objects.keyboard)
        lv_obj_add_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void create_inventory_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_pad_row(tab, 0, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(tab);
    objects.products_header_pnl = header;
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_left(header, 24, 0);
    lv_obj_set_style_pad_right(header, 24, 0);
    lv_obj_set_style_pad_top(header, 24, 0);
    lv_obj_set_style_pad_bottom(header, 12, 0);
    lv_obj_set_style_pad_row(header, 16, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title_row = make_row(header, 72);
    objects.products_title_lbl = ui_theme::make_label(
        title_row, "Your Pantry & Fridge", ui_theme::font_display,
        UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_flex_grow(objects.products_title_lbl, 1);
    objects.products_reload_btn = ui_theme::make_icon_button(
        title_row, LV_SYMBOL_REFRESH, 72);
    lv_obj_add_event_cb(objects.products_reload_btn, action_products_reload,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *controls = lv_obj_create(header);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, 64);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_style_pad_column(controls, 12, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    objects.product_search_btn = ui_theme::make_icon_button(
        controls, LV_SYMBOL_SEARCH, 64);
    lv_obj_add_event_cb(objects.product_search_btn, action_product_search_open,
                        LV_EVENT_CLICKED, NULL);

    objects.product_search_ta = lv_textarea_create(controls);
    lv_obj_set_size(objects.product_search_ta, 260, 64);
    lv_textarea_set_one_line(objects.product_search_ta, true);
    lv_textarea_set_placeholder_text(objects.product_search_ta, "Search products...");
    ui_theme::style_field(objects.product_search_ta);
    lv_obj_add_flag(objects.product_search_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(objects.product_search_ta, action_product_search_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(objects.product_search_ta, textarea_focus_cb,
                        LV_EVENT_FOCUSED, NULL);

    objects.product_filter_pill = lv_obj_create(controls);
    lv_obj_set_size(objects.product_filter_pill, 180, 64);
    ui_theme::style_pill(objects.product_filter_pill);
    lv_obj_add_flag(objects.product_filter_pill, LV_OBJ_FLAG_CLICKABLE);
    objects.product_filter_lbl = ui_theme::make_label(
        objects.product_filter_pill, "Filter: All", ui_theme::font_body,
        UI_COLOR_TEXT_SECONDARY);
    lv_obj_center(objects.product_filter_lbl);
    lv_obj_add_event_cb(objects.product_filter_pill, action_product_filter_toggle,
                        LV_EVENT_CLICKED, NULL);

    objects.product_sort_pill = lv_obj_create(controls);
    lv_obj_set_size(objects.product_sort_pill, 180, 64);
    ui_theme::style_pill(objects.product_sort_pill);
    lv_obj_add_flag(objects.product_sort_pill, LV_OBJ_FLAG_CLICKABLE);
    objects.product_sort_lbl = ui_theme::make_label(
        objects.product_sort_pill, "Sort: A-Z", ui_theme::font_body,
        UI_COLOR_TEXT_SECONDARY);
    lv_obj_center(objects.product_sort_lbl);
    lv_obj_add_event_cb(objects.product_sort_pill, action_product_sort_toggle,
                        LV_EVENT_CLICKED, NULL);

    objects.product_filter_chips = lv_obj_create(header);
    lv_obj_set_width(objects.product_filter_chips, LV_PCT(100));
    lv_obj_set_height(objects.product_filter_chips, 56);
    lv_obj_set_style_bg_opa(objects.product_filter_chips, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(objects.product_filter_chips, 0, 0);
    lv_obj_set_style_pad_all(objects.product_filter_chips, 0, 0);
    lv_obj_set_style_pad_column(objects.product_filter_chips, 12, 0);
    lv_obj_set_flex_flow(objects.product_filter_chips, LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(objects.product_filter_chips, LV_OBJ_FLAG_HIDDEN);

    objects.product_chip_all = ui_theme::make_chip(
        objects.product_filter_chips, "All", true);
    objects.product_chip_expiring = ui_theme::make_chip(
        objects.product_filter_chips, "Expiring soon", false);
    objects.product_chip_expired = ui_theme::make_chip(
        objects.product_filter_chips, "Expired", false);
    lv_obj_add_event_cb(objects.product_chip_all, action_product_filter_chip,
                        LV_EVENT_CLICKED, (void *)(intptr_t)ProductFilter::All);
    lv_obj_add_event_cb(objects.product_chip_expiring, action_product_filter_chip,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)ProductFilter::ExpiringSoon);
    lv_obj_add_event_cb(objects.product_chip_expired, action_product_filter_chip,
                        LV_EVENT_CLICKED, (void *)(intptr_t)ProductFilter::Expired);

    objects.product_sort_chips = lv_obj_create(header);
    lv_obj_set_width(objects.product_sort_chips, LV_PCT(100));
    lv_obj_set_height(objects.product_sort_chips, 56);
    lv_obj_set_style_bg_opa(objects.product_sort_chips, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(objects.product_sort_chips, 0, 0);
    lv_obj_set_style_pad_all(objects.product_sort_chips, 0, 0);
    lv_obj_set_style_pad_column(objects.product_sort_chips, 12, 0);
    lv_obj_set_flex_flow(objects.product_sort_chips, LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(objects.product_sort_chips, LV_OBJ_FLAG_HIDDEN);

    objects.product_sort_chip_az = ui_theme::make_chip(
        objects.product_sort_chips, "A-Z", true);
    objects.product_sort_chip_expiry = ui_theme::make_chip(
        objects.product_sort_chips, "By expiry", false);
    lv_obj_add_event_cb(objects.product_sort_chip_az, action_product_sort_chip,
                        LV_EVENT_CLICKED, (void *)(intptr_t)ProductSort::Name);
    lv_obj_add_event_cb(objects.product_sort_chip_expiry, action_product_sort_chip,
                        LV_EVENT_CLICKED, (void *)(intptr_t)ProductSort::Expiry);

    objects.products_list = lv_obj_create(tab);
    lv_obj_set_width(objects.products_list, LV_PCT(100));
    lv_obj_set_flex_grow(objects.products_list, 1);
    lv_obj_set_style_bg_opa(objects.products_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(objects.products_list, 0, 0);
    lv_obj_set_style_pad_left(objects.products_list, 24, 0);
    lv_obj_set_style_pad_right(objects.products_list, 24, 0);
    lv_obj_set_style_pad_top(objects.products_list, 8, 0);
    lv_obj_set_style_pad_bottom(objects.products_list, 24, 0);
    lv_obj_set_style_pad_row(objects.products_list, 16, 0);
    lv_obj_set_flex_flow(objects.products_list, LV_FLEX_FLOW_COLUMN);
}

static void create_cookbooks_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_pad_row(tab, 0, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(tab);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_left(header, 24, 0);
    lv_obj_set_style_pad_right(header, 24, 0);
    lv_obj_set_style_pad_top(header, 24, 0);
    lv_obj_set_style_pad_bottom(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title_row = make_row(header, 72);
    objects.cookbooks_title_lbl = ui_theme::make_label(
        title_row, "Cookbooks", ui_theme::font_display, UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_flex_grow(objects.cookbooks_title_lbl, 1);
    objects.cookbooks_reload_btn = ui_theme::make_icon_button(
        title_row, LV_SYMBOL_REFRESH, 72);
    lv_obj_add_event_cb(objects.cookbooks_reload_btn, action_cookbooks_reload,
                        LV_EVENT_CLICKED, NULL);

    objects.cookbooks_list = lv_obj_create(tab);
    lv_obj_set_width(objects.cookbooks_list, LV_PCT(100));
    lv_obj_set_flex_grow(objects.cookbooks_list, 1);
    lv_obj_set_style_bg_opa(objects.cookbooks_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(objects.cookbooks_list, 0, 0);
    lv_obj_set_style_pad_all(objects.cookbooks_list, 24, 0);
    lv_obj_set_style_pad_row(objects.cookbooks_list, 20, 0);
    lv_obj_set_style_pad_column(objects.cookbooks_list, 20, 0);
    lv_obj_set_flex_flow(objects.cookbooks_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(objects.cookbooks_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

static void create_settings_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 24, 0);
    lv_obj_set_style_pad_row(tab, 20, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    ui_theme::make_label(tab, "Settings", ui_theme::font_display,
                         UI_COLOR_TEXT_PRIMARY);

    lv_obj_t *card = lv_obj_create(tab);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, 240);
    ui_theme::style_card(card);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_pad_row(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    ui_theme::make_label(card, "WiFi network", ui_theme::font_title,
                         UI_COLOR_TEXT_PRIMARY);
    objects.current_wifi_lbl = ui_theme::make_label(
        card, "Not connected", ui_theme::font_body, UI_COLOR_TEXT_MUTED);

    objects.settings_wifi_btn = lv_button_create(card);
    lv_obj_set_size(objects.settings_wifi_btn, 260, 64);
    ui_theme::style_primary_button(objects.settings_wifi_btn);
    lv_obj_t *btn_label = ui_theme::make_label(
        objects.settings_wifi_btn, "WiFi settings", ui_theme::font_body, 0xFFFFFF);
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(objects.settings_wifi_btn, action_settings_wifi,
                        LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *make_meta_card(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, 92);
    ui_theme::style_card(card);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    ui_theme::make_label(card, caption, ui_theme::font_body,
                         UI_COLOR_TEXT_MUTED);
    return ui_theme::make_label(card, "-", ui_theme::font_title,
                                UI_COLOR_TEXT_PRIMARY);
}

static void create_cookbook_detail_screen()
{
    objects.cookbook_detail = make_screen();
    lv_obj_set_style_pad_all(objects.cookbook_detail, 0, 0);
    lv_obj_set_style_pad_row(objects.cookbook_detail, 0, 0);
    lv_obj_set_flex_flow(objects.cookbook_detail, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *top = lv_obj_create(objects.cookbook_detail);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 24, 0);
    lv_obj_set_style_pad_right(top, 24, 0);
    lv_obj_set_style_pad_top(top, 24, 0);
    lv_obj_set_style_pad_bottom(top, 12, 0);
    lv_obj_set_style_pad_column(top, 16, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    objects.cookbook_detail_back_btn = ui_theme::make_icon_button(
        top, LV_SYMBOL_LEFT, 72);
    lv_obj_add_event_cb(objects.cookbook_detail_back_btn, action_cookbook_back,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *titles = lv_obj_create(top);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_height(titles, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(titles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(titles, 0, 0);
    lv_obj_set_style_pad_all(titles, 0, 0);
    lv_obj_set_style_pad_row(titles, 4, 0);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);

    objects.cookbook_detail_title_lbl = ui_theme::make_label(
        titles, "Cookbook", ui_theme::font_display, UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_width(objects.cookbook_detail_title_lbl, LV_PCT(100));
    lv_label_set_long_mode(objects.cookbook_detail_title_lbl,
                           LV_LABEL_LONG_MODE_DOTS);
    objects.cookbook_detail_count_lbl = ui_theme::make_label(
        titles, "", ui_theme::font_body, UI_COLOR_TEXT_MUTED);

    objects.cookbook_detail_list = lv_obj_create(objects.cookbook_detail);
    lv_obj_set_width(objects.cookbook_detail_list, LV_PCT(100));
    lv_obj_set_flex_grow(objects.cookbook_detail_list, 1);
    lv_obj_set_style_bg_opa(objects.cookbook_detail_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(objects.cookbook_detail_list, 0, 0);
    lv_obj_set_style_pad_left(objects.cookbook_detail_list, 24, 0);
    lv_obj_set_style_pad_right(objects.cookbook_detail_list, 24, 0);
    lv_obj_set_style_pad_top(objects.cookbook_detail_list, 8, 0);
    lv_obj_set_style_pad_bottom(objects.cookbook_detail_list, 24, 0);
    lv_obj_set_style_pad_row(objects.cookbook_detail_list, 16, 0);
    lv_obj_set_flex_flow(objects.cookbook_detail_list, LV_FLEX_FLOW_COLUMN);

    objects.cookbook_detail_spinner = lv_spinner_create(objects.cookbook_detail);
    lv_obj_set_size(objects.cookbook_detail_spinner, 80, 80);
    lv_obj_align(objects.cookbook_detail_spinner, LV_ALIGN_CENTER, 0, 54);
    lv_obj_set_style_arc_color(objects.cookbook_detail_spinner,
                               lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(objects.cookbook_detail_spinner, LV_OBJ_FLAG_HIDDEN);

    objects.cookbook_detail_state_lbl = ui_theme::make_label(
        objects.cookbook_detail, "", ui_theme::font_title,
        UI_COLOR_TEXT_SECONDARY);
    lv_obj_align(objects.cookbook_detail_state_lbl, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(objects.cookbook_detail_state_lbl, LV_OBJ_FLAG_HIDDEN);

    objects.cookbook_detail_retry_btn = lv_button_create(objects.cookbook_detail);
    lv_obj_set_size(objects.cookbook_detail_retry_btn, 200, 64);
    ui_theme::style_primary_button(objects.cookbook_detail_retry_btn);
    lv_obj_t *retry_label = ui_theme::make_label(
        objects.cookbook_detail_retry_btn, "Retry", ui_theme::font_body,
        0xFFFFFF);
    lv_obj_center(retry_label);
    lv_obj_align(objects.cookbook_detail_retry_btn, LV_ALIGN_CENTER, 0, 100);
    lv_obj_add_flag(objects.cookbook_detail_retry_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(objects.cookbook_detail_retry_btn,
                        action_cookbook_retry, LV_EVENT_CLICKED, NULL);
}

static void create_recipe_detail_screen()
{
    objects.recipe_detail = make_screen();
    lv_obj_set_style_pad_all(objects.recipe_detail, 0, 0);
    lv_obj_set_style_pad_row(objects.recipe_detail, 0, 0);
    lv_obj_set_flex_flow(objects.recipe_detail, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hero = lv_obj_create(objects.recipe_detail);
    lv_obj_set_size(hero, 800, 420);
    lv_obj_set_style_bg_color(hero, lv_color_hex(UI_COLOR_SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hero, 0, 0);
    lv_obj_set_style_radius(hero, 0, 0);
    lv_obj_set_style_pad_all(hero, 0, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    objects.recipe_detail_hero = lv_image_create(hero);
    lv_obj_set_size(objects.recipe_detail_hero, 800, 420);
    lv_obj_set_style_bg_opa(objects.recipe_detail_hero, LV_OPA_TRANSP, 0);

    objects.recipe_detail_back_btn = ui_theme::make_icon_button(
        hero, LV_SYMBOL_LEFT, 72);
    lv_obj_align(objects.recipe_detail_back_btn, LV_ALIGN_TOP_LEFT, 24, 24);
    lv_obj_add_event_cb(objects.recipe_detail_back_btn, action_recipe_back,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *content = lv_obj_create(objects.recipe_detail);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    objects.recipe_detail_title_lbl = ui_theme::make_label(
        content, "", ui_theme::font_display, UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_width(objects.recipe_detail_title_lbl, LV_PCT(100));
    lv_label_set_long_mode(objects.recipe_detail_title_lbl,
                           LV_LABEL_LONG_MODE_DOTS);

    objects.recipe_detail_source_lbl = ui_theme::make_label(
        content, "", ui_theme::font_body, UI_COLOR_TEXT_MUTED);

    lv_obj_t *meta = lv_obj_create(content);
    lv_obj_set_width(meta, LV_PCT(100));
    lv_obj_set_height(meta, 92);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_set_style_pad_column(meta, 16, 0);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);

    objects.recipe_detail_time_lbl = make_meta_card(meta, "Total time");
    objects.recipe_detail_difficulty_lbl =
        make_meta_card(meta, "Difficulty");

    objects.recipe_detail_tabview = lv_tabview_create(content);
    lv_obj_set_width(objects.recipe_detail_tabview, LV_PCT(100));
    lv_obj_set_flex_grow(objects.recipe_detail_tabview, 1);
    lv_tabview_set_tab_bar_position(objects.recipe_detail_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(objects.recipe_detail_tabview, 92);

    objects.recipe_detail_ingredients_tab = lv_tabview_add_tab(
        objects.recipe_detail_tabview, "Ingredients");
    objects.recipe_detail_method_tab = lv_tabview_add_tab(
        objects.recipe_detail_tabview, "Method");

    for (lv_obj_t *page : {objects.recipe_detail_ingredients_tab,
                           objects.recipe_detail_method_tab})
    {
        lv_obj_set_style_pad_all(page, 24, 0);
        lv_obj_set_style_pad_row(page, 12, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    }

    lv_obj_add_event_cb(objects.recipe_detail_tabview, tab_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    style_tab_bar_for(objects.recipe_detail_tabview);
}

static lv_obj_t *create_text_field(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *field = lv_textarea_create(parent);
    lv_obj_set_width(field, LV_PCT(100));
    lv_obj_set_height(field, 64);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_placeholder_text(field, placeholder);
    ui_theme::style_field(field);
    lv_obj_add_event_cb(field, textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
    return field;
}

static void create_product_edit_modal()
{
    objects.product_edit_modal = lv_obj_create(objects.main);
    lv_obj_set_size(objects.product_edit_modal, 800, 1280);
    lv_obj_set_style_bg_color(objects.product_edit_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(objects.product_edit_modal, LV_OPA_40, 0);
    lv_obj_set_style_border_width(objects.product_edit_modal, 0, 0);
    lv_obj_set_style_radius(objects.product_edit_modal, 0, 0);
    lv_obj_set_style_pad_all(objects.product_edit_modal, 0, 0);
    lv_obj_add_flag(objects.product_edit_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(objects.product_edit_modal);
    objects.product_edit_panel = panel;
    lv_obj_set_width(panel, 640);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    ui_theme::style_card(panel);
    lv_obj_set_style_pad_all(panel, 24, 0);
    lv_obj_set_style_pad_row(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(panel);

    objects.product_edit_title = ui_theme::make_label(
        panel, "Edit product", ui_theme::font_title, UI_COLOR_TEXT_PRIMARY);

    objects.product_edit_name_ta = create_text_field(panel, "Product name");
    objects.product_edit_expiry_ta = create_text_field(panel, "Expiry YYYY-MM-DD");

    objects.product_edit_category_dd = lv_dropdown_create(panel);
    lv_obj_set_width(objects.product_edit_category_dd, LV_PCT(100));
    lv_obj_set_height(objects.product_edit_category_dd, 64);
    lv_dropdown_set_options(objects.product_edit_category_dd,
                            product_category_options());
    ui_theme::style_field(objects.product_edit_category_dd);

    lv_obj_t *qty_row = lv_obj_create(panel);
    lv_obj_set_width(qty_row, LV_PCT(100));
    lv_obj_set_height(qty_row, 64);
    lv_obj_set_style_bg_opa(qty_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qty_row, 0, 0);
    lv_obj_set_style_pad_all(qty_row, 0, 0);
    lv_obj_set_flex_flow(qty_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(qty_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    objects.product_edit_qty_minus = ui_theme::make_icon_button(
        qty_row, LV_SYMBOL_MINUS, 56);
    lv_obj_add_event_cb(objects.product_edit_qty_minus, action_product_qty_minus,
                        LV_EVENT_CLICKED, NULL);
    objects.product_edit_qty_lbl = ui_theme::make_label(
        qty_row, "1", ui_theme::font_title, UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_flex_grow(objects.product_edit_qty_lbl, 1);
    lv_obj_set_style_text_align(objects.product_edit_qty_lbl, LV_TEXT_ALIGN_CENTER, 0);
    objects.product_edit_qty_plus = ui_theme::make_icon_button(
        qty_row, LV_SYMBOL_PLUS, 56);
    lv_obj_add_event_cb(objects.product_edit_qty_plus, action_product_qty_plus,
                        LV_EVENT_CLICKED, NULL);

    objects.product_edit_frozen_cb = lv_checkbox_create(panel);
    lv_checkbox_set_text(objects.product_edit_frozen_cb, "Frozen");
    lv_obj_set_style_text_font(objects.product_edit_frozen_cb, ui_theme::font_body, 0);
    lv_obj_set_style_text_color(objects.product_edit_frozen_cb,
                                lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_height(objects.product_edit_frozen_cb, 64);

    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_set_width(buttons, LV_PCT(100));
    lv_obj_set_height(buttons, 64);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttons, 0, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 16, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);

    objects.product_edit_delete_btn = lv_button_create(buttons);
    lv_obj_set_size(objects.product_edit_delete_btn, 170, 64);
    ui_theme::style_danger_button(objects.product_edit_delete_btn);
    lv_obj_t *delete_lbl = ui_theme::make_label(
        objects.product_edit_delete_btn, "Delete", ui_theme::font_body,
        UI_COLOR_ERROR_TEXT);
    lv_obj_center(delete_lbl);
    lv_obj_add_event_cb(objects.product_edit_delete_btn, action_product_edit_delete,
                        LV_EVENT_CLICKED, NULL);

    objects.product_edit_cancel_btn = lv_button_create(buttons);
    lv_obj_set_size(objects.product_edit_cancel_btn, 160, 64);
    ui_theme::style_secondary_button(objects.product_edit_cancel_btn);
    lv_obj_t *cancel_lbl = ui_theme::make_label(
        objects.product_edit_cancel_btn, "Cancel", ui_theme::font_body,
        UI_COLOR_TEXT_PRIMARY);
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(objects.product_edit_cancel_btn, action_product_edit_cancel,
                        LV_EVENT_CLICKED, NULL);

    objects.product_edit_save_btn = lv_button_create(buttons);
    lv_obj_set_size(objects.product_edit_save_btn, 230, 64);
    ui_theme::style_primary_button(objects.product_edit_save_btn);
    lv_obj_t *save_lbl = ui_theme::make_label(
        objects.product_edit_save_btn, "Save", ui_theme::font_body, 0xFFFFFF);
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(objects.product_edit_save_btn, action_product_edit_save,
                        LV_EVENT_CLICKED, NULL);
}

static void create_overlays()
{
    objects.spinner = lv_spinner_create(objects.main);
    lv_obj_set_size(objects.spinner, 80, 80);
    lv_obj_center(objects.spinner);
    lv_obj_set_style_arc_color(objects.spinner,
                               lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(objects.spinner, LV_OBJ_FLAG_HIDDEN);

    objects.snackbar = lv_obj_create(objects.main);
    lv_obj_set_size(objects.snackbar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.snackbar, LV_ALIGN_BOTTOM_MID, 0, -132);
    lv_obj_set_style_bg_color(objects.snackbar,
                              lv_color_hex(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(objects.snackbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(objects.snackbar, 16, 0);
    lv_obj_set_style_border_width(objects.snackbar, 0, 0);
    lv_obj_set_style_pad_hor(objects.snackbar, 24, 0);
    lv_obj_set_style_pad_ver(objects.snackbar, 14, 0);
    lv_obj_set_style_shadow_width(objects.snackbar, 0, 0);
    lv_obj_add_flag(objects.snackbar, LV_OBJ_FLAG_HIDDEN);
    objects.snackbar_text = ui_theme::make_label(
        objects.snackbar, "", ui_theme::font_body, 0xFFFFFF);

    objects.keyboard = lv_keyboard_create(objects.main);
    lv_obj_set_size(objects.keyboard, 800, 380);
    lv_obj_align(objects.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(objects.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(objects.keyboard, keyboard_done_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(objects.keyboard, keyboard_done_cb, LV_EVENT_CANCEL, NULL);
}

static void create_main_screen()
{
    objects.main = make_screen();

    objects.tabview = lv_tabview_create(objects.main);
    lv_obj_set_size(objects.tabview, 800, 1280);
    lv_obj_align(objects.tabview, LV_ALIGN_TOP_MID, 0, 0);
    lv_tabview_set_tab_bar_position(objects.tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(objects.tabview, 108);

    objects.tab_inventory = lv_tabview_add_tab(objects.tabview, "Inventory");
    objects.tab_cookbooks = lv_tabview_add_tab(objects.tabview, "Cookbooks");
    objects.tab_settings = lv_tabview_add_tab(objects.tabview, "Settings");
    create_inventory_tab(objects.tab_inventory);
    create_cookbooks_tab(objects.tab_cookbooks);
    create_settings_tab(objects.tab_settings);

    lv_obj_add_event_cb(objects.tabview, tab_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    style_tab_bar_for(objects.tabview);

    create_product_edit_modal();
    create_overlays();
}

void create_screens()
{
    ui_theme::init();
    create_main_screen();
    create_cookbook_detail_screen();
    create_recipe_detail_screen();
}
