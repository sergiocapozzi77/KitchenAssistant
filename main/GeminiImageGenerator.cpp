#include "GeminiImageGenerator.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string>

static const char *TAG = "GeminiImageGenerator";

GeminiImageGenerator::GeminiImageGenerator(const std::string &endpoint, const std::string &model, const std::string &apiKey, int timeout_ms)
    : _httpClient(endpoint, "", "", timeout_ms), // Empty projectId and apiKey, headers will be skipped
      _endpoint(endpoint),
      _model(model),
      _apiKey(apiKey),
      _timeout_ms(timeout_ms)
{
}

std::string GeminiImageGenerator::buildUrl() const
{
    return _endpoint + "/" + _model + ":generateImages?key=" + _apiKey;
}

std::string GeminiImageGenerator::generateImage(const std::string &prompt, uint16_t width, uint16_t height, int &status, int timeout_ms)
{
    ESP_LOGI(TAG, "Generating image with Gemini API, prompt: %s", prompt.c_str());

    // Build the full URL with API key as query parameter
    std::string url = buildUrl();

    // Build JSON payload for image generation
    cJSON *payload = cJSON_CreateObject();
    if (!payload)
    {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        status = -1;
        return {};
    }

    cJSON_AddStringToObject(payload, "prompt", prompt.c_str());
    cJSON_AddNumberToObject(payload, "numberOfImages", 1);
    // Add width and height parameters
    cJSON_AddNumberToObject(payload, "width", width);
    cJSON_AddNumberToObject(payload, "height", height);
    // Calculate aspect ratio string (simplified)
    if (width == height)
    {
        cJSON_AddStringToObject(payload, "aspectRatio", "1:1");
    }
    else if (width > height)
    {
        cJSON_AddStringToObject(payload, "aspectRatio", "16:9"); // landscape
    }
    else
    {
        cJSON_AddStringToObject(payload, "aspectRatio", "9:16"); // portrait
    }
    cJSON_AddStringToObject(payload, "outputFormat", "JPEG");
    cJSON_AddNumberToObject(payload, "outputQuality", 85);

    char *payloadStr = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payloadStr)
    {
        ESP_LOGE(TAG, "Failed to stringify Gemini payload");
        status = -1;
        return {};
    }

    ESP_LOGI(TAG, "Gemini payload: %s", payloadStr);

    // Use AppwriteHttpClient to perform POST request
    // Note: timeout override not yet implemented
    std::string response = _httpClient.httpPost(url, payloadStr, status);
    free(payloadStr);

    if (status != 200)
    {
        ESP_LOGE(TAG, "Gemini API error: HTTP %d, response: %s", status, response.c_str());
        return {};
    }

    ESP_LOGI(TAG, "Gemini response length: %d", response.length());

    // Parse response to extract base64 image
    cJSON *root = cJSON_Parse(response.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse Gemini response JSON");
        return {};
    }

    cJSON *images = cJSON_GetObjectItem(root, "images");
    if (!cJSON_IsArray(images) || cJSON_GetArraySize(images) == 0)
    {
        ESP_LOGE(TAG, "No images array in Gemini response");
        cJSON_Delete(root);
        return {};
    }

    cJSON *firstImage = cJSON_GetArrayItem(images, 0);
    cJSON *bytesField = cJSON_GetObjectItem(firstImage, "bytes");
    if (!cJSON_IsString(bytesField))
    {
        ESP_LOGE(TAG, "No bytes field in Gemini image");
        cJSON_Delete(root);
        return {};
    }

    std::string base64Image = bytesField->valuestring;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Gemini image generation successful, base64 length: %d", base64Image.length());
    return base64Image;
}