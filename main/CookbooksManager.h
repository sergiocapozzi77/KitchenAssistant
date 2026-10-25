#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "models.h"

class CookbooksManager
{
public:
    bool fetchSync();
    bool refreshSync();

    bool loaded() const;
    std::vector<Cookbook> snapshot() const;
    bool getCookbook(const std::string &id, Cookbook &out) const;
    int recipeCountFor(const std::string &id) const;

private:
    mutable std::mutex _mutex;
    std::vector<Cookbook> _cookbooks;
    bool _loaded = false;
};

extern CookbooksManager cookbooksManager;
