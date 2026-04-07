#include "RecipeStepsAggregationService.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/task.h"

static const char *TAG = "RecipeStepsAggregationService";

RecipeStepsAggregationService recipeStepsAggregationService;

RecipeStepsAggregationService::RecipeStepsAggregationService()
    : _httpClient(endpoint, projectId, apiKey, 90000)
{
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

std::string RecipeStepsAggregationService::safeString(cJSON *obj, const char *key)
{
    if (!obj)
        return "";
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

// ─────────────────────────────────────────────────────────────
// Decode ISO 8601 duration
// ─────────────────────────────────────────────────────────────

std::string RecipeStepsAggregationService::decodeDuration(const std::string &isoDuration)
{
    if (isoDuration.empty() || isoDuration[0] != 'P')
        return isoDuration; // Return as-is if not ISO 8601 format

    int hours = 0, minutes = 0, seconds = 0;

    // Format: P[n]Y[n]M[n]DT[n]H[n]M[n]S
    // We only care about H, M (in time section), S
    std::string timeSection;
    size_t tPos = isoDuration.find('T');
    if (tPos != std::string::npos)
    {
        timeSection = isoDuration.substr(tPos + 1);
    }

    if (timeSection.empty())
        return isoDuration; // No time component

    size_t pos = 0;
    while (pos < timeSection.length())
    {
        size_t digitStart = pos;
        // Extract digits (handle decimal points too)
        while (pos < timeSection.length() && (isdigit(timeSection[pos]) || timeSection[pos] == '.'))
            pos++;

        if (pos == digitStart || pos >= timeSection.length())
            break;

        char unit = timeSection[pos];
        std::string numStr = timeSection.substr(digitStart, pos - digitStart);
        int value = std::stoi(numStr);

        if (unit == 'H')
            hours = value;
        else if (unit == 'M')
            minutes = value;
        else if (unit == 'S')
            seconds = value;

        pos++;
    }

    // Format output
    std::string result;
    if (hours > 0)
    {
        result += std::to_string(hours) + (hours == 1 ? " hr" : " hrs");
    }
    if (minutes > 0)
    {
        if (!result.empty())
            result += " ";
        result += std::to_string(minutes) + (minutes == 1 ? " min" : " mins");
    }
    if (seconds > 0)
    {
        if (!result.empty())
            result += " ";
        result += std::to_string(seconds) + (seconds == 1 ? " sec" : " secs");
    }

    return result.empty() ? isoDuration : result;
}

// ─────────────────────────────────────────────────────────────
// HTTP execution helper
// ─────────────────────────────────────────────────────────────

std::string RecipeStepsAggregationService::executeFunction(
    const std::string &url,
    int maxWidth,
    int maxHeight,
    bool async,
    int &statusOut)
{
    const std::string functionUrl = endpoint + "/functions/" + functionId + "/executions";

    cJSON *inner = cJSON_CreateObject();
    cJSON_AddStringToObject(inner, "url", url.c_str());
    cJSON_AddNumberToObject(inner, "maxWidth", maxWidth);
    cJSON_AddNumberToObject(inner, "maxHeight", maxHeight);

    char *innerStr = cJSON_PrintUnformatted(inner);
    cJSON_Delete(inner);

    cJSON *env = cJSON_CreateObject();
    cJSON_AddStringToObject(env, "body", innerStr);
    cJSON_AddBoolToObject(env, "async", async);

    char *bodyStr = cJSON_PrintUnformatted(env);
    cJSON_Delete(env);
    free(innerStr);

    std::string response = _httpClient.httpPost(functionUrl, bodyStr, statusOut);
    free(bodyStr);

    return response;
}

// ─────────────────────────────────────────────────────────────
// Poll execution
// ─────────────────────────────────────────────────────────────

cJSON *RecipeStepsAggregationService::pollExecution(const std::string &executionId)
{
    const int maxIterations = 360;
    const int delayMs = 1000;

    for (int i = 0; i < maxIterations; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(delayMs));

        std::string url = endpoint + "/functions/" + functionId + "/executions/" + executionId;

        int status = -1;
        std::string resp = _httpClient.httpGet(url, status);

        if (status != 200)
            continue;

        cJSON *json = cJSON_Parse(resp.c_str());
        if (!json)
            continue;

        std::string s = safeString(json, "status");

        if (i % 5 == 0)
            ESP_LOGI(TAG, "Polling %d/%d: %s", i + 1, maxIterations, s.c_str());

        if (s == "completed")
            return json;

        if (s == "failed")
        {
            ESP_LOGE(TAG, "Execution failed");
            cJSON_Delete(json);
            return nullptr;
        }

        cJSON_Delete(json);
    }

    ESP_LOGE(TAG, "Polling timeout");
    return nullptr;
}

// ─────────────────────────────────────────────────────────────
// Parse JSON helpers
// ─────────────────────────────────────────────────────────────

void RecipeStepsAggregationService::parseIngredient(cJSON *item, RecipeIngredient &out)
{
    if (!item)
        return;
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
            out.push_back(ing);
    }
}

