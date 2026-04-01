#include "FavouriteService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>

FavouriteService favouriteService;

static const char *TAG = "FavouriteService";

FavouriteService::FavouriteService()
{
}

/* =========================================================
 * URL ENCODING
 * ========================================================= */
std::string FavouriteService::urlEncode(const std::string &s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;

    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out << c;
        }
        else
        {
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return out.str();
}

/* =========================================================
 * HTTP HELPER - DRY PRINCIPLE
 * ========================================================= */
esp_http_client_handle_t FavouriteService::createHttpClient(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    // Set common headers
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());

    return client;
}

/* =========================================================
 * HTTP GET
 * ========================================================= */
std::string FavouriteService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "GET: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    int content_len = esp_http_client_get_content_length(client);

    std::string body;
    body.reserve(content_len > 0 ? content_len : 512);

    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        body.append(buffer, bytes_read);
    }

    if (bytes_read < 0)
    {
        ESP_LOGE(TAG, "HTTP read error");
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Status: %d, Body length: %d", status, body.length());

    esp_http_client_cleanup(client);
    return body;
}

/* =========================================================
 * HTTP POST
 * ========================================================= */
std::string FavouriteService::httpPost(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "POST: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    ESP_LOGI(TAG, "POST Body: %d", body.size());
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    ESP_LOGI(TAG, "POST Body write: %d", body.size());
    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written != body.size())
    {
        ESP_LOGE(TAG, "Write error: wrote %d of %d bytes", bytes_written, body.size());
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "POST Status: %d. Reading response", status);

    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, bytes_read);
    }

    ESP_LOGI(TAG, "POST Status: %d", status);
    esp_http_client_cleanup(client);
    return response;
}

/* =========================================================
 * HTTP DELETE
 * ========================================================= */
int FavouriteService::httpDelete(const std::string &url)
{
    ESP_LOGI(TAG, "DELETE: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return -1;
    }

    esp_http_client_set_method(client, HTTP_METHOD_DELETE);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "DELETE Status: %d", status);
    esp_http_client_cleanup(client);
    return status;
}

/* =========================================================
 * HTTP PATCH (not used for favourites but kept for consistency)
 * ========================================================= */
std::string FavouriteService::httpPatch(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "PATCH: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written != body.size())
    {
        ESP_LOGE(TAG, "Write error: wrote %d of %d bytes", bytes_written, body.size());
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, bytes_read);
    }

    ESP_LOGI(TAG, "PATCH Status: %d", status);
    esp_http_client_cleanup(client);
    return response;
}

/* =========================================================
 * GENERATE RANDOM ID (for new favourite documents)
 * ========================================================= */
std::string FavouriteService::generateId(int length)
{
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string id;
    id.reserve(length);

    for (int i = 0; i < length; i++)
    {
        id += chars[dist(gen)];
    }

    return id;
}

/* =========================================================
 * JSON PARSING: Parse favourite from Appwrite response
 * ========================================================= */
Favorite FavouriteService::parseFavouriteFromJson(cJSON *item)
{
    Favorite fav;

    // Extract document ID ($id) from root
    cJSON *id = cJSON_GetObjectItem(item, "$id");
    if (id && cJSON_IsString(id))
    {
        fav.id = id->valuestring;
    }

    // Extract data object
    cJSON *data = cJSON_GetObjectItem(item, "data");
    if (data && cJSON_IsObject(data))
    {
        cJSON *url = cJSON_GetObjectItem(data, "url");
        if (url && cJSON_IsString(url))
        {
            fav.url = url->valuestring;
        }

        cJSON *name = cJSON_GetObjectItem(data, "name");
        if (name && cJSON_IsString(name))
        {
            fav.name = name->valuestring;
        }

        cJSON *imageUrl = cJSON_GetObjectItem(data, "imageUrl");
        if (imageUrl && cJSON_IsString(imageUrl))
        {
            fav.imageUrl = imageUrl->valuestring;
        }
    }
    else
    {
        // Fallback: try to read fields directly from root (backwards compatibility)
        cJSON *url = cJSON_GetObjectItem(item, "url");
        if (url && cJSON_IsString(url))
        {
            fav.url = url->valuestring;
        }

        cJSON *name = cJSON_GetObjectItem(item, "name");
        if (name && cJSON_IsString(name))
        {
            fav.name = name->valuestring;
        }

        cJSON *imageUrl = cJSON_GetObjectItem(item, "imageUrl");
        if (imageUrl && cJSON_IsString(imageUrl))
        {
            fav.imageUrl = imageUrl->valuestring;
        }
    }

    return fav;
}

/* =========================================================
 * BUILD JSON for creating a favourite
 * ========================================================= */
