#include "actions.h"
#include "lvgl.h"
#include "ui.h"
#include "styles.h"
#include "esp_log.h"
#include "vars.h"
#include "images.h"

void set_tab_icon(lv_obj_t *tabview, uint32_t index, const void *img_src)
{
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_t *btn = lv_obj_get_child(tab_btns, index);

    if (!btn)
        return;

    // Remove existing children (label)
    uint32_t child_cnt = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < child_cnt; i++)
    {
        lv_obj_del(lv_obj_get_child(btn, 0));
    }

    // Add image
    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, img_src);
    lv_obj_center(img);
}

void action_screen_loading(lv_event_t *e)
{
    // This function can be used to perform actions when the loading screen is shown
    ESP_LOGI("actions", "Loading screen shown");
    set_tab_icon(objects.tabview, 0, &img_shopping);
}
