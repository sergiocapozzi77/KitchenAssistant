#include <vector>
#include <string>
#include <map>
#include <cstring>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_extensions.h"
#include "ui_extensions_internal.h"
#include "ui.h"
#include "fonts.h"
#include "models.h"
#include "RecipeDetailService.h"
#include "RecipeAIDetailService.h"
#include "FavouritesManager.h"
#include "FavouriteService.h"
#include "styles.h"
#include "filters_ui.h"
#include "ProductsManager.h"
#include "LeonardoImageGenerator.h"
#include "secrets.h"

static const char *TAG = "UIEXTENSIONS";
static uint32_t s_current_generation = 0;

// === RECIPE DETAIL SPECIFIC STRUCTS ===

struct DetailFetchCtx
{
    RecipeSuggestion recipe;
    // LVGL widget refs (nulled on delete)
    lv_obj_t *spinner;
    lv_obj_t *ingredients_cont;
    lv_obj_t *method_cont;
    lv_obj_t *header_img;
    uint32_t generation; // to detect stale tasks
};

struct HeartButtonContext
{
    std::string url;
    std::string name;
    std::string imageUrl;
    std::string imageUrlBig;
    std::string description;
    std::string difficulty;
    std::string totalTime;
    std::string recipeSource;
    std::vector<std::string> ingredients;
    std::vector<std::string> methodSteps;

    lv_obj_t *add;
    lv_obj_t *remove;
};

// === CLEANUP CALLBACKS ===

static void free_heart_button_ctx_cb(lv_event_t *e)
{
    HeartButtonContext *ctx =
        static_cast<HeartButtonContext *>(lv_event_get_user_data(e));

    if (!ctx)
        return;

    // Clear user data from both buttons to prevent dangling pointers
    if (ctx->add && lv_obj_is_valid(ctx->add))
        lv_obj_set_user_data(ctx->add, nullptr);
    if (ctx->remove && lv_obj_is_valid(ctx->remove))
        lv_obj_set_user_data(ctx->remove, nullptr);

    delete ctx;
}

// === EVENT CALLBACKS ===

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
}

static void heart_button_cb(lv_event_t *e)
{
    HeartButtonContext *ctx = static_cast<HeartButtonContext *>(lv_event_get_user_data(e));
    if (!ctx || !ctx->add || !ctx->remove)
        return;

    bool currentlyFav = favouritesManager.isFavouriteUrl(ctx->url);

    if (currentlyFav)
    {
        // Remove from favourites
        favouritesManager.removeFavourite(ctx->url);
        lv_lock();
        lv_obj_clear_flag(ctx->add, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->remove, LV_OBJ_FLAG_HIDDEN);
        lv_unlock();
        showSnackbar("Removed from favourites", 3000);

        // Background task to sync with Appwrite
        xTaskCreate([](void *param)
                    {
            std::string *url = static_cast<std::string *>(param);
            favouriteService.removeFavourite(*url);
            delete url;
            vTaskDelete(nullptr); }, "removeFav", 8192, new std::string(ctx->url), 1, nullptr);
    }
    else
    {
        // For AI recipes with empty URL, generate a synthetic URL
        std::string url = ctx->url;
        if (url.empty() && ctx->recipeSource == "ai-deepseek")
        {
            url = "ai://deepseek/" + favouriteService.generateId();
            ctx->url = url;
        }

        if (ctx->recipeSource == "ai-deepseek" && ctx->imageUrl.empty())
        {
            ESP_LOGI("Favourite", "Generating image for AI recipe: %s", ctx->name.c_str());

            std::string cached = get_leonardo_cached_url(
                "generate:" + ctx->name + "|||" + ctx->description, 112, 112);

            if (!cached.empty())
            {
                ESP_LOGI("Favourite", "Leonardo URL cache hit for: %s", ctx->name.c_str());
                ctx->imageUrl = cached;
            }
            else
            {
                ctx->imageUrl = "generate:" + ctx->name + "|||" + ctx->description;
            }
            std::string cachedBig = get_leonardo_cached_url(
                "generate:" + ctx->name + "|||" + ctx->description, 800, 280);

            if (!cachedBig.empty())
            {
                ESP_LOGI("Favourite", "Leonardo URL cache hit for big image: %s", ctx->name.c_str());
                ctx->imageUrlBig = cachedBig;
            }
            else
            {
                ESP_LOGI("Favourite", "No Leonardo cache for big image.");
            }
        }

        // Create Favorite for local cache
        Favorite fav;
        fav.url = url;
        fav.name = ctx->name;
        fav.imageUrl = ctx->imageUrl;
        fav.imageUrlBig = ctx->imageUrlBig;
        fav.description = ctx->description;
        fav.difficulty = ctx->difficulty;
        fav.totalTime = ctx->totalTime;
        fav.recipeSource = ctx->recipeSource;
        fav.ingredients = ctx->ingredients;
        fav.methodSteps = ctx->methodSteps;
        favouritesManager.addFavourite(fav);

        ESP_LOGI("FAVORITE", "Adding favourite:");
        ESP_LOGI("FAVORITE", "  url: %s", fav.url.c_str());
        ESP_LOGI("FAVORITE", "  name: %s", fav.name.c_str());
        ESP_LOGI("FAVORITE", "  imageUrl: %s", fav.imageUrl.c_str());
        ESP_LOGI("FAVORITE", "  imageUrlBig: %s", fav.imageUrlBig.c_str());
        ESP_LOGI("FAVORITE", "  description: %s", fav.description.c_str());
        ESP_LOGI("FAVORITE", "  difficulty: %s", fav.difficulty.c_str());
        ESP_LOGI("FAVORITE", "  totalTime: %s", fav.totalTime.c_str());
        ESP_LOGI("FAVORITE", "  recipeSource: %s", fav.recipeSource.c_str());

        // Log ingredients
        if (!fav.ingredients.empty())
        {
            for (size_t i = 0; i < fav.ingredients.size(); i++)
            {
                ESP_LOGI("FAVORITE", "  ingredient[%d]: %s", i, fav.ingredients[i].c_str());
            }
        }
        else
        {
            ESP_LOGI("FAVORITE", "  ingredients: (none)");
        }

        // Log method steps
        if (!fav.methodSteps.empty())
        {
            for (size_t i = 0; i < fav.methodSteps.size(); i++)
            {
                ESP_LOGI("FAVORITE", "  step[%d]: %s", i, fav.methodSteps[i].c_str());
            }
        }
        else
        {
            ESP_LOGI("FAVORITE", "  methodSteps: (none)");
        }

        lv_lock();
        // UI: Hide "Add" (empty heart), Show "Remove" (full heart)
        lv_obj_add_flag(ctx->add, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->remove, LV_OBJ_FLAG_HIDDEN);
        lv_unlock();
        showSnackbar("Added to favourites", 3000);

        // Background task to sync with Appwrite
        xTaskCreate([](void *param)
                    {
            Favorite *favourite = static_cast<Favorite *>(param);
            favouriteService.addFavourite(*favourite);
            delete favourite;
            vTaskDelete(nullptr); }, "addFav", 8192, new Favorite(fav), 1, nullptr);
    }
}

