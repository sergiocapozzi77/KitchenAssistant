#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include <string>
#include "models.h"
#include "lvgl.h"
#include <map>

void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products);
void setProductSearchFilter(const std::string &filter);
void populateRecipeList(lv_obj_t *root, const std::vector<RecipeSuggestion> &recipes);
void populateFavouritesList(lv_obj_t *root, const std::vector<Favorite> &favourites);
void showCurrentPageFavourites(bool force = false);
void showRecipeDetailScreen(const RecipeSuggestion &recipe);
void showSnackbar(const char *message, int duration_ms);
extern uint32_t s_thumb_generation;
extern std::map<std::string, std::string> s_leonardo_url_cache;
void showSpinner();
void hideSpinner();
void close_product_edit_modal();

class SemaphoreGuard
{
public:
    explicit SemaphoreGuard(SemaphoreHandle_t sem) : sem_(sem)
    {
        acquired_ = (sem_ && xSemaphoreTake(sem_, portMAX_DELAY) == pdTRUE);
    }
    ~SemaphoreGuard()
    {
        if (acquired_)
            xSemaphoreGive(sem_);
    }
    bool acquired() const { return acquired_; }
    void release()
    {
        if (acquired_)
        {
            xSemaphoreGive(sem_);
            acquired_ = false;
        }
    }

private:
    SemaphoreHandle_t sem_;
    bool acquired_;
};
