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

static const char *TAG = "UIEXTENSIONS";
static bool s_populating = false;
static std::string s_productSearchFilter;
static std::string s_selectedCategory;
static std::map<std::string, int> s_categoryExpiringCount;

static void update_selection_ui();
static void category_button_cb(lv_event_t *e);

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

struct RowClickCtx
{
    Product product;
    lv_obj_t *img = nullptr;
    bool selected = false;
};

// === CLEANUP CALLBACKS ===

static void free_row_click_ctx_cb(lv_event_t *e)
{
    delete (RowClickCtx *)lv_event_get_user_data(e);
}

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

// === PUBLIC API ===

void setProductSearchFilter(const std::string &filter)
{
    s_productSearchFilter = filter;
}

// === EVENT CALLBACKS ===

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

    char buf[64];
    if (selected_count == 1)
        snprintf(buf, sizeof(buf), "1 product selected");
    else
        snprintf(buf, sizeof(buf), "%d products selected", selected_count);

    lv_label_set_text(objects.products_filters_panel__product_selected_lbl, buf);
    lv_label_set_text(objects.recipes_filters_panel__product_selected_lbl, buf);

    ESP_LOGI(TAG, "Selection updated: %d products selected", selected_count);

    if (objects.create_recipe_pnl && lv_obj_is_valid(objects.create_recipe_pnl))
    {
        if (selected_count > 0)
            lv_obj_clear_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objects.create_recipe_pnl, LV_OBJ_FLAG_HIDDEN);
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

