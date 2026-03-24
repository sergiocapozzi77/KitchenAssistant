#include "RecipeGoodFoodService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>

// Include for heap diagnostics
#include "esp_heap_caps.h"

static const char *TAG = "RecipeGoodFoodService";

// Add this at the very bottom of the header
RecipeGoodFoodService recipeGoodFoodService;

RecipeGoodFoodService::RecipeGoodFoodService()
{
    std::random_device rd;
    _rng = std::mt19937(rd());
}

std::vector<RecipeSuggestion> RecipeGoodFoodService::getRecipeSuggestions(
    const std::vector<std::string> &ingredients,
    const std::string &mealType,
    const std::vector<std::string> &keywords,
    const std::string &difficulty,
    const std::string &totalTime,
    const std::string &diet,
    const std::string &cuisine,
    const std::string &ratings,
    const std::string &calories,
    int page)
{
    std::vector<std::string> shuffledIngredients = ingredients;
    std::shuffle(shuffledIngredients.begin(), shuffledIngredients.end(), _rng);

    std::string keywordsQuery;
    for (auto kw : keywords)
    {
        size_t start = kw.find_first_not_of(' ');
        size_t end = kw.find_last_not_of(' ');
        if (start != std::string::npos)
            kw = kw.substr(start, end - start + 1);
        if (!keywordsQuery.empty())
            keywordsQuery += '+';
        keywordsQuery += kw;
    }

    std::string rawQuery = keywordsQuery;
    for (const auto &ing : shuffledIngredients)
    {
        if (!rawQuery.empty())
            rawQuery += '+';
        rawQuery += ing;
    }

    std::string encodedQuery = urlEncode(rawQuery);

    std::vector<RecipeSuggestion> all;

    // Log available memory before each high-intensity TLS call
    ESP_LOGI(TAG, "Free Block: %d bytes. Starting Page %d",
             (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), page);

    // Scope the results to ensure they are cleaned up immediately
    {
        auto pageResults = fetchPage(encodedQuery, mealType, difficulty, totalTime, diet, cuisine, ratings, calories, page);
        all.insert(all.end(), pageResults.begin(), pageResults.end());
    }

    // Mandatory gap to let the network stack close sockets and free DMA memory
    vTaskDelay(pdMS_TO_TICKS(500));

    if (all.size() > 10)
        all.resize(10);

    return all;
}

