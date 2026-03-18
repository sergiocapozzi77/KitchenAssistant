#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "ProductService.h"
#include "ui_extensions.h"

static const char *TAG = "UIEXTENSIONS";

#include <ctime>
#include <cmath>

static int days_until_expiry(const std::string &isoDate)
{
    if (isoDate.empty())
        return 9999;

    std::tm tm_exp = {};
    sscanf(isoDate.c_str(), "%d-%d-%d",
           &tm_exp.tm_year,
           &tm_exp.tm_mon,
           &tm_exp.tm_mday);

    tm_exp.tm_year -= 1900;
    tm_exp.tm_mon -= 1;

    time_t exp_time = mktime(&tm_exp);

    // Normalize now to midnight so we compare dates, not timestamps
    time_t now_raw = time(nullptr);
    std::tm tm_now = {};
    localtime_r(&now_raw, &tm_now);
    tm_now.tm_hour = 0;
    tm_now.tm_min = 0;
    tm_now.tm_sec = 0;
    time_t now_midnight = mktime(&tm_now);

    double diff = difftime(exp_time, now_midnight);
    return (int)round(diff / (60.0 * 60.0 * 24.0)); // round, not ceil
}

// Forward declaration of delete callback
static void delete_btn_cb(lv_event_t *e);

struct GroupUI
{
    lv_obj_t *content;
    lv_obj_t *arrow;
    bool collapsed;
};

