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
    bool async,
    int &statusOut)
{
    const std::string functionUrl = endpoint + "/functions/" + functionId + "/executions";

    cJSON *inner = cJSON_CreateObject();
    cJSON_AddStringToObject(inner, "url", url.c_str());

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

    // Parse method in this phase
    cJSON *methodArr = cJSON_GetObjectItem(item, "method");
    if (methodArr && cJSON_IsArray(methodArr))
    {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, methodArr)
        {
            std::string ing = cJSON_IsString(item) ? item->valuestring : "";
            if (!ing.empty())
            {
                out.method.push_back(ing);
            }
        }
    }

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
            ref.ref = safeString(imgItem, "ref");
            ref.url = safeString(imgItem, "url");
            if (!ref.ref.empty())
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
    Recipe &outRecipe)
{
    if (functionId.empty() || url.empty())
    {
        ESP_LOGE(TAG, "Invalid config");
        return false;
    }

    ESP_LOGI(TAG, "getRecipe: %s", url.c_str());

    // First, try to fetch from database
    if (getRecipeFromDatabase(url, outRecipe))
    {
        ESP_LOGI(TAG, "Recipe found in database, skipping function execution");
        return true;
    }

    ESP_LOGI(TAG, "Recipe not in database, proceeding with function execution");

    // ─────────────────────────────────────────
    // 1. ASYNC EXECUTION
    // ─────────────────────────────────────────

    int status = -1;
    std::string resp = executeFunction(url, true, status);

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
    std::string syncResp = executeFunction(url, false, syncStatus);

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

// ─────────────────────────────────────────────────────────────
// Fetch recipe from Appwrite database by sourceUrl
// ─────────────────────────────────────────────────────────────

bool RecipeStepsAggregationService::getRecipeFromDatabase(const std::string &url, Recipe &outRecipe)
{
    if (url.empty())
    {
        ESP_LOGE(TAG, "Empty URL provided");
        return false;
    }

    ESP_LOGI(TAG, "Fetching recipe from database for URL: %s", url.c_str());

    // Build base URL for recipes collection
    std::string baseUrl = endpoint + "/tablesdb/" + DatabaseId + "/tables/" + RecipesCollectionId + "/rows";

    // Build equality query for sourceUrl column using cJSON for proper escaping
    cJSON *query = cJSON_CreateObject();
    cJSON_AddStringToObject(query, "method", "equal");
    cJSON_AddStringToObject(query, "attribute", SourceUrlColumn.c_str());
    cJSON *values = cJSON_CreateArray();
    cJSON_AddItemToArray(values, cJSON_CreateString(url.c_str()));
    cJSON_AddItemToObject(query, "values", values);
    char *queryStr = cJSON_PrintUnformatted(query);
    std::string queryJson(queryStr);
    free(queryStr);
    cJSON_Delete(query);
    std::string encodedQuery = AppwriteHttpClient::urlEncode(queryJson);

    std::string fullUrl = baseUrl + "?queries[0]=" + encodedQuery;

    int status = -1;
    std::string response = _httpClient.httpGet(fullUrl, status);

    if (status != 200)
    {
        ESP_LOGE(TAG, "Database query failed with status %d", status);
        return false;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        return false;
    }

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    if (!rows || !cJSON_IsArray(rows) || cJSON_GetArraySize(rows) == 0)
    {
        ESP_LOGI(TAG, "No recipe found for URL");
        cJSON_Delete(root);
        return false;
    }

    // Take first matching row
    cJSON *firstRow = cJSON_GetArrayItem(rows, 0);
    if (!firstRow)
    {
        ESP_LOGE(TAG, "Unexpected null first row");
        cJSON_Delete(root);
        return false;
    }

    // Get document ID
    outRecipe.documentId = safeString(firstRow, "$id");

    // Get recipe JSON column
    cJSON *recipeItem = cJSON_GetObjectItem(firstRow, RecipeDataColumn.c_str());
    if (!recipeItem)
    {
        ESP_LOGE(TAG, "Recipe data column '%s' not found", RecipeDataColumn.c_str());
        cJSON_Delete(root);
        return false;
    }

    std::string recipeJson;
    if (cJSON_IsString(recipeItem))
    {
        recipeJson = recipeItem->valuestring;
    }
    else if (cJSON_IsObject(recipeItem) || cJSON_IsArray(recipeItem))
    {
        char *printed = cJSON_PrintUnformatted(recipeItem);
        recipeJson = printed;
        free(printed);
    }
    else
    {
        ESP_LOGE(TAG, "Recipe data column '%s' is not a string, object, or array", RecipeDataColumn.c_str());
        cJSON_Delete(root);
        return false;
    }

    if (recipeJson.empty())
    {
        ESP_LOGE(TAG, "Recipe data column '%s' is empty", RecipeDataColumn.c_str());
        cJSON_Delete(root);
        return false;
    }

    cJSON_Delete(root);

    // Wrap recipe JSON with success wrapper to reuse parseRecipeResponse
    std::string wrappedJson = "{\"success\":true,\"recipe\":" + recipeJson + "}";

    if (!parseRecipeResponse(wrappedJson, outRecipe))
    {
        ESP_LOGE(TAG, "Failed to parse recipe JSON from database");
        return false;
    }

    ESP_LOGI(TAG, "Successfully loaded recipe from database: %s", outRecipe.title.c_str());
    return true;
}