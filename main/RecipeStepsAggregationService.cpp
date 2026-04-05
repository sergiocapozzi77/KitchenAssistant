#include "RecipeStepsAggregationService.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "RecipeStepsAggregationService";

RecipeStepsAggregationService recipeStepsAggregationService;
RecipeStepsAggregationService::RecipeStepsAggregationService()
    : _httpClient(endpoint, projectId, apiKey, 90000)
{
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string RecipeStepsAggregationService::safeString(cJSON *obj, const char *key)
{
    if (!obj)
        return "";
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring)
        return item->valuestring;
    return "";
}

void RecipeStepsAggregationService::parseIngredient(cJSON *item, RecipeIngredient &out)
{
    out.quantity = safeString(item, "quantity");
    out.unit = safeString(item, "unit");
    out.name = safeString(item, "name");
    out.notes = safeString(item, "notes");
}

void RecipeStepsAggregationService::parseIngredients(cJSON *arr, std::vector<RecipeIngredient> &out)
{
    if (!arr || !cJSON_IsArray(arr))
        return;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, arr)
    {
        RecipeIngredient ing;
        parseIngredient(item, ing);
        if (!ing.name.empty())
            out.push_back(std::move(ing));
    }
}

void RecipeStepsAggregationService::parsePhase(cJSON *item, RecipePhase &out)
{
    out.title = safeString(item, "title");
    out.method = safeString(item, "method");

    parseIngredients(cJSON_GetObjectItem(item, "ingredients"), out.ingredients);

    cJSON *imageRefsArr = cJSON_GetObjectItem(item, "imageRefs");
    if (imageRefsArr && cJSON_IsArray(imageRefsArr))
    {
        cJSON *ref = nullptr;
        cJSON_ArrayForEach(ref, imageRefsArr)
        {
            RecipeImageRef imgRef;
            imgRef.fileId = safeString(ref, "fileId");
            imgRef.fileName = safeString(ref, "fileName");
            if (!imgRef.fileId.empty())
                out.imageRefs.push_back(std::move(imgRef));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// getRecipe
// ─────────────────────────────────────────────────────────────────────────────

bool RecipeStepsAggregationService::getRecipe(const std::string &url, Recipe &outRecipe,
                                              int maxWidth, int maxHeight)
{
    if (functionId.empty())
    {
        ESP_LOGE(TAG, "functionId not configured");
        return false;
    }
    if (url.empty())
    {
        ESP_LOGE(TAG, "URL is empty");
        return false;
    }

    ESP_LOGI(TAG, "getRecipe: url='%s'", url.c_str());

    // ── Build request ──────────────────────────────────────────────────────
    const std::string functionUrl = endpoint + "/functions/" + functionId + "/executions";

    cJSON *body = cJSON_CreateObject();
    if (!body)
    {
        ESP_LOGE(TAG, "Failed to create JSON body");
        return false;
    }
    cJSON_AddStringToObject(body, "url", url.c_str());
    cJSON_AddNumberToObject(body, "maxWidth", maxWidth);
    cJSON_AddNumberToObject(body, "maxHeight", maxHeight);

    char *bodyStr = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!bodyStr)
    {
        ESP_LOGE(TAG, "Failed to serialise request body");
        return false;
    }

    // ── HTTP POST ──────────────────────────────────────────────────────────
    int status = -1;
    std::string response = _httpClient.httpPost(functionUrl, bodyStr, status);
    free(bodyStr);

    if (status != 200)
    {
        ESP_LOGE(TAG, "Function call failed: HTTP %d", status);
        if (!response.empty())
            ESP_LOGE(TAG, "Body: %s", response.c_str());
        return false;
    }

    // ── Parse top-level response ───────────────────────────────────────────
    cJSON *doc = cJSON_Parse(response.c_str());
    if (!doc)
    {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        return false;
    }

    // Check success flag
    cJSON *successItem = cJSON_GetObjectItem(doc, "success");
    if (!successItem || !cJSON_IsTrue(successItem))
    {
        const std::string errMsg = safeString(doc, "error");
        ESP_LOGE(TAG, "Function returned success=false: %s",
                 errMsg.empty() ? "(no error message)" : errMsg.c_str());
        cJSON_Delete(doc);
        return false;
    }

    // documentId
    outRecipe.documentId = safeString(doc, "documentId");

    // ── Parse recipe object ────────────────────────────────────────────────
    cJSON *recipe = cJSON_GetObjectItem(doc, "recipe");
    if (!recipe)
    {
        ESP_LOGE(TAG, "Response missing 'recipe' field");
        cJSON_Delete(doc);
        return false;
    }

    outRecipe.title = safeString(recipe, "title");
    outRecipe.description = safeString(recipe, "description");
    outRecipe.prepTime = safeString(recipe, "prepTime");
    outRecipe.cookTime = safeString(recipe, "cookTime");
    outRecipe.servings = safeString(recipe, "servings");

    parseIngredients(cJSON_GetObjectItem(recipe, "ingredients"), outRecipe.ingredients);

    cJSON *phases = cJSON_GetObjectItem(recipe, "aggregatedSteps");
    if (phases && cJSON_IsArray(phases))
    {
        cJSON *phaseItem = nullptr;
        cJSON_ArrayForEach(phaseItem, phases)
        {
            RecipePhase phase;
            parsePhase(phaseItem, phase);
            outRecipe.aggregatedSteps.push_back(std::move(phase));
        }
    }

    cJSON_Delete(doc);

    ESP_LOGI(TAG, "getRecipe OK: '%s', %d ingredients, %d phases",
             outRecipe.title.c_str(),
             (int)outRecipe.ingredients.size(),
             (int)outRecipe.aggregatedSteps.size());

    return true;
}