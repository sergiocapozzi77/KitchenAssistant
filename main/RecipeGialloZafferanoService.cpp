#include "RecipeGialloZafferanoService.h"
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

static const char *TAG = "RecipeGZService";

// Global singleton — mirrors recipeGoodFoodService pattern
RecipeGialloZafferanoService recipeGialloZafferanoService;

/* =========================================================
 * CONSTRUCTION
 * ========================================================= */
RecipeGialloZafferanoService::RecipeGialloZafferanoService()
{
    std::random_device rd;
    _rng = std::mt19937(rd());
}

/* =========================================================
 * PUBLIC — getRecipeSuggestions
 *
 * Same contract as RecipeGoodFoodService:
 *   - shuffle ingredients for variety
 *   - build query string
 *   - fetch one page
 *   - cap at 10 results
 *   - mandatory 500 ms gap after TLS to let sockets drain
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeGialloZafferanoService::getRecipeSuggestions(
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
    // Shuffle ingredients so repeated calls surface different orderings
    std::vector<std::string> shuffledIngredients = ingredients;
    std::shuffle(shuffledIngredients.begin(), shuffledIngredients.end(), _rng);

    // Build raw query: keywords first, then ingredients
    std::string rawQuery;
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
    for (const auto &ing : shuffledIngredients)
    {
        if (!rawQuery.empty())
            rawQuery += ' ';
        rawQuery += ing;
    }

    // GZ search paths use URL-encoded terms; spaces become '+' via urlEncode
    std::string encodedQuery = urlEncode(rawQuery);

    ESP_LOGI(TAG, "Free Block: %d bytes. Starting Page %d",
             (int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), page);

    std::vector<RecipeSuggestion> all;
    {
        auto pageResults = fetchPage(encodedQuery, mealType, difficulty, diet, page);
        all.insert(all.end(), pageResults.begin(), pageResults.end());
    }

    // Mandatory cooldown — mirrors GoodFood service pattern
    vTaskDelay(pdMS_TO_TICKS(500));

    if (all.size() > 10)
        all.resize(10);

    return all;
}

/* =========================================================
 * PRIVATE — fetchPage
 *
 * GiallozZafferano search URL format:
 *   https://www.giallozafferano.it/ricerca-ricette/QUERY/
 *
 * Supported query-string filters (appended as ?key=value):
 *   difficulty   → ?difficulty=facile|media|difficile
 *   diet         → ?diet=vegetariana|vegana
 *   mealType     → ?course=primo|secondo|dolce|antipasto|contorno
 *   page         → ?pag=N  (omitted for page 1)
 *
 * NOTE: GZ does not expose time, ratings, or calories as URL
 *       search filters — those parameters from the shared interface
 *       are silently ignored here. Difficulty and mealType are mapped
 *       from the English tokens used by the rest of the app.
 * ========================================================= */
