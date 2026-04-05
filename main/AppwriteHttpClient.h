#pragma once

#include <string>
#include "esp_http_client.h"

/**
 * @brief Shared HTTP client for Appwrite services
 *
 * Encapsulates common HTTP operations (GET, POST, PATCH, DELETE) with Appwrite
 * authentication headers. Each service can instantiate with its own configuration.
 */
class AppwriteHttpClient
{
public:
    /**
     * @brief Construct a new AppwriteHttpClient instance
     *
     * @param endpoint Appwrite endpoint (e.g., "https://fra.cloud.appwrite.io/v1")
     * @param projectId Appwrite project ID
     * @param apiKey Appwrite API key
     * @param timeout_ms HTTP request timeout in milliseconds
     */
    AppwriteHttpClient(const std::string &endpoint, const std::string &projectId, const std::string &apiKey, const int timeout_ms = 30000);

    /**
     * @brief Create an HTTP client configured for Appwrite
     *
     * @param url Full URL to request
     * @return esp_http_client_handle_t Initialized HTTP client handle
     */
    esp_http_client_handle_t createHttpClient(const std::string &url) const;

    /**
     * @brief Perform HTTP GET request
     *
     * @param url Full URL to request
     * @param status Output parameter for HTTP status code
     * @return std::string Response body, empty on error
     */
    std::string httpGet(const std::string &url, int &status) const;

    /**
     * @brief Perform HTTP POST request
     *
     * @param url Full URL to request
     * @param body Request body
     * @param status Output parameter for HTTP status code
     * @return std::string Response body, empty on error
     */
    std::string httpPost(const std::string &url, const std::string &body, int &status) const;

    /**
     * @brief Perform HTTP PATCH request
     *
     * @param url Full URL to request
     * @param body Request body
     * @param status Output parameter for HTTP status code
     * @return std::string Response body, empty on error
     */
    std::string httpPatch(const std::string &url, const std::string &body, int &status) const;

    /**
     * @brief Perform HTTP DELETE request
     *
     * @param url Full URL to request
     * @return int HTTP status code, -1 on error
     */
    int httpDelete(const std::string &url) const;

    /**
     * @brief URL-encode a string (RFC 3986)
     *
     * @param s Input string
     * @return std::string URL-encoded string
     */
    static std::string urlEncode(const std::string &s);

    /**
     * @brief Generate a random ID string
     *
     * @param length Desired length of ID
     * @return std::string Random ID
     */
    static std::string generateId(int length = 12);

private:
    std::string _endpoint;
    std::string _projectId;
    std::string _apiKey;
    int _timeout_ms;

    // Common configuration for HTTP client
    static void configureHttpClient(esp_http_client_handle_t client, const std::string &projectId, const std::string &apiKey);
};