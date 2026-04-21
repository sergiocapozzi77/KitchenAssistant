#pragma once

#include <string>
#include "AppwriteHttpClient.h"

class GeminiImageGenerator
{
public:
    /**
     * @brief Construct a new GeminiImageGenerator instance
     *
     * @param endpoint Gemini endpoint (e.g., "https://generativelanguage.googleapis.com/v1beta/models")
     * @param model Gemini image generation model (e.g., "imagen-3.0-generate-001")
     * @param apiKey Gemini API key
     * @param timeout_ms HTTP request timeout in milliseconds
     */
    GeminiImageGenerator(const std::string &endpoint, const std::string &model, const std::string &apiKey, int timeout_ms = 120000);

    /**
     * @brief Generate an image using Gemini API
     *
     * @param prompt Text prompt for image generation
     * @param width Desired image width
     * @param height Desired image height
     * @param status Output parameter for HTTP status code
     * @param timeout_ms Request timeout (overrides constructor timeout)
     * @return std::string Base64-encoded JPEG image, empty on error
     */
    std::string generateImage(const std::string &prompt, uint16_t width, uint16_t height, int &status, int timeout_ms = 0);

private:
    AppwriteHttpClient _httpClient;
    std::string _endpoint;
    std::string _model;
    std::string _apiKey;
    int _timeout_ms;

    // Helper to build the full URL with API key as query parameter
    std::string buildUrl() const;
};