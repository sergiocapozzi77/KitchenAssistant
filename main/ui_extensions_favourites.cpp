#include <vector>
#include <string>
#include <algorithm>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "models.h"

#include "styles.h"
#include "thumbnail_manager.h"
#include "CookbookManager.h"
#include "CookbookService.h"
#include "FavouriteService.h"
#include "FavouritesManager.h"

static const char *TAG = "UIEXTENSIONS";

FavouritesViewMode g_favouritesViewMode = FavouritesViewMode::ALL_FAVOURITES;
std::string g_activeCookbookId;
std::string g_activeCookbookName;

// === FAVOURITE CLICK CONTEXT ===

struct FavouriteClickCtx
{
    Favorite favourite;
};

// === BACK BUTTON (COOKBOOK DRILL-IN) ===

static lv_obj_t *s_back_btn = nullptr;

static void back_to_cookbooks_cb(lv_event_t *e)
{
    g_favouritesViewMode = FavouritesViewMode::ALL_FAVOURITES;
    g_activeCookbookId.clear();
    g_activeCookbookName.clear();

    if (s_back_btn && lv_obj_is_valid(s_back_btn))
    {
        lv_obj_del(s_back_btn);
        s_back_btn = nullptr;
    }

    showCurrentPageFavourites(true);
}

void ensure_back_button()
{
    if (s_back_btn && lv_obj_is_valid(s_back_btn))
        return;

    if (!objects.favourites_header_pnl || !lv_obj_is_valid(objects.favourites_header_pnl))
        return;

    s_back_btn = lv_button_create(objects.favourites_header_pnl);
    lv_obj_set_pos(s_back_btn, 0, 13);
    lv_obj_set_size(s_back_btn, 70, 50);
    add_style_main_button(s_back_btn);

    lv_obj_t *lbl = lv_label_create(s_back_btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(s_back_btn, back_to_cookbooks_cb, LV_EVENT_CLICKED, nullptr);
}

void cleanupCookbookDrill()
{
    g_favouritesViewMode = FavouritesViewMode::ALL_FAVOURITES;
    g_activeCookbookId.clear();
    g_activeCookbookName.clear();
    if (s_back_btn && lv_obj_is_valid(s_back_btn))
    {
        lv_obj_del(s_back_btn);
        s_back_btn = nullptr;
    }
}

// === CLEANUP CALLBACKS ===

static void free_favourite_click_ctx_cb(lv_event_t *e)
{
    delete (FavouriteClickCtx *)lv_event_get_user_data(e);
}

// === EVENT CALLBACKS ===

static void favourite_card_click_cb(lv_event_t *e)
{
    FavouriteClickCtx *ctx = (FavouriteClickCtx *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    ESP_LOGI(TAG, "Favourite clicked: %s", ctx->favourite.name.c_str());

    // Convert Favorite to minimal RecipeSuggestion
    RecipeSuggestion recipe;
    recipe.name = ctx->favourite.name;
    recipe.url = ctx->favourite.url;
    recipe.imageUrl = ctx->favourite.imageUrl;
    recipe.description = ctx->favourite.description;
    recipe.difficulty = ctx->favourite.difficulty;
    recipe.totalTime = ctx->favourite.totalTime;
    recipe.recipeSource = ctx->favourite.recipeSource;
    recipe.ingredients = ctx->favourite.ingredients;
    recipe.methodSteps = ctx->favourite.methodSteps;
    showRecipeDetailScreen(recipe);
}

// === HELPER FUNCTIONS (reused from recipes) ===

// === MAIN POPULATE FUNCTION ===

void populateFavouritesList(lv_obj_t *root, const std::vector<Favorite> &favourites)
{
    if (!root || !lv_obj_is_valid(root))
    {
        ESP_LOGE(TAG, "Root object is NULL or invalid");
        return;
    }

    s_thumb_generation++;

    lv_lock();
    init_styles();
    // Stop shimmer animations before cleaning — active animations on children
    // being deleted can cause use-after-free in the LVGL animation system.
    // stop_all_shimmer_animations();  // DISABLED — shimmer caused crashes
    // Capture scroll position before cleaning
    lv_coord_t scroll_y = lv_obj_get_scroll_y(root);
    lv_obj_clean(root);

    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 16, 0);

    for (const auto &fav : favourites)
    {
        // === CARD ===
        lv_obj_t *card = createRecipeCard(root, fav);

        // Click handler — open detail screen
        FavouriteClickCtx *fctx = new FavouriteClickCtx{fav};
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        make_children_bubble(card);
        lv_obj_add_event_cb(card, favourite_card_click_cb, LV_EVENT_CLICKED, fctx);
        lv_obj_add_event_cb(card, free_favourite_click_ctx_cb, LV_EVENT_DELETE, fctx);
    }
    // Restore scroll position
    lv_obj_scroll_to_y(root, scroll_y, LV_ANIM_OFF);

    lv_unlock();
}

// === COOKBOOK LIST ===

struct CookbookClickCtx
{
    std::string cookbookId;
    std::string cookbookName;
};

static void cookbook_card_click_cb(lv_event_t *e)
{
    CookbookClickCtx *ctx = (CookbookClickCtx *)lv_event_get_user_data(e);
    if (!ctx)
        return;

    ESP_LOGI(TAG, "Cookbook clicked: %s", ctx->cookbookName.c_str());

    // Switch to drill-in view
    g_favouritesViewMode = FavouritesViewMode::COOKBOOK_DRILL;
    g_activeCookbookId = ctx->cookbookId;
    g_activeCookbookName = ctx->cookbookName;

    // Show spinner while fetching
    showSpinner();

    // Fetch favourites in background and filter by cookbookId locally
    // (server-side array query on cookbookIds returns 400, so we fetch all and filter)
    struct DrillCtx
    {
        std::string cookbookId;
    };
    DrillCtx *dctx = new DrillCtx{ctx->cookbookId};
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        [](void *param)
        {
            DrillCtx *d = static_cast<DrillCtx *>(param);

            // Fetch all favourites and filter by cookbookId locally
            std::vector<Favorite> allFavs = favouriteService.getFavourites();
            std::vector<Favorite> filtered;
            for (const auto &fav : allFavs)
            {
                if (std::find(fav.cookbookIds.begin(), fav.cookbookIds.end(), d->cookbookId) != fav.cookbookIds.end())
                    filtered.push_back(fav);
            }

            // Update cache and show UI on LVGL thread
            lv_async_call([](void *p)
                          {
                auto *f = static_cast<std::vector<Favorite> *>(p);
                favouritesManager.setFavourites(*f);
                hideSpinner();
                showCurrentPageFavourites(true);
                delete f; }, new std::vector<Favorite>(std::move(filtered)));

            delete d;
            vTaskDelete(nullptr);
        },
        "drillFav", 12288, dctx, 1, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ret != pdPASS)
    {
        delete dctx;
        hideSpinner();
    }
}

