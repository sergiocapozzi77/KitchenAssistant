#include "RecipeGoodFoodService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>

RecipeGoodFoodService recipeGoodFoodService;

static const char *TAG = "RecipeGoodFoodService";

RecipeGoodFoodService::RecipeGoodFoodService()
{
    std::random_device rd;
    _rng = std::mt19937(rd());
}

/* =========================================================
 * PUBLIC API
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeGoodFoodService::getRecipeSuggestions(
    const std::vector<std::string> &ingredients,
    const std::string &dishType,
    const std::vector<std::string> &keywords,
    const std::string &difficulty,
    const std::string &totalTime)
{
    // 1. Shuffle ingredients to vary the base query each call
    std::vector<std::string> shuffledIngredients = ingredients;
    std::shuffle(shuffledIngredients.begin(), shuffledIngredients.end(), _rng);

    // Build keywords query: keyword1+keyword2+...
    std::string keywordsQuery;
    for (size_t i = 0; i < keywords.size(); i++)
    {
        std::string kw = keywords[i];
        // Trim leading/trailing spaces
        size_t start = kw.find_first_not_of(' ');
        size_t end   = kw.find_last_not_of(' ');
        if (start != std::string::npos)
            kw = kw.substr(start, end - start + 1);
        if (!keywordsQuery.empty()) keywordsQuery += '+';
        keywordsQuery += kw;
    }

    // Build full raw query: keywords+ingredient1+ingredient2+...
    std::string rawQuery = keywordsQuery;
    for (const auto &ing : shuffledIngredients)
    {
        if (!rawQuery.empty()) rawQuery += '+';
        rawQuery += ing;
    }
    // Remove trailing '+'
    while (!rawQuery.empty() && rawQuery.back() == '+')
        rawQuery.pop_back();

    std::string encodedQuery = urlEncode(rawQuery);

    // Encode dish type: lowercase, spaces to dashes
    std::string dishTypeLower = dishType;
    std::transform(dishTypeLower.begin(), dishTypeLower.end(), dishTypeLower.begin(), ::tolower);
    std::replace(dishTypeLower.begin(), dishTypeLower.end(), ' ', '-');
    std::string encodedDishType = urlEncode(dishTypeLower);

    // 2. Fetch page 1 and page 2 sequentially
    std::vector<RecipeSuggestion> all;

    for (int page = 1; page <= 2; page++)
    {
        auto pageResults = fetchPage(encodedQuery, encodedDishType, difficulty, totalTime, page);
        all.insert(all.end(), pageResults.begin(), pageResults.end());
    }

    // 3. Shuffle combined results and return up to 5
    std::shuffle(all.begin(), all.end(), _rng);
    if (all.size() > 5)
        all.resize(5);

    return all;
}

/* =========================================================
 * FETCH SINGLE PAGE
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeGoodFoodService::fetchPage(
    const std::string &query,
    const std::string &dishType,
    const std::string &difficulty,
    const std::string &totalTime,
    int page)
{
    std::vector<RecipeSuggestion> pageSuggestions;

    std::string url = "https://www.bbcgoodfood.com/search?q=" + query +
                      "&mealType=" + dishType +
                      "&page=" + std::to_string(page);

    if (!difficulty.empty())
        url += "&difficulty=" + urlEncode(difficulty);

    if (!totalTime.empty())
        url += "&totalTime=" + urlEncode(totalTime);

    int status = -1;
    std::string html = httpGet(url, status);

    if (status != 200 || html.empty())
    {
        ESP_LOGE(TAG, "Failed to fetch page %d, status=%d", page, status);
        return pageSuggestions;
    }

    // Extract __NEXT_DATA__ JSON from the HTML
    std::string jsonText = extractNextData(html);
    if (jsonText.empty())
    {
        ESP_LOGW(TAG, "No __NEXT_DATA__ found on page %d", page);
        return pageSuggestions;
    }

    cJSON *root = cJSON_Parse(jsonText.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "JSON parse failed on page %d", page);
        return pageSuggestions;
    }

    // Navigate: props -> pageProps -> searchResults -> items
    cJSON *props       = cJSON_GetObjectItem(root, "props");
    cJSON *pageProps   = props       ? cJSON_GetObjectItem(props, "pageProps")         : nullptr;
    cJSON *searchRes   = pageProps   ? cJSON_GetObjectItem(pageProps, "searchResults") : nullptr;
    cJSON *items       = searchRes   ? cJSON_GetObjectItem(searchRes, "items")         : nullptr;

    if (!items || !cJSON_IsArray(items))
    {
        ESP_LOGW(TAG, "No items array found on page %d", page);
        cJSON_Delete(root);
        return pageSuggestions;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, items)
    {
        RecipeSuggestion r;

        cJSON *title = cJSON_GetObjectItem(item, "title");
        r.name = (title && cJSON_IsString(title)) ? title->valuestring : "No Title";

        cJSON *relUrl = cJSON_GetObjectItem(item, "url");
        if (relUrl && cJSON_IsString(relUrl))
        {
            std::string u = relUrl->valuestring;
            r.url = (u.find("http") == 0) ? u : ("https://www.bbcgoodfood.com" + u);
        }

        cJSON *image = cJSON_GetObjectItem(item, "image");
        if (image)
        {
            cJSON *imgUrl = cJSON_GetObjectItem(image, "url");
            if (imgUrl && cJSON_IsString(imgUrl))
                r.imageUrl = imgUrl->valuestring;
        }

        // Parse terms array for prepTime ("time") and difficulty ("skillLevel")
        cJSON *terms = cJSON_GetObjectItem(item, "terms");
        if (terms && cJSON_IsArray(terms))
        {
            cJSON *term;
            cJSON_ArrayForEach(term, terms)
            {
                cJSON *slug    = cJSON_GetObjectItem(term, "slug");
                cJSON *display = cJSON_GetObjectItem(term, "display");
                if (!slug || !cJSON_IsString(slug) || !display || !cJSON_IsString(display))
                    continue;

                if (strcmp(slug->valuestring, "time") == 0)
                    r.prepTime = parseMinutes(display->valuestring);
                else if (strcmp(slug->valuestring, "skillLevel") == 0)
                    r.difficulty = display->valuestring;
            }
        }

        if (r.difficulty.empty())
            r.difficulty = "Easy";

        r.recipeSource = "goodfood";
        pageSuggestions.push_back(r);
    }

    ESP_LOGI(TAG, "Page %d: found %d recipes", page, pageSuggestions.size());
    cJSON_Delete(root);
    return pageSuggestions;
}

/* =========================================================
 * EXTRACT __NEXT_DATA__ FROM HTML
 * ========================================================= */
