#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <ctime>
#include <cctype>
#include <cstring>
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
#include "esp_task_wdt.h"

static const char *TAG = "UIEXTENSIONS";
static bool s_populating = false;
static std::string s_productSearchFilter;
static std::string s_selectedCategory;
static std::map<std::string, int> s_categoryExpiringCount;

static void update_selection_ui();
static void category_button_cb(lv_event_t *e);

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

void setProductSearchFilter(const std::string &filter)
{
    s_productSearchFilter = filter;
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
    char buf[64];
    if (selected_count == 1)
        snprintf(buf, sizeof(buf), "1 product selected");
    else
        snprintf(buf, sizeof(buf), "%d products selected", selected_count);
    lv_label_set_text(objects.products_filters_panel__product_selected_lbl, buf);
    lv_label_set_text(objects.recipes_filters_panel__product_selected_lbl, buf);

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
    lv_obj_t *frozen_cb = objects.product_edit__product_edit_frozen_cb;     // product_edit_frozen

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
    if (frozen_cb && lv_obj_is_valid(frozen_cb))
    {
        if (product.frozen)
            lv_obj_add_state(frozen_cb, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(frozen_cb, LV_STATE_CHECKED);
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

static void category_button_cb(lv_event_t *e)
{
    const char *category = static_cast<const char *>(lv_event_get_user_data(e));
    if (!category)
        return;
    s_selectedCategory = category;
    productsManager.populateProductList();
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
    // Reset scroll position because layout changed to horizontal
    scroll_y = 0;

    // Root setup: Light gray background, horizontal layout
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(root, 15, 0);

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

    // Step 1: Filter products (dropdown + search)
    std::vector<const Product *> filtered;
    for (const auto &p : products)
        filtered.push_back(&p);

    if (filter_idx == 1)
    {
        filtered.erase(std::remove_if(filtered.begin(), filtered.end(), [](const Product *p)
                                      {
        int days = days_until_expiry(p->expiry, p->frozen);
        return days == 9999 || days >= 7; }),
                       filtered.end());
    }

    if (s_productSearchFilter.length() >= 3)
    {
        filtered.erase(std::remove_if(filtered.begin(), filtered.end(),
                                      [](const Product *p)
                                      {
                                          std::string nameLower = p->name;
                                          std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                                          std::string filterLower = s_productSearchFilter;
                                          std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
                                          return nameLower.find(filterLower) == std::string::npos;
                                      }),
                       filtered.end());
    }

    // Step 2: Compute categories and expiring counts from filtered list
    s_categoryExpiringCount.clear();
    std::set<std::string> uniqueCategories;
    for (const Product *p : filtered)
    {
        uniqueCategories.insert(p->category);
        int days = days_until_expiry(p->expiry, p->frozen);
        if (days < 7 && days != 9999) // expiring soon
        {
            s_categoryExpiringCount[p->category]++;
        }
    }

    ESP_LOGI(TAG, "Filtered products: %d, Categories: %d", (int)filtered.size(), (int)uniqueCategories.size());
    ESP_LOGI(TAG, "Expiring counts by category:");
    for (const auto &entry : s_categoryExpiringCount)
    {
        ESP_LOGI(TAG, "  %s: %d", entry.first.c_str(), entry.second);
    }

    // Ensure selected category exists in filtered list
    if (!uniqueCategories.empty())
    {
        if (s_selectedCategory.empty() || uniqueCategories.find(s_selectedCategory) == uniqueCategories.end())
        {
            s_selectedCategory = *uniqueCategories.begin();
        }
    }
    else
    {
        s_selectedCategory.clear();
    }

    // Step 3: Create left sidebar container
    lv_obj_t *sidebar = lv_obj_create(root);
    lv_obj_set_width(sidebar, lv_pct(30)); // 30% width
    lv_obj_set_height(sidebar, lv_pct(100));
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sidebar, 10, 0);
    lv_obj_set_style_pad_all(sidebar, 10, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(sidebar, 8, 0);
    lv_obj_set_style_shadow_width(sidebar, 10, 0);
    lv_obj_set_style_shadow_color(sidebar, lv_color_hex(0x888888), 0);
    // Make sidebar scrollable vertically if many categories
    lv_obj_add_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sidebar, LV_SCROLLBAR_MODE_AUTO);

    // Step 4: Create right content container
    lv_obj_t *content_container = lv_obj_create(root);
    lv_obj_set_width(content_container, lv_pct(70));
    lv_obj_set_height(content_container, lv_pct(100));
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content_container, 10, 0);
    lv_obj_set_style_border_width(content_container, 0, 0);
    lv_obj_set_style_bg_color(content_container, lv_color_hex(0xF8F9FA), 0);
    lv_obj_add_flag(content_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content_container, LV_SCROLLBAR_MODE_AUTO);

    // Step 5: Create category buttons in sidebar
    for (const std::string &category : uniqueCategories)
    {
        lv_obj_t *btn = lv_btn_create(sidebar);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 120);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE9ECEF), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xDEE2E6), 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 8, 0); // Uniform padding

        // Highlight selected category
        if (category == s_selectedCategory)
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xE3F2FD), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x007AFF), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
        }

        // Create container for image + label
        lv_obj_t *container = lv_obj_create(btn);
        lv_obj_remove_style_all(container); // Remove default styling
        lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(container, 6, 0); // Gap between image and label
        lv_obj_center(container);                  // Center the container in the button

        // Image
        lv_obj_t *img = lv_image_create(container);
        if (category == "Baby")
        {
            lv_image_set_src(img, &img_baby);
        }
        else if (category == "Pet Supplies")
        {
            // ...
        }
        else if (category == "Wine, Beer & Spirit")
        {
            // ...
        }
        else if (category == "Produce")
        {
            lv_image_set_src(img, &img_produce);
        }
        else if (category == "Meat")
        {
            lv_image_set_src(img, &img_meat);
        }
        else if (category == "Seafood")
        {
            // ...
        }
        else if (category == "Deli")
        {
            // ...
        }
        else if (category == "Dairy")
        {
            // ...
        }
        else if (category == "Bakery")
        {
            // ...
        }
        else if (category == "Frozen Foods")
        {
            // ...
        }
        else if (category == "Beverages")
        {
            // ...
        }
        else if (category == "Snacks")
        {
            // ...
        }
        else if (category == "Breakfast & Cereal")
        {
            // ...
        }
        else if (category == "Soups & Canned Food")
        {
            // ...
        }
        else if (category == "Grains, Pasta & Sides")
        {
            // ...
        }
        else if (category == "Cooking & Baking")
        {
            // ...
        }
        else if (category == "Condiments & Dressing")
        {
            lv_image_set_src(img, &img_condiment);
        }
        else if (category == "Health & Personal Care")
        {
            // ...
        }
        else if (category == "Household & Cleaning")
        {
            // ...
        }
        else
        {
            // default case
        }

        lv_obj_set_size(img, 60, 60);

        // Category label (multiline wrap under image)
        // Label
        lv_obj_t *label = lv_label_create(container);
        lv_label_set_text(label, category.c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, 100); // Fixed width for wrapping
        lv_obj_set_style_text_color(label, lv_color_hex(0x495057), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        // Expiring count badge (red) in top right corner
        int expiringCount = s_categoryExpiringCount[category];
        if (expiringCount > 0)
        {
            lv_obj_t *badge = lv_label_create(btn);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", expiringCount);
            lv_label_set_text(badge, buf);
            lv_obj_set_style_bg_color(badge, lv_color_hex(0xE74C3C), 0);
            lv_obj_set_style_text_color(badge, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_pad_hor(badge, 8, 0);
            lv_obj_set_style_pad_ver(badge, 4, 0);
            lv_obj_set_style_radius(badge, 20, 0);
            lv_obj_set_style_text_font(badge, &lv_font_montserrat_14, 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0); // Ensure background is visible

            // Make badge float above flex layout
            lv_obj_add_flag(badge, LV_OBJ_FLAG_FLOATING);

            // Position badge top right inside button
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -5, 5);
            lv_obj_move_foreground(badge);
            ESP_LOGI(TAG, "Category '%s' has %d expiring products", category.c_str(), expiringCount);
        }

        // Attach category string as user data
        char *category_str = new char[category.size() + 1];
        strcpy(category_str, category.c_str());
        lv_obj_set_user_data(btn, category_str);
        lv_obj_add_event_cb(btn, category_button_cb, LV_EVENT_CLICKED, category_str);
        lv_obj_add_event_cb(btn, [](lv_event_t *e)
                            {
            char *str = static_cast<char*>(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            delete[] str; }, LV_EVENT_DELETE, nullptr);
    }

    // Step 6: Filter products by selected category
    std::vector<const Product *> category_filtered;
    for (const Product *p : filtered)
    {
        if (p->category == s_selectedCategory)
        {
            category_filtered.push_back(p);
        }
    }

    // Step 7: Sort the filtered list
    std::sort(category_filtered.begin(), category_filtered.end(), [sort_idx](const Product *a, const Product *b)
              {
    if (a->category != b->category)
        return a->category < b->category;

    if (sort_idx == 1)
    {
        bool a_valid = days_until_expiry(a->expiry, a->frozen) != 9999;
        bool b_valid = days_until_expiry(b->expiry, b->frozen) != 9999;
        if (a_valid != b_valid)
            return a_valid > b_valid;
        return a->expiry < b->expiry;
    }

    return a->name < b->name; });

    // Step 8: Create product rows in content container
    for (const Product *p : category_filtered)
    {
        // === THE ROW ===
        lv_obj_t *row = lv_obj_create(content_container);
        lv_obj_add_style(row, &style_row, 0);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 60);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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

        // Frozen indicator
        if (p->frozen)
        {
            lv_obj_t *frozen_img = lv_image_create(row);
            lv_image_set_src(frozen_img, &img_snowflake);
            lv_obj_set_style_translate_y(frozen_img, 5, 0);
        }

        // Expiry badge
        int days = days_until_expiry(p->expiry, p->frozen);

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

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Restore scroll position (only vertical for content container)
    lv_obj_scroll_to_y(content_container, scroll_y, LV_ANIM_OFF);

    // Initialize selection UI to correct state (panel hidden, count = 0)
    update_selection_ui();

    s_populating = false;
    lv_unlock();
}
