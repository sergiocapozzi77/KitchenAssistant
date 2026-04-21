#include "LeonardoImageGenerator.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

static const char *TAG = "LeonardoImageGenerator";

LeonardoImageGenerator::LeonardoImageGenerator(const std::string &endpoint, const std::string &model, const std::string &apiKey, int timeout_ms)
    : _httpClient(endpoint, "", "", timeout_ms), // Empty projectId and apiKey, headers will be skipped
      _endpoint(endpoint),
      _model(model),
      _apiKey(apiKey),
      _timeout_ms(timeout_ms)
{
}

std::string LeonardoImageGenerator::generateImage(const std::string &prompt, uint16_t width, uint16_t height, int &status, int timeout_ms)
{
    ESP_LOGI(TAG, "Generating image with Leonardo AI API, prompt: %s", prompt.c_str());

    // Build the full URL for generations endpoint
    std::string url = _endpoint + "/generations";

    // Build JSON payload for image generation
    cJSON *payload = cJSON_CreateObject();
    if (!payload)
    {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        status = -1;
        return {};
    }

    cJSON_AddStringToObject(payload, "prompt", prompt.c_str());
    cJSON_AddNumberToObject(payload, "num_images", 1);
    cJSON_AddNumberToObject(payload, "width", width);
    cJSON_AddNumberToObject(payload, "height", height);
    // Use model ID from constructor (or default)
    std::string modelId = _model.empty() ? "b2614463-296c-462a-9586-aafdb8f00e36" : _model;
    cJSON_AddStringToObject(payload, "modelId", modelId.c_str());

    char *payloadStr = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payloadStr)
    {
        ESP_LOGE(TAG, "Failed to stringify Leonardo payload");
        status = -1;
        return {};
    }

    ESP_LOGI(TAG, "Leonardo payload: %s", payloadStr);

    // Perform POST request with Bearer token authorization
    std::string response = leonardoHttpPost(url, payloadStr, status);
    free(payloadStr);

    if (status != 200 && status != 201 && status != 202)
    {
        ESP_LOGE(TAG, "Leonardo API error: HTTP %d, response: %s", status, response.c_str());
        return {};
    }

    ESP_LOGI(TAG, "Leonardo response length: %d", response.length());

    // Parse response to extract generation ID
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse Leonardo response JSON");
        return {};
    }

    cJSON *sdGenJob = cJSON_GetObjectItem(root, "sdGenerationJob");
    if (!sdGenJob)
    {
        ESP_LOGE(TAG, "No sdGenerationJob in Leonardo response");
        cJSON_Delete(root);
        return {};
    }

    cJSON *generationIdField = cJSON_GetObjectItem(sdGenJob, "generationId");
    if (!cJSON_IsString(generationIdField))
    {
        ESP_LOGE(TAG, "No generationId field in Leonardo response");
        cJSON_Delete(root);
        return {};
    }

    std::string generationId = generationIdField->valuestring;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Leonardo generation ID: %s", generationId.c_str());

    // Poll for generation completion
    std::string imageUrl;
    const int maxAttempts = 30; // 30 * 2 seconds = 60 seconds max
    for (int attempt = 0; attempt < maxAttempts; ++attempt)
    {
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds between polls

        std::string pollUrl = _endpoint + "/generations/" + generationId;
        std::string pollResponse = leonardoHttpGet(pollUrl, status);
        if (status != 200)
        {
            ESP_LOGE(TAG, "Poll failed: HTTP %d", status);
            continue;
        }

        cJSON *pollRoot = cJSON_Parse(pollResponse.c_str());
        if (!pollRoot)
        {
            ESP_LOGE(TAG, "Failed to parse poll response JSON");
            continue;
        }

        cJSON *generationsByPk = cJSON_GetObjectItem(pollRoot, "generations_by_pk");
        if (!generationsByPk)
        {
            ESP_LOGE(TAG, "No generations_by_pk in poll response");
            cJSON_Delete(pollRoot);
            continue;
        }

        cJSON *statusField = cJSON_GetObjectItem(generationsByPk, "status");
        if (!cJSON_IsString(statusField))
        {
            ESP_LOGE(TAG, "No status field in poll response");
            cJSON_Delete(pollRoot);
            continue;
        }

        std::string genStatus = statusField->valuestring;
        ESP_LOGI(TAG, "Generation status: %s", genStatus.c_str());

        if (genStatus == "COMPLETE")
        {
            cJSON *generatedImages = cJSON_GetObjectItem(generationsByPk, "generated_images");
            if (cJSON_IsArray(generatedImages) && cJSON_GetArraySize(generatedImages) > 0)
            {
                cJSON *firstImage = cJSON_GetArrayItem(generatedImages, 0);
                cJSON *urlField = cJSON_GetObjectItem(firstImage, "url");
                if (cJSON_IsString(urlField))
                {
                    imageUrl = urlField->valuestring;
                    cJSON_Delete(pollRoot);
                    break;
                }
            }
            ESP_LOGE(TAG, "Completed but no image URL found");
        }
        else if (genStatus == "FAILED")
        {
            ESP_LOGE(TAG, "Generation failed");
            cJSON_Delete(pollRoot);
            return {};
        }
        // PENDING, PROCESSING, etc. continue polling
        cJSON_Delete(pollRoot);
    }

    if (imageUrl.empty())
    {
        ESP_LOGE(TAG, "Timeout waiting for generation completion");
        return {};
    }

    ESP_LOGI(TAG, "Image URL: %s", imageUrl.c_str());
    ESP_LOGI(TAG, "Leonardo image generation successful, returning URL");

    return imageUrl;
}

std::string LeonardoImageGenerator::leonardoHttpPost(const std::string &url, const std::string &body, int &status) const
{
    ESP_LOGI(TAG, "Leonardo POST: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = _timeout_ms;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!_apiKey.empty())
    {
        std::string authHeader = "Bearer " + _apiKey;
        esp_http_client_set_header(client, "Authorization", authHeader.c_str());
    }

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

std::string LeonardoImageGenerator::leonardoHttpGet(const std::string &url, int &status) const
{
    ESP_LOGI(TAG, "Leonardo GET: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = _timeout_ms;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!_apiKey.empty())
    {
        std::string authHeader = "Bearer " + _apiKey;
        esp_http_client_set_header(client, "Authorization", authHeader.c_str());
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
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GET Status: %d. Reading response", status);
    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, bytes_read);
    }

    ESP_LOGI(TAG, "GET Status: %d", status);
    esp_http_client_cleanup(client);
    return response;
}