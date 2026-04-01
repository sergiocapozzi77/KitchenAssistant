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
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        status = -1;
        return {};
    }

    int contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    if (contentLength > 0)
    {
        response.resize(contentLength);
        int readLen = esp_http_client_read(client, &response[0], contentLength);
        if (readLen != contentLength)
        {
            ESP_LOGW(TAG, "Read %d bytes, expected %d", readLen, contentLength);
        }
    }
    else
    {
        // Read chunked
        char buffer[512];
        int readLen;
        while ((readLen = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
        {
            response.append(buffer, readLen);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "GET status: %d, response length: %zu", status, response.size());
    return response;
}

/* =========================================================
 * HTTP POST
 * ========================================================= */
std::string FavouriteService::httpPost(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "POST: %s", url.c_str());
    ESP_LOGI(TAG, "Body: %s", body.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.length());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        status = -1;
        return {};
    }

    int written = esp_http_client_write(client, body.c_str(), body.length());
    if (written != body.length())
    {
        ESP_LOGW(TAG, "Written %d bytes, expected %zu", written, body.length());
    }

    int contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    if (contentLength > 0)
    {
        response.resize(contentLength);
        int readLen = esp_http_client_read(client, &response[0], contentLength);
        if (readLen != contentLength)
        {
            ESP_LOGW(TAG, "Read %d bytes, expected %d", readLen, contentLength);
        }
    }
    else
    {
        char buffer[512];
        int readLen;
        while ((readLen = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
        {
            response.append(buffer, readLen);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "POST status: %d, response length: %zu", status, response.size());
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
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "DELETE status: %d", status);
    return status;
}

/* =========================================================
 * HTTP PATCH (not used for favourites but kept for consistency)
 * ========================================================= */
std::string FavouriteService::httpPatch(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "PATCH: %s", url.c_str());
    ESP_LOGI(TAG, "Body: %s", body.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.length());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        status = -1;
        return {};
    }

    int written = esp_http_client_write(client, body.c_str(), body.length());
    if (written != body.length())
    {
        ESP_LOGW(TAG, "Written %d bytes, expected %zu", written, body.length());
    }

    int contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    if (contentLength > 0)
    {
        response.resize(contentLength);
        int readLen = esp_http_client_read(client, &response[0], contentLength);
        if (readLen != contentLength)
        {
            ESP_LOGW(TAG, "Read %d bytes, expected %d", readLen, contentLength);
        }
    }
    else
    {
        char buffer[512];
        int readLen;
        while ((readLen = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
        {
            response.append(buffer, readLen);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "PATCH status: %d, response length: %zu", status, response.size());
    return response;
}

/* =========================================================
 * GENERATE RANDOM ID (for new favourite documents)
 * ========================================================= */
std::string FavouriteService::generateId(int length)
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

    std::string id;
    id.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        id += charset[dis(gen)];
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
 * PUBLIC API: Get all favourites
 * ========================================================= */
std::vector<Favorite> FavouriteService::getFavourites()
{
    std::vector<Favorite> favourites;

    // Build URL for listing favourites
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows";

    int status = 0;
    std::string response = httpGet(url, status);

    if (status != 200 || response.empty())
    {
        ESP_LOGE(TAG, "Failed to get favourites: status=%d, response=%s", status, response.c_str());
        return favourites;
    }

    // Parse JSON response
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse favourites JSON");
        return favourites;
    }

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    if (!cJSON_IsArray(rows))
    {
        ESP_LOGE(TAG, "No 'rows' array in response");
        cJSON_Delete(root);
        return favourites;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, rows)
    {
        favourites.push_back(parseFavouriteFromJson(item));
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %zu favourites", favourites.size());
    return favourites;
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

    // Check if already favourited
    if (isFavourite(recipe.url))
    {
        ESP_LOGI(TAG, "Recipe already favourited: %s", recipe.url.c_str());
        return true; // Already favourited, treat as success
    }

    // Build URL for creating favourite
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows";

    // Build JSON body
    std::string body = buildFavouriteJson(recipe);

    int status = 0;
    std::string response = httpPost(url, body, status);

    if (status != 201) // 201 Created
    {
        ESP_LOGE(TAG, "Failed to add favourite: status=%d, response=%s", status, response.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Added favourite: %s", recipe.url.c_str());
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

    // First, find the favourite document ID by URL
    // Build query URL
    std::string query = "{\"method\":\"equal\",\"attribute\":\"url\",\"values\":[\"" + url + "\"]}";
    std::string encodedQuery = urlEncode(query);
    std::string listUrl = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows?queries=" + encodedQuery;

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

    if (deleteStatus != 204) // 204 No Content
    {
        ESP_LOGE(TAG, "Failed to delete favourite: status=%d", deleteStatus);
        return false;
    }

    ESP_LOGI(TAG, "Removed favourite: %s", url.c_str());
    return true;
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

    // Build query URL
    std::string query = "{\"method\":\"equal\",\"attribute\":\"url\",\"values\":[\"" + url + "\"]}";
    std::string encodedQuery = urlEncode(query);
    std::string listUrl = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + FavouritesCollectionId + "/rows?queries=" + encodedQuery;

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