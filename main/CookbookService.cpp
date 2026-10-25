#include "CookbookService.h"

#include "SupabaseClient.h"

#include "cJSON.h"
#include "esp_log.h"

#include <utility>

static const char *TAG = "COOKBOOKS";

namespace
{

std::string jsonString(cJSON *obj, const char *key)
{
    if (!obj)
        return "";
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(value) && value->valuestring)
        return value->valuestring;
    return "";
}

} // namespace

CookbookService::CookbookService() : _client(getSupabaseClient())
{
}

bool CookbookService::fetchCookbooks(std::vector<Cookbook> &out)
{
    out.clear();

    int status = -1;
    std::string body = _client.select(
        "cookbooks", "select=id,name&order=name.asc", status);
    if (status != 200)
    {
        ESP_LOGE(TAG, "cookbooks fetch failed: status=%d", status);
        return false;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!cJSON_IsArray(root))
    {
        ESP_LOGE(TAG, "cookbooks response is not an array");
        cJSON_Delete(root);
        return false;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (!cJSON_IsObject(item))
            continue;

        Cookbook cookbook;
        cookbook.id = jsonString(item, "id");
        cookbook.name = jsonString(item, "name");
        if (cookbook.id.empty() || cookbook.name.empty())
            continue;
        out.push_back(std::move(cookbook));
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Fetched %u cookbooks", (unsigned)out.size());
    return true;
}

CookbookService cookbookService;
