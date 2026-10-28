#include "ui_extensions.h"

#include "ui_extensions_internal.h"

#include "CookbooksManager.h"
#include "RecipesManager.h"
#include "actions.h"
#include "diag.h"
#include "screens.h"
#include "thumbnail_manager.h"
#include "ui_theme.h"

#include "esp_log.h"

#include <string>
#include <vector>

static const char *TAG = "COOKBOOK_UI";

// Every function here assumes the caller holds the LVGL lock.

namespace
{

std::string s_open_cookbook_id;

// Recipe cards and the recipe detail header render the image at the same size,
// so the detail reuses the card's cached thumbnail (same source URL, same
// target dimensions = same cache key) instead of downloading it again.
constexpr uint16_t kRecipeImageW = 260;
constexpr uint16_t kRecipeImageH = 200;

// The grid shows a fixed 3×3 page instead of scrolling.
constexpr int kCookbookColumns = 3;
constexpr int kCookbookRows = 3;
constexpr int kCookbooksPerPage = kCookbookColumns * kCookbookRows;
int s_cookbook_page = 0;

// The cookbook detail screen shows five 200 px recipe cards per page, which is
// what fits below the header once the pager row is added.
constexpr int kRecipesPerPage = 5;
int s_cookbook_detail_page = 0;

struct CheckRowState
{
    lv_obj_t *box = nullptr;
    lv_obj_t *tick = nullptr;
    lv_obj_t *label = nullptr;
    bool checked = false;
};

bool valid(lv_obj_t *obj)
{
    return obj && lv_obj_is_valid(obj);
}

std::string plural_count(int count, const char *singular, const char *plural)
{
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

void free_string_cb(lv_event_t *event)
{
    delete static_cast<std::string *>(lv_event_get_user_data(event));
}

void free_recipe_cb(lv_event_t *event)
{
    delete static_cast<Recipe *>(lv_event_get_user_data(event));
}

void free_check_row_cb(lv_event_t *event)
{
    delete static_cast<CheckRowState *>(lv_event_get_user_data(event));
}

// Card children must not swallow the card's click: hit testing stops at the
// first clickable object.
void clear_child_clicks(lv_obj_t *obj)
{
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++)
    {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        lv_obj_remove_flag(child, LV_OBJ_FLAG_CLICKABLE);
        clear_child_clicks(child);
    }
}

void add_image(lv_obj_t *image, const std::string &url, uint16_t width,
               uint16_t height)
{
    if (!url.empty())
    {
        // Fetch at half the displayed resolution and let LVGL scale it up:
        // a quarter of the pixels to download and decode, at the cost of a
        // little softness.
        const uint16_t fetch_w = width > 1 ? width / 2 : width;
        const uint16_t fetch_h = height > 1 ? height / 2 : height;
        push_thumbnail(image, url, fetch_w, fetch_h, true);
        return;
    }
    lv_obj_t *placeholder = ui_theme::make_label(
        image, LV_SYMBOL_IMAGE, ui_theme::font_icon, UI_COLOR_TEXT_MUTED);
    lv_obj_center(placeholder);
}

void add_empty_state(lv_obj_t *root, const char *title, const char *subtitle)
{
    lv_obj_t *empty = lv_obj_create(root);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_height(empty, 320);
    lv_obj_set_style_bg_opa(empty, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(empty, 0, 0);
    lv_obj_set_style_pad_all(empty, 0, 0);
    lv_obj_set_style_pad_row(empty, 12, 0);
    lv_obj_set_flex_flow(empty, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(empty, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(empty, LV_OBJ_FLAG_SCROLLABLE);

    ui_theme::make_label(empty, title, ui_theme::font_title,
                         UI_COLOR_TEXT_PRIMARY);
    ui_theme::make_label(empty, subtitle, ui_theme::font_body,
                         UI_COLOR_TEXT_MUTED);
}

void add_badge_if_present(lv_obj_t *row, const std::string &text, uint32_t bg,
                          uint32_t fg)
{
    if (text.empty())
        return;
    ui_theme::make_badge(row, text.c_str(), bg, fg);
}

void add_cookbook_card(lv_obj_t *parent, const Cookbook &cookbook)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 237, 252);
    ui_theme::style_card(card);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    // The cover size doubles as the Supabase render size, so a smaller card
    // also downloads a smaller image.
    lv_obj_t *cover = lv_image_create(card);
    lv_obj_set_size(cover, 237, 160);
    lv_obj_set_style_bg_color(cover, lv_color_hex(UI_COLOR_SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    add_image(cover, cookbook.coverImageUrl, 237, 160);

    lv_obj_t *info = lv_obj_create(card);
    lv_obj_set_width(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 12, 0);
    lv_obj_set_style_pad_row(info, 6, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = ui_theme::make_label(
        info, cookbook.name.c_str(), ui_theme::font_title,
        UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_width(name, LV_PCT(100));
    // One line only: a wrapped title would overflow the short info area, so
    // let DOTS truncate instead.
    lv_obj_set_height(name, lv_font_get_line_height(ui_theme::font_title));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

    ui_theme::make_label(info,
                         plural_count(cookbook.recipeCount, "recipe",
                                      "recipes")
                             .c_str(),
                         ui_theme::font_body, UI_COLOR_TEXT_MUTED);

    clear_child_clicks(card);

    std::string *id = new std::string(cookbook.id);
    lv_obj_add_event_cb(card, action_cookbook_open, LV_EVENT_CLICKED, id);
    lv_obj_add_event_cb(card, free_string_cb, LV_EVENT_DELETE, id);
}

void add_recipe_card(lv_obj_t *parent, const Recipe &recipe)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 752, 200);
    ui_theme::style_card(card);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_pad_column(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *image = lv_image_create(card);
    lv_obj_set_size(image, kRecipeImageW, kRecipeImageH);
    lv_obj_set_style_bg_color(image, lv_color_hex(UI_COLOR_SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(image, LV_OPA_COVER, 0);
    add_image(image, recipe.imageUrl, kRecipeImageW, kRecipeImageH);

    lv_obj_t *info = lv_obj_create(card);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, LV_PCT(100));
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 20, 0);
    lv_obj_set_style_pad_row(info, 10, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = ui_theme::make_label(
        info, recipe.name.c_str(), ui_theme::font_title,
        UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);

    if (!recipe.recipeSource.empty())
        ui_theme::make_label(info, recipe.recipeSource.c_str(),
                             ui_theme::font_small, UI_COLOR_TEXT_MUTED);

    lv_obj_t *meta = lv_obj_create(info);
    lv_obj_set_width(meta, LV_PCT(100));
    lv_obj_set_height(meta, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_set_style_pad_column(meta, 8, 0);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);

    add_badge_if_present(meta, recipe.totalTime, UI_COLOR_TIME_BG,
                         UI_COLOR_TIME_TEXT);
    add_badge_if_present(meta, recipe.difficulty, UI_COLOR_DIFF_BG,
                         UI_COLOR_DIFF_TEXT);
    add_badge_if_present(meta, recipe.cuisine, UI_COLOR_CUISINE_BG,
                         UI_COLOR_CUISINE_TEXT);

    clear_child_clicks(card);

    Recipe *payload = new Recipe(recipe);
    lv_obj_add_event_cb(card, action_recipe_open, LV_EVENT_CLICKED, payload);
    lv_obj_add_event_cb(card, free_recipe_cb, LV_EVENT_DELETE, payload);
}

std::string ingredient_text(const Ingredient &ingredient)
{
    if (!ingredient.rawIngredient.empty())
        return ingredient.rawIngredient;

    std::string text;
    if (!ingredient.quantity.empty())
        text = ingredient.quantity;
    if (!ingredient.unit.empty())
        text += (text.empty() ? "" : " ") + ingredient.unit;
    if (!ingredient.name.empty())
        text += (text.empty() ? "" : " ") + ingredient.name;
    return text;
}

void apply_check_row(CheckRowState *state)
{
    if (!state)
        return;

    lv_obj_set_style_bg_color(
        state->box,
        lv_color_hex(state->checked ? UI_COLOR_ACCENT : UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(
        state->box,
        lv_color_hex(state->checked ? UI_COLOR_ACCENT
                                    : UI_COLOR_FILTER_STROKE),
        0);
    if (state->checked)
        lv_obj_remove_flag(state->tick, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(state->tick, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_text_decor(
        state->label,
        state->checked ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
    lv_obj_set_style_text_color(
        state->label,
        lv_color_hex(state->checked ? UI_COLOR_TEXT_MUTED
                                    : UI_COLOR_TEXT_PRIMARY),
        0);
}

void check_row_clicked_cb(lv_event_t *event)
{
    auto *state = static_cast<CheckRowState *>(lv_event_get_user_data(event));
    if (!state)
        return;
    state->checked = !state->checked;
    apply_check_row(state);
}

void add_ingredient_row(lv_obj_t *parent, const Ingredient &ingredient)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_set_size(box, 32, 32);
    lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_COLOR_FILTER_STROKE),
                                  0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *tick = ui_theme::make_label(box, LV_SYMBOL_OK,
                                          ui_theme::font_icon_small, 0xFFFFFF);
    lv_obj_center(tick);

    lv_obj_t *label = ui_theme::make_label(
        row, ingredient_text(ingredient).c_str(), ui_theme::font_body,
        UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);

    auto *state = new CheckRowState{box, tick, label, false};
    apply_check_row(state);
    lv_obj_add_event_cb(row, check_row_clicked_cb, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(row, free_check_row_cb, LV_EVENT_DELETE, state);
}

void add_section_title(lv_obj_t *parent, const std::string &name)
{
    if (name.empty())
        return;
    ui_theme::make_label(parent, name.c_str(), ui_theme::font_title,
                         UI_COLOR_TEXT_SECONDARY);
}

void add_method_step(lv_obj_t *parent, int number, const std::string &step)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *number_lbl = ui_theme::make_label(
        row, std::to_string(number).c_str(), ui_theme::font_title,
        UI_COLOR_ACCENT);
    lv_obj_set_width(number_lbl, 40);

    lv_obj_t *text = ui_theme::make_label(
        row, step.c_str(), ui_theme::font_body, UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
}

void populate_ingredients_tab(const Recipe &recipe)
{
    diag_bc(DIAG_UI_POP_ING_ENTER, 0);

    lv_obj_t *tab = objects.recipe_detail_ingredients_tab;
    if (!valid(tab))
        return;
    lv_obj_clean(objects.recipe_detail_ingredients_tab);

    bool any = false;
    for (const auto &section : recipe.ingredients)
    {
        if (section.items.empty())
            continue;
        add_section_title(tab, section.name);
        for (const auto &ingredient : section.items)
        {
            add_ingredient_row(tab, ingredient);
            any = true;
        }
    }

    if (!any)
        ui_theme::make_label(tab, "No ingredients listed", ui_theme::font_body,
                             UI_COLOR_TEXT_MUTED);

    diag_bc(DIAG_UI_POP_ING_DONE, 0);
}

void populate_method_tab(const Recipe &recipe)
{
    diag_bc(DIAG_UI_POP_METH_ENTER, 0);

    lv_obj_t *tab = objects.recipe_detail_method_tab;
    if (!valid(tab))
        return;
    lv_obj_clean(objects.recipe_detail_method_tab);

    int any = 0;
    int number = 1;
    for (const auto &section : recipe.methodSteps)
    {
        bool has_steps = false;
        for (const auto &step : section.steps)
        {
            if (!step.empty())
            {
                has_steps = true;
                break;
            }
        }
        if (!has_steps)
            continue;

        add_section_title(tab, section.name);
        for (const auto &step : section.steps)
        {
            if (step.empty())
                continue;
            add_method_step(tab, number++, step);
            any++;
        }
    }

    if (any == 0)
        ui_theme::make_label(tab, "No method steps", ui_theme::font_body,
                             UI_COLOR_TEXT_MUTED);

    diag_bc(DIAG_UI_POP_METH_DONE, 0);
}

void reset_hero_image(const Recipe &recipe)
{
    diag_bc(DIAG_UI_HERO_ENTER, 0);

    if (!valid(objects.recipe_detail_hero))
        return;

    lv_obj_t *parent = lv_obj_get_parent(objects.recipe_detail_hero);
    // Recreating the image frees the previous recipe's decoded buffer through
    // the free_thumb_data_cb attached by the thumbnail worker.
    lv_obj_delete(objects.recipe_detail_hero);
    objects.recipe_detail_hero = nullptr;
    if (!valid(parent))
        return;

    diag_bc(DIAG_UI_HERO_DELETED, 0);

    lv_obj_t *hero = lv_image_create(parent);
    lv_obj_set_size(hero, kRecipeImageW, kRecipeImageH);
    lv_obj_set_style_bg_color(hero, lv_color_hex(UI_COLOR_SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hero, 0, 0);
    lv_obj_move_to_index(hero, 0);
    objects.recipe_detail_hero = hero;
    diag_bc(DIAG_UI_HERO_CREATED, 0);

    // Same URL and size as the recipe card, so this is a cache hit when the
    // card that opened this screen already downloaded its thumbnail.
    const std::string &url =
        recipe.imageUrl.empty() ? recipe.imageUrlBig : recipe.imageUrl;
    add_image(hero, url, kRecipeImageW, kRecipeImageH);
    diag_bc(DIAG_UI_HERO_IMAGED, 0);
}

void set_spinner_visible(bool visible)
{
    if (!valid(objects.cookbook_detail_spinner))
        return;
    if (visible)
        lv_obj_remove_flag(objects.cookbook_detail_spinner,
                           LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(objects.cookbook_detail_spinner, LV_OBJ_FLAG_HIDDEN);
}

void set_detail_list_visible(bool visible)
{
    if (valid(objects.cookbook_detail_list))
    {
        if (visible)
            lv_obj_remove_flag(objects.cookbook_detail_list,
                               LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objects.cookbook_detail_list, LV_OBJ_FLAG_HIDDEN);
    }

    // The pager only makes sense alongside the list; update_recipe_pager()
    // re-shows it when the loaded cookbook spans more than one page.
    if (!visible && valid(objects.cookbook_detail_pager))
        lv_obj_add_flag(objects.cookbook_detail_pager, LV_OBJ_FLAG_HIDDEN);
}

void set_detail_state(const char *text, bool show_retry)
{
    if (valid(objects.cookbook_detail_state_lbl))
    {
        if (text && text[0])
        {
            lv_label_set_text(objects.cookbook_detail_state_lbl, text);
            lv_obj_remove_flag(objects.cookbook_detail_state_lbl,
                               LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(objects.cookbook_detail_state_lbl,
                            LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (valid(objects.cookbook_detail_retry_btn))
    {
        if (show_retry)
            lv_obj_remove_flag(objects.cookbook_detail_retry_btn,
                               LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objects.cookbook_detail_retry_btn,
                            LV_OBJ_FLAG_HIDDEN);
    }
}

void set_pager_button_enabled(lv_obj_t *button, bool enabled)
{
    if (!valid(button))
        return;
    if (enabled)
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(button, LV_STATE_DISABLED);
}

void update_cookbook_pager(int page_count)
{
    if (valid(objects.cookbooks_pager))
    {
        if (page_count > 1)
            lv_obj_remove_flag(objects.cookbooks_pager, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objects.cookbooks_pager, LV_OBJ_FLAG_HIDDEN);
    }

    if (valid(objects.cookbooks_page_lbl))
    {
        std::string text = "Page " + std::to_string(s_cookbook_page + 1) +
                           " of " + std::to_string(page_count);
        lv_label_set_text(objects.cookbooks_page_lbl, text.c_str());
    }

    set_pager_button_enabled(objects.cookbooks_prev_btn, s_cookbook_page > 0);
    set_pager_button_enabled(objects.cookbooks_next_btn,
                             s_cookbook_page + 1 < page_count);
}

void update_recipe_pager(int page_count)
{
    if (valid(objects.cookbook_detail_pager))
    {
        if (page_count > 1)
            lv_obj_remove_flag(objects.cookbook_detail_pager,
                               LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objects.cookbook_detail_pager,
                            LV_OBJ_FLAG_HIDDEN);
    }

    if (valid(objects.cookbook_detail_page_lbl))
    {
        std::string text = "Page " + std::to_string(s_cookbook_detail_page + 1) +
                           " of " + std::to_string(page_count);
        lv_label_set_text(objects.cookbook_detail_page_lbl, text.c_str());
    }

    set_pager_button_enabled(objects.cookbook_detail_prev_btn,
                             s_cookbook_detail_page > 0);
    set_pager_button_enabled(objects.cookbook_detail_next_btn,
                             s_cookbook_detail_page + 1 < page_count);
}

} // namespace

void populateCookbooksUi()
{
    if (!valid(objects.cookbooks_list))
        return;

    thumb_queue_cancel_all();
    lv_obj_clean(objects.cookbooks_list);

    std::vector<Cookbook> cookbooks = cookbooksManager.snapshot();
    if (cookbooks.empty())
    {
        s_cookbook_page = 0;
        update_cookbook_pager(0);
        add_empty_state(objects.cookbooks_list, "No cookbooks yet",
                        "Create cookbooks in the Kitchen Assistant app");
        return;
    }

    const int page_count =
        (static_cast<int>(cookbooks.size()) + kCookbooksPerPage - 1) /
        kCookbooksPerPage;
    if (s_cookbook_page >= page_count)
        s_cookbook_page = page_count - 1;
    if (s_cookbook_page < 0)
        s_cookbook_page = 0;

    const size_t first =
        static_cast<size_t>(s_cookbook_page) * kCookbooksPerPage;
    size_t last = first + kCookbooksPerPage;
    if (last > cookbooks.size())
        last = cookbooks.size();

    // DIAGNOSTIC (temporary): headroom before and after building the cards, so
    // the delta is the grid's own draw-time allocation.
    diag_heap_log("grid render begin");

    for (size_t i = first; i < last; ++i)
        add_cookbook_card(objects.cookbooks_list, cookbooks[i]);

    update_cookbook_pager(page_count);

    diag_heap_log("grid render done");

    ESP_LOGI(TAG, "Cookbook grid rendered (page %d/%d, %u cards)",
             s_cookbook_page + 1, page_count, (unsigned)(last - first));
}

void shiftCookbookPage(int delta)
{
    s_cookbook_page += delta;
    if (s_cookbook_page < 0)
        s_cookbook_page = 0;
    populateCookbooksUi();
}

void showCookbookDetail(const std::string &cookbookId)
{
    s_open_cookbook_id = cookbookId;
    s_cookbook_detail_page = 0;

    if (valid(objects.cookbook_detail_pager))
        lv_obj_add_flag(objects.cookbook_detail_pager, LV_OBJ_FLAG_HIDDEN);

    Cookbook cookbook;
    const bool known = cookbooksManager.getCookbook(cookbookId, cookbook);

    if (valid(objects.cookbook_detail_title_lbl))
        lv_label_set_text(objects.cookbook_detail_title_lbl,
                          known ? cookbook.name.c_str() : "Cookbook");

    if (valid(objects.cookbook_detail_count_lbl))
    {
        std::string count =
            known ? plural_count(cookbook.recipeCount, "recipe", "recipes")
                  : std::string();
        lv_label_set_text(objects.cookbook_detail_count_lbl, count.c_str());
    }

    if (valid(objects.cookbook_detail_list))
    {
        thumb_queue_cancel_all();
        lv_obj_clean(objects.cookbook_detail_list);
    }

    const bool cached = recipesManager.hasCookbook(cookbookId);
    set_detail_state("", false);
    set_spinner_visible(!cached);
    set_detail_list_visible(cached);

    // Open the cookbook without a slide transition.
    if (valid(objects.cookbook_detail))
        lv_screen_load(objects.cookbook_detail);
}

void populateCookbookRecipesUi(const std::string &cookbookId)
{
    if (!valid(objects.cookbook_detail_list))
        return;
    // The user may have left the screen while the fetch was running.
    if (cookbookId != s_open_cookbook_id)
        return;

    lv_obj_t *list = objects.cookbook_detail_list;

    if (recipesManager.cookbookFailed(cookbookId))
    {
        thumb_queue_cancel_all();
        lv_obj_clean(list);
        set_spinner_visible(false);
        set_detail_list_visible(false);
        set_detail_state("Could not load recipes", true);
        return;
    }

    const size_t total = recipesManager.loadedRecipeCount(cookbookId);
    thumb_queue_cancel_all();
    lv_obj_clean(list);

    if (total == 0)
    {
        set_spinner_visible(false);
        set_detail_list_visible(false);
        set_detail_state("No recipes in this cookbook", false);
        return;
    }

    set_detail_state("", false);
    set_spinner_visible(false);
    set_detail_list_visible(true);

    if (valid(objects.cookbook_detail_count_lbl))
        lv_label_set_text(
            objects.cookbook_detail_count_lbl,
            plural_count((int)total, "recipe", "recipes").c_str());

    const int page_count =
        (static_cast<int>(total) + kRecipesPerPage - 1) / kRecipesPerPage;
    if (s_cookbook_detail_page >= page_count)
        s_cookbook_detail_page = page_count - 1;
    if (s_cookbook_detail_page < 0)
        s_cookbook_detail_page = 0;

    const size_t first =
        static_cast<size_t>(s_cookbook_detail_page) * kRecipesPerPage;
    // Only this page's recipes are copied; the full cookbook stays in the
    // manager so flipping pages does not duplicate every ingredient string.
    std::vector<Recipe> recipes =
        recipesManager.getCookbookRecipePage(cookbookId, first, kRecipesPerPage);

    if (recipes.empty())
    {
        set_spinner_visible(false);
        set_detail_list_visible(false);
        set_detail_state("No recipes in this cookbook", false);
        return;
    }

    for (const auto &recipe : recipes)
        add_recipe_card(list, recipe);

    update_recipe_pager(page_count);

    ESP_LOGI(TAG, "Recipe list rendered (page %d/%d, %u cards)",
             s_cookbook_detail_page + 1, page_count,
             (unsigned)recipes.size());
}

void shiftCookbookRecipesPage(int delta)
{
    if (s_open_cookbook_id.empty())
        return;
    s_cookbook_detail_page += delta;
    if (s_cookbook_detail_page < 0)
        s_cookbook_detail_page = 0;
    // Re-renders from the cached recipe list and clamps the page to its range.
    populateCookbookRecipesUi(s_open_cookbook_id);
}

void retryCookbookDetail()
{
    set_detail_state("", false);
    set_spinner_visible(true);
    set_detail_list_visible(false);
}

void showRecipeDetailScreen(const Recipe &recipe)
{
    diag_bc(DIAG_UI_DETAIL_ENTER, 0);

    thumb_queue_cancel_all();
    diag_bc(DIAG_UI_DETAIL_CANCELLED, 0);

    reset_hero_image(recipe);

    if (valid(objects.recipe_detail_title_lbl))
        lv_label_set_text(objects.recipe_detail_title_lbl,
                          recipe.name.empty() ? "Recipe" : recipe.name.c_str());

    std::string source = recipe.recipeSource;
    if (recipe.serving > 0)
    {
        if (!source.empty())
            source += " - ";
        source += "Serves " + std::to_string(recipe.serving);
    }
    if (valid(objects.recipe_detail_source_lbl))
    {
        lv_label_set_text(objects.recipe_detail_source_lbl, source.c_str());
        if (source.empty())
            lv_obj_add_flag(objects.recipe_detail_source_lbl,
                            LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(objects.recipe_detail_source_lbl,
                               LV_OBJ_FLAG_HIDDEN);
    }

    if (valid(objects.recipe_detail_time_lbl))
        lv_label_set_text(objects.recipe_detail_time_lbl,
                          recipe.totalTime.empty() ? "-"
                                                   : recipe.totalTime.c_str());
    if (valid(objects.recipe_detail_difficulty_lbl))
        lv_label_set_text(objects.recipe_detail_difficulty_lbl,
                          recipe.difficulty.empty() ? "-"
                                                    : recipe.difficulty.c_str());

    diag_bc(DIAG_UI_DETAIL_TEXT, 0);

    if (valid(objects.recipe_detail_tabview))
        lv_tabview_set_active(objects.recipe_detail_tabview, 0, LV_ANIM_OFF);
    diag_bc(DIAG_UI_DETAIL_TAB, 0);

    populate_ingredients_tab(recipe);
    populate_method_tab(recipe);

    if (valid(objects.recipe_detail))
    {
        diag_bc(DIAG_UI_DETAIL_ANIM, 0);
        // Open the recipe without a slide transition.
        lv_screen_load(objects.recipe_detail);
    }

    diag_bc(DIAG_UI_DETAIL_DONE, 0);
}

void showCookbookDetailFailure(const std::string &cookbookId)
{
    if (cookbookId != s_open_cookbook_id)
        return;
    set_spinner_visible(false);
    set_detail_list_visible(false);
    set_detail_state("Could not load recipes", true);
}

void leaveCookbookDetail()
{
    s_open_cookbook_id.clear();
    thumb_queue_cancel_all();
}

std::string openCookbookId()
{
    return s_open_cookbook_id;
}