void show_product_edit_modal()
{
    lv_obj_clear_flag(objects.product_edit_modal, LV_OBJ_FLAG_HIDDEN);
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
    return 20;
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

    show_product_edit_modal();

    auto products = productsManager.getAllProducts();
    auto it = std::find_if(products.begin(), products.end(),
                           [&rowId](const Product &p)
                           { return p.rowId == *rowId; });
    if (it == products.end())
    {
        ESP_LOGE(TAG, "Product with rowId %s not found", rowId->c_str());
        return;
    }
    const Product &product = *it;

    lv_obj_t *name_ta = objects.product_edit__product_edit_name_ta;
    lv_obj_t *expiry_ta = objects.product_edit__product_edit_expiry_ta;
    lv_obj_t *category_dd = objects.product_edit__product_edit_category_dd;
    lv_obj_t *frozen_cb = objects.product_edit__product_edit_frozen_cb;

    if (name_ta && lv_obj_is_valid(name_ta))
        lv_textarea_set_text(name_ta, product.name.c_str());

    if (expiry_ta && lv_obj_is_valid(expiry_ta))
        lv_textarea_set_text(expiry_ta, product.expiry.c_str());

    if (category_dd && lv_obj_is_valid(category_dd))
        lv_dropdown_set_selected(category_dd, category_to_index(product.category));

    if (frozen_cb && lv_obj_is_valid(frozen_cb))
    {
        if (product.frozen)
            lv_obj_add_state(frozen_cb, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(frozen_cb, LV_STATE_CHECKED);
    }

    lv_obj_t *panel = objects.product_edit__product_edit_panel;
    if (panel && lv_obj_is_valid(panel))
    {
        // Remove any previously attached free callback to avoid double-free
        // if the modal is reused without the panel being deleted first.
        lv_obj_remove_event_cb_with_user_data(panel, free_panel_rowid_cb,
                                              lv_obj_get_user_data(panel));
        std::string *old = static_cast<std::string *>(lv_obj_get_user_data(panel));
        delete old;

        std::string *rowIdCopy = new std::string(*rowId);
        lv_obj_set_user_data(panel, rowIdCopy);
        lv_obj_add_event_cb(panel, free_panel_rowid_cb, LV_EVENT_DELETE, rowIdCopy);
    }
}

static void category_button_cb(lv_event_t *e)
{
    // user_data is a heap-allocated std::string* owned by the event itself
    const std::string *category = static_cast<const std::string *>(lv_event_get_user_data(e));
    if (!category)
        return;
    s_selectedCategory = *category;
    productsManager.populateProductList();
}

// ---------------------------------------------------------------------------
// Helper: set the category image on an lv_image widget
// ---------------------------------------------------------------------------
static void set_category_image(lv_obj_t *img, const std::string &category)
{
    if (category == "Baby")
        lv_image_set_src(img, &img_baby);
    else if (category == "Wine, Beer & Spirit")
        lv_image_set_src(img, &img_wine);
    else if (category == "Produce")
        lv_image_set_src(img, &img_produce);
    else if (category == "Meat")
        lv_image_set_src(img, &img_meat);
    else if (category == "Dairy")
        lv_image_set_src(img, &img_dairy);
    else if (category == "Bakery")
        lv_image_set_src(img, &img_bakery);
    else if (category == "Snacks")
        lv_image_set_src(img, &img_snacks);
    else if (category == "Condiments & Dressing")
        lv_image_set_src(img, &img_condiment);
    else
        lv_image_set_src(img, &img_other);
    // Categories with no dedicated image fall through to img_other.
    // Add further cases here as assets become available.

    /*
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
            lv_image_set_src(img, &img_wine);
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
            lv_image_set_src(img, &img_dairy);
        }
        else if (category == "Bakery")
        {
            lv_image_set_src(img, &img_bakery);
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
            lv_image_set_src(img, &img_snacks);
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
            lv_image_set_src(img, &img_other);
        }

    */
}

// ---------------------------------------------------------------------------
// Sidebar builder – called while holding lv_lock()
// ---------------------------------------------------------------------------
static void build_sidebar(lv_obj_t *sidebar,
                          const std::set<std::string> &uniqueCategories,
                          const std::map<std::string, int> &expiringCount)
{
    lv_obj_clean(sidebar);

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
        lv_obj_set_style_pad_all(btn, 8, 0);

        if (category == s_selectedCategory)
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xE3F2FD), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x007AFF), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
        }

        // Icon + label container
        lv_obj_t *container = lv_obj_create(btn);
        lv_obj_remove_style_all(container);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(container, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(container, 6, 0);
        lv_obj_center(container);

        lv_obj_t *img = lv_image_create(container);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        set_category_image(img, category);
        lv_obj_set_size(img, 60, 60);

        lv_obj_t *label = lv_label_create(container);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(label, category.c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_color(label, lv_color_hex(0x495057), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        // Expiry badge
        auto it = expiringCount.find(category);
        if (it != expiringCount.end() && it->second > 0)
        {
            lv_obj_t *badge = lv_label_create(btn);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", it->second);
            lv_label_set_text(badge, buf);
            lv_obj_set_style_bg_color(badge, lv_color_hex(0xE74C3C), 0);
            lv_obj_set_style_text_color(badge, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_pad_hor(badge, 8, 0);
            lv_obj_set_style_pad_ver(badge, 4, 0);
            lv_obj_set_style_radius(badge, 20, 0);
            lv_obj_set_style_text_font(badge, &lv_font_montserrat_14, 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_add_flag(badge, LV_OBJ_FLAG_FLOATING);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -5, 5);
            lv_obj_move_foreground(badge);

            ESP_LOGI(TAG, "Category '%s' has %d expiring products", category.c_str(), it->second);
        }

        // Attach category as heap std::string – freed on DELETE, passed to callback
        std::string *cat_str = new std::string(category);
        lv_obj_add_event_cb(btn, category_button_cb, LV_EVENT_CLICKED, cat_str);
        lv_obj_add_event_cb(btn, [](lv_event_t *e)
                            { delete static_cast<std::string *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, cat_str);
        // NOTE: lv_obj_set_user_data is intentionally NOT used here so that
        //       nothing else can clobber cat_str before DELETE fires.
    }
}

// ---------------------------------------------------------------------------
// Content row builder – called while holding lv_lock()
// Yields the lock every YIELD_INTERVAL rows so LVGL can render between batches.
// ---------------------------------------------------------------------------
static constexpr int ROW_YIELD_INTERVAL = 8;

static void build_content_rows(lv_obj_t *content_container,
                               const std::vector<const Product *> &category_filtered,
                               const std::set<std::string> &selectedRowIds)
{
    lv_obj_clean(content_container);

    int row_index = 0;
    for (const Product *p : category_filtered)
    {
        // Yield every N rows so LVGL can render and other tasks can run.
        // We re-validate the container after re-acquiring the lock.
        if (row_index > 0 && (row_index % ROW_YIELD_INTERVAL) == 0)
        {
            lv_unlock();
            taskYIELD();
            lv_lock();

            if (!lv_obj_is_valid(content_container))
            {
                ESP_LOGW(TAG, "content_container invalidated during population, aborting");
                return;
            }
        }
        row_index++;

        lv_obj_t *row = lv_obj_create(content_container);
        lv_obj_add_style(row, &style_row, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 60);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Selection indicator image
        lv_obj_t *sel_img = lv_image_create(row);
        lv_obj_clear_flag(sel_img, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(sel_img, &img_restaurant);
        bool isSelected = selectedRowIds.find(p->rowId) != selectedRowIds.end();
        if (!isSelected)
            lv_obj_add_flag(sel_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_y(sel_img, 5, 0);

        // Product name
        lv_obj_t *name = lv_label_create(row);
        lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(name, p->name.c_str());
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, lv_color_hex(0x495057), 0);
        lv_obj_set_style_text_font(name, &ui_font_ext_font_montserrat_18, 0);
        lv_obj_set_style_translate_y(name, 5, 0);

        // Row click context – owns the selected state and image reference
        RowClickCtx *row_ctx = new RowClickCtx{*p, sel_img, isSelected};
        lv_obj_add_event_cb(row, row_prod_click_cb, LV_EVENT_CLICKED, row_ctx);
        lv_obj_add_event_cb(row, free_row_click_ctx_cb, LV_EVENT_DELETE, row_ctx);

        // Frozen indicator
        if (p->frozen)
        {
            lv_obj_t *frozen_img = lv_image_create(row);
            lv_obj_clear_flag(frozen_img, LV_OBJ_FLAG_CLICKABLE);
            lv_image_set_src(frozen_img, &img_snowflake);
            lv_obj_set_style_translate_y(frozen_img, 5, 0);
        }

        // Expiry badge
        int days = days_until_expiry(p->expiry, p->frozen);
        if (days != 9999)
        {
            lv_obj_t *expiry = lv_label_create(row);
            lv_obj_clear_flag(expiry, LV_OBJ_FLAG_CLICKABLE);
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

        // Edit button
        lv_obj_t *btn_edit = lv_btn_create(row);
        lv_obj_add_style(btn_edit, &style_del_btn, 0);
        lv_obj_set_size(btn_edit, 50, 50);
        lv_obj_set_style_translate_y(btn_edit, 5, 0);

        lv_obj_t *lbl_edit = lv_label_create(btn_edit);
        lv_obj_clear_flag(lbl_edit, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(lbl_edit, LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(lbl_edit, lv_color_hex(theme_colors[active_theme_index][0]), 0);
        lv_obj_center(lbl_edit);

        std::string *edit_id = new std::string(p->rowId);
        lv_obj_add_event_cb(btn_edit, edit_btn_cb, LV_EVENT_CLICKED, edit_id);
        lv_obj_add_event_cb(btn_edit, free_rowid_cb, LV_EVENT_DELETE, edit_id);
    }
}

// === MAIN POPULATE FUNCTION ===

void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products)
{
    if (!root)
    {
        ESP_LOGE(TAG, "Root object is NULL");
        return;
    }

    // ------------------------------------------------------------------
    // Phase 1: ALL data preparation – no LVGL lock held.
    // This is the bulk of the CPU work; let other tasks run freely.
    // ------------------------------------------------------------------

    // Read UI state (dropdown indices).  These widgets are written only from
    // the LVGL task so a brief lock window is enough.
    uint32_t filter_idx = 0;
    uint32_t sort_idx = 0;
    {
        lv_lock();
        if (objects.product_filter_dropdown && lv_obj_is_valid(objects.product_filter_dropdown))
            filter_idx = lv_dropdown_get_selected(objects.product_filter_dropdown);
        if (objects.product_sort_dropdown && lv_obj_is_valid(objects.product_sort_dropdown))
            sort_idx = lv_dropdown_get_selected(objects.product_sort_dropdown);
        lv_unlock();
    }

    // Filter by expiry
    std::vector<const Product *> filtered;
    filtered.reserve(products.size());
    for (const auto &p : products)
        filtered.push_back(&p);

    if (filter_idx == 1)
    {
        filtered.erase(
            std::remove_if(filtered.begin(), filtered.end(), [](const Product *p)
                           {
                int d = days_until_expiry(p->expiry, p->frozen);
                return d == 9999 || d >= 7; }),
            filtered.end());
    }

    // Filter by search text
    if (s_productSearchFilter.length() >= 3)
    {
        std::string filterLower = s_productSearchFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

        filtered.erase(
            std::remove_if(filtered.begin(), filtered.end(), [&filterLower](const Product *p)
                           {
                std::string nameLower = p->name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                return nameLower.find(filterLower) == std::string::npos; }),
            filtered.end());
    }

    // Compute categories and expiring counts
    std::map<std::string, int> categoryExpiringCount;
    std::set<std::string> uniqueCategories;
    for (const Product *p : filtered)
    {
        uniqueCategories.insert(p->category);
        int d = days_until_expiry(p->expiry, p->frozen);
        if (d != 9999 && d < 7)
            categoryExpiringCount[p->category]++;
    }

    ESP_LOGI(TAG, "Filtered products: %d, Categories: %d",
             (int)filtered.size(), (int)uniqueCategories.size());
    ESP_LOGI(TAG, "Expiring counts by category:");
    for (const auto &entry : categoryExpiringCount)
        ESP_LOGI(TAG, "  %s: %d", entry.first.c_str(), entry.second);

    // Resolve selected category
    std::string selectedCategory = s_selectedCategory;
    if (!uniqueCategories.empty())
    {
        if (selectedCategory.empty() ||
            uniqueCategories.find(selectedCategory) == uniqueCategories.end())
        {
            selectedCategory = *uniqueCategories.begin();
        }
    }
    else
    {
        selectedCategory.clear();
    }

    // Filter + sort products for the selected category
    std::vector<const Product *> category_filtered;
    for (const Product *p : filtered)
    {
        if (p->category == selectedCategory)
            category_filtered.push_back(p);
    }

    std::sort(category_filtered.begin(), category_filtered.end(),
              [sort_idx](const Product *a, const Product *b)
              {
                  if (a->category != b->category)
                      return a->category < b->category;
                  if (sort_idx == 1)
                  {
                      bool av = days_until_expiry(a->expiry, a->frozen) != 9999;
                      bool bv = days_until_expiry(b->expiry, b->frozen) != 9999;
                      if (av != bv)
                          return av > bv;
                      return a->expiry < b->expiry;
                  }
                  return a->name < b->name;
              });

    // Snapshot selected row IDs (productsManager may be accessed from other tasks)
    std::set<std::string> selectedRowIds;
    {
        auto sel = productsManager.getSelectedProducts();
        for (const auto &p : sel)
            selectedRowIds.insert(p.rowId);
    }

    // ------------------------------------------------------------------
    // Phase 2: commit state and check for re-entrancy under the lock.
    // ------------------------------------------------------------------
    lv_lock();

    if (!lv_obj_is_valid(root))
    {
        ESP_LOGE(TAG, "Root became invalid before UI update");
        lv_unlock();
        return;
    }

    if (s_populating)
    {
        ESP_LOGW(TAG, "populateProductList already in progress, skipping");
        lv_unlock();
        return;
    }
    s_populating = true;

    // Commit resolved state
    s_selectedCategory = selectedCategory;
    s_categoryExpiringCount = categoryExpiringCount;

    init_styles();

    // ------------------------------------------------------------------
    // Phase 3: build sidebar (brief lock window).
    // ------------------------------------------------------------------
    lv_obj_t *sidebar = objects.products_sidebar;
    if (!sidebar || !lv_obj_is_valid(sidebar))
    {
        ESP_LOGE(TAG, "products_sidebar is NULL or invalid");
        s_populating = false;
        lv_unlock();
        return;
    }

    build_sidebar(sidebar, uniqueCategories, categoryExpiringCount);

    lv_unlock();
    taskYIELD(); // let LVGL render the sidebar before we build rows

    // ------------------------------------------------------------------
    // Phase 4: build content rows (yields internally every N rows).
    // ------------------------------------------------------------------
    lv_lock();

    lv_obj_t *content_container = objects.products_container;
    if (!content_container || !lv_obj_is_valid(content_container))
    {
        ESP_LOGE(TAG, "products_container is NULL or invalid");
        s_populating = false;
        lv_unlock();
        return;
    }

    // build_content_rows takes ownership of the lock and may release/re-acquire
    // it internally; see ROW_YIELD_INTERVAL above.
    build_content_rows(content_container, category_filtered, selectedRowIds);

    // ------------------------------------------------------------------
    // Phase 5: finalise (scroll restore, selection UI).
    // ------------------------------------------------------------------
    if (lv_obj_is_valid(content_container))
        lv_obj_scroll_to_y(content_container, 0, LV_ANIM_OFF);

    update_selection_ui();

    s_populating = false;
    lv_unlock();
}