void RecipeStepsAggregationService::parsePhase(cJSON *item, RecipePhase &out)
{
    if (!item)
        return;
    out.title = safeString(item, "title");
    out.method = safeString(item, "method");

    // Parse ingredients in this phase
    cJSON *ingredientsArr = cJSON_GetObjectItem(item, "ingredients");
    if (ingredientsArr && cJSON_IsArray(ingredientsArr))
    {
        parseIngredients(ingredientsArr, out.ingredients);
    }

    // Parse image refs (if any)
    cJSON *imageRefsArr = cJSON_GetObjectItem(item, "imageRefs");
    if (imageRefsArr && cJSON_IsArray(imageRefsArr))
    {
        cJSON *imgItem = nullptr;
        cJSON_ArrayForEach(imgItem, imageRefsArr)
        {
            RecipeImageRef ref;
            ref.fileId = safeString(imgItem, "fileId");
            ref.fileName = safeString(imgItem, "fileName");
            if (!ref.fileId.empty())
                out.imageRefs.push_back(ref);
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Parse final response
// ─────────────────────────────────────────────────────────────

bool RecipeStepsAggregationService::parseRecipeResponse(
    const std::string &body,
    Recipe &out)
{
    cJSON *doc = cJSON_Parse(body.c_str());
    if (!doc)
        return false;

    if (!cJSON_IsTrue(cJSON_GetObjectItem(doc, "success")))
    {
        ESP_LOGE(TAG, "Function returned success=false");
        cJSON_Delete(doc);
        return false;
    }

    out.documentId = safeString(doc, "documentId");

    cJSON *recipe = cJSON_GetObjectItem(doc, "recipe");
    if (!recipe)
    {
        cJSON_Delete(doc);
        return false;
    }

    // Parse basic fields
    out.title = safeString(recipe, "title");
    out.description = safeString(recipe, "description");
    out.prepTime = safeString(recipe, "prepTime");
    out.cookTime = safeString(recipe, "cookTime");
    out.servings = safeString(recipe, "servings");

    // Parse full ingredient list
    cJSON *ingredientsArr = cJSON_GetObjectItem(recipe, "ingredients");
    if (ingredientsArr && cJSON_IsArray(ingredientsArr))
    {
        parseIngredients(ingredientsArr, out.ingredients);
        ESP_LOGI(TAG, "Parsed %d ingredients", (int)out.ingredients.size());
    }

    // Parse aggregated steps (phases)
    cJSON *phasesArr = cJSON_GetObjectItem(recipe, "aggregatedSteps");
    if (phasesArr && cJSON_IsArray(phasesArr))
    {
        cJSON *phaseItem = nullptr;
        cJSON_ArrayForEach(phaseItem, phasesArr)
        {
            RecipePhase phase;
            parsePhase(phaseItem, phase);
            if (!phase.title.empty())
                out.aggregatedSteps.push_back(phase);
        }
        ESP_LOGI(TAG, "Parsed %d phases", (int)out.aggregatedSteps.size());
    }

    cJSON_Delete(doc);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────

bool RecipeStepsAggregationService::getRecipe(
    const std::string &url,
    Recipe &outRecipe,
    int maxWidth,
    int maxHeight)
{
    if (functionId.empty() || url.empty())
    {
        ESP_LOGE(TAG, "Invalid config");
        return false;
    }

    ESP_LOGI(TAG, "getRecipe: %s", url.c_str());

    // ─────────────────────────────────────────
    // 1. ASYNC EXECUTION
    // ─────────────────────────────────────────

    int status = -1;
    std::string resp = executeFunction(url, maxWidth, maxHeight, true, status);

    if (status != 201 && status != 202 && status != 200)
    {
        ESP_LOGE(TAG, "Async execution failed: %d", status);
        return false;
    }

    cJSON *env = cJSON_Parse(resp.c_str());
    if (!env)
        return false;

    std::string executionId = safeString(env, "$id");
    cJSON_Delete(env);

    if (executionId.empty())
    {
        ESP_LOGE(TAG, "Missing executionId");
        return false;
    }

    // ─────────────────────────────────────────
    // 2. POLL
    // ─────────────────────────────────────────

    cJSON *completed = pollExecution(executionId);
    if (!completed)
        return false;

    int code = cJSON_GetObjectItem(completed, "responseStatusCode")->valueint;
    std::string body = safeString(completed, "responseBody");

    ESP_LOGI(TAG, "Async completed: code=%d, body=%d bytes", code, body.length());

    cJSON_Delete(completed);

    // ─────────────────────────────────────────
    // 3. IF BODY OK → DONE
    // ─────────────────────────────────────────

    if (code == 200 && !body.empty())
    {
        return parseRecipeResponse(body, outRecipe);
    }

    // ─────────────────────────────────────────
    // 4. SYNC FALLBACK
    // ─────────────────────────────────────────

    ESP_LOGW(TAG, "Retrying synchronously...");

    int syncStatus = -1;
    std::string syncResp = executeFunction(url, maxWidth, maxHeight, false, syncStatus);

    if (syncStatus != 200 && syncStatus != 201)
    {
        ESP_LOGE(TAG, "Sync execution failed: %d", syncStatus);
        return false;
    }

    cJSON *syncEnv = cJSON_Parse(syncResp.c_str());
    if (!syncEnv)
        return false;

    std::string syncBody = safeString(syncEnv, "responseBody");
    cJSON_Delete(syncEnv);

    if (syncBody.empty())
    {
        ESP_LOGE(TAG, "Sync body still empty");
        return false;
    }

    ESP_LOGI(TAG, "Sync success (%d bytes)", syncBody.length());
    ESP_LOGI(TAG, "Sync success: \n%s", syncBody.c_str());

    return parseRecipeResponse(syncBody, outRecipe);
}