#include "ui_extensions.h"

#include "ui_extensions_internal.h"

#include "diag.h"
#include "screens.h"
#include "thumbnail_manager.h"

#include "esp_log.h"

static const char *TAG = "UI_EXT";
static lv_timer_t *s_snackbar_timer = nullptr;
static bool s_thumbnails_enabled = true;
static bool s_cookbook_recipe_ui_enabled = true;

void ui_extensions_init(uint16_t thumbMaxWidth, uint16_t thumbMaxHeight,
                        bool thumbEnableCache, bool thumbnailsEnabled,
                        bool cookbookRecipeUiEnabled)
{
    s_thumbnails_enabled = thumbnailsEnabled;
    s_cookbook_recipe_ui_enabled = cookbookRecipeUiEnabled;
    if (s_thumbnails_enabled)
    {
        thumbnail_manager_init(thumbMaxWidth, thumbMaxHeight, thumbEnableCache);
    }
    else
    {
        ESP_LOGI(TAG, "Thumbnail downloads and decoding disabled");
    }
    if (!s_cookbook_recipe_ui_enabled)
        ESP_LOGI(TAG, "Cookbook and recipe UI population disabled");
    ESP_LOGI(TAG, "UI extensions initialised");
}

bool cookbook_recipe_ui_enabled()
{
    return s_cookbook_recipe_ui_enabled;
}

bool push_thumbnail(lv_obj_t *image, const std::string &url, uint16_t width,
                    uint16_t height, bool cacheAllowed)
{
    if (!s_thumbnails_enabled)
        return false;
    if (!image || !lv_obj_is_valid(image) || url.empty())
        return false;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
    {
        ESP_LOGW(TAG, "Skipping non-HTTP image: %.80s", url.c_str());
        return false;
    }

    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
    lv_obj_set_style_clip_corner(image, true, 0);

    ThumbContext *ctx = new ThumbContext{};
    ctx->thumb = image;
    ctx->url = thumbnail_url(url, width, height);
    ctx->maxW = width;
    ctx->maxH = height;
    ctx->cacheAllowed = cacheAllowed;

    lv_obj_add_event_cb(image, thumb_obj_deleted_cb, LV_EVENT_DELETE, ctx);
    diag_bc(DIAG_UI_PUSH_THUMB, width);
    thumb_queue_push(ctx);
    return true;
}

static void hide_snackbar_cb(lv_timer_t *)
{
    s_snackbar_timer = nullptr;
    if (objects.snackbar && lv_obj_is_valid(objects.snackbar))
        lv_obj_set_hidden(objects.snackbar, true);
}

void showSnackbar(const char *message, int duration_ms)
{
    if (!message)
        return;
    if (!objects.snackbar || !lv_obj_is_valid(objects.snackbar) ||
        !objects.snackbar_text || !lv_obj_is_valid(objects.snackbar_text))
    {
        ESP_LOGI(TAG, "Snackbar: %s", message);
        return;
    }

    lv_label_set_text(objects.snackbar_text, message);
    lv_obj_set_hidden(objects.snackbar, false);
    lv_obj_move_foreground(objects.snackbar);

    if (s_snackbar_timer)
        lv_timer_delete(s_snackbar_timer);
    s_snackbar_timer = lv_timer_create(hide_snackbar_cb, duration_ms, nullptr);
    lv_timer_set_repeat_count(s_snackbar_timer, 1);
}

void showSpinner()
{
    if (!objects.spinner || !lv_obj_is_valid(objects.spinner))
        return;
    lv_obj_set_hidden(objects.spinner, false);
    lv_obj_move_foreground(objects.spinner);
}

void hideSpinner()
{
    if (!objects.spinner || !lv_obj_is_valid(objects.spinner))
        return;
    lv_obj_set_hidden(objects.spinner, true);
}