static void free_cookbook_click_ctx_cb(lv_event_t *e)
{
    delete (CookbookClickCtx *)lv_event_get_user_data(e);
}

// ── Delete cookbook with confirmation ─────────────────────────────────

static void delete_cookbook_with_confirmation(const std::string &cookbookId, const std::string &cookbookName)
{
    ESP_LOGI(TAG, "Deleting cookbook: %s (%s)", cookbookName.c_str(), cookbookId.c_str());

    // Background task: delete from Appwrite + local cache
    struct DeleteCtx
    {
        std::string cookbookId;
        std::string cookbookName;
    };
    DeleteCtx *dctx = new DeleteCtx{cookbookId, cookbookName};
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        [](void *param)
        {
            DeleteCtx *ctx = static_cast<DeleteCtx *>(param);

            // 1. Delete all favourites in this cookbook
            ESP_LOGI(TAG, "Deleting favourites for cookbook: %s", ctx->cookbookId.c_str());
            favouriteService.removeCookbookFromFavourites(ctx->cookbookId);

            // 2. Delete the cookbook document
            ESP_LOGI(TAG, "Deleting cookbook document: %s", ctx->cookbookId.c_str());
            cookbookService.deleteCookbook(ctx->cookbookId);

            // 3. Update local caches (on LVGL thread)
            lv_async_call([](void *p)
                          {
                DeleteCtx *c = static_cast<DeleteCtx *>(p);
                cookbookManager.removeCookbook(c->cookbookId);
                favouritesManager.removeFavouritesByCookbook(c->cookbookId);
                showSnackbar(("Deleted '" + c->cookbookName + "'").c_str(), 2000);
                showCurrentPageCookbooks(true);
                delete c; }, ctx);
        },
        "delCookbook", 12288, dctx, 1, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS)
    {
        delete dctx;
    }
}