std::vector<RecipeSuggestion> RecipeGialloZafferanoService::fetchPage(
    const std::string &query,
    const std::string &mealType,
    const std::string &difficulty,
    const std::string &diet,
    int page)
{
    std::vector<RecipeSuggestion> pageSuggestions;

    // ── Build URL ────────────────────────────────────────────────────────────
    std::string url = "https://www.giallozafferano.it/ricerca-ricette/" + query + "/";

    bool hasParam = false;
    auto appendParam = [&](const std::string &key, const std::string &val)
    {
        url += (hasParam ? '&' : '?');
        url += key + '=' + urlEncode(val);
        hasParam = true;
    };

    // Map English tokens → Italian GZ filter values
    std::string gzDifficulty = mapDifficulty(difficulty);
    std::string gzDiet = mapDiet(diet);

    if (!gzDifficulty.empty())
        appendParam("difficulty", gzDifficulty);

    if (!gzDiet.empty())
        appendParam("diet", gzDiet);

    // GZ uses "course" for meal type (primo, secondo, dolce …)
    // Pass the caller's value through — it's expected to be Italian already
    // (or empty). If the app maps English→Italian elsewhere, do it there.
    if (!mealType.empty())
        appendParam("course", mealType);

    // Pagination: GZ uses ?pag=N, omit for page 1
    if (page > 1)
        appendParam("pag", std::to_string(page));

    // ── Fetch HTML ──────────────────────────────────────────────────────────
    int status = -1;
    std::string html = httpGet(url, status);

    if (status != 200 || html.empty())
    {
        ESP_LOGE(TAG, "Failed to fetch HTML for page %d (status %d)", page, status);
        return pageSuggestions;
    }

    // ── Parse recipe cards ──────────────────────────────────────────────────
    std::vector<std::string> articles = splitArticles(html);
    ESP_LOGI(TAG, "Found %d article blocks", (int)articles.size());

    for (const auto &article : articles)
    {
        RecipeSuggestion r;

        // ── URL & ID ────────────────────────────────────────────────────────
        // First <a href="..."> inside the article is the recipe link.
        // GZ recipe URLs look like: /ricette/Pasta-al-pomodoro_123456.html
        std::string firstHref = extractBetween(article, "href=\"", "\"");
        if (firstHref.empty())
            continue; // malformed card — skip

        r.url = (firstHref.find("http") == 0)
                    ? firstHref
                    : ("https://www.giallozafferano.it" + firstHref);

        // Extract slug as ID (e.g. "Pasta-al-pomodoro_123456")
        {
            size_t slash = r.url.rfind('/');
            size_t dot = r.url.rfind('.');
            if (slash != std::string::npos && dot != std::string::npos && dot > slash)
                r.id = r.url.substr(slash + 1, dot - slash - 1);
            else
                r.id = r.url;
        }

        // ── Title ───────────────────────────────────────────────────────────
        // <h2 class="gz-card__title">Pasta al pomodoro</h2>
        std::string titleBlock = extractBetween(article, "gz-card__title\">", "</h2>");
        if (titleBlock.empty())
        {
            // Fallback: pull from img alt or link title attr
            titleBlock = extractBetween(article, "title=\"", "\"");
        }
        r.name = stripTags(titleBlock);

        // ── Image URL ───────────────────────────────────────────────────────
        // GZ uses lazy-loading: the real src is in data-src; the placeholder
        // is in src.  Prefer data-src.
        {
            size_t imgPos = article.find("<img ");
            if (imgPos != std::string::npos)
            {
                size_t imgEnd = article.find('>', imgPos);
                if (imgEnd != std::string::npos)
                {
                    std::string imgTag = article.substr(imgPos, imgEnd - imgPos + 1);
                    std::string dataSrc = extractAttr(imgTag, "data-src");
                    r.imageUrl = dataSrc.empty() ? extractAttr(imgTag, "src") : dataSrc;
                }
            }
        }

        // ── Description ─────────────────────────────────────────────────────
        // <p class="gz-card__description">...</p>
        r.description = stripTags(extractBetween(article, "gz-card__description\">", "</p>"));

        // ── Author ──────────────────────────────────────────────────────────
        // Individual author names are rarely shown in search results on GZ;
        // default to the site brand.
        r.author = "Giallo Zafferano";

        // ── Difficulty & Time ───────────────────────────────────────────────
        // GZ wraps metadata in <ul class="gz-list-featured">
        // Each <li> contains an icon span (class contains "difficulty" or
        // "time") followed by a value span.
        //
        // Pattern (simplified):
        //   <li class="gz-name-featured">
        //     <span class="gz-icon-featured gz-icon-difficulty"></span>
        //     <span class="gz-featured-value">Facile</span>
        //   </li>
        {
            size_t listStart = article.find("gz-list-featured");
            if (listStart != std::string::npos)
            {
                size_t listEnd = article.find("</ul>", listStart);
                if (listEnd == std::string::npos)
                    listEnd = article.size();

                std::string list = article.substr(listStart, listEnd - listStart);

                // Walk each <li>
                size_t liPos = 0;
                while (true)
                {
                    size_t liStart = list.find("<li", liPos);
                    if (liStart == std::string::npos)
                        break;
                    size_t liEnd = list.find("</li>", liStart);
                    if (liEnd == std::string::npos)
                        break;

                    std::string li = list.substr(liStart, liEnd - liStart + 5);

                    // The icon span's class tells us the data type
                    bool isDifficulty = (li.find("gz-icon-difficulty") != std::string::npos);
                    bool isTimePrep = (li.find("gz-icon-time-prep") != std::string::npos);
                    bool isTimeCook = (li.find("gz-icon-time-cook") != std::string::npos);
                    bool isTimeTotal = (li.find("gz-icon-time-total") != std::string::npos ||
                                       li.find("gz-icon-time") != std::string::npos);

                    std::string value = stripTags(extractBetween(li, "gz-featured-value\">", "</span>"));

                    if (isDifficulty && !value.empty())
                        r.difficulty = value; // already Italian ("Facile" etc.)

                    if (isTimeTotal && !value.empty())
                        r.totalTime = value;
                    else if ((isTimePrep || isTimeCook) && r.totalTime.empty() && !value.empty())
                        r.totalTime = value; // best-effort fallback

                    liPos = liEnd + 5;
                }
            }
        }

        // ── Rating ──────────────────────────────────────────────────────────
        // GZ embeds ratings as data attributes on a span:
        //   <span class="gz-rating-star" data-average="4.5" data-count="120">
        {
            size_t ratingPos = article.find("gz-rating-star");
            if (ratingPos != std::string::npos)
            {
                size_t spanEnd = article.find('>', ratingPos);
                if (spanEnd != std::string::npos)
                {
                    std::string ratingTag = article.substr(ratingPos, spanEnd - ratingPos);
                    std::string avg = extractAttr(ratingTag, "data-average");
                    std::string cnt = extractAttr(ratingTag, "data-count");
                    if (!avg.empty())
                        r.ratingValue = std::stod(avg);
                    if (!cnt.empty())
                        r.ratingCount = std::stoi(cnt);
                }
            }
        }

        // ── Premium flag ─────────────────────────────────────────────────────
        // GZ has no paywalled recipes; always false.
        r.isPremium = false;

        r.contentType = "recipe";
        r.recipeSource = "giallozafferano";

        pageSuggestions.push_back(std::move(r));
    }

    return pageSuggestions;
}

