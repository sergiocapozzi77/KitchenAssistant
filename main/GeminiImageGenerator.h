#pragma once

#include <string>
#include "AppwriteHttpClient.h"

class GeminiImageGenerator
{
public:
    /**
     * @brief Construct a new Leonardo AI image generator instance
     *
     * @param endpoint Leonardo AI endpoint (e.g., "https://cloud.leonardo.ai/api/rest/v1")
     * @param model Leonardo AI model ID (e.g., "b24e16ff-06e3-43eb-8d33-4416c2d75876")
     * @param apiKey Leonardo API key (Bearer token)
     * @param timeout_ms HTTP request timeout in milliseconds
     */
    GeminiImageGenerator(const std::string &endpoint, const std::string &model, const std::string &apiKey, int timeout_ms = 120000);

    /**
     * @brief Generate an image using Leonardo AI API
     *
     * @param prompt Text prompt for image generation
     * @param width Desired image width
     * @param height Desired image height
     * @param status Output parameter for HTTP status code
     * @param timeout_ms Request timeout (overrides constructor timeout)
     * @return std::string URL of the generated image, empty on error
     */
    std::string generateImage(const std::string &prompt, uint16_t width, uint16_t height, int &status, int timeout_ms = 0);

private:
    AppwriteHttpClient _httpClient;
    std::string _endpoint;
    std::string _model;
    std::string _apiKey;
    int _timeout_ms;

    // Helper to perform HTTP POST with Bearer token authorization
    std::string leonardoHttpPost(const std::string &url, const std::string &body, int &status) const;

    // Helper to perform HTTP GET with Bearer token authorization
    std::string leonardoHttpGet(const std::string &url, int &status) const;
};