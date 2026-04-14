#include "RecipeAniaGotujeService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdlib>
#include <cJSON.h>

static const char *TAG = "RecipeAniaService";

// Global singleton
RecipeAniaGotujeService recipeAniaGotujeService;

/* =========================================================
 * CONSTRUCTION
 * ========================================================= */
RecipeAniaGotujeService::RecipeAniaGotujeService()
{
    std::random_device rd;
    _rng = std::mt19937(rd());
}

/* =========================================================
 * PUBLIC — getRecipeSuggestions
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeAniaGotujeService::getRecipeSuggestions(
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

    std::string rawQuery;

    // Add keywords first
    for (const auto &kw : keywords)
    {
        std::string trimmed = kw;
        size_t s = trimmed.find_first_not_of(' ');
        size_t e = trimmed.find_last_not_of(' ');
        if (s != std::string::npos)
            trimmed = trimmed.substr(s, e - s + 1);
        if (!trimmed.empty())
        {
            if (!rawQuery.empty())
                rawQuery += ' ';
            rawQuery += trimmed;
        }
    }

    // Add shuffled ingredients
    for (const auto &ing : shuffledIngredients)
    {
        if (!rawQuery.empty())
            rawQuery += ' ';
        rawQuery += ing;
    }

    std::transform(rawQuery.begin(), rawQuery.end(), rawQuery.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });

    std::string encodedQuery = urlEncode(rawQuery);

    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes. Starting Page %d",
             esp_get_free_heap_size(), page);

    std::vector<RecipeSuggestion> all;
    auto pageResults = fetchPage(encodedQuery, mealType, difficulty, diet, page);
    all.insert(all.end(), pageResults.begin(), pageResults.end());

    vTaskDelay(pdMS_TO_TICKS(500));

    if (all.size() > 10)
        all.resize(10);

    return all;
}

/* =========================================================
 * PRIVATE — fetchPage
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeAniaGotujeService::fetchPage(
    const std::string &query,
    const std::string &mealType,
    const std::string &difficulty,
    const std::string &diet,
    int page)
{
    std::vector<RecipeSuggestion> pageSuggestions;

    // Build URL (site uses 's' parameter)
    std::string url = "https://aniagotuje.pl/szukaj?s=" + query;
    if (page > 1)
        url += "&page=" + std::to_string(page);

    ESP_LOGI(TAG, "Fetching URL: %s", url.c_str());

    int status = -1;
    std::string html = httpGet(url, status);

    if (status != 200 || html.empty())
    {
        ESP_LOGE(TAG, "Failed to fetch HTML (status %d)", status);
        return pageSuggestions;
    }

    // Primary method: parse JSON from __NUXT__
    pageSuggestions = parseNuxtJson(html);

    // Fallback to HTML parsing if JSON fails
    if (pageSuggestions.empty())
    {
        ESP_LOGW(TAG, "JSON extraction failed, falling back to HTML parsing");
        pageSuggestions = parseHtmlCards(html);
    }

    return pageSuggestions;
}

/* =========================================================
 * PRIVATE — httpGet
 * ========================================================= */
std::string RecipeAniaGotujeService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "Free heap before HTTP: %" PRIu32, esp_get_free_heap_size());
    ESP_LOGI(TAG, "GET: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 2048;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        status = -1;
        return {};
    }

    esp_http_client_set_header(client, "User-Agent",
                               "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    esp_http_client_set_header(client, "Accept",
                               "text/html,application/xhtml+xml");
    esp_http_client_set_header(client, "Accept-Language", "pl-PL,pl;q=0.9");

    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        esp_http_client_cleanup(client);
        status = -1;
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string html;
    html.reserve(65536); // Enough for typical page with JSON

    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        html.append(buffer, bytes_read);

        // Early break if we've found the closing tags
        if (html.find("</main>") != std::string::npos ||
            html.find("</body>") != std::string::npos)
        {
            // Continue reading to get full JSON though
        }
        vTaskDelay(1);
    }

    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "HTML received: %d bytes", (int)html.size());

    return html;
}

