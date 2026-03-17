#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "ProductService.h"

static const char *TAG = "UIEXTENSIONS";

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
    ESP_LOGI(TAG, "START populateProductList");
    ESP_LOGI(TAG, "start");

    lv_lock();

    ESP_LOGI(TAG, "Cleaning root");
    lv_obj_clean(root);
    ESP_LOGI(TAG, "after clean");

    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF5F5F5), 0);

    ESP_LOGI(TAG, "Preparing sorted list (%d products)", products.size());

    std::vector<const Product *> sorted;
    sorted.reserve(products.size());

    for (const auto &p : products)
        sorted.push_back(&p);

    ESP_LOGI(TAG, "Sorting...");
    std::sort(sorted.begin(), sorted.end(),
              [](const Product *a, const Product *b)
              {
                  return a->category < b->category;
              });

    ESP_LOGI(TAG, "after sort");

    std::string currentCategory;
    lv_obj_t *content = nullptr;

    int groupCount = 0;
    int rowCount = 0;

    for (const Product *p : sorted)
    {
        // NEW CATEGORY
        if (p->category != currentCategory)
        {
            currentCategory = p->category;
            groupCount++;

            ESP_LOGI(TAG, "Creating group %d: %s", groupCount, currentCategory.c_str());
            ESP_LOGI(TAG, "before group");

            lv_obj_t *card = lv_obj_create(root);
            lv_obj_set_width(card, lv_pct(100));
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

            lv_obj_t *header = lv_btn_create(card);
            lv_obj_set_width(header, lv_pct(100));
            lv_obj_set_height(header, 40);

            lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(header,
                                  LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);

            lv_obj_t *title = lv_label_create(header);
            lv_label_set_text(title, currentCategory.c_str());

            lv_obj_t *arrow = lv_label_create(header);
            lv_label_set_text(arrow, LV_SYMBOL_DOWN);

            content = lv_obj_create(card);
            lv_obj_set_width(content, lv_pct(100));
            lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

            GroupUI *group = new GroupUI{content, arrow, false};
            lv_obj_add_event_cb(header, group_toggle_cb, LV_EVENT_CLICKED, group);
            lv_obj_add_event_cb(header, free_group_cb, LV_EVENT_DELETE, group);

            ESP_LOGI(TAG, "after group");
        }

        // ROW
        rowCount++;

        if (rowCount % 5 == 0)
        {
            ESP_LOGI(TAG, "Created %d rows so far", rowCount);
            ESP_LOGI(TAG, "rows progress");
        }

        lv_obj_t *row = lv_obj_create(content);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 44);

        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, p->name.c_str());
        lv_obj_set_flex_grow(name, 1);

        lv_obj_t *qty = lv_label_create(row);
        lv_label_set_text_fmt(qty, "- %d +", p->quantity);

        lv_obj_t *btn_del = lv_btn_create(row);
        lv_obj_set_size(btn_del, 32, 32);

        lv_obj_t *lbl = lv_label_create(btn_del);
        lv_label_set_text(lbl, LV_SYMBOL_TRASH);
        lv_obj_center(lbl);

        std::string *rowId = new std::string(p->rowId);

        lv_obj_add_event_cb(btn_del, delete_btn_cb, LV_EVENT_CLICKED, rowId);
        lv_obj_add_event_cb(btn_del, free_rowid_cb, LV_EVENT_DELETE, rowId);
    }

    ESP_LOGI(TAG, "Finished UI: %d groups, %d rows", groupCount, rowCount);
    ESP_LOGI(TAG, "end");

    lv_unlock();

    ESP_LOGI(TAG, "END populateProductList");
}

static void delete_btn_cb(lv_event_t *e)
{
    std::string *rowId = static_cast<std::string *>(lv_event_get_user_data(e));

    // Callbacks are already executed within LVGL's context, but if the delete
    // operation triggers async work (network, service layer), we should protect UI access

    // TODO: call your service to delete product with *rowId
    LV_LOG_USER("Delete product: %s", rowId->c_str());

    // UI deletion is safe here as we're in LVGL event context,
    // but wrap it anyway for consistency if service calls might trigger re-entrant UI updates
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_obj_t *row = lv_obj_get_parent(btn);
    lv_obj_del(row);
}