#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "models.h"

class ProductsManager
{
public:
    void updateProduct(const Product &updated);

    void deleteProduct(const std::string &rowId);
    std::optional<Product> getProductById(const std::string &rowId);
    // void addProduct(const Product &product);
    void addProducts(const std::vector<Product> &products);
    // void removeProduct(const std::string &id);
    std::vector<Product> getProducts() const;
    //  Product getProduct(const std::string &id) const;
    std::vector<Product> getAllProducts();

    void addSelectedProduct(const Product &product);
    void removeSelectedProduct(const std::string &id);
    int getSelectedCount() const;
    std::vector<Product> getSelectedProducts() const;

    void fetchProductsAsync();
    void fetchProductsSync();

    void populateProductList();

private:
    static void fetchProductsTask(void *param);

    StackType_t *_productTaskStack = nullptr;
    StaticTask_t _productTaskBuf;

    std::vector<Product> _allProducts;
    std::vector<Product> _selectedProducts;
    // ProductsManager.h
    mutable std::mutex _productMutex;
};

extern ProductsManager productsManager;