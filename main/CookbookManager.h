#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "models.h"
#include "CookbookService.h"

class CookbookManager
{
public:
    void fetchCookbooks();
    std::vector<Cookbook> getCookbooks() const;
    void addCookbook(const Cookbook &cookbook);
    void removeCookbook(const std::string &id);

private:
    std::vector<Cookbook> _cookbooks;
    mutable std::mutex _cookbooksMutex;
    CookbookService _cookbookService;
};

extern CookbookManager cookbookManager;
