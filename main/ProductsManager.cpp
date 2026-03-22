#include "ProductsManager.h"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "ProductsManager";

ProductsManager productsManager;

void ProductsManager::addProducts(const std::vector<Product> &products)
{
    std::lock_guard<std::mutex> lock(_productMutex);

    _allProducts = products;

    ESP_LOGI(TAG, "Stored %d products in Manager memory", (int)_allProducts.size());
}

std::vector<Product> ProductsManager::getAllProducts()
{
    std::lock_guard<std::mutex> lock(_productMutex);

    // Returning a copy ensures the UI task has a stable "snapshot"
    // even if the WiFi task starts a new sync mid-scroll.
    return _allProducts;
}

void ProductsManager::addSelectedProduct(const Product &product)
{
    std::lock_guard<std::mutex> lock(_productMutex);
    _selectedProducts.push_back(product);
    ESP_LOGI(TAG, "Added product to selected: %s", product.name.c_str());
}

void ProductsManager::removeSelectedProduct(const std::string &id)
{
    std::lock_guard<std::mutex> lock(_productMutex);
    auto it = std::remove_if(_selectedProducts.begin(), _selectedProducts.end(),
                             [&id](const Product &p)
                             { return p.rowId == id; });
    if (it != _selectedProducts.end())
    {
        ESP_LOGI(TAG, "Removed product from selected: %s", it->name.c_str());
        _selectedProducts.erase(it, _selectedProducts.end());
    }
}

int ProductsManager::getSelectedCount() const
{
    std::lock_guard<std::mutex> lock(_productMutex);
    return _selectedProducts.size();
}