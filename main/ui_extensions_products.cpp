#include <vector>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "ProductService.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "ProductsManager.h"
#include "models.h"

static const char *TAG = "UIEXTENSIONS";

static void update_selection_ui();

// === PRODUCT-SPECIFIC STRUCTS ===

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
        // Call service to delete product
        bool success = productService.deleteProduct(ctx->rowId);
        if (success) {
            ESP_LOGI(TAG, "Product deleted successfully");
        } else {
            ESP_LOGE(TAG, "Failed to delete product");
            // Still delete UI row; product will reappear on next sync if deletion failed
        }

        // Deferred deletion to avoid re-entrancy issues
        if (ctx->row)
        {
            if (ctx->row)
                lv_obj_delete_async(ctx->row);
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
    // Call service to delete product
    bool success = productService.deleteProduct(*rowId);
    if (success) {
        ESP_LOGI(TAG, "Product deleted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to delete product");
        // Still delete UI row; product will reappear on next sync if deletion failed
    }

    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!btn)
        return;

    lv_obj_t *row = lv_obj_get_parent(btn);
    if (!row)
        return;

    // Deferred deletion to avoid re-entrancy issues
    if (row)
        lv_obj_delete_async(row);
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

static void checkbox_changed_cb(lv_event_t *e)
{
    lv_obj_t *checkbox = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!checkbox)
        return;

    CheckboxContext *ctx = (CheckboxContext *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    // Add or remove product from selected set
    if (lv_obj_has_state(checkbox, LV_STATE_CHECKED))
    {
        productsManager.addSelectedProduct(ctx->product);
    }
    else
    {
        productsManager.removeSelectedProduct(ctx->product.rowId);
    }

    update_selection_ui();
}

static void update_selection_ui()
{
    int selected_count = productsManager.getSelectedCount();

    // Update panel visibility
    if (selected_count > 0)
    {
        lv_obj_clear_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
    }

    // Update label text
    char buf[64];
    if (selected_count == 1)
        snprintf(buf, sizeof(buf), "1 product selected");
    else
        snprintf(buf, sizeof(buf), "%d products selected", selected_count);
    lv_label_set_text(objects.product_selected_lbl, buf);

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
        lv_obj_add_style(checkbox, &style_checkbox_indicator, 0);

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
        lv_obj_set_style_text_font(name, &ui_font_ext_font_montserrat_18, 0);

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
            else if (days < 7)
                snprintf(buf, sizeof(buf), "%dd left", days);
            else
                lv_obj_add_flag(expiry, LV_OBJ_FLAG_HIDDEN);

            lv_label_set_text(expiry, buf);
            lv_obj_set_style_bg_color(expiry, get_expiry_color(days), 0);
        }

        // Quantity Selector Container (commented out in original)
        // lv_obj_t *qty_cont = lv_obj_create(row);
        // lv_obj_add_style(qty_cont, &style_qty_cont, 0);
        // lv_obj_set_size(qty_cont, 144, 36);
        // lv_obj_clear_flag(qty_cont, LV_OBJ_FLAG_SCROLLABLE);
        // lv_obj_set_flex_flow(qty_cont, LV_FLEX_FLOW_ROW);
        // lv_obj_set_flex_align(qty_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        //
        // // Minus Button
        // lv_obj_t *btn_minus = lv_btn_create(qty_cont);
        // lv_obj_add_style(btn_minus, &style_qty_btn, 0);
        // lv_obj_set_size(btn_minus, 30, 36);
        // lv_obj_t *lbl_minus = lv_label_create(btn_minus);
        // lv_label_set_text(lbl_minus, LV_SYMBOL_MINUS);
        // lv_obj_set_style_text_color(lbl_minus, lv_color_hex(0x007AFF), 0);
        // lv_obj_center(lbl_minus);
        //
        // // Quantity Label
        // lv_obj_t *qty_val = lv_label_create(qty_cont);
        // lv_label_set_text_fmt(qty_val, "%d", p->quantity);
        // lv_obj_set_style_bg_color(qty_val, lv_color_hex(0xFFFFFF), 0);
        // lv_obj_set_style_text_font(qty_val, &lv_font_montserrat_14, 0);
        // lv_obj_set_style_pad_hor(qty_val, 5, 0);
        //
        // // Plus Button
        // lv_obj_t *btn_plus = lv_btn_create(qty_cont);
        // lv_obj_add_style(btn_plus, &style_qty_btn, 0);
        // lv_obj_set_size(btn_plus, 30, 36);
        // lv_obj_t *lbl_plus = lv_label_create(btn_plus);
        // lv_label_set_text(lbl_plus, LV_SYMBOL_PLUS);
        // lv_obj_set_style_text_color(lbl_plus, lv_color_hex(0x007AFF), 0);
        // lv_obj_center(lbl_plus);
        //
        // // Shared QtyContext — owned by btn_minus, freed on its deletion
        // QtyContext *qty_ctx = new QtyContext{qty_val, row, p->rowId, p->quantity};
        // lv_obj_add_event_cb(btn_minus, qty_minus_cb, LV_EVENT_CLICKED, qty_ctx);
        // lv_obj_add_event_cb(btn_minus, free_qty_ctx_cb, LV_EVENT_DELETE, qty_ctx);
        // lv_obj_add_event_cb(btn_plus, qty_plus_cb, LV_EVENT_CLICKED, qty_ctx);
        //

        // Edit Button
        lv_obj_t *btn_edit = lv_btn_create(row);
        lv_obj_add_style(btn_edit, &style_del_btn, 0);
        lv_obj_set_size(btn_edit, 56, 56);

        lv_obj_t *lbl_edit = lv_label_create(btn_edit);
        lv_label_set_text(lbl_edit, LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(lbl_edit, lv_color_hex(theme_colors[active_theme_index][0]), 0);
        lv_obj_center(lbl_edit);

        // Delete Button
        lv_obj_t *btn_del = lv_btn_create(row);
        lv_obj_add_style(btn_del, &style_del_btn, 0);
        lv_obj_set_size(btn_del, 56, 56);

        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(lbl_del, lv_color_hex(0xE74C3C), 0);
        lv_obj_center(lbl_del);

        std::string *rowId = new std::string(p->rowId);
        lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED, rowId);
        lv_obj_add_event_cb(btn_del, free_rowid_cb, LV_EVENT_DELETE, rowId);

        //
        // lv_obj_add_flag(qty_cont, LV_OBJ_FLAG_HIDDEN);
    }

    // Initialize selection UI to correct state (panel hidden, count = 0)
    update_selection_ui();

    lv_unlock();
}