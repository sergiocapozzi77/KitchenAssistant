#pragma once

#include <string>
#include <vector>

#include "models.h"

class SupabaseClient;

class ProductService
{
public:
    ProductService();

    bool getProducts(std::vector<Product> &out);
    bool updateProduct(const Product &product);
    bool deleteProduct(const std::string &id);
    bool upsertBarcode(const std::string &barcode, const std::string &name,
                       const std::string &category);

private:
    SupabaseClient &_client;
};

extern ProductService productService;
