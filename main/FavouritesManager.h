#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include "models.h"
#include "FavouriteService.h"

class FavouritesManager
{
public:
    // Public API
    void fetchFavourites();
    std::vector<Favorite> getFavourites() const;
    bool isFavouriteUrl(const std::string &url) const;
    void addFavourite(const Favorite &favourite);
    void removeFavourite(const std::string &url);

    // Background task for fetching favourites
    void startBackgroundFetch();

private:
    // Background task
    static void fetchFavouritesTask(void *param);

    // In-memory cache
    std::vector<Favorite> _favourites;
    mutable std::mutex _favouritesMutex;

    // Service instance
    FavouriteService _favouriteService;
};

extern FavouritesManager favouritesManager;