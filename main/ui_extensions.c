#include <vector>
#include <map>
#include <string>
#include "lvgl.h"

#include "ProductService.h"

// Forward declaration of delete callback
static void delete_btn_cb(lv_event_t *e);

void populateProductList(lv_obj_t *root, const std::vector<Product> &products)
{
    // Clear existing children
    lv_obj_clean(root);

    // Group products by category
    std::map<std::string, std::vector<const Product *>> grouped;
    for (const auto &p : products) {
        grouped[p.category].push_back(&p);
    }

    // Build UI
    for (const auto &[category, items] : grouped) {

        // --- Group header ---
        lv_obj_t *header = lv_label_create(root);
        lv_label_set_text(header, category.c_str());
        lv_obj_set_width(header, lv_pct(100));
        lv_obj_set_style_text_color(header, lv_color_hex(0x888888), 0);
        lv_obj_set_style_pad_top(header, 8, 0);
        lv_obj_set_style_pad_bottom(header, 4, 0);
        lv_obj_set_style_pad_left(header, 6, 0);

        // --- Product rows ---
        for (const Product *p : items) {

            // Row container
            lv_obj_t *row = lv_obj_create(root);
            lv_obj_set_width(row, lv_pct(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                       LV_FLEX_ALIGN_CENTER,
                                       LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(row, 6, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            // Product name (grows to fill available space)
            lv_obj_t *name_lbl = lv_label_create(row);
            lv_label_set_text(name_lbl, p->name.c_str());
            lv_obj_set_flex_grow(name_lbl, 1);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

            // Delete button (right-aligned)
            lv_obj_t *btn_del = lv_btn_create(row);
            lv_obj_set_size(btn_del, 32, 32);
            lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xE53935), 0);
            lv_obj_t *lbl_del = lv_label_create(btn_del);
            lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
            lv_obj_center(lbl_del);

            // Pass rowId as user_data for the callback
            // We heap-allocate a copy so it outlives this scope
            std::string *rowId = new std::string(p->rowId);
            lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED,
                                static_cast<void *>(rowId));

            // Free the rowId string when the button is deleted
            lv_obj_add_event_cb(btn_del, [](lv_event_t *e) {
                delete static_cast<std::string *>(lv_event_get_user_data(e));
            }, LV_EVENT_DELETE, rowId);
        }
    }
}

static void delete_btn_cb(lv_event_t *e)
{
    std::string *rowId = static_cast<std::string *>(lv_event_get_user_data(e));
    // TODO: call your service to delete product with *rowId
    LV_LOG_USER("Delete product: %s", rowId->c_str());

    // Remove the row from the UI (btn -> row container)
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *row = lv_obj_get_parent(btn);
    lv_obj_del(row);
}