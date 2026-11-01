#pragma once

#include <mutex>
#include <string>

class SupabaseClient
{
public:
    SupabaseClient(const std::string &baseUrl, const std::string &serviceKey,
                   const std::string &userId, int timeoutMs = 30000);

    // GET /rest/v1/{table}?{filters} — user_id filter is always prepended.
    std::string select(const std::string &table, const std::string &filters,
                       int &status) const;

    // POST /rest/v1/{table} — user_id is injected into the JSON body.
    std::string insert(const std::string &table, const std::string &jsonBody,
                       int &status) const;

    // PATCH /rest/v1/{table}?{filters} — user_id filter is always appended.
    std::string update(const std::string &table, const std::string &filters,
                       const std::string &jsonBody, int &status) const;

    // DELETE /rest/v1/{table}?{filters} — returns the HTTP status.
    int remove(const std::string &table, const std::string &filters) const;

    static std::string urlEncode(const std::string &s);

private:
    std::string request(const char *method, const std::string &url,
                        const std::string &body, int &status) const;

    std::string _baseUrl;
    std::string _serviceKey;
    std::string _userId;
    int _timeoutMs;
    mutable void *_handle = nullptr; // esp_http_client_handle_t, hidden from includers
    mutable std::mutex _mutex;
};

SupabaseClient &getSupabaseClient();
