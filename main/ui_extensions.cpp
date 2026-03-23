#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "ProductService.h"
#include "ui_extensions.h"
#include "ui.h"
#include <ctime>
#include <cmath>
#include "ProductsManager.h"
#include "models.h"
#include "tjpgd.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "RecipeDetailService.h"

static const char *TAG = "UIEXTENSIONS";

static uint32_t s_thumb_generation = 0;

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
static void recipe_card_click_cb(lv_event_t *e);
static void recipe_detail_back_cb(lv_event_t *e);
void showRecipeDetailScreen(const RecipeSuggestion &recipe);

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

struct RecipeClickCtx
{
    RecipeSuggestion recipe;
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

static void free_recipe_click_ctx_cb(lv_event_t *e)
{
    delete (RecipeClickCtx *)lv_event_get_user_data(e);
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

    lv_obj_send_event(checkbox, LV_EVENT_VALUE_CHANGED, NULL);
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

        // lv_obj_add_flag(qty_cont, LV_OBJ_FLAG_HIDDEN);
    }

    // Initialize selection UI to correct state (panel hidden, count = 0)
    update_selection_ui();

    lv_unlock();
}

// === THUMBNAIL FETCH/DECODE ===

struct ThumbContext
{
    lv_obj_t *thumb; // nulled under lv_lock if object deleted before task finishes
    std::string url;
    uint32_t generation;
};

struct ThumbWorkerCtx
{
    std::vector<ThumbContext *> items;
};

struct ThumbDataCtx
{
    lv_image_dsc_t *dsc;
    uint8_t *px;
};

struct JpegIo
{
    const uint8_t *src;
    size_t src_len;
    size_t src_pos;
    uint8_t *dst; // RGB888 output
    uint16_t out_w;
};

static size_t tjpgd_in_cb(JDEC *jd, uint8_t *buf, size_t n)
{
    JpegIo *io = (JpegIo *)jd->device;
    size_t avail = io->src_len - io->src_pos;
    n = (n < avail) ? n : avail;
    if (buf)
        memcpy(buf, io->src + io->src_pos, n);
    io->src_pos += n;
    return n;
}

static int tjpgd_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    JpegIo *io = (JpegIo *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    int cols = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom; y++)
    {
        uint8_t *dst = io->dst + (y * io->out_w + rect->left) * 3;
        for (int x = 0; x < cols; x++, src += 3, dst += 3)
        {
            dst[0] = src[2]; // B
            dst[1] = src[1]; // G
            dst[2] = src[0]; // R
        }
    }
    return 1;
}

// Fired under lv_lock if thumb is deleted while task is still running
static void thumb_obj_deleted_cb(lv_event_t *e)
{
    ThumbContext *ctx = (ThumbContext *)lv_event_get_user_data(e);
    if (ctx)
        ctx->thumb = nullptr;
}

// Fired under lv_lock when thumb is deleted after image data was set
static void free_thumb_data_cb(lv_event_t *e)
{
    ThumbDataCtx *d = (ThumbDataCtx *)lv_event_get_user_data(e);
    if (!d)
        return;
    free((void *)d->dsc->data);
    delete d->dsc;
    delete d;
}

