
#include "ProductsManager.h"
#include "freertos/FreeRTOS.h" // MUST be first FreeRTOS header
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <algorithm>
#include "ui_extensions.h"
#include "WiFiManager.h"
#include "ProductService.h"
#include "ui.h"

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

void ProductsManager::deleteProduct(const std::string &rowId)
{
    {
        std::lock_guard<std::mutex> lock(_productMutex);

        auto eraseFrom = [&rowId](std::vector<Product> &vec)
        {
            auto it = std::remove_if(vec.begin(), vec.end(),
                                     [&rowId](const Product &p)
                                     { return p.rowId == rowId; });
            if (it != vec.end())
            {
                vec.erase(it, vec.end());
                return true;
            }
            return false;
        };

        bool removed = eraseFrom(_allProducts);
        eraseFrom(_selectedProducts);

        if (!removed)
        {
            ESP_LOGW(TAG, "deleteProduct: rowId %s not found locally", rowId.c_str());
            return;
        }

        ESP_LOGI(TAG, "deleteProduct: removed %s locally", rowId.c_str());
    }
}

std::optional<Product> ProductsManager::getProductById(const std::string &rowId)
{
    std::lock_guard<std::mutex> lock(_productMutex);
    const auto &products = getAllProducts(); // call on self, not productsManager
    auto it = std::find_if(products.begin(), products.end(),
                           [&rowId](const Product &p)
                           { return p.rowId == rowId; });
    if (it == products.end())
    {
        ESP_LOGE("ProductsManager", "Product with rowId %s not found", rowId.c_str());
        return std::nullopt;
    }

    return *it;
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

std::vector<Product> ProductsManager::getSelectedProducts() const
{
    std::lock_guard<std::mutex> lock(_productMutex);

    // Returning a copy ensures the UI task has a stable "snapshot"
    // even if the WiFi task starts a new sync mid-scroll.
    return _selectedProducts;
}

void ProductsManager::fetchProductsAsync()
{
    if (!_productTaskStack)
    {
        _productTaskStack = (StackType_t *)heap_caps_malloc(16384 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
        if (!_productTaskStack)
        {
            ESP_LOGE(TAG, "Failed to allocate PSRAM stack for FetchProducts task");
            return;
        }
        ESP_LOGI(TAG, "Allocated FetchProducts task stack in PSRAM");
    }

    xTaskCreateStatic(ProductsManager::fetchProductsTask, "FetchProducts", 16384, this, 2, _productTaskStack, &_productTaskBuf);
}

void ProductsManager::fetchProductsSync()
{
    ESP_LOGI(TAG, "Fetching products...");

    int out;
    auto products = productService.getProductsRetry({}, out);
    if (products.empty())
    {
        ESP_LOGI(TAG, "No products found");
        return;
    }

    ESP_LOGI(TAG, "Fetched %d products", products.size());
    productsManager.addProducts(products);

    if (objects.products_list && lv_obj_is_valid(objects.products_list))
    {
        populateProductListUi(objects.products_list, productsManager.getAllProducts());
    }
    else
    {
        ESP_LOGE(TAG, "products_list is invalid, skipping UI update");
    }
}

// Task to fetch products expiring today or tomorrow
void ProductsManager::fetchProductsTask(void *param)
{
    showSpinner();
    while (!wifiManager.isConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (!wifiManager.isSntpSynced())
        vTaskDelay(pdMS_TO_TICKS(500));

    lv_lock();
    if (objects.current_wifi_lbl && lv_obj_is_valid(objects.current_wifi_lbl))
    {
        lv_label_set_text(objects.current_wifi_lbl, wifiManager.getSSID().c_str());
    }
    lv_unlock();

    ESP_LOGI(TAG, "WiFi connected. Fetching products...");

    productsManager.fetchProductsSync();
    hideSpinner();
    vTaskDelete(NULL);
}

void ProductsManager::populateProductList()
{
    if (!objects.products_list || !lv_obj_is_valid(objects.products_list))
    {
        ESP_LOGE(TAG, "products_list is invalid");
        return;
    }
    populateProductListUi(objects.products_list, getAllProducts());
}

void ProductsManager::updateProduct(const Product &updated)
{
    std::lock_guard<std::mutex> lock(_productMutex);
    auto it = std::find_if(_allProducts.begin(), _allProducts.end(),
                           [&updated](const Product &p)
                           { return p.rowId == updated.rowId; });
    if (it != _allProducts.end())
        *it = updated;
}