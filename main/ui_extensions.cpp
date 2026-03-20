#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "ProductService.h"
#include "ui_extensions.h"
#include <ctime>
#include <cmath>

static const char *TAG = "UIEXTENSIONS";

// External UI objects (declare in your main UI code or EEZ-generated files):
// lv_obj_t *objects_createRecipe_pnl;    // Panel to show when products selected
// lv_obj_t *objects_productSelected_lbl; // Label showing selection count
extern lv_obj_t *objects_createRecipe_pnl;
extern lv_obj_t *objects_productSelected_lbl;

// Track selected products (rowId -> Product)
static std::map<std::string, Product> selected_products;

// === REUSABLE STYLES (created once, applied many times) ===
static lv_style_t style_card;
static lv_style_t style_header;
static lv_style_t style_row;
static lv_style_t style_qty_cont;
static lv_style_t style_qty_btn;
static lv_style_t style_del_btn;
static lv_style_t style_expiry_badge;
static lv_style_t style_checkbox_indicator;
static bool styles_initialized = false;

static void init_styles()
{
    if (styles_initialized)
        return;

    // Card style
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0xFFFFFF));
    lv_style_set_radius(&style_card, 12);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0xE0E0E0));
    lv_style_set_pad_all(&style_card, 0);

    // Header style
    lv_style_init(&style_header);
    lv_style_set_bg_color(&style_header, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_header, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_header, 0);
    lv_style_set_pad_hor(&style_header, 15);

    // Row style
    lv_style_init(&style_row);
    lv_style_set_border_side(&style_row, LV_BORDER_SIDE_TOP);
    lv_style_set_border_width(&style_row, 1);
    lv_style_set_border_color(&style_row, lv_color_hex(0xF1F3F5));
    lv_style_set_pad_hor(&style_row, 15);
    lv_style_set_bg_color(&style_row, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_row, LV_OPA_COVER);

    // Quantity container style
    lv_style_init(&style_qty_cont);
    lv_style_set_radius(&style_qty_cont, 4);
    lv_style_set_bg_color(&style_qty_cont, lv_color_hex(0xF8F9FA));
    lv_style_set_border_width(&style_qty_cont, 1);
    lv_style_set_border_color(&style_qty_cont, lv_color_hex(0xE9ECEF));
    lv_style_set_pad_all(&style_qty_cont, 0);

    // Quantity button style
    lv_style_init(&style_qty_btn);
    lv_style_set_bg_color(&style_qty_btn, lv_color_hex(0xF8F9FA));
    lv_style_set_bg_opa(&style_qty_btn, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_qty_btn, 0);

    // Delete button style
    lv_style_init(&style_del_btn);
    lv_style_set_bg_color(&style_del_btn, lv_color_hex(0xF8F9FA));
    lv_style_set_bg_opa(&style_del_btn, LV_OPA_COVER);
    lv_style_set_shadow_opa(&style_del_btn, 0);
    lv_style_set_border_width(&style_del_btn, 0);
    lv_style_set_border_side(&style_del_btn, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&style_del_btn, 1);
    lv_style_set_border_color(&style_del_btn, lv_color_hex(0xE9ECEF));

    // Expiry badge style
    lv_style_init(&style_expiry_badge);
    lv_style_set_bg_opa(&style_expiry_badge, LV_OPA_COVER);
    lv_style_set_text_color(&style_expiry_badge, lv_color_white());
    lv_style_set_pad_hor(&style_expiry_badge, 8);
    lv_style_set_pad_ver(&style_expiry_badge, 2);
    lv_style_set_radius(&style_expiry_badge, 6);

    // Checkbox indicator style
    lv_style_init(&style_checkbox_indicator);
    lv_style_set_border_color(&style_checkbox_indicator, lv_color_hex(0x007AFF));
    lv_style_set_bg_color(&style_checkbox_indicator, lv_color_hex(0x007AFF));

    styles_initialized = true;
    ESP_LOGI(TAG, "Styles initialized");
}

// Forward declarations
static void delete_btn_cb(lv_event_t *e);
static void qty_minus_cb(lv_event_t *e);
static void qty_plus_cb(lv_event_t *e);
static void group_toggle_cb(lv_event_t *e);
static void row_click_cb(lv_event_t *e);
static void checkbox_changed_cb(lv_event_t *e);
static void update_selection_ui();

// === STRUCTS ===

struct QtyContext
{
    lv_obj_t *qty_val;
    lv_obj_t *row;
    std::string rowId;
    int quantity;
};

