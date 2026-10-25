#include "ProductService.h"

#include "SupabaseClient.h"

#include "cJSON.h"
#include "esp_log.h"

#include <cstdio>
#include <cstring>
#include <utility>

static const char *TAG = "PRODUCTS";

namespace
{

std::string jsonString(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring)
        return v->valuestring;
    return "";
}

int jsonInt(cJSON *obj, const char *key, int fallback = 0)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : fallback;
}

bool jsonBool(cJSON *obj, const char *key, bool fallback = false)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v))
        return cJSON_IsTrue(v) != 0;
    return fallback;
}

std::string normalizedCategory(const std::string &category)
{
    return category.empty() ? "Other" : category;
}

} // namespace

ProductService::ProductService() : _client(getSupabaseClient())
{
}

bool ProductService::getProducts(std::vector<Product> &out)
{
    out.clear();
    constexpr int kPageSize = 1000;

    int offset = 0;
    for (int page = 0; page < 10; ++page)
    {
        std::string filters =
            "select=id,name,quantity,category,expiry,frozen,barcode"
            "&order=name.asc&limit=" +
            std::to_string(kPageSize) + "&offset=" + std::to_string(offset);

        int status = -1;
        std::string body = _client.select("products", filters, status);
        if (status != 200)
        {
            ESP_LOGE(TAG, "products fetch failed: status=%d", status);
            return false;
        }

        cJSON *root = cJSON_Parse(body.c_str());
        if (!cJSON_IsArray(root))
        {
            ESP_LOGE(TAG, "products response is not an array");
            cJSON_Delete(root);
            return false;
        }

        int count = cJSON_GetArraySize(root);
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, root)
        {
            if (!cJSON_IsObject(item))
                continue;

            Product p;
            p.id = jsonString(item, "id");
            p.name = jsonString(item, "name");
            if (p.id.empty() || p.name.empty())
                continue;

            p.category = normalizedCategory(jsonString(item, "category"));
            p.expiry = jsonString(item, "expiry");
            p.barcode = jsonString(item, "barcode");
            p.quantity = jsonInt(item, "quantity", 0);
            p.frozen = jsonBool(item, "frozen", false);
            out.push_back(std::move(p));
        }
        cJSON_Delete(root);

        offset += count;
        if (count < kPageSize)
            break;
    }

    ESP_LOGI(TAG, "Fetched %u products", (unsigned)out.size());
    return true;
}

bool ProductService::updateProduct(const Product &product)
{
    if (product.id.empty())
        return false;

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return false;
    cJSON_AddStringToObject(root, "name", product.name.c_str());
    cJSON_AddNumberToObject(root, "quantity", product.quantity);
    cJSON_AddStringToObject(root, "category", normalizedCategory(product.category).c_str());
    // A PostgreSQL date column accepts null, not the empty string.
    if (product.expiry.empty())
        cJSON_AddNullToObject(root, "expiry");
    else
        cJSON_AddStringToObject(root, "expiry", product.expiry.c_str());
    cJSON_AddBoolToObject(root, "frozen", product.frozen);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
        return false;

    int status = -1;
    std::string filters = "id=eq." + SupabaseClient::urlEncode(product.id);
    std::string response = _client.update("products", filters, body, status);
    cJSON_free(body);

    if (status != 200 && status != 204)
    {
        ESP_LOGE(TAG, "update product %s failed: status=%d response=%.120s",
                 product.id.c_str(), status, response.c_str());
        return false;
    }
    return true;
}

bool ProductService::deleteProduct(const std::string &id)
{
    if (id.empty())
        return false;
    int status = _client.remove("products", "id=eq." + SupabaseClient::urlEncode(id));
    if (status != 204 && status != 200)
    {
        ESP_LOGE(TAG, "delete product %s failed: status=%d", id.c_str(), status);
        return false;
    }
    return true;
}

bool ProductService::upsertBarcode(const std::string &barcode,
                                   const std::string &name,
                                   const std::string &category)
{
    if (barcode.empty())
        return false;

    std::string filter = "select=id&barcode=eq." +
                         SupabaseClient::urlEncode(barcode) + "&limit=1";
    int status = -1;
    std::string body = _client.select("barcodes", filter, status);
    if (status != 200)
    {
        ESP_LOGE(TAG, "barcode lookup failed: status=%d", status);
        return false;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    std::string existingId;
    if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0)
    {
        cJSON *first = cJSON_GetArrayItem(root, 0);
        existingId = jsonString(first, "id");
    }
    cJSON_Delete(root);

    cJSON *row = cJSON_CreateObject();
    if (!row)
        return false;
    cJSON_AddStringToObject(row, "barcode", barcode.c_str());
    cJSON_AddStringToObject(row, "name", name.c_str());
    cJSON_AddStringToObject(row, "category", normalizedCategory(category).c_str());
    char *printed = cJSON_PrintUnformatted(row);
    cJSON_Delete(row);
    if (!printed)
        return false;

    bool ok = false;
    if (!existingId.empty())
    {
        std::string filters = "id=eq." + SupabaseClient::urlEncode(existingId);
        std::string response = _client.update("barcodes", filters, printed, status);
        ok = (status == 200 || status == 204);
        if (!ok)
            ESP_LOGE(TAG, "barcode update failed: status=%d response=%.120s",
                     status, response.c_str());
    }
    else
    {
        std::string response = _client.insert("barcodes", printed, status);
        ok = (status == 201 || status == 200);
        if (!ok)
            ESP_LOGE(TAG, "barcode insert failed: status=%d response=%.120s",
                     status, response.c_str());
    }
    cJSON_free(printed);
    return ok;
}

ProductService productService;
