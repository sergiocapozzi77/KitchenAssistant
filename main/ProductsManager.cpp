#include "ProductsManager.h"

#include "ProductService.h"
#include "esp_log.h"
#include "lvgl.h"
#include "screens.h"
#include "ui_extensions.h"

#include <algorithm>

static const char *TAG = "PRODUCT_MGR";

void ProductsManager::addProducts(const std::vector<Product> &products)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _products = products;
}

std::vector<Product> ProductsManager::getAllProducts() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _products;
}

std::optional<Product> ProductsManager::getProductById(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &product : _products)
    {
        if (product.id == id)
            return product;
    }
    return std::nullopt;
}

void ProductsManager::updateProduct(const Product &updated)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto &product : _products)
    {
        if (product.id == updated.id)
        {
            product = updated;
            return;
        }
    }
}

void ProductsManager::deleteProduct(const std::string &id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _products.erase(
        std::remove_if(_products.begin(), _products.end(),
                       [&id](const Product &p)
                       { return p.id == id; }),
        _products.end());
}

size_t ProductsManager::count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _products.size();
}

void ProductsManager::fetchProductsSync()
{
    std::vector<Product> fetched;
    if (productService.getProducts(fetched))
    {
        addProducts(fetched);
        ESP_LOGI(TAG, "Product cache updated (%u rows)", (unsigned)fetched.size());
    }
    else
    {
        ESP_LOGE(TAG, "Product fetch failed; keeping %u cached rows", (unsigned)count());
    }

    populateProductList();
}

void ProductsManager::populateProductList()
{
    std::vector<Product> snapshot = getAllProducts();

    lv_lock();
    if (objects.products_list && lv_obj_is_valid(objects.products_list))
        populateProductListUi(objects.products_list, snapshot);
    lv_unlock();
}

ProductsManager productsManager;
