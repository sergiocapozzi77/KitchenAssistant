#pragma once

#include <string>
#include <vector>
#include "esp_http_client.h"
#include "cJSON.h"
#include "secrets.h"
#include "models.h"

class CookbookService
{
public:
    CookbookService();

    const std::string apiKey = APPWRITE_API_KEY;
    const std::string Endpoint = "https://fra.cloud.appwrite.io/v1";
    const std::string ProjectId = APPWRITE_PROJECT_ID;
    const std::string DatabaseId = "695404ac0021bf7d9707";
    const std::string CookbooksCollectionId = COOKBOOKS_COLLECTION_ID;

    std::vector<Cookbook> getCookbooks();
    std::string createCookbook(const std::string &name); // returns new $id
    bool deleteCookbook(const std::string &id);

private:
    esp_http_client_handle_t createHttpClient(const std::string &url);
    std::string httpGet(const std::string &url, int &status);
    std::string httpPost(const std::string &url, const std::string &body, int &status);
    int httpDelete(const std::string &url);
    std::string urlEncode(const std::string &s);
    std::string generateId(int length = 20);
    Cookbook parseCookbookFromJson(cJSON *item);
};

extern CookbookService cookbookService;
