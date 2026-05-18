#include "CookbookService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "secrets.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>

CookbookService cookbookService;

static const char *TAG = "CookbookService";

CookbookService::CookbookService() {}

// ── URL Encode ────────────────────────────────────────────────────
std::string CookbookService::urlEncode(const std::string &s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
    }
    return out.str();
}

// ── HTTP helpers ──────────────────────────────────────────────────
esp_http_client_handle_t CookbookService::createHttpClient(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "X-Appwrite-Project", ProjectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());
    return client;
}

std::string CookbookService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "GET: %s", url.c_str());
    esp_http_client_handle_t client = createHttpClient(url);
    if (!client) { ESP_LOGE(TAG, "Failed to create HTTP client"); status = -1; return {}; }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err)); status = -1; esp_http_client_cleanup(client); return {}; }

    esp_http_client_fetch_headers(client);
    int content_len = esp_http_client_get_content_length(client);
    std::string body;
    body.reserve(content_len > 0 ? content_len : 512);
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
        body.append(buffer, bytes_read);
    if (bytes_read < 0) { status = -1; esp_http_client_cleanup(client); return {}; }
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Status: %d, Body length: %d", status, body.length());
    esp_http_client_cleanup(client);
    return body;
}

std::string CookbookService::httpPost(const std::string &url, const std::string &body, int &status)
{
    ESP_LOGI(TAG, "POST: %s", url.c_str());
    esp_http_client_handle_t client = createHttpClient(url);
    if (!client) { ESP_LOGE(TAG, "Failed to create HTTP client"); status = -1; return {}; }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK) { ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err)); status = -1; esp_http_client_cleanup(client); return {}; }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written != (int)body.size()) { status = -1; esp_http_client_cleanup(client); return {}; }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
        response.append(buffer, bytes_read);

    esp_http_client_cleanup(client);
    return response;
}

int CookbookService::httpDelete(const std::string &url)
{
    ESP_LOGI(TAG, "DELETE: %s", url.c_str());
    esp_http_client_handle_t client = createHttpClient(url);
    if (!client) { ESP_LOGE(TAG, "Failed to create HTTP client"); return -1; }

    esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err)); esp_http_client_cleanup(client); return -1; }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return status;
}

// ── Generate ID ───────────────────────────────────────────────────
std::string CookbookService::generateId(int length)
{
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    std::string id;
    id.reserve(length);
    for (int i = 0; i < length; i++) id += chars[dist(gen)];
    return id;
}

// ── JSON parsing ──────────────────────────────────────────────────
Cookbook CookbookService::parseCookbookFromJson(cJSON *item)
{
    Cookbook cb;
    cJSON *id = cJSON_GetObjectItem(item, "$id");
    if (id && cJSON_IsString(id)) cb.id = id->valuestring;

    cJSON *data = cJSON_GetObjectItem(item, "data");
    if (data && cJSON_IsObject(data))
    {
        cJSON *name = cJSON_GetObjectItem(data, "name");
        if (name && cJSON_IsString(name)) cb.name = name->valuestring;
    }
    else
    {
        // Fallback: direct field
        cJSON *name = cJSON_GetObjectItem(item, "name");
        if (name && cJSON_IsString(name)) cb.name = name->valuestring;
    }
    return cb;
}

// ── Public API ────────────────────────────────────────────────────

std::vector<Cookbook> CookbookService::getCookbooks()
{
    std::vector<Cookbook> results;
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CookbooksCollectionId + "/rows";

    int status;
    std::string body = httpGet(url, status);
    if (status != 200) { ESP_LOGE(TAG, "getCookbooks failed: %d", status); return results; }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) { ESP_LOGE(TAG, "JSON parse error"); return results; }

    cJSON *rows = cJSON_GetObjectItem(root, "rows");
    if (cJSON_IsArray(rows))
    {
        cJSON *item;
        cJSON_ArrayForEach(item, rows)
            results.push_back(parseCookbookFromJson(item));
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %zu cookbooks", results.size());
    return results;
}

std::string CookbookService::createCookbook(const std::string &name)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CookbooksCollectionId + "/rows";

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "JSON alloc failed");
        return "";
    }
    cJSON_AddStringToObject(root, "rowId", generateId().c_str());
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (!data)
    {
        ESP_LOGE(TAG, "JSON data alloc failed");
        cJSON_Delete(root);
        return "";
    }
    cJSON_AddStringToObject(data, "name", name.c_str());

    char *json = cJSON_PrintUnformatted(root);
    std::string body = json ? json : "{}";
    free(json);
    cJSON_Delete(root);

    int status;
    std::string response = httpPost(url, body, status);
    if (status != 200 && status != 201) { ESP_LOGE(TAG, "createCookbook failed: %d", status); return ""; }

    // Parse response to get $id
    cJSON *respRoot = cJSON_Parse(response.c_str());
    if (!respRoot) return "";
    std::string newId;
    cJSON *idItem = cJSON_GetObjectItem(respRoot, "$id");
    if (idItem && cJSON_IsString(idItem)) newId = idItem->valuestring;
    cJSON_Delete(respRoot);

    ESP_LOGI(TAG, "Created cookbook '%s' with id %s", name.c_str(), newId.c_str());
    return newId;
}

bool CookbookService::deleteCookbook(const std::string &id)
{
    std::string url = Endpoint + "/tablesdb/" + DatabaseId + "/tables/" + CookbooksCollectionId + "/rows/" + id;
    int status = httpDelete(url);
    bool ok = (status == 200 || status == 204);
    ESP_LOGI(TAG, "Deleted cookbook %s: %s", id.c_str(), ok ? "ok" : "fail");
    return ok;
}
