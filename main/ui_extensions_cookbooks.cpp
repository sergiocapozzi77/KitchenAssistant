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

#include <cctype>
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
        lv_obj_set_clickable(child, false);
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
        if (push_thumbnail(image, url, fetch_w, fetch_h, true))
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
    lv_obj_set_clickable(empty, false);
    lv_obj_set_scrollable(empty, false);

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
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);

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
    lv_obj_set_scrollable(info, false);

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
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);

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
    lv_obj_set_scrollable(info, false);

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
    lv_obj_set_scrollable(meta, false);

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
        lv_obj_set_hidden(state->tick, false);
    else
        lv_obj_set_hidden(state->tick, true);

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
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, true);

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_set_size(box, 32, 32);
    lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_COLOR_FILTER_STROKE),
                                  0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);

    lv_obj_t *tick = ui_theme::make_label(box, LV_SYMBOL_OK,
                                          ui_theme::font_icon_small, 0xFFFFFF);
    lv_obj_center(tick);

    lv_obj_t *label = ui_theme::make_label(
        row, ingredient_text(ingredient).c_str(), ui_theme::font_recipe_body,
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

void add_method_step(lv_obj_t *parent, int number, const std::string &step,
                     bool numbered = true, int top_margin = 0)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    if (top_margin > 0)
        lv_obj_set_style_margin_top(row, top_margin, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(row, false);

    if (numbered)
    {
        lv_obj_t *number_lbl = ui_theme::make_label(
            row, std::to_string(number).c_str(), ui_theme::font_title,
            UI_COLOR_ACCENT);
        lv_obj_set_width(number_lbl, 40);
    }
    else
    {
        lv_obj_t *bullet = lv_obj_create(row);
        lv_obj_set_size(bullet, 10, 10);
        lv_obj_set_style_radius(bullet, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bullet, lv_color_hex(UI_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_opa(bullet, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bullet, 0, 0);
        lv_obj_set_style_margin_top(bullet, 7, 0);
        lv_obj_set_scrollable(bullet, false);
    }

    lv_obj_t *text = ui_theme::make_label(
        row, step.c_str(), ui_theme::font_recipe_body, UI_COLOR_TEXT_PRIMARY);
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

bool instruction_section_visible(const InstructionSection &section)
{
    for (const auto &step : section.steps)
        if (!step.empty())
            return true;
    return false;
}

std::string normalized_section_name(const std::string &name)
{
    const size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();

    std::string out = name.substr(first, name.find_last_not_of(" \t\r\n") -
                                             first + 1);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The Step by Step tab only makes sense when both sides are split into the same
// named groups in the same order. Sections that render empty in their own tabs
// are ignored so the pairing matches what those tabs show.
bool collect_paired_sections(
    const Recipe &recipe,
    std::vector<const IngredientSection *> &ingredient_sections,
    std::vector<const InstructionSection *> &method_sections)
{
    ingredient_sections.clear();
    method_sections.clear();

    for (const auto &section : recipe.ingredients)
        if (!section.items.empty())
            ingredient_sections.push_back(&section);
    for (const auto &section : recipe.methodSteps)
        if (instruction_section_visible(section))
            method_sections.push_back(&section);

    if (ingredient_sections.size() < 2 ||
        ingredient_sections.size() != method_sections.size())
        return false;

    for (size_t i = 0; i < ingredient_sections.size(); ++i)
        if (normalized_section_name(ingredient_sections[i]->name) !=
            normalized_section_name(method_sections[i]->name))
            return false;

    return true;
}

// ── Step by Step pager ──────────────────────────────────────────────────────
// One matched group is shown at a time. Every group keeps its own panel so
// ingredient checkmarks survive moving between pages.
std::vector<lv_obj_t *> s_step_panels;
std::vector<lv_obj_t *> s_step_labels;
int s_step_group = 0;

lv_obj_t *s_step_bar = nullptr;
lv_obj_t *s_step_content = nullptr;
lv_obj_t *s_step_page_lbl = nullptr;
lv_obj_t *s_step_prev_btn = nullptr;
lv_obj_t *s_step_next_btn = nullptr;

void set_pager_button_enabled(lv_obj_t *button, bool enabled); // defined below

void show_step_group()
{
    const int count = (int)s_step_panels.size();
    if (count == 0)
        return;
    if (s_step_group < 0)
        s_step_group = 0;
    if (s_step_group >= count)
        s_step_group = count - 1;

    for (int i = 0; i < count; ++i)
    {
        if (!valid(s_step_panels[i]))
            continue;
        if (i == s_step_group)
            lv_obj_set_hidden(s_step_panels[i], false);
        else
            lv_obj_set_hidden(s_step_panels[i], true);
    }

    if (valid(s_step_bar))
        lv_bar_set_value(s_step_bar, s_step_group + 1, LV_ANIM_OFF);

    for (size_t i = 0; i < s_step_labels.size(); ++i)
    {
        if (!valid(s_step_labels[i]))
            continue;
        uint32_t color = UI_COLOR_TEXT_MUTED;
        if ((int)i == s_step_group)
            color = UI_COLOR_ACCENT;
        else if ((int)i < s_step_group)
            color = UI_COLOR_TEXT_SECONDARY;
        lv_obj_set_style_text_color(s_step_labels[i], lv_color_hex(color), 0);
    }

    if (valid(s_step_page_lbl))
    {
        std::string text = "Page " + std::to_string(s_step_group + 1) +
                           " of " + std::to_string(count);
        lv_label_set_text(s_step_page_lbl, text.c_str());
    }

    set_pager_button_enabled(s_step_prev_btn, s_step_group > 0);
    set_pager_button_enabled(s_step_next_btn, s_step_group + 1 < count);
    if (valid(s_step_content))
        lv_obj_scroll_to_y(s_step_content, 0, LV_ANIM_OFF);
}

void step_prev_clicked_cb(lv_event_t *)
{
    if (s_step_group <= 0)
        return;
    --s_step_group;
    show_step_group();
}

void step_next_clicked_cb(lv_event_t *)
{
    if (s_step_group + 1 >= (int)s_step_panels.size())
        return;
    ++s_step_group;
    show_step_group();
}

void build_step_pager(lv_obj_t *tab,
                      const std::vector<const IngredientSection *> &sections)
{
    const int group_count = (int)sections.size();

    // The page itself must not scroll: the group content sits between a fixed
    // progress bar and a fixed pager row.
    lv_obj_set_scrollable(tab, false);

    // Each group name sits over its equal slice of the progress bar below.
    lv_obj_t *names = lv_obj_create(tab);
    lv_obj_set_width(names, LV_PCT(100));
    lv_obj_set_height(names, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(names, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(names, 0, 0);
    lv_obj_set_style_pad_all(names, 0, 0);
    lv_obj_set_flex_flow(names, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(names, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(names, false);

    for (const auto *section : sections)
    {
        lv_obj_t *cell = lv_obj_create(names);
        lv_obj_set_flex_grow(cell, 1);
        lv_obj_set_height(cell, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_top(cell, 0, 0);
        lv_obj_set_style_pad_bottom(cell, 0, 0);
        lv_obj_set_style_pad_left(cell, 4, 0);
        lv_obj_set_style_pad_right(cell, 4, 0);
        lv_obj_set_scrollable(cell, false);

        lv_obj_t *label = ui_theme::make_label(
            cell, section->name.c_str(), ui_theme::font_small,
            UI_COLOR_TEXT_MUTED);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        s_step_labels.push_back(label);
    }

    s_step_bar = lv_bar_create(tab);
    lv_obj_set_width(s_step_bar, LV_PCT(100));
    lv_obj_set_height(s_step_bar, 12);
    lv_bar_set_range(s_step_bar, 0, group_count);
    lv_bar_set_value(s_step_bar, 1, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_step_bar, lv_color_hex(UI_COLOR_SURFACE_HIGHEST),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_step_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_step_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_step_bar, lv_color_hex(UI_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_step_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_step_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    s_step_content = lv_obj_create(tab);
    lv_obj_set_width(s_step_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_step_content, 1);
    lv_obj_set_style_bg_opa(s_step_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_step_content, 0, 0);
    lv_obj_set_style_pad_all(s_step_content, 0, 0);
    lv_obj_set_style_pad_row(s_step_content, 12, 0);
    lv_obj_set_flex_flow(s_step_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_step_content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *pager = lv_obj_create(tab);
    lv_obj_set_width(pager, LV_PCT(100));
    lv_obj_set_height(pager, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pager, 0, 0);
    lv_obj_set_style_pad_all(pager, 0, 0);
    lv_obj_set_style_pad_column(pager, 24, 0);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(pager, false);

    s_step_prev_btn = ui_theme::make_icon_button(pager, LV_SYMBOL_LEFT, 64);
    lv_obj_set_style_opa(s_step_prev_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_step_prev_btn, step_prev_clicked_cb, LV_EVENT_CLICKED,
                        NULL);

    s_step_page_lbl = ui_theme::make_label(pager, "", ui_theme::font_body,
                                           UI_COLOR_TEXT_MUTED);
    lv_obj_set_width(s_step_page_lbl, 180);
    lv_obj_set_style_text_align(s_step_page_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_step_next_btn = ui_theme::make_icon_button(pager, LV_SYMBOL_RIGHT, 64);
    lv_obj_set_style_opa(s_step_next_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_step_next_btn, step_next_clicked_cb, LV_EVENT_CLICKED,
                        NULL);
}

void populate_step_by_step_tab(const Recipe &recipe)
{
    lv_obj_t *tab = objects.recipe_detail_steps_tab;
    if (!valid(tab))
        return;

    std::vector<const IngredientSection *> ingredient_sections;
    std::vector<const InstructionSection *> method_sections;
    const bool available =
        collect_paired_sections(recipe, ingredient_sections, method_sections);

    // The tab is always the third one; hide its button when the recipe does not
    // have matching groups so the screen keeps its usual two tabs.
    lv_obj_t *button =
        valid(objects.recipe_detail_tabview)
            ? lv_tabview_get_tab_button(objects.recipe_detail_tabview, 2)
            : nullptr;
    if (valid(button))
    {
        if (available)
            lv_obj_set_hidden(button, false);
        else
            lv_obj_set_hidden(button, true);
    }

    lv_obj_clean(tab);
    s_step_bar = nullptr;
    s_step_content = nullptr;
    s_step_page_lbl = nullptr;
    s_step_prev_btn = nullptr;
    s_step_next_btn = nullptr;
    s_step_panels.clear();
    s_step_labels.clear();
    s_step_group = 0;

    if (!available)
        return;

    build_step_pager(tab, ingredient_sections);

    // Method numbering continues across groups, so the numbers match the
    // Method tab even though only one group is visible.
    int number = 1;
    for (size_t i = 0; i < ingredient_sections.size(); ++i)
    {
        lv_obj_t *panel = lv_obj_create(s_step_content);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);
        lv_obj_set_style_pad_row(panel, 12, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollable(panel, false);

        add_section_title(panel, ingredient_sections[i]->name);
        for (const auto &ingredient : ingredient_sections[i]->items)
            add_ingredient_row(panel, ingredient);
        bool first_step = true;
        for (const auto &step : method_sections[i]->steps)
        {
            if (step.empty())
                continue;
            add_method_step(panel, number++, step, false,
                            first_step ? 20 : 0);
            first_step = false;
        }

        if (i > 0)
            lv_obj_set_hidden(panel, true);
        s_step_panels.push_back(panel);
    }

    show_step_group();
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
        lv_obj_set_hidden(objects.cookbook_detail_spinner, false);
    else
        lv_obj_set_hidden(objects.cookbook_detail_spinner, true);
}

void set_detail_list_visible(bool visible)
{
    if (valid(objects.cookbook_detail_list))
    {
        if (visible)
            lv_obj_set_hidden(objects.cookbook_detail_list, false);
        else
            lv_obj_set_hidden(objects.cookbook_detail_list, true);
    }

    // The pager only makes sense alongside the list; update_recipe_pager()
    // re-shows it when the loaded cookbook spans more than one page.
    if (!visible && valid(objects.cookbook_detail_pager))
        lv_obj_set_hidden(objects.cookbook_detail_pager, true);
}

void set_detail_state(const char *text, bool show_retry)
{
    if (valid(objects.cookbook_detail_state_lbl))
    {
        if (text && text[0])
        {
            lv_label_set_text(objects.cookbook_detail_state_lbl, text);
            lv_obj_set_hidden(objects.cookbook_detail_state_lbl, false);
        }
        else
        {
            lv_obj_set_hidden(objects.cookbook_detail_state_lbl, true);
        }
    }

    if (valid(objects.cookbook_detail_retry_btn))
    {
        if (show_retry)
            lv_obj_set_hidden(objects.cookbook_detail_retry_btn, false);
        else
            lv_obj_set_hidden(objects.cookbook_detail_retry_btn, true);
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
            lv_obj_set_hidden(objects.cookbooks_pager, false);
        else
            lv_obj_set_hidden(objects.cookbooks_pager, true);
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
            lv_obj_set_hidden(objects.cookbook_detail_pager, false);
        else
            lv_obj_set_hidden(objects.cookbook_detail_pager, true);
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

    if (!cookbook_recipe_ui_enabled())
    {
        lv_obj_clean(objects.cookbooks_list);
        if (valid(objects.cookbooks_pager))
            lv_obj_set_hidden(objects.cookbooks_pager, true);
        return;
    }

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
    if (!cookbook_recipe_ui_enabled())
        return;

    s_open_cookbook_id = cookbookId;
    s_cookbook_detail_page = 0;

    if (valid(objects.cookbook_detail_pager))
        lv_obj_set_hidden(objects.cookbook_detail_pager, true);

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
    if (!cookbook_recipe_ui_enabled())
    {
        lv_obj_clean(objects.cookbook_detail_list);
        if (valid(objects.cookbook_detail_pager))
            lv_obj_set_hidden(objects.cookbook_detail_pager, true);
        set_spinner_visible(false);
        set_detail_list_visible(false);
        return;
    }
    // The user may have left the screen while the fetch was running.
    if (cookbookId != s_open_cookbook_id)
        return;

    lv_obj_t *list = objects.cookbook_detail_list;

    if (recipesManager.cookbookFailed(cookbookId))
    {
        thumb_queue_cancel_all();
        lv_obj_clean(list);
        set_spinner_visible(true);
        set_detail_list_visible(false);
        set_detail_state("", false);
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
    if (!cookbook_recipe_ui_enabled())
        return;
    set_detail_state("", false);
    set_spinner_visible(true);
    set_detail_list_visible(false);
}

void showRecipeDetailScreen(const Recipe &recipe)
{
    if (!cookbook_recipe_ui_enabled())
        return;

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
            lv_obj_set_hidden(objects.recipe_detail_source_lbl, true);
        else
            lv_obj_set_hidden(objects.recipe_detail_source_lbl, false);
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
    populate_step_by_step_tab(recipe);

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
    set_spinner_visible(true);
    set_detail_list_visible(false);
    set_detail_state("", false);
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
