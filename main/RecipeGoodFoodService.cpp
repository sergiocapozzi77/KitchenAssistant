#include "RecipeGoodFoodService.h"
#include "esp_log.h"
#include "cJSON.h"
#include "secrets.h"
#include "AppwriteClientInstance.h"

static const char *TAG = "RecipeGoodFoodService";

// Global singleton
RecipeGoodFoodService recipeGoodFoodService;

// Helper functions for safe JSON extraction
static std::string safeJsonString(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

static double safeJsonDouble(cJSON *obj, const char *key, double def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : def;
}

static int safeJsonInt(cJSON *obj, const char *key, int def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : def;
}

RecipeGoodFoodService::RecipeGoodFoodService()
    : _httpClient(getAppwriteClient())
{
    std::random_device rd;
    _rng = std::mt19937(rd());
}

std::vector<RecipeSuggestion> RecipeGoodFoodService::getRecipeSuggestions(
    const std::vector<std::string> &ingredients,
    const std::string &mealType,
    const std::vector<std::string> &keywords,
    const std::string &difficulty,
    const std::string &totalTime,
    const std::string &diet,
    const std::string &cuisine,
    const std::string &ratings,
    const std::string &calories,
    int page)
{
    ESP_LOGI(TAG, "Calling Appwrite function for GoodFood search (page %d)", page);

    // Build the JSON payload for the function
    cJSON *payload = cJSON_CreateObject();
    if (!payload)
    {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return {};
    }

    cJSON_AddStringToObject(payload, "mode", "search");
    cJSON_AddStringToObject(payload, "source", "goodfood");
    cJSON_AddNumberToObject(payload, "page", page);

    // Optional parameters (the function expects them)
    cJSON_AddStringToObject(payload, "mealType", mealType.c_str());
    cJSON_AddStringToObject(payload, "difficulty", difficulty.c_str());
    cJSON_AddStringToObject(payload, "totalTime", totalTime.c_str());
    cJSON_AddStringToObject(payload, "diet", diet.c_str());
    cJSON_AddStringToObject(payload, "cuisine", cuisine.c_str());
    cJSON_AddStringToObject(payload, "ratings", ratings.c_str());
    cJSON_AddStringToObject(payload, "calories", calories.c_str());

    // Add ingredients array
    cJSON *ingredientsArray = cJSON_AddArrayToObject(payload, "ingredients");
    for (const auto &ing : ingredients)
    {
        cJSON_AddItemToArray(ingredientsArray, cJSON_CreateString(ing.c_str()));
    }

    // Add keywords array
    cJSON *keywordsArray = cJSON_AddArrayToObject(payload, "keywords");
    for (const auto &kw : keywords)
    {
        cJSON_AddItemToArray(keywordsArray, cJSON_CreateString(kw.c_str()));
    }

    char *payloadStr = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!payloadStr)
    {
        ESP_LOGE(TAG, "Failed to stringify payload");
        return {};
    }

    // Call the Appwrite function
    int status = 0;
    std::string envelope = _httpClient.executeFunction(APPWRITE_FUNCTION_ID, payloadStr, false, status);
    free(payloadStr);

    if (status < 0)
    {
        ESP_LOGE(TAG, "executeFunction transport error");
        return {};
    }
    if (envelope.empty())
    {
        ESP_LOGW(TAG, "Empty response from function (HTTP %d)", status);
        return {};
    }

    // Parse the envelope and extract suggestions
    return parseSuggestionsResponse(envelope);
}

std::vector<RecipeSuggestion> RecipeGoodFoodService::parseSuggestionsResponse(const std::string &envelope)
{
    std::vector<RecipeSuggestion> results;

    // Layer 1: Appwrite execution envelope
    cJSON *outer = cJSON_Parse(envelope.c_str());
    if (!outer)
    {
        ESP_LOGE(TAG, "Failed to parse Appwrite envelope");
        return results;
    }

    cJSON *bodyItem = cJSON_GetObjectItem(outer, "responseBody");
    if (!bodyItem || !cJSON_IsString(bodyItem))
    {
        ESP_LOGE(TAG, "No responseBody string in envelope");
        cJSON_Delete(outer);
        return results;
    }

    std::string innerJson = bodyItem->valuestring;
    cJSON_Delete(outer);

    // Layer 2: function response
    cJSON *inner = cJSON_Parse(innerJson.c_str());
    if (!inner)
    {
        ESP_LOGE(TAG, "Failed to parse function response body");
        return results;
    }

    cJSON *success = cJSON_GetObjectItem(inner, "success");
    if (!success || !cJSON_IsTrue(success))
    {
        cJSON *errItem = cJSON_GetObjectItem(inner, "error");
        ESP_LOGE(TAG, "Function returned success=false: %s",
                 (errItem && cJSON_IsString(errItem)) ? errItem->valuestring : "(no error field)");
        cJSON_Delete(inner);
        return results;
    }

    cJSON *suggestions = cJSON_GetObjectItem(inner, "suggestions");
    if (!cJSON_IsArray(suggestions))
    {
        ESP_LOGE(TAG, "No suggestions array in function response");
        cJSON_Delete(inner);
        return results;
    }

    int count = cJSON_GetArraySize(suggestions);
    ESP_LOGI(TAG, "Received %d GoodFood suggestions", count);

    for (int i = 0; i < count; i++)
    {
        cJSON *item = cJSON_GetArrayItem(suggestions, i);
        if (!item)
            continue;

        RecipeSuggestion r;
        r.name = safeJsonString(item, "name");
        r.url = safeJsonString(item, "url");
        r.imageUrl = safeJsonString(item, "imageUrl");
        r.description = safeJsonString(item, "description");
        r.totalTime = safeJsonString(item, "totalTime");
        r.ratingValue = (float)safeJsonDouble(item, "ratingValue", 0.0);
        r.ratingCount = safeJsonInt(item, "ratingCount", 0);
        r.difficulty = safeJsonString(item, "difficulty");
        r.isPremium = cJSON_IsTrue(cJSON_GetObjectItem(item, "isPremium"));
        r.contentType = safeJsonString(item, "contentType");
        r.recipeSource = safeJsonString(item, "recipeSource");
        r.author = safeJsonString(item, "author");
        r.id = safeJsonString(item, "id");

        results.push_back(r);
    }

    cJSON_Delete(inner);
    return results;
}