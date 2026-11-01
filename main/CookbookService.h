#pragma once

#include <vector>

#include "models.h"

class SupabaseClient;

class CookbookService
{
public:
    CookbookService();

    bool fetchCookbooks(std::vector<Cookbook> &out);

private:
    SupabaseClient &_client;
};

extern CookbookService cookbookService;