std::vector<RecipeSuggestion> RecipeGoodFoodService::fetchPage(
    const std::string &query, const std::string &mealType,
    const std::string &difficulty, const std::string &totalTime,
    const std::string &diet, const std::string &cuisine,
    const std::string &ratings, const std::string &calories, int page)
{
    std::vector<RecipeSuggestion> pageSuggestions;
    std::string url = "https://www.bbcgoodfood.com/search?q=" + query;
    if (!difficulty.empty())
        url += "&mealType=" + urlEncode(mealType);
    if (!difficulty.empty())
        url += "&difficulty=" + urlEncode(difficulty);
    if (!totalTime.empty())
        url += "&totalTime=" + urlEncode(totalTime);
    if (!diet.empty())
        url += "&diet=" + urlEncode(diet);
    if (!cuisine.empty())
        url += "&cuisine=" + urlEncode(cuisine);
    if (!ratings.empty())
        url += "&ratings=" + urlEncode(ratings);
    if (!calories.empty())
        url += "&calories=" + urlEncode(calories);
    url += "&page=" + std::to_string(page);

    int status = -1;
    std::string jsonText = httpGet(url, status);

    if (status != 200 || jsonText.empty())
    {
        ESP_LOGE(TAG, "Failed to fetch JSON for page %d", page);
        return pageSuggestions;
    }

    cJSON *root = cJSON_Parse(jsonText.c_str());
    if (!root)
        return pageSuggestions;

    cJSON *props = cJSON_GetObjectItem(root, "props");
    cJSON *pageProps = props ? cJSON_GetObjectItem(props, "pageProps") : nullptr;
    cJSON *searchRes = pageProps ? cJSON_GetObjectItem(pageProps, "searchResults") : nullptr;
    cJSON *items = searchRes ? cJSON_GetObjectItem(searchRes, "items") : nullptr;

    if (items && cJSON_IsArray(items))
    {
        cJSON *item;
        cJSON_ArrayForEach(item, items)
        {
            RecipeSuggestion r;

            // Simple Strings & IDs
            cJSON *title = cJSON_GetObjectItem(item, "title");
            r.name = (title && cJSON_IsString(title)) ? title->valuestring : "";

            cJSON *id = cJSON_GetObjectItem(item, "id");
            r.id = (id && cJSON_IsString(id)) ? id->valuestring : "";

            cJSON *cType = cJSON_GetObjectItem(item, "contentType");
            r.contentType = (cType && cJSON_IsString(cType)) ? cType->valuestring : "";

            cJSON *author = cJSON_GetObjectItem(item, "authorName");
            r.author = (author && cJSON_IsString(author)) ? author->valuestring : "";

            cJSON *desc = cJSON_GetObjectItem(item, "description");
            r.description = (desc && cJSON_IsString(desc)) ? desc->valuestring : "";

            // Boolean
            cJSON *premium = cJSON_GetObjectItem(item, "isPremium");
            r.isPremium = cJSON_IsTrue(premium);

            // URL
            cJSON *relUrl = cJSON_GetObjectItem(item, "url");
            if (relUrl && cJSON_IsString(relUrl))
            {
                std::string u = relUrl->valuestring;
                r.url = (u.find("http") == 0) ? u : ("https://www.bbcgoodfood.com" + u);
            }

            // Image Object
            cJSON *imgObj = cJSON_GetObjectItem(item, "image");
            if (imgObj)
            {
                cJSON *imgUrl = cJSON_GetObjectItem(imgObj, "url");
                r.imageUrl = (imgUrl && cJSON_IsString(imgUrl)) ? imgUrl->valuestring : "";
            }

            // Rating Object
            cJSON *ratObj = cJSON_GetObjectItem(item, "rating");
            if (ratObj)
            {
                cJSON *rv = cJSON_GetObjectItem(ratObj, "ratingValue");
                cJSON *rc = cJSON_GetObjectItem(ratObj, "ratingCount");
                r.ratingValue = (rv && cJSON_IsNumber(rv)) ? rv->valuedouble : 0.0;
                r.ratingCount = (rc && cJSON_IsNumber(rc)) ? rc->valueint : 0;
            }

            // Terms Array (Time and Difficulty)
            cJSON *terms = cJSON_GetObjectItem(item, "terms");
            if (terms && cJSON_IsArray(terms))
            {
                cJSON *term;
                cJSON_ArrayForEach(term, terms)
                {
                    cJSON *slug = cJSON_GetObjectItem(term, "slug");
                    cJSON *disp = cJSON_GetObjectItem(term, "display");
                    if (slug && disp && cJSON_IsString(slug) && cJSON_IsString(disp))
                    {
                        std::string s = slug->valuestring;
                        if (s == "time")
                            r.totalTime = disp->valuestring;
                        else if (s == "skillLevel")
                            r.difficulty = disp->valuestring;
                    }
                }
            }

            r.recipeSource = "goodfood";
            pageSuggestions.push_back(r);
        }
    }

    cJSON_Delete(root);
    return pageSuggestions;
}

std::string RecipeGoodFoodService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "Free heap before ssl_setup: %" PRIu32, esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free heap:              %" PRIu32, esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "GET: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 2048; // Smaller buffer = successful DMA allocation
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.use_global_ca_store = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return {};

    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0...");
    esp_http_client_set_header(client, "Accept", "text/html");

    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string jsonTarget;
    jsonTarget.reserve(16384); // Pre-allocate 16KB to prevent fragmentation

    char buffer[1024];
    int bytes_read;
    bool capturing = false;

    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        std::string chunk(buffer, bytes_read);

        if (!capturing)
        {
            size_t markerPos = chunk.find("id=\"__NEXT_DATA__\"");
            if (markerPos != std::string::npos)
            {
                size_t start = chunk.find('>', markerPos);
                if (start != std::string::npos)
                {
                    capturing = true;
                    jsonTarget.append(chunk.substr(start + 1));
                }
            }
        }
        else
        {
            size_t endPos = chunk.find("</script>");
            if (endPos != std::string::npos)
            {
                jsonTarget.append(chunk.substr(0, endPos));
                break; // Exit early!
            }
            else
            {
                jsonTarget.append(chunk);
            }
        }

        // if (jsonTarget.length() > 40000)
        //     break; // Emergency OOM cap

        vTaskDelay(1);
    }

    //  ESP_LOGI(TAG, "JSON: %s", jsonTarget.c_str());

    esp_http_client_cleanup(client);
    return jsonTarget;
}

/* =========================================================
 * URL ENCODE
 * ========================================================= */
std::string RecipeGoodFoodService::urlEncode(const std::string &s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;

    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
    }
    return out.str();
}

/* =========================================================
 * PARSE MINUTES FROM TIME STRING (e.g. "30 mins", "1 hr 15 mins")
 * ========================================================= */
int RecipeGoodFoodService::parseMinutes(const std::string &timeInput)
{
    if (timeInput.empty())
        return 0;

    std::string digits;
    for (char c : timeInput)
    {
        if (isdigit(c))
            digits += c;
    }

    if (digits.empty())
        return 0;

    int result = 0;
    for (char c : digits)
    {
        result = result * 10 + (c - '0');
    }

    return result;
}
