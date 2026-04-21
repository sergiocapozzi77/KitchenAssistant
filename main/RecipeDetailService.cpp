#include "RecipeDetailService.h"
#include "AppwriteClientInstance.h"
#include "esp_log.h"
#include "cJSON.h"
#include "secrets.h"

static const char *TAG = "RecipeDetail";

// Initialised in main / app startup with the shared HttpClientHelper and
// the function ID from AppwriteConfig (e.g. "recipe-scraper").
RecipeDetailService recipeDetailService(getAppwriteClient(),
                                        APPWRITE_FUNCTION_ID);

// ── public API ────────────────────────────────────────────────────────────────

bool RecipeDetailService::fetchDetails(RecipeSuggestion &recipe)
{
    if (recipe.url.empty())
    {
        ESP_LOGW(TAG, "fetchDetails called with empty URL");
        return false;
    }

    selectedRecipe = recipe;

    // Build payload: { "mode": "detail", "url": "<url>" }
    cJSON *payload = cJSON_CreateObject();
    if (!payload)
        return false;
    cJSON_AddStringToObject(payload, "mode", "detail");
    cJSON_AddStringToObject(payload, "url", recipe.url.c_str());

    char *payloadStr = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payloadStr)
        return false;

    ESP_LOGI(TAG, "Calling function %s for: %s", _functionId.c_str(), recipe.url.c_str());

    int status = 0;
    std::string raw = _client.executeFunction(_functionId, payloadStr, /*async=*/false, status);
    free(payloadStr);

    if (status < 0)
    {
        ESP_LOGE(TAG, "executeFunction transport error");
        return false;
    }
    if (raw.empty())
    {
        ESP_LOGW(TAG, "Empty response from function (HTTP %d)", status);
        return false;
    }

    if (!parseResponse(raw, recipe))
        return false;

    recipe.detailsFetched = true;
    return true;
}

// ── response parsing ──────────────────────────────────────────────────────────

bool RecipeDetailService::parseResponse(const std::string &raw, RecipeSuggestion &recipe)
{
    // ── Layer 1: Appwrite execution envelope ──────────────────────────────────
    // { "responseBody": "<escaped-json-string>", "responseStatusCode": 200, ... }

    cJSON *outer = cJSON_Parse(raw.c_str());
    if (!outer)
    {
        ESP_LOGW(TAG, "Failed to parse Appwrite envelope");
        return false;
    }

    cJSON *bodyItem = cJSON_GetObjectItem(outer, "responseBody");
    if (!bodyItem || !cJSON_IsString(bodyItem))
    {
        ESP_LOGW(TAG, "No responseBody in envelope");
        cJSON_Delete(outer);
        return false;
    }

    // responseBody is a JSON string — copy it before deleting outer
    std::string innerJson = bodyItem->valuestring;
    cJSON_Delete(outer);

    // ── Layer 2: function response ────────────────────────────────────────────
    // { "ok": true, "recipe": { "name": "...", "ingredients": [...], ... } }

    cJSON *inner = cJSON_Parse(innerJson.c_str());
    if (!inner)
    {
        ESP_LOGW(TAG, "Failed to parse function response body");
        return false;
    }

    cJSON *okItem = cJSON_GetObjectItem(inner, "success");
    if (!okItem || !cJSON_IsTrue(okItem))
    {
        cJSON *errItem = cJSON_GetObjectItem(inner, "error");
        ESP_LOGW(TAG, "Function returned success=false: %s",
                 (errItem && cJSON_IsString(errItem)) ? errItem->valuestring : "(no error field)");
        cJSON_Delete(inner);
        return false;
    }

    cJSON *r = cJSON_GetObjectItem(inner, "recipe");
    if (!r || !cJSON_IsObject(r))
    {
        ESP_LOGW(TAG, "No 'recipe' object in function response");
        cJSON_Delete(inner);
        return false;
    }

    // Helper: safely read a string field
    auto str = [](cJSON *obj, const char *key) -> const char *
    {
        cJSON *v = cJSON_GetObjectItem(obj, key);
        return (v && cJSON_IsString(v)) ? v->valuestring : nullptr;
    };

    // Scalar fields — only overwrite if recipe doesn't already have a value
    if (recipe.name.empty())
        if (const char *v = str(r, "name"))
            recipe.name = v;
    if (recipe.servings.empty())
        if (const char *v = str(r, "servings"))
            recipe.servings = v;
    if (recipe.prepTime.empty())
        if (const char *v = str(r, "prepTime"))
            recipe.prepTime = v;
    if (recipe.cookTime.empty())
        if (const char *v = str(r, "cookTime"))
            recipe.cookTime = v;
    if (recipe.difficulty.empty())
        if (const char *v = str(r, "difficulty"))
            recipe.difficulty = v;

    // ingredients array
    if (recipe.ingredients.empty())
    {
        cJSON *ings = cJSON_GetObjectItem(r, "ingredients");
        if (ings && cJSON_IsArray(ings))
        {
            cJSON *item;
            cJSON_ArrayForEach(item, ings)
            {
                if (cJSON_IsString(item) && item->valuestring[0] != '\0')
                    recipe.ingredients.emplace_back(item->valuestring);
            }
        }
    }

    // methodSteps array
    if (recipe.methodSteps.empty())
    {
        cJSON *steps = cJSON_GetObjectItem(r, "methodSteps");
        if (steps && cJSON_IsArray(steps))
        {
            cJSON *item;
            cJSON_ArrayForEach(item, steps)
            {
                if (cJSON_IsString(item) && item->valuestring[0] != '\0')
                    recipe.methodSteps.emplace_back(item->valuestring);
            }
        }
    }

    cJSON_Delete(inner);

    bool ok = !recipe.ingredients.empty() || !recipe.methodSteps.empty();
    ESP_LOGI(TAG, "parseResponse: ings=%d steps=%d name='%s'",
             (int)recipe.ingredients.size(),
             (int)recipe.methodSteps.size(),
             recipe.name.c_str());
    return ok;
}