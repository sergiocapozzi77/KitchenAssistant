#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "models.h"

class ProductsManager
{
public:
    // void addProduct(const Product &product);
    void addProducts(const std::vector<Product> &products);
    // void removeProduct(const std::string &id);
    std::vector<Product> getProducts() const;
    //  Product getProduct(const std::string &id) const;
    void updateProduct(const Product &product);
    std::vector<Product> getAllProducts();

    void addSelectedProduct(const Product &product);
    void removeSelectedProduct(const std::string &id);
    int getSelectedCount() const;
    std::vector<Product> getSelectedProducts() const;

    void fetchProducts();

    void pupulateProductList();

private:
    static void fetchProductsTask(void *param);

    std::vector<Product> _allProducts;
    std::vector<Product> _selectedProducts;
    // ProductsManager.h
    mutable std::mutex _productMutex;
};

extern ProductsManager productsManager;