/* =========================================================
 * PRIVATE — parseNuxtJson (cJSON)
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeAniaGotujeService::parseNuxtJson(const std::string &html) const
{
    std::vector<RecipeSuggestion> results;

    const std::string marker = "window.__NUXT__=";
    size_t start = html.find(marker);
    if (start == std::string::npos)
    {
        ESP_LOGE(TAG, "__NUXT__ marker not found");
        return results;
    }

    start += marker.length();
    if (html[start] == '(')
        start++; // Skip possible '('

    size_t end = html.find(");", start);
    if (end == std::string::npos)
    {
        ESP_LOGE(TAG, "__NUXT__ end not found");
        return results;
    }

    std::string jsonStr = html.substr(start, end - start);

    cJSON *root = cJSON_Parse(jsonStr.c_str());
    if (!root)
    {
        const char *err = cJSON_GetErrorPtr();
        if (err)
            ESP_LOGE(TAG, "JSON parse error: %s", err);
        return results;
    }

    cJSON *state = cJSON_GetObjectItem(root, "state");
    cJSON *postsObj = state ? cJSON_GetObjectItem(state, "posts") : nullptr;
    cJSON *postsArray = postsObj ? cJSON_GetObjectItem(postsObj, "posts") : nullptr;

    if (!cJSON_IsArray(postsArray))
    {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No 'posts' array in JSON");
        return results;
    }

    int count = cJSON_GetArraySize(postsArray);
    ESP_LOGI(TAG, "JSON contains %d posts", count);

    for (int i = 0; i < count; i++)
    {
        cJSON *post = cJSON_GetArrayItem(postsArray, i);
        if (!post)
            continue;

        cJSON *title = cJSON_GetObjectItem(post, "title");
        cJSON *slug = cJSON_GetObjectItem(post, "slug");
        cJSON *intro = cJSON_GetObjectItem(post, "intro");
        cJSON *cookTime = cJSON_GetObjectItem(post, "recipeCookTime");
        cJSON *score = cJSON_GetObjectItem(post, "score");

        cJSON *postThumb = cJSON_GetObjectItem(post, "postThumb");
        cJSON *imageUrl = postThumb ? cJSON_GetObjectItem(postThumb, "url") : nullptr;

        if (!cJSON_IsString(title) || !cJSON_IsString(slug))
            continue;

        RecipeSuggestion r;
        r.name = title->valuestring;
        r.url = "https://aniagotuje.pl/przepis/" + std::string(slug->valuestring);
        r.imageUrl = (cJSON_IsString(imageUrl)) ? imageUrl->valuestring : "";
        r.description = (cJSON_IsString(intro)) ? stripTags(intro->valuestring) : "";
        r.totalTime = (cJSON_IsString(cookTime)) ? cookTime->valuestring : "";
        r.ratingValue = (cJSON_IsNumber(score)) ? static_cast<float>(score->valuedouble) : 0.0f;
        r.difficulty = "";
        r.isPremium = false;
        r.contentType = "recipe";
        r.recipeSource = "aniagotuje";
        r.author = "Ania Gotuje";

        results.push_back(r);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Parsed %d recipes from JSON", (int)results.size());
    return results;
}

/* =========================================================
 * PRIVATE — parseHtmlCards (fallback)
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeAniaGotujeService::parseHtmlCards(const std::string &html) const
{
    std::vector<RecipeSuggestion> results;
    size_t pos = 0;

    const std::string articleStart = "<article class=\"res-col post-list-";
    const std::string articleEnd = "</article>";

    while ((pos = html.find(articleStart, pos)) != std::string::npos)
    {
        size_t endPos = html.find(articleEnd, pos);
        if (endPos == std::string::npos)
            break;
        endPos += articleEnd.length();

        std::string card = html.substr(pos, endPos - pos);
        pos = endPos;

        RecipeSuggestion r;

        // Extract link
        size_t linkStart = card.find("<a href=\"");
        if (linkStart != std::string::npos)
        {
            linkStart += 9;
            size_t linkEnd = card.find('"', linkStart);
            if (linkEnd != std::string::npos)
                r.url = "https://aniagotuje.pl" + card.substr(linkStart, linkEnd - linkStart);
        }

        // Title
        r.name = extractBetween(card, "<h2", "</h2>");
        if (!r.name.empty())
        {
            size_t bracket = r.name.find('>');
            if (bracket != std::string::npos)
                r.name = r.name.substr(bracket + 1);
        }

        // Image
        r.imageUrl = extractAttr(card, "src");

        // Rating
        std::string ratingText = extractBetween(card, "<i title=\"Oceny\"", "</a>");
        if (!ratingText.empty())
        {
            size_t numStart = ratingText.find_last_of('>');
            if (numStart != std::string::npos)
                r.ratingValue = parseFloat(ratingText.substr(numStart + 1));
        }

        r.description = "";
        r.difficulty = "";
        r.totalTime = "";
        r.isPremium = false;
        r.contentType = "recipe";
        r.recipeSource = "aniagotuje";
        r.author = "Ania Gotuje";

        results.push_back(r);
    }

    ESP_LOGI(TAG, "Fallback HTML parsed %d recipes", (int)results.size());
    return results;
}

/* =========================================================
 * UTILITIES
 * ========================================================= */
