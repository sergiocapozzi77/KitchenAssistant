#include "AppwriteHttpClient.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>

static const char *TAG = "AppwriteHttpClient";

AppwriteHttpClient::AppwriteHttpClient(const std::string &endpoint, const std::string &projectId, const std::string &apiKey, const int timeout_ms)
    : _endpoint(endpoint), _projectId(projectId), _apiKey(apiKey), _timeout_ms(timeout_ms)
{
}

void AppwriteHttpClient::configureHttpClient(esp_http_client_handle_t client, const std::string &projectId, const std::string &apiKey)
{
    // Set common Appwrite headers
    esp_http_client_set_header(client, "X-Appwrite-Project", projectId.c_str());
    esp_http_client_set_header(client, "X-Appwrite-Key", apiKey.c_str());
}

esp_http_client_handle_t AppwriteHttpClient::createHttpClient(const std::string &url) const
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = _timeout_ms;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client)
    {
        configureHttpClient(client, _projectId, _apiKey);
    }
    return client;
}

std::string AppwriteHttpClient::httpGet(const std::string &url, int &status) const
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

std::string AppwriteHttpClient::httpPost(const std::string &url, const std::string &body, int &status) const
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

std::string AppwriteHttpClient::httpPatch(const std::string &url, const std::string &body, int &status) const
{
    ESP_LOGI(TAG, "PATCH: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
    {
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

int AppwriteHttpClient::httpDelete(const std::string &url) const
{
    ESP_LOGI(TAG, "DELETE: %s", url.c_str());

    esp_http_client_handle_t client = createHttpClient(url);
    if (!client)
        return -1;

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

std::string AppwriteHttpClient::urlEncode(const std::string &s)
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

std::string AppwriteHttpClient::generateId(int length)
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