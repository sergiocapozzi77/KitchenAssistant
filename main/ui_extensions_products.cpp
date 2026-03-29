#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include "lvgl.h"
#include "esp_log.h"
#include "ProductService.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "ProductsManager.h"
#include "models.h"
#include "images.h"

static const char *TAG = "UIEXTENSIONS";
static bool s_populating = false;

static void update_selection_ui();

// // Calendar picker helpers
// static void add_calendar_button_to_expiry(lv_obj_t *expiry_ta);
// static void calendar_btn_cb(lv_event_t *e);
// static void calendar_close_cb(lv_event_t *e);
// static void calendar_day_selected_cb(lv_event_t *e);
// static void show_calendar_popup(lv_obj_t *expiry_ta);

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

struct RowClickCtx
{
    Product product;
    lv_obj_t *img = nullptr;
    bool selected = false;
};

static void free_row_click_ctx_cb(lv_event_t *e)
{
    delete (RowClickCtx *)lv_event_get_user_data(e);
}

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
        if (success)
        {
            ESP_LOGI(TAG, "Product deleted successfully");
            showSnackbar("Product deleted", 5000);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to delete product");
            showSnackbar("Failed to delete product", 5000);
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
    if (success)
    {
        ESP_LOGI(TAG, "Product deleted successfully");
        showSnackbar("Product deleted", 5000);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to delete product");
        showSnackbar("Failed to delete product", 5000);
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

static void row_prod_click_cb(lv_event_t *e)
{
    lv_obj_t *row = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!row)
        return;

    RowClickCtx *ctx = (RowClickCtx *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    ctx->selected = !ctx->selected;

    if (ctx->img && lv_obj_is_valid(ctx->img))
    {
        if (ctx->selected)
            lv_obj_clear_flag(ctx->img, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(ctx->img, LV_OBJ_FLAG_HIDDEN);
    }

    if (ctx->selected)
        productsManager.addSelectedProduct(ctx->product);
    else
        productsManager.removeSelectedProduct(ctx->product.rowId);

    update_selection_ui();
}

static void update_selection_ui()
{
    int selected_count = productsManager.getSelectedCount();

    // Update label text - with NULL/validity check
    if (objects.product_selected_lbl && lv_obj_is_valid(objects.product_selected_lbl))
    {
        char buf[64];
        if (selected_count == 1)
            snprintf(buf, sizeof(buf), "1 product selected");
        else
            snprintf(buf, sizeof(buf), "%d products selected", selected_count);
        lv_label_set_text(objects.product_selected_lbl, buf);
    }
    else
    {
        ESP_LOGW(TAG, "product_selected_lbl is NULL or invalid, skipping update");
    }

    ESP_LOGI(TAG, "Selection updated: %d products selected", selected_count);

    // Update panel visibility - with NULL/validity check
    if (objects.create_recipe_pnl && lv_obj_is_valid(objects.create_recipe_pnl))
    {
        if (selected_count > 0)
        {
            lv_obj_clear_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        ESP_LOGW(TAG, "create_recipe_pnl is NULL or invalid, skipping visibility update");
    }
}

void close_product_edit_modal()
{
    lv_obj_add_flag(objects.product_edit_modal, LV_OBJ_FLAG_HIDDEN);
}

static int category_to_index(const std::string &category)
{
    static const char *categories[] = {
        "Baby", "Bakery", "Beverages", "Breakfast & Cereal", "Condiments & Dressing",
        "Cooking & Baking", "Dairy", "Deli", "Frozen Foods", "Grains",
        "Pasta & Sides", "Health & Personal Care", "Household & Cleaning", "Meat",
        "Pet Supplies", "Produce", "Seafood", "Snacks", "Soups & Canned Food",
        "Wine, Beer & Spirit", "Other"};
    for (int i = 0; i < 21; ++i)
    {
        if (category == categories[i])
            return i;
    }
    return 20; // default to "Other"
}

void show_product_edit_modal()
{
    lv_obj_clear_flag(objects.product_edit_modal, LV_OBJ_FLAG_HIDDEN);
}

static void free_panel_rowid_cb(lv_event_t *e)
{
    std::string *data = static_cast<std::string *>(lv_event_get_user_data(e));
    delete data;
}

static void edit_btn_cb(lv_event_t *e)
{
    std::string *rowId = static_cast<std::string *>(lv_event_get_user_data(e));
    if (!rowId)
        return;

    // Show modal overlay (we'll create it below)
    show_product_edit_modal();

    // Load product data into modal widgets
    auto products = productsManager.getAllProducts();
    auto it = std::find_if(products.begin(), products.end(),
                           [&rowId](const Product &p)
                           { return p.rowId == *rowId; });
    if (it == products.end())
    {
        ESP_LOGE("UIEXTENSIONS", "Product with rowId %s not found", rowId->c_str());
        return;
    }
    const Product &product = *it;

    // Get modal widgets (startWidgetIndex = 0)
    lv_obj_t *name_ta = objects.product_edit__product_edit_name_ta;         // product_edit_name_ta
    lv_obj_t *expiry_ta = objects.product_edit__product_edit_expiry_ta;     // product_edit_expiry_ta
    lv_obj_t *category_dd = objects.product_edit__product_edit_category_dd; // product_edit_category_dd

    if (name_ta && lv_obj_is_valid(name_ta))
    {
        lv_textarea_set_text(name_ta, product.name.c_str());
    }
    if (expiry_ta && lv_obj_is_valid(expiry_ta))
    {
        lv_textarea_set_text(expiry_ta, product.expiry.c_str());
    }
    if (category_dd && lv_obj_is_valid(category_dd))
    {
        int idx = category_to_index(product.category);
        lv_dropdown_set_selected(category_dd, idx);
    }

    // Store rowId in panel user data for save action
    lv_obj_t *panel = objects.product_edit__product_edit_panel; // product_edit_panel
    if (panel && lv_obj_is_valid(panel))
    {
        // Copy rowId string and attach to panel; will be freed on panel deletion
        std::string *rowIdCopy = new std::string(*rowId);
        lv_obj_set_user_data(panel, rowIdCopy);
        // Set delete callback to free the copy
        lv_obj_add_event_cb(panel, free_panel_rowid_cb, LV_EVENT_DELETE, rowIdCopy);
    }
}

// === MAIN POPULATE FUNCTION ===

void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products)
{
    if (!root || !lv_obj_is_valid(root))
    {
        ESP_LOGE(TAG, "Root object is NULL or invalid");
        return;
    }

    lv_lock();

    // Re-check after lock (object could have been deleted)
    if (!lv_obj_is_valid(root))
    {
        ESP_LOGE(TAG, "Root object became invalid after lock");
        lv_unlock();
        return;
    }

    // Prevent re-entrant calls
    if (s_populating)
    {
        ESP_LOGW(TAG, "populateProductList already in progress, skipping");
        lv_unlock();
        return;
    }
    s_populating = true;

    // Initialize styles once
    init_styles();

    // Capture scroll position before cleaning
    lv_coord_t scroll_y = lv_obj_get_scroll_y(root);
    lv_obj_clean(root);

    // Root setup: Light gray background
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 15, 0);

    uint32_t filter_idx = 0;
    if (objects.product_filter_dropdown && lv_obj_is_valid(objects.product_filter_dropdown))
    {
        filter_idx = lv_dropdown_get_selected(objects.product_filter_dropdown);
    }
    // filter_idx = 0: show all
    // filter_idx = 1: show only product expirying < 7 days

    uint32_t sort_idx = 0;
    if (objects.product_sort_dropdown && lv_obj_is_valid(objects.product_sort_dropdown))
    {
        sort_idx = lv_dropdown_get_selected(objects.product_sort_dropdown);
    }
    // sort_idx = 0: sort alphabetically (by category, then name)
    // sort_idx = 1: sort by expiry (by category, then expiry date)

    std::vector<const Product *> sorted;
    for (const auto &p : products)
        sorted.push_back(&p);

    // Filter first (fewer elements to sort)
    if (filter_idx == 1)
    {
        sorted.erase(std::remove_if(sorted.begin(), sorted.end(), [](const Product *p)
                                    {
        int days = days_until_expiry(p->expiry);
        return days == 9999 || days >= 7; }),
                     sorted.end());
    }

    std::sort(sorted.begin(), sorted.end(), [sort_idx](const Product *a, const Product *b)
              {
    if (a->category != b->category)
        return a->category < b->category;

    if (sort_idx == 1)
    {
        // Push products with no expiry (9999) to the end
        bool a_valid = days_until_expiry(a->expiry) != 9999;
        bool b_valid = days_until_expiry(b->expiry) != 9999;
        if (a_valid != b_valid)
            return a_valid > b_valid;
        return a->expiry < b->expiry;
    }

    return a->name < b->name; });

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
            lv_obj_set_style_translate_y(title, 5, 0);
            lv_obj_set_style_translate_y(arrow, 5, 0);

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

        // // Checkbox (before product name)
        // lv_obj_t *checkbox = lv_checkbox_create(row);
        // lv_checkbox_set_text(checkbox, "");
        // lv_obj_set_style_pad_right(checkbox, 8, 0);
        // lv_obj_add_style(checkbox, &style_checkbox_indicator, LV_PART_INDICATOR);
        // lv_obj_add_style(checkbox, &style_checkbox_indicator, 0);
        // lv_obj_set_style_translate_y(checkbox, 5, 0);
        // // Attach product data to checkbox for selection tracking
        // CheckboxContext *checkbox_ctx = new CheckboxContext{*p};
        // lv_obj_add_event_cb(checkbox, checkbox_changed_cb, LV_EVENT_VALUE_CHANGED, checkbox_ctx);
        // lv_obj_add_event_cb(checkbox, free_checkbox_ctx_cb, LV_EVENT_DELETE, checkbox_ctx);

        // Selection image — hidden by default, shown when row is selected
        lv_obj_t *img = lv_image_create(row);
        lv_image_set_src(img, &img_restaurant);
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_y(img, 5, 0);

        // Product Name
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, p->name.c_str());
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, lv_color_hex(0x495057), 0);
        lv_obj_set_style_text_font(name, &ui_font_ext_font_montserrat_18, 0);
        lv_obj_set_style_translate_y(name, 5, 0);

        RowClickCtx *row_ctx = new RowClickCtx{*p, img, false};
        lv_obj_add_event_cb(row, row_prod_click_cb, LV_EVENT_CLICKED, row_ctx);
        lv_obj_add_event_cb(row, free_row_click_ctx_cb, LV_EVENT_DELETE, row_ctx);

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
            lv_obj_set_style_translate_y(expiry, 5, 0);
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
        lv_obj_set_size(btn_edit, 50, 50);

        lv_obj_t *lbl_edit = lv_label_create(btn_edit);
        lv_label_set_text(lbl_edit, LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(lbl_edit, lv_color_hex(theme_colors[active_theme_index][0]), 0);
        lv_obj_center(lbl_edit);
        lv_obj_set_style_translate_y(btn_edit, 5, 0);

        // Pass rowId
        std::string *edit_id = new std::string(p->rowId);
        lv_obj_add_event_cb(btn_edit, edit_btn_cb, LV_EVENT_CLICKED, edit_id);
        lv_obj_add_event_cb(btn_edit, free_rowid_cb, LV_EVENT_DELETE, edit_id);

        // // Delete Button
        // lv_obj_t *btn_del = lv_btn_create(row);
        // lv_obj_add_style(btn_del, &style_del_btn, 0);
        // lv_obj_set_size(btn_del, 50, 50);

        // lv_obj_t *lbl_del = lv_label_create(btn_del);
        // lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
        // lv_obj_set_style_text_color(lbl_del, lv_color_hex(0xE74C3C), 0);
        // lv_obj_center(lbl_del);
        // lv_obj_set_style_translate_y(btn_del, 5, 0);

        // std::string *rowId = new std::string(p->rowId);
        // lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED, rowId);
        // lv_obj_add_event_cb(btn_del, free_rowid_cb, LV_EVENT_DELETE, rowId);

        //
        // lv_obj_add_flag(qty_cont, LV_OBJ_FLAG_HIDDEN);
    }
    // Restore scroll position
    lv_obj_scroll_to_y(root, scroll_y, LV_ANIM_OFF);

    // Initialize selection UI to correct state (panel hidden, count = 0)
    update_selection_ui();

    s_populating = false;
    lv_unlock();
}
