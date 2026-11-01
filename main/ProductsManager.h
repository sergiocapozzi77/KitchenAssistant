#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "models.h"

class ProductsManager
{
public:
    void addProducts(const std::vector<Product> &products);
    std::vector<Product> getAllProducts() const;
    std::optional<Product> getProductById(const std::string &id) const;
    void updateProduct(const Product &updated);
    void deleteProduct(const std::string &id);

    void fetchProductsSync();
    void populateProductList();
    size_t count() const;

private:
    mutable std::mutex _mutex;
    std::vector<Product> _products;
};

extern ProductsManager productsManager;