struct GroupUI
{
    lv_obj_t *content;
    lv_obj_t *arrow;
    bool collapsed;
};

struct DeleteCtx
{
    lv_obj_t *obj;
};

struct CheckboxContext
{
    Product product;
};

// === CLEANUP CALLBACKS ===

static void free_qty_ctx_cb(lv_event_t *e)
{
    delete (QtyContext *)lv_event_get_user_data(e);
}

static void free_group_cb(lv_event_t *e)
{
    delete (GroupUI *)lv_event_get_user_data(e);
}

static void free_rowid_cb(lv_event_t *e)
{
    delete (std::string *)lv_event_get_user_data(e);
}

static void free_checkbox_ctx_cb(lv_event_t *e)
{
    delete (CheckboxContext *)lv_event_get_user_data(e);
}

static void deferred_delete_cb(void *user_data)
{
    DeleteCtx *ctx = (DeleteCtx *)user_data;
    if (ctx && ctx->obj)
    {
        lv_obj_del(ctx->obj);
    }
    delete ctx;
}

// === HELPER FUNCTIONS ===

static int days_until_expiry(const std::string &isoDate)
{
    if (isoDate.empty())
        return 9999;

    std::tm tm_exp = {};
    int parsed = sscanf(isoDate.c_str(), "%d-%d-%d",
                        &tm_exp.tm_year,
                        &tm_exp.tm_mon,
                        &tm_exp.tm_mday);

    // Validate parsing succeeded
    if (parsed != 3)
    {
        ESP_LOGW(TAG, "Invalid date format: %s", isoDate.c_str());
        return 9999;
    }

    // Validate date ranges
    if (tm_exp.tm_year < 1900 || tm_exp.tm_year > 3000 ||
        tm_exp.tm_mon < 1 || tm_exp.tm_mon > 12 ||
        tm_exp.tm_mday < 1 || tm_exp.tm_mday > 31)
    {
        ESP_LOGW(TAG, "Date out of range: %s", isoDate.c_str());
        return 9999;
    }

    tm_exp.tm_year -= 1900;
    tm_exp.tm_mon -= 1;

    time_t exp_time = mktime(&tm_exp);
    if (exp_time == -1)
    {
        ESP_LOGW(TAG, "mktime failed for date: %s", isoDate.c_str());
        return 9999;
    }

    // Normalize now to midnight so we compare dates, not timestamps
    time_t now_raw = time(nullptr);
    std::tm tm_now = {};
    localtime_r(&now_raw, &tm_now);
    tm_now.tm_hour = 0;
    tm_now.tm_min = 0;
    tm_now.tm_sec = 0;
    time_t now_midnight = mktime(&tm_now);

    double diff = difftime(exp_time, now_midnight);
    return (int)round(diff / (60.0 * 60.0 * 24.0));
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

// === EVENT CALLBACKS ===

static void qty_minus_cb(lv_event_t *e)
{
    QtyContext *ctx = (QtyContext *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    ctx->quantity--;

    if (ctx->quantity <= 0)
    {
        ESP_LOGI(TAG, "Auto-delete product: %s", ctx->rowId.c_str());
        // TODO: call your service to delete product with ctx->rowId

        // Deferred deletion to avoid re-entrancy issues
        if (ctx->row)
        {
            DeleteCtx *del_ctx = new DeleteCtx{ctx->row};
            lv_async_call(deferred_delete_cb, del_ctx);
        }
        return;
    }

    if (ctx->qty_val)
    {
        lv_label_set_text_fmt(ctx->qty_val, "%d", ctx->quantity);
    }

    ESP_LOGI(TAG, "Update qty: %s -> %d", ctx->rowId.c_str(), ctx->quantity);
    // TODO: call your service to update quantity: ctx->rowId, ctx->quantity
}

static void qty_plus_cb(lv_event_t *e)
{
    QtyContext *ctx = (QtyContext *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    ctx->quantity++;

    if (ctx->qty_val)
    {
        lv_label_set_text_fmt(ctx->qty_val, "%d", ctx->quantity);
    }

    ESP_LOGI(TAG, "Update qty: %s -> %d", ctx->rowId.c_str(), ctx->quantity);
    // TODO: call your service to update quantity: ctx->rowId, ctx->quantity
}

static void delete_btn_cb(lv_event_t *e)
{
    std::string *rowId = static_cast<std::string *>(lv_event_get_user_data(e));
    if (!rowId)
        return;

    ESP_LOGI(TAG, "Delete product: %s", rowId->c_str());
    // TODO: call your service to delete product

    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!btn)
        return;

    lv_obj_t *qty_cont = lv_obj_get_parent(btn);
    if (!qty_cont)
        return;

    lv_obj_t *row = lv_obj_get_parent(qty_cont);
    if (!row)
        return;

    // Deferred deletion to avoid re-entrancy issues
    DeleteCtx *del_ctx = new DeleteCtx{row};
    lv_async_call(deferred_delete_cb, del_ctx);
}

static void group_toggle_cb(lv_event_t *e)
{
    GroupUI *group = (GroupUI *)lv_event_get_user_data(e);
    if (!group || !group->content || !group->arrow)
        return;

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

static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    if (!checkbox)
        return;

    // Toggle checkbox state
    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
        lv_obj_clear_state(checkbox, LV_STATE_CHECKED);
    else
        lv_obj_add_state(checkbox, LV_STATE_CHECKED);

    // Manually send VALUE_CHANGED event to trigger checkbox_changed_cb
    lv_obj_send_event(checkbox, LV_EVENT_VALUE_CHANGED, NULL);
}

static void checkbox_changed_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = lv_event_get_target(e);
    if (!checkbox)
        return;

    CheckboxContext *ctx = (CheckboxContext *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    // Add or remove product from selected set
    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
    {
        selected_products[ctx->product.rowId] = ctx->product;
    }
    else
    {
        selected_products.erase(ctx->product.rowId);
    }

    update_selection_ui();
}

static void update_selection_ui()
{
    int selected_count = selected_products.size();

    // Update panel visibility
    if (objects_createRecipe_pnl)
    {
        if (selected_count > 0)
        {
            lv_obj_clear_flag(objects_createRecipe_pnl, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(objects_createRecipe_pnl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update label text
    if (objects_productSelected_lbl)
    {
        char buf[64];
        if (selected_count == 1)
            snprintf(buf, sizeof(buf), "1 product selected");
        else
            snprintf(buf, sizeof(buf), "%d products selected", selected_count);
        lv_label_set_text(objects_productSelected_lbl, buf);
    }

    ESP_LOGI(TAG, "Selection updated: %d products selected", selected_count);
}

// === MAIN POPULATE FUNCTION ===

void populateProductList(lv_obj_t *root, const std::vector<Product> &products)
{
    if (!root)
    {
        ESP_LOGE(TAG, "Root object is NULL");
        return;
    }

    lv_lock();

    // Initialize styles once
    init_styles();

    lv_obj_clean(root);

    // Clear selected products since we're rebuilding the list
    selected_products.clear();

    // Root setup: Light gray background
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 15, 0);

    // Sort products by category
    std::vector<const Product *> sorted;
    for (const auto &p : products)
        sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [](const Product *a, const Product *b)
              { return a->category < b->category; });

    std::string currentCategory;
    lv_obj_t *content = nullptr;

    for (const Product *p : sorted)
    {
        // Create new category card if category changed
        if (p->category != currentCategory)
        {
            currentCategory = p->category;

            // The Card: Use reusable style
            lv_obj_t *card = lv_obj_create(root);
            lv_obj_add_style(card, &style_card, 0);
            lv_obj_set_width(card, lv_pct(100));
            lv_obj_set_height(card, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            // Header: Use reusable style
            lv_obj_t *header = lv_btn_create(card);
            lv_obj_add_style(header, &style_header, 0);
            lv_obj_set_width(header, lv_pct(100));
            lv_obj_set_height(header, 50);
            lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *title = lv_label_create(header);
            lv_label_set_text(title, currentCategory.c_str());
            lv_obj_set_style_text_color(title, lv_color_hex(0x495057), 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

            lv_obj_t *arrow = lv_label_create(header);
            lv_label_set_text(arrow, LV_SYMBOL_DOWN);
            lv_obj_set_style_text_color(arrow, lv_color_hex(0xADB5BD), 0);

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

        if (!content)
            continue; // Safety check

        // === THE ROW ===
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_add_style(row, &style_row, 0);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 60);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Checkbox (before product name)
        lv_obj_t *checkbox = lv_checkbox_create(row);
        lv_checkbox_set_text(checkbox, "");
        lv_obj_set_style_pad_right(checkbox, 8, 0);
        lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR);
        lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);

        // Attach product data to checkbox for selection tracking
        CheckboxContext *checkbox_ctx = new CheckboxContext{*p};
        lv_obj_add_event_cb(checkbox, checkbox_changed_cb, LV_EVENT_VALUE_CHANGED, checkbox_ctx);
        lv_obj_add_event_cb(checkbox, free_checkbox_ctx_cb, LV_EVENT_DELETE, checkbox_ctx);

        // Row click selects the checkbox
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, checkbox);

        // Product Name
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, p->name.c_str());
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, lv_color_hex(0x495057), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);

        // Expiry badge
        int days = days_until_expiry(p->expiry);

        if (days != 9999) // Only show expiry if date is valid
        {
            lv_obj_t *expiry = lv_label_create(row);
            lv_obj_add_style(expiry, &style_expiry_badge, 0);

            char buf[32];
            if (days < 0)
                snprintf(buf, sizeof(buf), "Expired");
            else if (days == 0)
                snprintf(buf, sizeof(buf), "Today");
            else if (days == 1)
                snprintf(buf, sizeof(buf), "1d left");
            else
                snprintf(buf, sizeof(buf), "%dd left", days);

            lv_label_set_text(expiry, buf);
            lv_obj_set_style_bg_color(expiry, get_expiry_color(days), 0);
        }

        // Quantity Selector Container
        lv_obj_t *qty_cont = lv_obj_create(row);
        lv_obj_add_style(qty_cont, &style_qty_cont, 0);
        lv_obj_set_size(qty_cont, 144, 36);
        lv_obj_clear_flag(qty_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(qty_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(qty_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Minus Button
        lv_obj_t *btn_minus = lv_btn_create(qty_cont);
        lv_obj_add_style(btn_minus, &style_qty_btn, 0);
        lv_obj_set_size(btn_minus, 30, 36);
        lv_obj_t *lbl_minus = lv_label_create(btn_minus);
        lv_label_set_text(lbl_minus, LV_SYMBOL_MINUS);
        lv_obj_set_style_text_color(lbl_minus, lv_color_hex(0x007AFF), 0);
        lv_obj_center(lbl_minus);

        // Quantity Label
        lv_obj_t *qty_val = lv_label_create(qty_cont);
        lv_label_set_text_fmt(qty_val, "%d", p->quantity);
        lv_obj_set_style_bg_color(qty_val, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(qty_val, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_hor(qty_val, 5, 0);

        // Plus Button
        lv_obj_t *btn_plus = lv_btn_create(qty_cont);
        lv_obj_add_style(btn_plus, &style_qty_btn, 0);
        lv_obj_set_size(btn_plus, 30, 36);
        lv_obj_t *lbl_plus = lv_label_create(btn_plus);
        lv_label_set_text(lbl_plus, LV_SYMBOL_PLUS);
        lv_obj_set_style_text_color(lbl_plus, lv_color_hex(0x007AFF), 0);
        lv_obj_center(lbl_plus);

        // Shared QtyContext — owned by btn_minus, freed on its deletion
        QtyContext *qty_ctx = new QtyContext{qty_val, row, p->rowId, p->quantity};
        lv_obj_add_event_cb(btn_minus, qty_minus_cb, LV_EVENT_CLICKED, qty_ctx);
        lv_obj_add_event_cb(btn_minus, free_qty_ctx_cb, LV_EVENT_DELETE, qty_ctx);
        lv_obj_add_event_cb(btn_plus, qty_plus_cb, LV_EVENT_CLICKED, qty_ctx);

        // Delete Button
        lv_obj_t *btn_del = lv_btn_create(qty_cont);
        lv_obj_add_style(btn_del, &style_del_btn, 0);
        lv_obj_set_size(btn_del, 36, 36);

        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(lbl_del, lv_color_hex(0xE74C3C), 0);
        lv_obj_center(lbl_del);

        std::string *rowId = new std::string(p->rowId);
        lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED, rowId);
        lv_obj_add_event_cb(btn_del, free_rowid_cb, LV_EVENT_DELETE, rowId);
    }

    // Initialize selection UI to correct state (panel hidden, count = 0)
    update_selection_ui();

    lv_unlock();
}

// Helper function to get selected products
std::vector<Product> getSelectedProducts()
{
    std::vector<Product> result;
    result.reserve(selected_products.size());

    for (const auto &pair : selected_products)
    {
        result.push_back(pair.second);
    }

    ESP_LOGI(TAG, "Retrieved %d selected products", result.size());
    return result;
}

// Helper function to clear selected products
void clearSelectedProducts()
{
    selected_products.clear();
    update_selection_ui();
    ESP_LOGI(TAG, "Cleared all selected products");
}