/* =========================================================
 * PRIVATE — httpGet
 *
 * Identical to RecipeGoodFoodService::httpGet with one difference:
 * we stream the full HTML (not just a __NEXT_DATA__ script block)
 * and cap at 80 KB to avoid OOM on large pages.
 * ========================================================= */
std::string RecipeGialloZafferanoService::httpGet(const std::string &url, int &status)
{
    ESP_LOGI(TAG, "Free heap before ssl_setup: %" PRIu32, esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free heap:              %" PRIu32, esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "GET: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 2048;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.use_global_ca_store = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return {};

    esp_http_client_set_header(client, "User-Agent",
                               "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    esp_http_client_set_header(client, "Accept",
                               "text/html,application/xhtml+xml");
    esp_http_client_set_header(client, "Accept-Language", "it-IT,it;q=0.9");

    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    std::string html;
    html.reserve(32768); // 32 KB initial reservation

    char buffer[1024];
    int bytes_read;

    // We only need the search results section, which ends around </main>.
    // Stop streaming once we've seen that closing tag to save memory.
    bool done = false;
    while (!done && (bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        html.append(buffer, bytes_read);

        // Emergency cap: 80 KB — the recipes list is always within the first
        // ~50 KB of the GZ search page.
        if (html.size() > 81920)
        {
            ESP_LOGW(TAG, "HTML cap reached (%d bytes), stopping early", (int)html.size());
            done = true;
        }
        else if (html.find("</main>") != std::string::npos)
        {
            // Main content block fully received; discard the rest (footer etc.)
            done = true;
        }

        vTaskDelay(1);
    }

    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "HTML received: %d bytes", (int)html.size());
    return html;
}

/* =========================================================
 * PRIVATE — splitArticles
 *
 * GZ search results wrap each recipe card in:
 *   <article class="gz-card ..."> ... </article>
 *
 * We split on <article and collect everything up to the
 * matching </article>.
 * ========================================================= */
std::vector<std::string> RecipeGialloZafferanoService::splitArticles(const std::string &html) const
{
    std::vector<std::string> result;
    size_t pos = 0;

    while (true)
    {
        size_t start = html.find("<article", pos);
        if (start == std::string::npos)
            break;

        size_t end = html.find("</article>", start);
        if (end == std::string::npos)
            break;

        result.push_back(html.substr(start, end + 10 - start));
        pos = end + 10;
    }

    return result;
}

/* =========================================================
 * PRIVATE — extractBetween
 * ========================================================= */
std::string RecipeGialloZafferanoService::extractBetween(
    const std::string &html,
    const std::string &open,
    const std::string &close,
    size_t fromPos,
    size_t *endPos) const
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

/* =========================================================
 * PRIVATE — extractAttr
 *
 * Searches for attr="VALUE" or attr='VALUE' in a tag string.
 * ========================================================= */
std::string RecipeGialloZafferanoService::extractAttr(
    const std::string &tag, const std::string &attr) const
{
    // Try attr="value"
    std::string needle = attr + "=\"";
    size_t pos = tag.find(needle);
    if (pos != std::string::npos)
    {
        size_t start = pos + needle.size();
        size_t end = tag.find('"', start);
        if (end != std::string::npos)
            return tag.substr(start, end - start);
    }
    // Try attr='value'
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

/* =========================================================
 * PRIVATE — stripTags
 * ========================================================= */
std::string RecipeGialloZafferanoService::stripTags(const std::string &html) const
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

    // Trim leading/trailing whitespace
    size_t s = out.find_first_not_of(" \t\r\n");
    size_t e = out.find_last_not_of(" \t\r\n");
    if (s == std::string::npos)
        return {};
    return out.substr(s, e - s + 1);
}

/* =========================================================
 * PRIVATE — mapDifficulty
 *
 * The rest of the app uses English tokens; GZ expects Italian.
 * ========================================================= */
std::string RecipeGialloZafferanoService::mapDifficulty(const std::string &d) const
{
    if (d == "easy")   return "facile";
    if (d == "medium") return "media";
    if (d == "hard")   return "difficile";
    return d; // pass through if already Italian or empty
}

/* =========================================================
 * PRIVATE — mapDiet
 * ========================================================= */
std::string RecipeGialloZafferanoService::mapDiet(const std::string &diet) const
{
    if (diet == "vegetarian") return "vegetariana";
    if (diet == "vegan")      return "vegana";
    return diet;
}

/* =========================================================
 * PRIVATE — urlEncode
 * ========================================================= */
std::string RecipeGialloZafferanoService::urlEncode(const std::string &s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;

    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else if (c == ' ')
            out << '+'; // GZ search accepts + as word separator in path
        else
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
    }
    return out.str();
}

/* =========================================================
 * PRIVATE — parseMinutes
 * ========================================================= */
int RecipeGialloZafferanoService::parseMinutes(const std::string &timeInput)
{
    if (timeInput.empty())
        return 0;

    // Handles "30 min", "1 ora 15 min", "45 minuti" etc.
    // Extracts all digit runs and sums hours*60 + minutes.
    int hours = 0, minutes = 0;
    bool hasOra = (timeInput.find("ora") != std::string::npos ||
                   timeInput.find("h")   != std::string::npos);

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

    if (hasOra && nums.size() >= 2)
    {
        hours = nums[0];
        minutes = nums[1];
    }
    else if (hasOra && nums.size() == 1)
    {
        hours = nums[0];
    }
    else
    {
        minutes = nums[0];
    }

    return hours * 60 + minutes;
}