void populateCookbooksList(lv_obj_t *root, const std::vector<Cookbook> &cookbooks)
{
    if (!root || !lv_obj_is_valid(root))
        return;

    lv_lock();
    init_styles();
    // stop_all_shimmer_animations();  // DISABLED — shimmer caused crashes
    lv_coord_t scroll_y = lv_obj_get_scroll_y(root);
    lv_obj_clean(root);

    lv_obj_set_style_bg_color(root, lv_color_hex(0xF8F9FA), 0);
    lv_obj_set_style_pad_all(root, 15, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(root, 16, 0);
    lv_obj_set_style_pad_column(root, 16, 0);

    for (const auto &cb : cookbooks)
    {
        // Count favourites in this cookbook
        std::vector<Favorite> allFavs = favouritesManager.getFavourites();
        int count = 0;
        for (const auto &fav : allFavs)
        {
            if (std::find(fav.cookbookIds.begin(), fav.cookbookIds.end(), cb.id) != fav.cookbookIds.end())
                count++;
        }

        // Card — takes roughly half width (2 columns with gap)
        lv_obj_t *card = lv_obj_create(root);
        lv_obj_set_width(card, lv_pct(45));
        lv_obj_set_height(card, 120);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(card, 4, 0);
        lv_obj_set_style_shadow_opa(card, 60, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);

        // Name label
        lv_obj_t *name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, cb.name.c_str());
        lv_obj_set_style_text_font(name_lbl, &ui_font_ext_font_montserrat_26, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x212529), 0);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name_lbl, lv_pct(100));
        lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);

        // Count badge
        if (count > 0)
        {
            lv_obj_t *count_lbl = lv_label_create(card);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d recipes", count);
            lv_label_set_text(count_lbl, buf);
            lv_obj_set_style_text_font(count_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(count_lbl, lv_color_hex(0x6C757D), 0);
        }

        // Click handler
        CookbookClickCtx *cctx = new CookbookClickCtx{cb.id, cb.name};
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, cookbook_card_click_cb, LV_EVENT_CLICKED, cctx);
        lv_obj_add_event_cb(card, free_cookbook_click_ctx_cb, LV_EVENT_DELETE, cctx);

        // Delete button (X) in top-right corner
        lv_obj_t *del_btn = lv_button_create(card);
        lv_obj_set_size(del_btn, 28, 28);
        lv_obj_add_flag(del_btn, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(del_btn, LV_ALIGN_TOP_RIGHT, -6, 6);
        lv_obj_set_style_border_width(del_btn, 0, 0);
        lv_obj_set_style_radius(del_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xE74C3C), 0);
        lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(del_btn, 0, 0);

        lv_obj_t *x_lbl = lv_label_create(del_btn);
        lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
        lv_obj_center(x_lbl);
        lv_obj_set_style_text_color(x_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(x_lbl, &lv_font_montserrat_14, 0);

        // Store cookbookId on the delete button's user data
        std::string *delCtx = new std::string(cb.id);
        lv_obj_add_event_cb(del_btn, [](lv_event_t *e)
                            {
            std::string *cookbookId = static_cast<std::string *>(lv_event_get_user_data(e));
            if (!cookbookId) return;
            std::vector<Cookbook> cbs = cookbookManager.getCookbooks();
            std::string name;
            for (const auto &cb : cbs) { if (cb.id == *cookbookId) { name = cb.name; break; } }
            delete_cookbook_with_confirmation(*cookbookId, name); }, LV_EVENT_CLICKED, delCtx);
        lv_obj_add_event_cb(del_btn, [](lv_event_t *e)
                            { delete static_cast<std::string *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, delCtx);
    }

    lv_obj_scroll_to_y(root, scroll_y, LV_ANIM_OFF);
    lv_unlock();
}

void showCurrentPageCookbooks(bool force)
{
    if (!objects.favourites_list || !lv_obj_is_valid(objects.favourites_list))
        return;

    if (lv_obj_get_child_count(objects.favourites_list) > 0 && !force)
        return;

    std::vector<Cookbook> cookbooks = cookbookManager.getCookbooks();
    populateCookbooksList(objects.favourites_list, cookbooks);
}