// === HELPER FUNCTIONS ===

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

static void scroll_begin_hide_img_cb(lv_event_t *e)
{
    lv_obj_t *img = (lv_obj_t *)lv_event_get_user_data(e);
    if (img && lv_obj_is_valid(img))
        lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);
}

static void scroll_end_show_img_cb(lv_event_t *e)
{
    lv_obj_t *img = (lv_obj_t *)lv_event_get_user_data(e);
    if (img && lv_obj_is_valid(img))
        lv_obj_set_style_opa(img, LV_OPA_COVER, 0);
}

// FreeRTOS task: fetch details, then populate ingredients & method widgets
static void fetch_recipe_detail_task(void *arg)
{
    DetailFetchCtx *ctx = (DetailFetchCtx *)arg;

    // Check if this task is for the current generation (not stale)
    if (ctx->generation != s_current_generation)
    {
        ESP_LOGI(TAG, "Skipping stale recipe detail task (generation %lu != %lu)",
                 ctx->generation, s_current_generation);
        delete ctx;
        vTaskDelete(NULL);
        return;
    }

    bool ok = false;
    bool skipFetch = false;
    if (!ctx->recipe.ingredients.empty() && !ctx->recipe.methodSteps.empty())
    {
        ESP_LOGI(TAG, "Recipe already has ingredients and method steps, skipping fetch");
        ok = true;
        skipFetch = true;
        ctx->recipe.detailsFetched = true;
    }

    if (!skipFetch)
    {
        if (ctx->recipe.recipeSource == "ai-deepseek")
        {
            // Build ingredients list from selected products (if checkbox checked)
            std::vector<std::string> ingredients;
            lv_lock();
            bool useSelected = lv_obj_has_state(objects.products_filters_panel__poducts_selected_cb, LV_STATE_CHECKED);
            lv_unlock();
            if (useSelected)
            {
                std::vector<Product> selectedProducts = productsManager.getSelectedProducts();
                for (const auto &p : selectedProducts)
                {
                    ingredients.push_back(p.name);
                }
            }
            // Get filter state
            filter_state_t *filterState = get_filter_state();
            ok = recipeAIDetailService.fetchDetails(ctx->recipe, ingredients, filterState);
        }
        else
        {
            ok = recipeDetailService.fetchDetails(ctx->recipe);
        }
    }
    ESP_LOGI("RecipeDetail", "fetchDetails: %s, ings=%d steps=%d",
             ok ? "ok" : "fail",
             (int)ctx->recipe.ingredients.size(),
             (int)ctx->recipe.methodSteps.size());

    lv_lock();

    // Update HeartButtonContext with fetched recipe details
    if (ok)
    {
        HeartButtonContext *heartCtx = static_cast<HeartButtonContext *>(lv_obj_get_user_data(objects.recipe_favourite_add));
        if (heartCtx)
        {
            // Update vectors
            heartCtx->ingredients = ctx->recipe.ingredients;
            heartCtx->methodSteps = ctx->recipe.methodSteps;
            // Update scalar fields if they are empty in context but present in fetched recipe
            if (heartCtx->imageUrl.empty() && !ctx->recipe.imageUrl.empty())
                heartCtx->imageUrl = ctx->recipe.imageUrl;
            if (heartCtx->imageUrlBig.empty() && !ctx->recipe.imageUrlBig.empty())
                heartCtx->imageUrlBig = ctx->recipe.imageUrlBig;
            if (heartCtx->description.empty() && !ctx->recipe.description.empty())
                heartCtx->description = ctx->recipe.description;
            if (heartCtx->difficulty.empty() && !ctx->recipe.difficulty.empty())
                heartCtx->difficulty = ctx->recipe.difficulty;
            if (heartCtx->totalTime.empty() && !ctx->recipe.totalTime.empty())
                heartCtx->totalTime = ctx->recipe.totalTime;
        }
    }

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
        // Set up container for two-column layout
        lv_obj_set_flex_flow(ctx->ingredients_cont, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(ctx->ingredients_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(ctx->ingredients_cont, 12, 0);
        lv_obj_set_style_pad_row(ctx->ingredients_cont, 12, 0);

        if (ok && !ctx->recipe.ingredients.empty())
        {
            // Use shared function to populate UI
            populateIngredientsUI(ctx->ingredients_cont, ctx->recipe.ingredients);
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
                lv_obj_set_style_radius(step_card, 0, 0); // No rounded corners
                lv_obj_set_style_bg_color(step_card, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(step_card, LV_OPA_COVER, 0);

                // Step number circle
                lv_obj_t *num_cont = lv_obj_create(step_card);
                lv_obj_set_size(num_cont, 32, 32);
                lv_obj_clear_flag(num_cont, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_radius(num_cont, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(num_cont, lv_color_hex(theme_colors[active_theme_index][0]), 0);
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
                lv_obj_set_style_text_font(text_lbl, &ui_font_ext_font_montserrat_18, 0); // Increased from 16 to 18
                lv_obj_set_style_text_color(text_lbl, lv_color_hex(0x212529), 0);
                vTaskDelay(pdMS_TO_TICKS(10));
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

    // Kick off header image fetch at larger size if we have a URL or need to generate for AI recipe
    std::string thumbUrl;
    if (!ctx->recipe.imageUrlBig.empty())
    {
        thumbUrl = ctx->recipe.imageUrlBig;
    }
    else if (!ctx->recipe.imageUrl.empty())
    {
        thumbUrl = ctx->recipe.imageUrl;
    }
    else if (ctx->recipe.recipeSource == "ai-deepseek")
    {
        // Generate a placeholder URL that will trigger AI image generation
        thumbUrl = "generate:" + ctx->recipe.name + "|||" + ctx->recipe.description;
        ESP_LOGI(TAG, "AI recipe with no image, will generate header: %s", ctx->recipe.name.c_str());
    }

    if (!thumbUrl.empty() && ctx->header_img)
    {
        lv_lock();
        lv_obj_t *shimmer = create_shimmer_overlay(ctx->header_img);
        start_shimmer_animation(shimmer, ctx->header_img);

        ThumbContext *tctx = new ThumbContext{ctx->header_img, shimmer, thumbUrl, 0, {}, 800, 280};
        lv_obj_add_event_cb(ctx->header_img, thumb_obj_deleted_cb, LV_EVENT_DELETE, tctx);
        lv_unlock();

        thumb_queue_push(tctx);
    }

    delete ctx;
    vTaskDelete(NULL);
}

// === PUBLIC FUNCTION ===
void showRecipeDetailScreen(const RecipeSuggestion &recipe)
{
    lv_obj_t *prev_screen = lv_scr_act();

    // Load screen from EEZ Studio UI manager
    lv_scr_load_anim(objects.recipe_detail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

    // Get UI components by identifier
    lv_obj_t *bar_title = objects.recipe_title;
    lv_obj_t *back_btn = objects.recipe_back_btn;
    lv_obj_t *header_img = objects.recipe_header_img;
    lv_obj_t *detail_spinner = objects.recipe_detail_spinner;
    lv_obj_t *ing_cont = objects.recipe_ing_cont;
    lv_obj_t *method_cont = objects.recipe_method_cont;
    lv_obj_t *total_time_val = objects.recipe_total_time_val;
    lv_obj_t *difficulty_val = objects.recipe_difficulty_val;

    // Clear any previous content and reset UI state
    if (ing_cont && lv_obj_is_valid(ing_cont))
    {
        lv_obj_clean(ing_cont);
    }
    if (method_cont && lv_obj_is_valid(method_cont))
    {
        lv_obj_clean(method_cont);
    }
    if (header_img && lv_obj_is_valid(header_img))
    {
        lv_image_set_src(header_img, NULL);
        lv_obj_set_size(header_img, lv_pct(100), 280);
        lv_obj_set_style_radius(header_img, 12, 0);        // rounded corners
        lv_obj_set_style_clip_corner(header_img, true, 0); // clip image to rounded corners
                                                           //  lv_image_set_inner_align(header_img, LV_IMAGE_ALIGN_COVER); // cover align
    }
    // Clear meta fields
    if (total_time_val && lv_obj_is_valid(total_time_val))
    {
        lv_label_set_text(total_time_val, "");
    }
    if (difficulty_val && lv_obj_is_valid(difficulty_val))
    {
        lv_label_set_text(difficulty_val, "");
    }

    // Increment generation to invalidate any previous task
    s_current_generation++;

    // Set recipe title
    lv_label_set_text(bar_title, recipe.name.c_str());

    // Set meta information
    if (!recipe.totalTime.empty())
    {
        lv_label_set_text(total_time_val, recipe.totalTime.c_str());
    }

    if (!recipe.difficulty.empty())
    {
        lv_label_set_text(difficulty_val, recipe.difficulty.c_str());
    }

    lv_obj_clear_flag(detail_spinner, LV_OBJ_FLAG_HIDDEN);

    // Set up back button callback

    // Set initial color based on favourite status
    bool isFav = favouritesManager.isFavouriteUrl(recipe.url);
    if (isFav)
    {
        lv_obj_add_flag(objects.recipe_favourite_add, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(objects.recipe_favourite_remove, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(objects.recipe_favourite_add, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.recipe_favourite_remove, LV_OBJ_FLAG_HIDDEN);
    }

    // 1. CLEANUP: Remove any existing callbacks to prevent duplicates
    lv_obj_remove_event_cb(objects.recipe_favourite_add, heart_button_cb);
    lv_obj_remove_event_cb(objects.recipe_favourite_remove, heart_button_cb);
    lv_obj_remove_event_cb(objects.recipe_favourite_add, free_heart_button_ctx_cb);
    lv_obj_remove_event_cb(objects.recipe_favourite_remove, free_heart_button_ctx_cb);

    // Create context with recipe data
    HeartButtonContext *ctx = new HeartButtonContext{
        recipe.url,
        recipe.name,
        recipe.imageUrl,
        recipe.imageUrlBig,
        recipe.description,
        recipe.difficulty,
        recipe.totalTime,
        recipe.recipeSource,
        recipe.ingredients,
        recipe.methodSteps,
        objects.recipe_favourite_add,
        objects.recipe_favourite_remove};

    lv_obj_set_user_data(objects.recipe_favourite_add, ctx);
    lv_obj_set_user_data(objects.recipe_favourite_remove, ctx);

    lv_obj_add_event_cb(objects.recipe_favourite_add, heart_button_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(objects.recipe_favourite_add, free_heart_button_ctx_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(objects.recipe_favourite_remove, heart_button_cb, LV_EVENT_CLICKED, ctx);

    // Kick off detail fetch task
    DetailFetchCtx *fctx = new DetailFetchCtx{
        recipe,
        detail_spinner,
        ing_cont,
        method_cont,
        header_img,
        s_current_generation};

    // Register delete guard callbacks
    lv_obj_add_event_cb(detail_spinner, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(ing_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(method_cont, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);
    lv_obj_add_event_cb(header_img, detail_widget_deleted_cb, LV_EVENT_DELETE, fctx);

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        fetch_recipe_detail_task, "RecipeDetail",
        16384, fctx, 2, NULL, tskNO_AFFINITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create RecipeDetail task");
        lv_obj_add_flag(detail_spinner, LV_OBJ_FLAG_HIDDEN);
        // fctx will be cleaned up by widget delete events
    }
}