std::string RecipeGoodFoodService::extractNextData(const std::string &html)
{
    const char *marker = "id=\"__NEXT_DATA__\"";
    size_t markerPos = html.find(marker);
    if (markerPos == std::string::npos)
        return {};

    size_t start = html.find('>', markerPos);
    if (start == std::string::npos)
        return {};
    start++; // skip '>'

    size_t end = html.find("</script>", start);
    if (end == std::string::npos)
        return {};

    return html.substr(start, end - start);
}

/* =========================================================
 * HTTP GET
 * ========================================================= */
std::string RecipeGoodFoodService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "GET: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url                      = url.c_str();
    cfg.timeout_ms               = 30000;
    cfg.buffer_size              = 8192;
    cfg.buffer_size_tx           = 1024;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach        = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    // Mimic a browser to avoid bot blocks
    esp_http_client_set_header(client, "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    esp_http_client_set_header(client, "Accept", "text/html");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);

    std::string body;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
        body.append(buffer, bytes_read);

    if (bytes_read < 0)
    {
        ESP_LOGE(TAG, "HTTP read error");
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Status: %d, Body: %d bytes", status, body.length());

    esp_http_client_cleanup(client);
    return body;
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
    if (timeInput.empty()) return 0;

    std::string digits;
    for (char c : timeInput)
    {
        if (isdigit(c)) digits += c;
    }

    if (digits.empty()) return 0;

    int result = 0;
    for (char c : digits)
    {
        result = result * 10 + (c - '0');
    }
    
    return result;
}