static bool fetch_and_decode_jpeg(const std::string &url, uint16_t W, uint16_t H,
                                  lv_image_dsc_t **out_dsc, uint8_t **out_px)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 4096;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "http_client_init failed");
        return false;
    }

    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "http_client_open failed");
        esp_http_client_cleanup(client);
        return false;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d, content-length: %d", status, content_len);

    size_t buf_sz = (content_len > 0) ? (size_t)content_len : 64 * 1024;
    uint8_t *jpeg_buf = (uint8_t *)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buf)
    {
        ESP_LOGE(TAG, "jpeg_buf malloc failed, requested: %u", buf_sz);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    size_t total = 0;
    int n;
    while ((n = esp_http_client_read(client, (char *)jpeg_buf + total, buf_sz - total)) > 0)
        total += (size_t)n;
    ESP_LOGI(TAG, "Downloaded %u bytes", total);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total == 0)
    {
        ESP_LOGE(TAG, "No data received");
        free(jpeg_buf);
        return false;
    }

    uint8_t *work = (uint8_t *)heap_caps_malloc(3100, MALLOC_CAP_INTERNAL);
    if (!work)
    {
        ESP_LOGE(TAG, "work malloc failed");
        free(jpeg_buf);
        return false;
    }

    JpegIo io = {jpeg_buf, total, 0, nullptr, 0};
    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgd_in_cb, work, 3100, &io);
    ESP_LOGI(TAG, "jd_prepare: %d  img size: %ux%u", res, jd.width, jd.height);

    uint8_t *px = nullptr;
    if (res == JDR_OK)
    {
        uint8_t scale = 0;
        if (jd.width >= W * 8)
            scale = 3;
        else if (jd.width >= W * 4)
            scale = 2;
        else if (jd.width >= W * 2)
            scale = 1;

        uint16_t decoded_w = jd.width >> scale;
        uint16_t decoded_h = jd.height >> scale;
        px = (uint8_t *)heap_caps_malloc(decoded_w * decoded_h * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!px)
        {
            ESP_LOGE(TAG, "px malloc failed (%ux%u)", decoded_w, decoded_h);
            free(jpeg_buf);
            free(work);
            return false;
        }
        io.dst = px;
        io.out_w = decoded_w;
        W = decoded_w;
        H = decoded_h;

        res = jd_decomp(&jd, tjpgd_out_cb, scale);
        ESP_LOGI(TAG, "jd_decomp: %d", res);
    }

    free(jpeg_buf);
    free(work);

    if (res != JDR_OK)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %d", res);
        free(px);
        return false;
    }

    lv_image_dsc_t *dsc = new lv_image_dsc_t{};
    dsc->header.cf = LV_COLOR_FORMAT_RGB888;
    dsc->header.w = W;
    dsc->header.h = H;
    dsc->header.stride = W * 3;
    dsc->data_size = W * H * 3;
    dsc->data = px;

    *out_dsc = dsc;
    *out_px = px;
    return true;
}

static void thumb_worker_task(void *arg)
{
    ThumbWorkerCtx *wctx = (ThumbWorkerCtx *)arg;

    for (ThumbContext *ctx : wctx->items)
    {
        if (ctx->generation != s_thumb_generation)
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
            lv_unlock();
            delete ctx;
            continue;
        }

        ESP_LOGI(TAG, "Fetching thumb: %s", ctx->url.c_str());
        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;

        if (fetch_and_decode_jpeg(ctx->url, 90, 90, &dsc, &px))
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
            {
                lv_image_set_src(thumb, dsc);
                lv_obj_set_size(thumb, 90, 90);
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
                ThumbDataCtx *data_ctx = new ThumbDataCtx{dsc, px};
                lv_obj_add_event_cb(thumb, free_thumb_data_cb, LV_EVENT_DELETE, data_ctx);
            }
            else
            {
                free(px);
                delete dsc;
            }
            lv_unlock();
        }
        else
        {
            lv_lock();
            lv_obj_t *thumb = ctx->thumb;
            if (thumb && lv_obj_is_valid(thumb))
                lv_obj_remove_event_cb_with_user_data(thumb, thumb_obj_deleted_cb, ctx);
            lv_unlock();
            ESP_LOGW(TAG, "Thumb fetch failed: %s", ctx->url.c_str());
        }

        delete ctx;
    }

    delete wctx;
    vTaskDelete(NULL);
}

// ═══════════════════════════════════════════════════════════════════════════
// RECIPE DETAIL SCREEN
// ═══════════════════════════════════════════════════════════════════════════

// Tracks the detail screen so back button can delete it
static lv_obj_t *s_detail_screen = nullptr;

struct DetailFetchCtx
{
    RecipeSuggestion recipe;
    // LVGL widget refs (nulled on delete)
    lv_obj_t *spinner;
    lv_obj_t *ingredients_cont;
    lv_obj_t *method_cont;
    lv_obj_t *header_img;
    lv_obj_t *screen;
};

static void detail_widget_deleted_cb(lv_event_t *e)
{
    DetailFetchCtx *ctx = (DetailFetchCtx *)lv_event_get_user_data(e);
    if (!ctx)
        return;
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (obj == ctx->spinner)
        ctx->spinner = nullptr;
    if (obj == ctx->ingredients_cont)
        ctx->ingredients_cont = nullptr;
    if (obj == ctx->method_cont)
        ctx->method_cont = nullptr;
    if (obj == ctx->header_img)
        ctx->header_img = nullptr;
    if (obj == ctx->screen)
        ctx->screen = nullptr;
}