std::string RecipeAniaGotujeService::extractBetween(
    const std::string &html, const std::string &open,
    const std::string &close, size_t fromPos, size_t *endPos) const
{
    size_t s = html.find(open, fromPos);
    if (s == std::string::npos)
        return {};
    s += open.size();
    size_t e = html.find(close, s);
    if (e == std::string::npos)
        return {};
    if (endPos)
        *endPos = e + close.size();
    return html.substr(s, e - s);
}

std::string RecipeAniaGotujeService::extractAttr(const std::string &tag, const std::string &attr) const
{
    std::string needle = attr + "=\"";
    size_t pos = tag.find(needle);
    if (pos != std::string::npos)
    {
        size_t start = pos + needle.size();
        size_t end = tag.find('"', start);
        if (end != std::string::npos)
            return tag.substr(start, end - start);
    }
    needle = attr + "='";
    pos = tag.find(needle);
    if (pos != std::string::npos)
    {
        size_t start = pos + needle.size();
        size_t end = tag.find('\'', start);
        if (end != std::string::npos)
            return tag.substr(start, end - start);
    }
    return {};
}

std::string RecipeAniaGotujeService::stripTags(const std::string &html) const
{
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (char c : html)
    {
        if (c == '<')
            inTag = true;
        else if (c == '>')
            inTag = false;
        else if (!inTag)
            out += c;
    }
    size_t s = out.find_first_not_of(" \t\r\n");
    size_t e = out.find_last_not_of(" \t\r\n");
    if (s == std::string::npos)
        return {};
    return out.substr(s, e - s + 1);
}

float RecipeAniaGotujeService::parseFloat(const std::string &str) const
{
    if (str.empty())
        return 0.0f;
    char *end;
    float val = std::strtof(str.c_str(), &end);
    return (end == str.c_str()) ? 0.0f : val;
}

std::string RecipeAniaGotujeService::urlEncode(const std::string &s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else if (c == ' ')
            out << '+';
        else
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
    }
    return out.str();
}

int RecipeAniaGotujeService::parseMinutes(const std::string &timeInput)
{
    if (timeInput.empty())
        return 0;
    int hours = 0, minutes = 0;
    bool hasGodz = (timeInput.find("godz") != std::string::npos ||
                    timeInput.find("h") != std::string::npos);
    std::vector<int> nums;
    int cur = -1;
    for (char c : timeInput)
    {
        if (isdigit(c))
        {
            cur = (cur < 0 ? 0 : cur) * 10 + (c - '0');
        }
        else if (cur >= 0)
        {
            nums.push_back(cur);
            cur = -1;
        }
    }
    if (cur >= 0)
        nums.push_back(cur);
    if (nums.empty())
        return 0;
    if (hasGodz && nums.size() >= 2)
    {
        hours = nums[0];
        minutes = nums[1];
    }
    else if (hasGodz && nums.size() == 1)
        hours = nums[0];
    else
        minutes = nums[0];
    return hours * 60 + minutes;
}

std::string RecipeAniaGotujeService::mapDifficulty(const std::string &d) const
{
    if (d == "easy")
        return "łatwy";
    if (d == "medium")
        return "średni";
    if (d == "hard")
        return "trudny";
    return d;
}

std::string RecipeAniaGotujeService::mapDiet(const std::string &diet) const
{
    if (diet == "vegetarian")
        return "wegetariańska";
    if (diet == "vegan")
        return "wegańska";
    return diet;
}

std::string RecipeAniaGotujeService::mapMealType(const std::string &mealType) const
{
    // The site does not support meal type filters via URL
    return "";
}