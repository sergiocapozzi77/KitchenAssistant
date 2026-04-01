#pragma once

#include <string>
#include <vector>
#include "esp_http_client.h"
#include "cJSON.h"
#include "secrets.h"
#include "models.h"

/* =========================================================
 * FAVOURITE SERVICE CLASS
 * ========================================================= */
class FavouriteService
{
public:
    FavouriteService();

    // Configuration
    // Configuration (from secrets.h)
    const std::string apiKey = APPWRITE_API_KEY;
    const std::string Endpoint = "https://fra.cloud.appwrite.io/v1";
    const std::string ProjectId = "6954045e003c75c1c3bf";
    const std::string DatabaseId = "695404ac0021bf7d9707";
    const std::string FavouritesCollectionId = FAVOURITES_COLLECTION_ID;

    // Public API
    std::vector<Favorite> getFavourites();                                                       // Simple version (backwards compatible)
    std::vector<Favorite> getFavourites(const std::vector<std::string> &queries, int &out);      // With query support and pagination
    std::vector<Favorite> getFavouritesRetry(const std::vector<std::string> &queries, int &out); // With retry logic

    bool addFavourite(const RecipeSuggestion &recipe);
    bool removeFavourite(const std::string &url);
    bool isFavourite(const std::string &url);

private:
    // HTTP helpers
    esp_http_client_handle_t createHttpClient(const std::string &url);
    std::string httpGet(const std::string &url, int &status);
    std::string httpPost(const std::string &url, const std::string &body, int &status);
    std::string httpPatch(const std::string &url, const std::string &body, int &status);
    int httpDelete(const std::string &url);

    // Utility
    std::string urlEncode(const std::string &s);
    std::string generateId(int length = 20);

    // JSON helpers
    Favorite parseFavouriteFromJson(cJSON *item);
    std::string buildFavouriteJson(const RecipeSuggestion &recipe);
};

extern FavouriteService favouriteService;