std::string FavouriteService::buildFavouriteJson(const RecipeSuggestion &recipe)
{
    // Appwrite expects a JSON object with rowId and data
    std::string rowId = generateId();
    ESP_LOGD(TAG, "Generated rowId: %s", rowId.c_str());

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "JSON alloc failed");
        return "{}";
    }

    cJSON_AddStringToObject(root, "rowId", rowId.c_str());

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (!data)
    {
        ESP_LOGE(TAG, "JSON data alloc failed");
        cJSON_Delete(root);
        return "{}";
    }

    cJSON_AddStringToObject(data, "url", recipe.url.c_str());
    cJSON_AddStringToObject(data, "name", recipe.name.c_str());
    cJSON_AddStringToObject(data, "imageUrl", recipe.imageUrl.c_str());

    char *json = cJSON_PrintUnformatted(root);
    if (!json)
    {
        ESP_LOGE(TAG, "JSON print failed");
        cJSON_Delete(root);
        return "{}";
    }

    std::string jsonStr = json;
    free(json);
    cJSON_Delete(root);
    return jsonStr;
}

/* =========================================================
 * RETRY WRAPPER (similar to ProductService)
 * ========================================================= */
std::vector<Favorite> FavouriteService::getFavouritesRetry(const std::vector<std::string> &queries, int &out)
{
    std::vector<Favorite> result;
    int maxRetry = 5; // Set the limit clearly
    int attempt = 0;

    while (attempt < maxRetry)
    {
        result = getFavourites(queries, out);

        if (out == 0)
        {
            ESP_LOGI(TAG, "Successfully fetched %d favourites on attempt %d", result.size(), attempt + 1);
            return result; // Success! Exit early
        }

        attempt++;
        ESP_LOGE(TAG, "Attempt %d/%d failed. Retrying in 2s...", attempt, maxRetry);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGE(TAG, "All retry attempts failed.");
    return result;
}

/* =========================================================
 * PUBLIC API: Get all favourites (with pagination)
 * ========================================================= */
std::vector<Favorite> FavouriteService::getFavourites(const std::vector<std::string> &queries, int &out)
{
    out = -1; // Default to error
    std::vector<Favorite> allFavourites;

    const int perPage = 25;
    int offset = 0;
    int total = 2147483647;
    int safetyCounter = 0; // Prevent infinite loops

    std::string baseUrl = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows";

    while ((int)allFavourites.size() < total && safetyCounter++ < 40)
    {
        // 1. Give the system a moment to breathe (Reset WDT)
        vTaskDelay(pdMS_TO_TICKS(50));

        std::string url = baseUrl + "?";
        int qIdx = 0;

        for (const auto &q : queries)
        {
            url += "queries[" + std::to_string(qIdx++) + "]=" + urlEncode(q) + "&";
        }

        std::string limitJson = "{\"method\":\"limit\",\"values\":[" + std::to_string(perPage) + "]}";
        std::string offsetJson = "{\"method\":\"offset\",\"values\":[" + std::to_string(offset) + "]}";

        url += "queries[" + std::to_string(qIdx++) + "]=" + urlEncode(limitJson) + "&";
        url += "queries[" + std::to_string(qIdx++) + "]=" + urlEncode(offsetJson);

        // 2. HTTP Request
        int status;
        std::string body = httpGet(url, status);

        if (status != 200)
        {
            ESP_LOGE(TAG, "Network Error: %d at offset %d", status, offset);
            out = -1;
            return allFavourites; // Returning early ensures getFavouritesRetry sees out != 0
        }

        // 3. JSON Parsing
        cJSON *root = cJSON_Parse(body.c_str());
        if (!root)
        {
            ESP_LOGE(TAG, "JSON Parse Error at offset %d", offset);
            out = -1;
            return allFavourites;
        }

        if (total == 2147483647)
        {
            cJSON *totalItem = cJSON_GetObjectItem(root, "total");
            if (totalItem && cJSON_IsNumber(totalItem))
                total = totalItem->valueint;
        }

        cJSON *rows = cJSON_GetObjectItem(root, "rows");
        if (!rows || !cJSON_IsArray(rows) || cJSON_GetArraySize(rows) == 0)
        {
            cJSON_Delete(root);
            break;
        }

        cJSON *item;
        cJSON_ArrayForEach(item, rows)
        {
            allFavourites.push_back(parseFavouriteFromJson(item));
        }

        int rowsFetched = cJSON_GetArraySize(rows);
        cJSON_Delete(root);
        offset = allFavourites.size();

        if (rowsFetched < perPage || (int)allFavourites.size() >= total)
            break;
    }

    out = 0; // Success!
    ESP_LOGI(TAG, "Loaded %zu favourites", allFavourites.size());
    return allFavourites;
}

/* =========================================================
 * PUBLIC API: Get all favourites (simple version - backwards compatible)
 * ========================================================= */
std::vector<Favorite> FavouriteService::getFavourites()
{
    int out;
    return getFavouritesRetry({}, out);
}

/* =========================================================
 * PUBLIC API: Add favourite
 * ========================================================= */
bool FavouriteService::addFavourite(const RecipeSuggestion &recipe)
{
    if (recipe.url.empty())
    {
        ESP_LOGE(TAG, "Cannot add favourite: recipe URL is empty");
        return false;
    }

    ESP_LOGI(TAG, "Adding favourite: url='%s', name='%s', imageUrl='%s'",
             recipe.url.c_str(), recipe.name.c_str(), recipe.imageUrl.c_str());

    // Check if already favourited
    if (isFavourite(recipe.url))
    {
        ESP_LOGI(TAG, "Recipe already favourited: %s", recipe.url.c_str());
        return true; // Already favourited, treat as success
    }

    // Build URL for creating favourite
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows";
    ESP_LOGD(TAG, "POST URL: %s", url.c_str());

    // Build JSON body
    std::string body = buildFavouriteJson(recipe);
    ESP_LOGD(TAG, "Request payload: %s", body.c_str());
    ESP_LOGI(TAG, "Sending HTTP POST request...");

    int status = 0;
    std::string response = httpPost(url, body, status);

    ESP_LOGI(TAG, "HTTP response: status=%d, length=%d", status, response.length());

    if (status != 200 && status != 201)
    {
        ESP_LOGE(TAG, "addFavourite failed: status=%d, response='%s'",
                 status, response.c_str());
        return false;
    }

    ESP_LOGD(TAG, "Response body: %s", response.c_str());
    ESP_LOGI(TAG, "Added favourite: %s", recipe.url.c_str());
    ESP_LOGI(TAG, "Favourite addition complete");
    return true;
}

/* =========================================================
 * PUBLIC API: Remove favourite by URL
 * ========================================================= */
bool FavouriteService::removeFavourite(const std::string &url)
{
    if (url.empty())
    {
        ESP_LOGE(TAG, "Cannot remove favourite: URL is empty");
        return false;
    }

    ESP_LOGI(TAG, "Removing favourite: url='%s'", url.c_str());

    // First, find the favourite document ID by URL
    // Build query URL with proper indexed parameter format
    std::string query = "{\"method\":\"equal\",\"attribute\":\"url\",\"values\":[\"" + url + "\"]}";
    std::string encodedQuery = urlEncode(query);
    std::string listUrl = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows?queries[0]=" + encodedQuery;

    int status = 0;
    std::string response = httpGet(listUrl, status);

    if (status != 200 || response.empty())
    {
        ESP_LOGE(TAG, "Failed to find favourite for removal: status=%d", status);
        return false;
    }

    // Parse response to get document ID
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse favourite query JSON");
        return false;
    }

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    if (!cJSON_IsArray(rows) || cJSON_GetArraySize(rows) == 0)
    {
        ESP_LOGI(TAG, "No favourite found for URL: %s", url.c_str());
        cJSON_Delete(root);
        return true; // Not found, treat as success (already removed)
    }

    // Get first matching favourite
    cJSON *firstItem = cJSON_GetArrayItem(rows, 0);
    cJSON *id = cJSON_GetObjectItem(firstItem, "$id");
    if (!id || !cJSON_IsString(id))
    {
        ESP_LOGE(TAG, "No $id field in favourite document");
        cJSON_Delete(root);
        return false;
    }

    std::string docId = id->valuestring;
    cJSON_Delete(root);

    // Now delete the document
    std::string deleteUrl = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows/" + docId;
    int deleteStatus = httpDelete(deleteUrl);

    bool success = (deleteStatus == 200 || deleteStatus == 204);

    if (success)
    {
        ESP_LOGI(TAG, "Removed favourite: %s", url.c_str());
    }
    else
    {
        ESP_LOGE(TAG, "Failed to delete favourite: status=%d", deleteStatus);
    }

    return success;
}

/* =========================================================
 * PUBLIC API: Check if URL is favourited
 * ========================================================= */
bool FavouriteService::isFavourite(const std::string &url)
{
    if (url.empty())
    {
        return false;
    }

    // Build query URL with proper indexed parameter format
    std::string query = "{\"method\":\"equal\",\"attribute\":\"url\",\"values\":[\"" + url + "\"]}";
    std::string encodedQuery = urlEncode(query);
    std::string listUrl = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows?queries[0]=" + encodedQuery;

    int status = 0;
    std::string response = httpGet(listUrl, status);

    if (status != 200 || response.empty())
    {
        ESP_LOGE(TAG, "Failed to check favourite status: status=%d", status);
        return false;
    }

    // Parse response
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse favourite check JSON");
        return false;
    }

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    bool isFav = cJSON_IsArray(rows) && cJSON_GetArraySize(rows) > 0;

    cJSON_Delete(root);
    return isFav;
}