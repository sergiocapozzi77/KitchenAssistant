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

    // ── Build Appwrite execution envelope ──────────────────────────────────
    // Inner body: what your function receives as req.body
    cJSON *innerBody = cJSON_CreateObject();
    cJSON_AddStringToObject(innerBody, "url", url.c_str());
    cJSON_AddNumberToObject(innerBody, "maxWidth", maxWidth);
    cJSON_AddNumberToObject(innerBody, "maxHeight", maxHeight);
    char *innerBodyStr = cJSON_PrintUnformatted(innerBody);
    cJSON_Delete(innerBody);

    // Outer envelope: what Appwrite's /executions endpoint expects
    cJSON *envelope = cJSON_CreateObject();
    cJSON_AddStringToObject(envelope, "body", innerBodyStr); // body as a string
    cJSON_AddBoolToObject(envelope, "async", true);          // wait for result
    char *bodyStr = cJSON_PrintUnformatted(envelope);
    cJSON_Delete(envelope);
    free(innerBodyStr);

    if (!bodyStr)
    {
        ESP_LOGE(TAG, "Failed to serialise request envelope");
        return false;
    }

    ESP_LOGI(TAG, "Request payload: %s", bodyStr);

    int status = -1;
    std::string response = _httpClient.httpPost(functionUrl, bodyStr, status);
    free(bodyStr);

    // Appwrite returns 201 for a new execution, not 200
    if (status != 201 && status != 200)
    {
        ESP_LOGE(TAG, "HTTP error: %d", status);
        if (!response.empty())
            ESP_LOGE(TAG, "Body: %s", response.c_str());
        return false;
    }

    // ── Unwrap Appwrite execution envelope ─────────────────────────────────
    // The real payload is inside responseBody (a JSON string) and
    // responseStatusCode tells us if the function itself succeeded.
    envelope = cJSON_Parse(response.c_str());
    if (!envelope)
    {
        ESP_LOGE(TAG, "Failed to parse execution envelope");
        return false;
    }

    // Check the function's own HTTP status
    cJSON *innerStatus = cJSON_GetObjectItem(envelope, "responseStatusCode");
    if (!innerStatus || !cJSON_IsNumber(innerStatus) || (int)innerStatus->valuedouble != 200)
    {
        ESP_LOGE(TAG, "Function inner status: %d",
                 innerStatus ? (int)innerStatus->valuedouble : -1);
        // Log responseBody for debugging
        const std::string innerBody = safeString(envelope, "responseBody");
        if (!innerBody.empty())
            ESP_LOGE(TAG, "Function response: %s", innerBody.c_str());
        cJSON_Delete(envelope);
        return false;
    }

    // responseBody is a JSON string — parse it
    const std::string responseBodyStr = safeString(envelope, "responseBody");
    cJSON_Delete(envelope);

    if (responseBodyStr.empty())
    {
        ESP_LOGE(TAG, "responseBody is empty");
        return false;
    }

    cJSON *doc = cJSON_Parse(responseBodyStr.c_str());
    if (!doc)
    {
        ESP_LOGE(TAG, "Failed to parse responseBody JSON");
        return false;
    }

    // ... rest of your existing parsing code unchanged

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