static void group_toggle_cb(lv_event_t *e)
{
    GroupUI *group = (GroupUI *)lv_event_get_user_data(e);

    group->collapsed = !group->collapsed;

    if (group->collapsed)
    {
        lv_obj_add_flag(group->content, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(group->arrow, LV_SYMBOL_RIGHT);
    }
    else
    {
        lv_obj_clear_flag(group->content, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(group->arrow, LV_SYMBOL_DOWN);
    }
}

static lv_color_t get_expiry_color(int days)
{
    if (days < 0)
        return lv_color_hex(0xE74C3C); // red
    else if (days <= 3)
        return lv_color_hex(0xF39C12); // orange
    else
        return lv_color_hex(0x27AE60); // green
}

static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
        lv_obj_clear_state(checkbox, LV_STATE_CHECKED);
    else
        lv_obj_add_state(checkbox, LV_STATE_CHECKED);
}

static void free_group_cb(lv_event_t *e)
{
    delete (GroupUI *)lv_event_get_user_data(e);
}

static void free_rowid_cb(lv_event_t *e)
{
    delete (std::string *)lv_event_get_user_data(e);
}

void populateProductList(lv_obj_t *root, const std::vector<Product> &products)
{
    lv_lock();
    lv_obj_clean(root);

    // Root setup: Light gray background like the image
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 15, 0);

    std::vector<const Product *> sorted;
    for (const auto &p : products)
        sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [](const Product *a, const Product *b)
              { return a->category < b->category; });

    std::string currentCategory;
    lv_obj_t *content = nullptr;

    for (const Product *p : sorted)
    {
        if (p->category != currentCategory)
        {
            currentCategory = p->category;

            // The Card: White background, subtle border, rounded corners
            lv_obj_t *card = lv_obj_create(root);
            lv_obj_set_width(card, lv_pct(100));
            lv_obj_set_height(card, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_radius(card, 12, 0); // Rounded card
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0xE0E0E0), 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_set_style_clip_corner(card, true, 0); // Important for inner content

            // Header: Minimalist style
            lv_obj_t *header = lv_btn_create(card);
            lv_obj_set_width(header, lv_pct(100));
            lv_obj_set_height(header, 50);
            lv_obj_set_style_bg_opa(header, 0, 0); // Transparent button
            lv_obj_set_style_shadow_opa(header, 0, 0);
            lv_obj_set_style_pad_hor(header, 15, 0);
            lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *title = lv_label_create(header);
            lv_label_set_text(title, currentCategory.c_str());
            lv_obj_set_style_text_color(title, lv_color_hex(0x495057), 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

            lv_obj_t *arrow = lv_label_create(header);
            lv_label_set_text(arrow, LV_SYMBOL_DOWN);
            lv_obj_set_style_text_color(arrow, lv_color_hex(0xADB5BD), 0); // Corrected '0x' prefix and gray hex

            content = lv_obj_create(card);
            lv_obj_set_width(content, lv_pct(100));
            lv_obj_set_height(content, LV_SIZE_CONTENT);
            lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_all(content, 0, 0);
            lv_obj_set_style_border_width(content, 0, 0);

            GroupUI *group = new GroupUI{content, arrow, false};
            lv_obj_add_event_cb(header, group_toggle_cb, LV_EVENT_CLICKED, group);
            lv_obj_add_event_cb(header, free_group_cb, LV_EVENT_DELETE, group);
        }

        // --- THE ROW ---
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 60);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Horizontal line separator between rows
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xF1F3F5), 0);
        lv_obj_set_style_pad_hor(row, 15, 0);
        lv_obj_set_style_bg_opa(row, 0, 0);

        // Checkbox (before product name)
        lv_obj_t *checkbox = lv_checkbox_create(row);
        lv_checkbox_set_text(checkbox, "");
        lv_obj_set_style_pad_right(checkbox, 8, 0);
        lv_obj_set_style_border_color(checkbox, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(checkbox, lv_color_hex(0x007AFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(checkbox, lv_color_hex(0x007AFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
        // Row click selects the checkbox; buttons are excluded automatically (no bubble)
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, checkbox);

        // Product Name
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, p->name.c_str());
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, lv_color_hex(0x495057), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);

        // expiry
        // Compute days from ISO date string (YYYY-MM-DD or full ISO)
        int days = days_until_expiry(p->expiry);

        if (days != 9999) // Only show expiry if date is valid
        {

            // Create label
            lv_obj_t *expiry = lv_label_create(row);

            // Format text
            char buf[32];
            if (days < 0)
                sprintf(buf, "Expired");
            else if (days == 0)
                sprintf(buf, "Today");
            else if (days == 1)
                sprintf(buf, "1d left");
            else
                sprintf(buf, "%dd left", days);

            lv_label_set_text(expiry, buf);

            lv_obj_set_style_bg_color(expiry, get_expiry_color(days), 0);
            lv_obj_set_style_bg_opa(expiry, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(expiry, lv_color_white(), 0);
            lv_obj_set_style_pad_hor(expiry, 8, 0);
            lv_obj_set_style_pad_ver(expiry, 2, 0);
            lv_obj_set_style_radius(expiry, 10, 0);
        }

        // Quantity Selector
        lv_obj_t *qty_cont = lv_obj_create(row);
        lv_obj_set_size(qty_cont, 144, 36); // ← wider to fit delete button
        lv_obj_set_style_radius(qty_cont, 8, 0);
        lv_obj_set_style_bg_color(qty_cont, lv_color_hex(0xF8F9FA), 0);
        lv_obj_set_style_border_width(qty_cont, 1, 0);
        lv_obj_set_style_border_color(qty_cont, lv_color_hex(0xE9ECEF), 0);
        lv_obj_set_style_pad_all(qty_cont, 0, 0);
        lv_obj_clear_flag(qty_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(qty_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(qty_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *btn_minus = lv_btn_create(qty_cont);
        lv_obj_set_size(btn_minus, 30, 36);
        lv_obj_set_style_bg_opa(btn_minus, 0, 0);
        lv_obj_set_style_shadow_opa(btn_minus, 0, 0);
        lv_obj_t *lbl_minus = lv_label_create(btn_minus);
        lv_label_set_text(lbl_minus, LV_SYMBOL_MINUS);
        lv_obj_set_style_text_color(lbl_minus, lv_color_hex(0x007AFF), 0);
        lv_obj_center(lbl_minus);

        lv_obj_t *qty_val = lv_label_create(qty_cont);
        lv_label_set_text_fmt(qty_val, "%d", p->quantity);
        lv_obj_set_style_bg_color(qty_val, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(qty_val, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_hor(qty_val, 5, 0);

        lv_obj_t *btn_plus = lv_btn_create(qty_cont);
        lv_obj_set_size(btn_plus, 30, 36);
        lv_obj_set_style_bg_opa(btn_plus, 0, 0);
        lv_obj_set_style_shadow_opa(btn_plus, 0, 0);
        lv_obj_t *lbl_plus = lv_label_create(btn_plus);
        lv_label_set_text(lbl_plus, LV_SYMBOL_PLUS);
        lv_obj_set_style_text_color(lbl_plus, lv_color_hex(0x007AFF), 0);
        lv_obj_center(lbl_plus);

        // Delete Button — now inside qty_cont, after plus
        lv_obj_t *btn_del = lv_btn_create(qty_cont); // ← parent is qty_cont
        lv_obj_set_size(btn_del, 36, 36);
        lv_obj_set_style_bg_opa(btn_del, 0, 0); // ← transparent to blend in
        lv_obj_set_style_shadow_opa(btn_del, 0, 0);
        lv_obj_set_style_border_width(btn_del, 0, LV_PART_MAIN);
        lv_obj_set_style_border_side(btn_del, LV_BORDER_SIDE_LEFT, 0); // subtle divider
        lv_obj_set_style_border_width(btn_del, 1, 0);
        lv_obj_set_style_border_color(btn_del, lv_color_hex(0xE9ECEF), 0);

        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(lbl_del, lv_color_hex(0xE74C3C), 0);
        lv_obj_center(lbl_del);

        std::string *rowId = new std::string(p->rowId);
        lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED, rowId);
        lv_obj_add_event_cb(btn_del, free_rowid_cb, LV_EVENT_DELETE, rowId);
    }

    lv_unlock();
}

static void delete_btn_cb(lv_event_t *e)
{
    std::string *rowId = static_cast<std::string *>(lv_event_get_user_data(e));
    LV_LOG_USER("Delete product: %s", rowId->c_str());

    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_obj_t *qty_cont = lv_obj_get_parent(btn); // ← qty_cont
    lv_obj_t *row = lv_obj_get_parent(qty_cont); // ← row
    lv_obj_del(row);
}