static lv_obj_t *make_section_header(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);
    lv_obj_set_style_pad_top(lbl, 12, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

// FreeRTOS task: fetch details, then populate ingredients & method widgets
static void fetch_recipe_detail_task(void *arg)
{
    DetailFetchCtx *ctx = (DetailFetchCtx *)arg;

    bool ok = recipeDetailService.fetchDetails(ctx->recipe);
    ESP_LOGI("RecipeDetail", "fetchDetails: %s, ings=%d steps=%d",
             ok ? "ok" : "fail",
             (int)ctx->recipe.ingredients.size(),
             (int)ctx->recipe.methodSteps.size());

    lv_lock();

    // Hide spinner
    if (ctx->spinner && lv_obj_is_valid(ctx->spinner))
        lv_obj_add_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);

    // Header image
    if (!ok && ctx->header_img && lv_obj_is_valid(ctx->header_img))
    {
        // nothing extra — already has thumbnail or grey placeholder
    }

    // Ingredients
    if (ctx->ingredients_cont && lv_obj_is_valid(ctx->ingredients_cont))
    {
        if (ok && !ctx->recipe.ingredients.empty())
        {
            for (const auto &ing : ctx->recipe.ingredients)
            {
                lv_obj_t *row = lv_obj_create(ctx->ingredients_cont);
                lv_obj_set_width(row, lv_pct(100));
                lv_obj_set_height(row, LV_SIZE_CONTENT);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_column(row, 8, 0);

                lv_obj_t *dot = lv_label_create(row);
                lv_label_set_text(dot, LV_SYMBOL_BULLET);
                lv_obj_set_style_text_color(dot, lv_color_hex(0x4CAF50), 0);
                lv_obj_set_style_text_font(dot, &lv_font_montserrat_16, 0);

                lv_obj_t *lbl = lv_label_create(row);
                lv_label_set_text(lbl, ing.c_str());
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(lbl, 1);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x212529), 0);
            }
        }
        else
        {
            lv_obj_t *lbl = lv_label_create(ctx->ingredients_cont);
            lv_label_set_text(lbl, "Could not load ingredients.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }
    }

    // Method steps
    if (ctx->method_cont && lv_obj_is_valid(ctx->method_cont))
    {
        if (ok && !ctx->recipe.methodSteps.empty())
        {
            int step_num = 1;
            for (const auto &step : ctx->recipe.methodSteps)
            {
                lv_obj_t *step_card = lv_obj_create(ctx->method_cont);
                lv_obj_set_width(step_card, lv_pct(100));
                lv_obj_set_height(step_card, LV_SIZE_CONTENT);
                lv_obj_clear_flag(step_card, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_flex_flow(step_card, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(step_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_all(step_card, 12, 0);
                lv_obj_set_style_pad_column(step_card, 12, 0);
                lv_obj_set_style_border_width(step_card, 1, 0);
                lv_obj_set_style_border_color(step_card, lv_color_hex(0xE9ECEF), 0);
                lv_obj_set_style_radius(step_card, 10, 0);
                lv_obj_set_style_bg_color(step_card, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(step_card, LV_OPA_COVER, 0);

                // Step number circle
                lv_obj_t *num_cont = lv_obj_create(step_card);
                lv_obj_set_size(num_cont, 32, 32);
                lv_obj_clear_flag(num_cont, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_radius(num_cont, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(num_cont, lv_color_hex(0x4CAF50), 0);
                lv_obj_set_style_bg_opa(num_cont, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(num_cont, 0, 0);
                lv_obj_set_style_pad_all(num_cont, 0, 0);
                lv_obj_set_flex_flow(num_cont, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(num_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t *num_lbl = lv_label_create(num_cont);
                char nbuf[8];
                snprintf(nbuf, sizeof(nbuf), "%d", step_num++);
                lv_label_set_text(num_lbl, nbuf);
                lv_obj_set_style_text_color(num_lbl, lv_color_white(), 0);
                lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_16, 0);

                lv_obj_t *text_lbl = lv_label_create(step_card);
                lv_label_set_text(text_lbl, step.c_str());
                lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_flex_grow(text_lbl, 1);
                lv_obj_set_style_text_font(text_lbl, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(text_lbl, lv_color_hex(0x212529), 0);
            }
        }
        else if (!ok)
        {
            lv_obj_t *lbl = lv_label_create(ctx->method_cont);
            lv_label_set_text(lbl, "Could not load method steps.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }
    }

    lv_unlock();

    // Kick off header image fetch at larger size if we have a URL
    if (!ctx->recipe.imageUrl.empty() && ctx->header_img)
    {
        lv_image_dsc_t *dsc = nullptr;
        uint8_t *px = nullptr;
        if (fetch_and_decode_jpeg(ctx->recipe.imageUrl, 800, 280, &dsc, &px))
        {
            lv_lock();
            if (ctx->header_img && lv_obj_is_valid(ctx->header_img))
            {
                lv_image_set_src(ctx->header_img, dsc);
                lv_obj_set_size(ctx->header_img, lv_pct(100), 280);
                ThumbDataCtx *data_ctx = new ThumbDataCtx{dsc, px};
                lv_obj_add_event_cb(ctx->header_img, free_thumb_data_cb, LV_EVENT_DELETE, data_ctx);
            }
            else
            {
                free(px);
                delete dsc;
            }
            lv_unlock();
        }
    }

    delete ctx;
    vTaskDelete(NULL);
}

static void recipe_detail_back_cb(lv_event_t *e)
{
    lv_obj_t *prev = (lv_obj_t *)lv_event_get_user_data(e);
    if (prev && lv_obj_is_valid(prev))
        lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    else
        lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    s_detail_screen = nullptr;
}

static void recipe_card_click_cb(lv_event_t *e)
{
    RecipeClickCtx *ctx = (RecipeClickCtx *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Recipe clicked");

    if (!ctx)
        return;
    showRecipeDetailScreen(ctx->recipe);
}

void showRecipeDetailScreen(const RecipeSuggestion &recipe)
{
    lv_obj_t *prev_screen = lv_scr_act();

    // Create new full screen
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF8F9FA), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_detail_screen = scr;

    // ── Fixed top bar ──────────────────────────────────────────────────────
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, lv_pct(100), 56);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_pad_hor(top_bar, 8, 0);
    lv_obj_set_style_pad_ver(top_bar, 0, 0);
    lv_obj_set_style_shadow_opa(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);

    // Back button
    lv_obj_t *back_btn = lv_button_create(top_bar);
    lv_obj_set_size(back_btn, 44, 44);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_opa(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x212529), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, recipe_detail_back_cb, LV_EVENT_CLICKED, prev_screen);

    // Title in top bar
    lv_obj_t *bar_title = lv_label_create(top_bar);
    lv_label_set_text(bar_title, recipe.name.c_str());
    lv_label_set_long_mode(bar_title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(bar_title, 1);
    lv_obj_set_style_text_font(bar_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bar_title, lv_color_hex(0x212529), 0);
    lv_obj_set_style_pad_left(bar_title, 8, 0);

    // ── Scrollable content below top bar ──────────────────────────────────
    lv_obj_t *scroll = lv_obj_create(scr);
    lv_obj_set_size(scroll, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align(scroll, LV_ALIGN_TOP_MID, 0, 56);
    // Make it fill remaining height
    lv_obj_set_height(scroll, LV_VER_RES - 56);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_style_radius(scroll, 0, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 0, 0);

    // ── Header image (grey placeholder, filled by task) ────────────────────
    lv_obj_t *header_img = lv_image_create(scroll);
    lv_obj_set_size(header_img, lv_pct(100), 280);
    lv_obj_set_style_bg_color(header_img, lv_color_hex(0xDEE2E6), 0);
    lv_obj_set_style_bg_opa(header_img, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header_img, 0, 0);
    lv_obj_set_style_border_width(header_img, 0, 0);
    lv_image_set_inner_align(header_img, LV_IMAGE_ALIGN_COVER);

    // ── Info card (meta row) ───────────────────────────────────────────────
    lv_obj_t *meta_card = lv_obj_create(scroll);
    lv_obj_set_width(meta_card, lv_pct(100));
    lv_obj_set_height(meta_card, LV_SIZE_CONTENT);
    lv_obj_clear_flag(meta_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(meta_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta_card, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(meta_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(meta_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(meta_card, 0, 0);
    lv_obj_set_style_radius(meta_card, 0, 0);
    lv_obj_set_style_pad_all(meta_card, 16, 0);
    lv_obj_set_style_shadow_opa(meta_card, 0, 0);
    lv_obj_set_style_border_side(meta_card, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(meta_card, lv_color_hex(0xE9ECEF), 0);
    lv_obj_set_style_border_width(meta_card, 1, 0);

    auto make_meta_item = [&](const char *symbol, const std::string &val, const char *label_text)
    {
        if (val.empty())
            return;
        lv_obj_t *col = lv_obj_create(meta_card);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_row(col, 2, 0);

        lv_obj_t *ico = lv_label_create(col);
        lv_label_set_text(ico, symbol);
        lv_obj_set_style_text_color(ico, lv_color_hex(0x4CAF50), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);

        lv_obj_t *val_lbl = lv_label_create(col);
        lv_label_set_text(val_lbl, val.c_str());
        lv_obj_set_style_text_color(val_lbl, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_16, 0);

        lv_obj_t *sub_lbl = lv_label_create(col);
        lv_label_set_text(sub_lbl, label_text);
        lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x6C757D), 0);
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_14, 0);
    };

    make_meta_item(LV_SYMBOL_LOOP, recipe.totalTime, "Total");
    make_meta_item(LV_SYMBOL_EDIT, recipe.difficulty, "Difficulty");

    // ── Content area (padding) ─────────────────────────────────────────────
    lv_obj_t *content = lv_obj_create(scroll);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(content, 0, 0);

    // Loading spinner
    lv_obj_t *detail_spinner = lv_spinner_create(content);
    lv_obj_set_size(detail_spinner, 60, 60);
    lv_obj_set_style_arc_color(detail_spinner, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);

    // Ingredients section
    make_section_header(content, "Ingredients");
    lv_obj_t *ing_cont = lv_obj_create(content);
    lv_obj_set_width(ing_cont, lv_pct(100));
    lv_obj_set_height(ing_cont, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ing_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ing_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ing_cont, 12, 0);
    lv_obj_set_style_pad_row(ing_cont, 8, 0);
    lv_obj_set_style_bg_color(ing_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ing_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ing_cont, 1, 0);
    lv_obj_set_style_border_color(ing_cont, lv_color_hex(0xE9ECEF), 0);
    lv_obj_set_style_radius(ing_cont, 12, 0);
    lv_obj_set_style_shadow_opa(ing_cont, 0, 0);

    // Method section
    make_section_header(content, "Method");
    lv_obj_t *method_cont = lv_obj_create(content);
    lv_obj_set_width(method_cont, lv_pct(100));
    lv_obj_set_height(method_cont, LV_SIZE_CONTENT);
    lv_obj_clear_flag(method_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(method_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(method_cont, 0, 0);
    lv_obj_set_style_pad_row(method_cont, 10, 0);
    lv_obj_set_style_border_width(method_cont, 0, 0);
    lv_obj_set_style_bg_opa(method_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(method_cont, 0, 0);

    // Extra bottom padding
    lv_obj_t *bottom_pad = lv_obj_create(content);
    lv_obj_set_size(bottom_pad, 1, 40);
    lv_obj_set_style_border_width(bottom_pad, 0, 0);
    lv_obj_set_style_bg_opa(bottom_pad, LV_OPA_TRANSP, 0);

    // Transition in
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

    // Kick off detail fetch task
    DetailFetchCtx *fctx = new DetailFetchCtx{
        recipe,
        detail_spinner,
        ing_cont,
        method_cont,
        header_img,
        scr};

    // Register delete guard callbacks
    lv_obj_add_event_cb(detail_spinner, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(ing_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(method_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(header_img, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(scr, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        fetch_recipe_detail_task, "RecipeDetail",
        16384, fctx, 5, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create RecipeDetail task");
        lv_obj_add_flag(detail_spinner, LV_OBJ_FLAG_HIDDEN);
        // fctx will be cleaned up by widget delete events
    }
}

// Add this helper above populateRecipeList
static void make_children_bubble(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++)
        make_children_bubble(lv_obj_get_child(obj, i));
}

// ═══════════════════════════════════════════════════════════════════════════

void populateRecipeList(lv_obj_t *root, const std::vector<RecipeSuggestion> &recipes)
{
    if (!root)
    {
        ESP_LOGE(TAG, "Root object is NULL");
        return;
    }

    s_thumb_generation++;

    lv_lock();
    init_styles();
    lv_obj_clean(root);

    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 12, 0);

    std::vector<ThumbContext *> pending_thumbs;

    for (const auto &r : recipes)
    {
        // === CARD ===
        lv_obj_t *card = lv_obj_create(root);
        lv_obj_add_style(card, &style_card, 0);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_column(card, 12, 0);

        // === THUMBNAIL PLACEHOLDER ===
        lv_obj_t *thumb = lv_image_create(card);
        lv_obj_set_size(thumb, 90, 90);
        lv_obj_set_style_bg_color(thumb, lv_color_hex(0xDEE2E6), 0); // grey until loaded
        lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(thumb, 8, 0);
        lv_obj_set_style_border_width(thumb, 0, 0);
        lv_image_set_inner_align(thumb, LV_IMAGE_ALIGN_COVER);

        if (!r.imageUrl.empty())
        {
            ESP_LOGI(TAG, "Scheduling thumb fetch for recipe: %s", r.name.c_str());
            ThumbContext *tctx = new ThumbContext{thumb, r.imageUrl, s_thumb_generation};

            ESP_LOGI(TAG, ">>> about to create task, internal heap: %" PRIu32,
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

            pending_thumbs.push_back(tctx);
        }
        else
        {
            ESP_LOGI(TAG, "No image URL for recipe: %s", r.name.c_str());
        }

        // === RIGHT COLUMN ===
        lv_obj_t *info = lv_obj_create(card);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_set_style_border_width(info, 0, 0);
        lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_row(info, 4, 0);

        // Title
        lv_obj_t *title = lv_label_create(info);
        lv_label_set_text(title, r.name.c_str());
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_color(title, lv_color_hex(0x212529), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

        // Description
        if (!r.description.empty())
        {
            lv_obj_t *desc = lv_label_create(info);
            lv_label_set_text(desc, r.description.c_str());
            lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(desc, lv_pct(100));
            lv_obj_set_style_text_color(desc, lv_color_hex(0x6C757D), 0);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
        }

        // === BADGES ROW (time + difficulty) ===
        lv_obj_t *badges = lv_obj_create(info);
        lv_obj_set_width(badges, lv_pct(100));
        lv_obj_set_height(badges, LV_SIZE_CONTENT);
        lv_obj_clear_flag(badges, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badges, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(badges, 0, 0);
        lv_obj_set_style_border_width(badges, 0, 0);
        lv_obj_set_style_bg_opa(badges, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_column(badges, 10, 0);

        auto make_badge = [&](lv_obj_t *parent, const char *symbol, const std::string &text)
        {
            if (text.empty())
                return;
            lv_obj_t *wrap = lv_obj_create(parent);
            lv_obj_set_height(wrap, LV_SIZE_CONTENT);
            lv_obj_set_width(wrap, LV_SIZE_CONTENT);
            lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(wrap, 0, 0);
            lv_obj_set_style_border_width(wrap, 0, 0);
            lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_column(wrap, 4, 0);

            lv_obj_t *ico = lv_label_create(wrap);
            lv_label_set_text(ico, symbol);
            lv_obj_set_style_text_color(ico, lv_color_hex(0x212529), 0);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);

            lv_obj_t *lbl = lv_label_create(wrap);
            lv_label_set_text(lbl, text.c_str());
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x495057), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        };

        make_badge(badges, LV_SYMBOL_LOOP, r.totalTime); // clock-like symbol
        make_badge(badges, LV_SYMBOL_EDIT, r.difficulty);

        // Click handler — open detail screen
        RecipeClickCtx *rctx = new RecipeClickCtx{r};
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        make_children_bubble(card);
        lv_obj_add_event_cb(card, recipe_card_click_cb, LV_EVENT_CLICKED, rctx);
        lv_obj_add_event_cb(card, free_recipe_click_ctx_cb, LV_EVENT_DELETE, rctx);

        // Visual press feedback
        lv_obj_set_style_bg_color(card, lv_color_hex(0xF1F3F5), LV_STATE_PRESSED);
    }

    lv_unlock();

    if (!pending_thumbs.empty())
    {
        ThumbWorkerCtx *wctx = new ThumbWorkerCtx{pending_thumbs};
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
            thumb_worker_task, "thumb_worker", 8192, wctx, 5, NULL, 1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create thumb worker task");
            for (auto *tctx : pending_thumbs)
                delete tctx;
            delete wctx;
        }
    }
}