#include "SupabaseClient.h"

#include "secrets.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef SUPABASE_URL
#error "main/secrets.h must define SUPABASE_URL (see secrets.h.example)"
#endif
#ifndef SUPABASE_SERVICE_KEY
#error "main/secrets.h must define SUPABASE_SERVICE_KEY (see secrets.h.example)"
#endif
#ifndef SUPABASE_USER_ID
#error "main/secrets.h must define SUPABASE_USER_ID (see secrets.h.example)"
#endif

static const char *TAG = "SUPABASE";

namespace
{

esp_http_client_method_t methodFromString(const char *method)
{
    if (std::strcmp(method, "POST") == 0)
        return HTTP_METHOD_POST;
    if (std::strcmp(method, "PATCH") == 0)
        return HTTP_METHOD_PATCH;
    if (std::strcmp(method, "DELETE") == 0)
        return HTTP_METHOD_DELETE;
    return HTTP_METHOD_GET;
}

esp_err_t httpEventCb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA)
    {
        auto *out = static_cast<std::string *>(evt->user_data);
        if (out && evt->data && evt->data_len > 0)
            out->append(static_cast<const char *>(evt->data), evt->data_len);
    }
    return ESP_OK;
}

} // namespace

SupabaseClient::SupabaseClient(const std::string &baseUrl,
                               const std::string &serviceKey,
                               const std::string &userId,
                               int timeoutMs)
    : _baseUrl(baseUrl), _serviceKey(serviceKey), _userId(userId),
      _timeoutMs(timeoutMs)
{
}

std::string SupabaseClient::request(const char *method, const std::string &url,
                                    const std::string &body, int &status) const
{
    status = -1;
    std::lock_guard<std::mutex> lock(_mutex);
    std::string response;

    if (!_handle)
    {
        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.timeout_ms = _timeoutMs;
        cfg.buffer_size = 4096;
        cfg.buffer_size_tx = 2048;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.keep_alive_enable = true;
        cfg.keep_alive_idle = 5;
        cfg.keep_alive_interval = 2;
        cfg.keep_alive_count = 2;
        cfg.event_handler = httpEventCb;
        cfg.user_data = &response;
        _handle = esp_http_client_init(&cfg);
        if (!_handle)
        {
            ESP_LOGE(TAG, "esp_http_client_init failed");
            return "";
        }
    }
    else
    {
        esp_http_client_set_url(static_cast<esp_http_client_handle_t>(_handle), url.c_str());
    }

    auto client = static_cast<esp_http_client_handle_t>(_handle);
    esp_http_client_set_user_data(client, &response);
    esp_http_client_set_method(client, methodFromString(method));
    esp_http_client_set_header(client, "apikey", _serviceKey.c_str());
    std::string auth = "Bearer " + _serviceKey;
    esp_http_client_set_header(client, "Authorization", auth.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Prefer", "return=representation");

    if (!body.empty())
        esp_http_client_set_post_field(client, body.data(), (int)body.size());
    else
        esp_http_client_set_post_field(client, nullptr, 0);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "%s %s failed: %s", method, url.c_str(), esp_err_to_name(err));
        esp_http_client_cleanup(client);
        _handle = nullptr;
        return "";
    }

    status = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "%s %s -> %d (%u bytes)", method, url.c_str(), status,
             (unsigned)response.size());
    return response;
}

std::string SupabaseClient::select(const std::string &table,
                                   const std::string &filters, int &status) const
{
    std::string url = _baseUrl + "/rest/v1/" + table +
                      "?user_id=eq." + urlEncode(_userId);
    if (!filters.empty())
        url += "&" + filters;
    return request("GET", url, "", status);
}

std::string SupabaseClient::insert(const std::string &table,
                                   const std::string &jsonBody, int &status) const
{
    cJSON *root = cJSON_Parse(jsonBody.c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        status = -1;
        ESP_LOGE(TAG, "insert(%s): body is not a JSON object", table.c_str());
        return "";
    }

    cJSON_DeleteItemFromObjectCaseSensitive(root, "user_id");
    cJSON_AddStringToObject(root, "user_id", _userId.c_str());
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed)
    {
        status = -1;
        return "";
    }

    std::string body(printed);
    cJSON_free(printed);
    return request("POST", _baseUrl + "/rest/v1/" + table, body, status);
}

std::string SupabaseClient::update(const std::string &table,
                                   const std::string &filters,
                                   const std::string &jsonBody, int &status) const
{
    std::string query = "user_id=eq." + urlEncode(_userId);
    if (!filters.empty())
        query += "&" + filters;
    return request("PATCH", _baseUrl + "/rest/v1/" + table + "?" + query,
                   jsonBody, status);
}

int SupabaseClient::remove(const std::string &table,
                           const std::string &filters) const
{
    std::string query = "user_id=eq." + urlEncode(_userId);
    if (!filters.empty())
        query += "&" + filters;
    int status = -1;
    request("DELETE", _baseUrl + "/rest/v1/" + table + "?" + query, "", status);
    return status;
}

std::string SupabaseClient::urlEncode(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

SupabaseClient &getSupabaseClient()
{
    static SupabaseClient client(SUPABASE_URL, SUPABASE_SERVICE_KEY,
                                 SUPABASE_USER_ID);
    return client;
}
