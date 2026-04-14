#pragma once

#include <string>
#include <vector>
#include "esp_http_client.h"
#include "cJSON.h"
#include "secrets.h"
#include "models.h"
#include "AppwriteHttpClient.h"
#include "AppwriteClientInstance.h"

class ProductService
{
public:
    ProductService();

    // Public API methods
    std::vector<Product> getProducts(const std::vector<std::string> &queries, int &out);
    std::vector<Product> getProductsRetry(const std::vector<std::string> &queries, int &out);
    // bool manageUpdateProduct(Product &product);
    bool addProduct(Product &product);
    bool updateProduct(Product &product);
    bool deleteProduct(const std::string &rowId);
    bool upsertBarcode(const std::string &barcode, const std::string &name, const std::string &category);

    // Fetch products expiring today or tomorrow
    std::vector<Product> getExpiringProducts();

private:
    void saveProductTask(void *arg);
    // Configuration (from secrets.h)
    const std::string apiKey = APPWRITE_API_KEY;
    const std::string Endpoint = APPWRITE_ENDPOINT;
    const std::string ProjectId = APPWRITE_PROJECT_ID;
    const std::string DatabaseId = "695404ac0021bf7d9707";
    const std::string CollectionId = "products";
    const std::string BarcodeCollectionId = "barcodes";

    // HTTP client
    AppwriteHttpClient& _httpClient;
};

extern ProductService productService;