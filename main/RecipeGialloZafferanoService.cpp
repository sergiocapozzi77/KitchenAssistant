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

    // GZ search paths are case-sensitive — lowercase everything to avoid 301s
    std::transform(rawQuery.begin(), rawQuery.end(), rawQuery.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });

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
        // Skip non-recipe cards (gz-card-special, blog carousels, etc.).
        // Real search result cards always carry a data-index attribute.
        if (article.find("data-index=") == std::string::npos)
            continue;

        RecipeSuggestion r;

        // ── URL & ID ────────────────────────────────────────────────────────
        // Actual structure:
        //   <div class="gz-card-image">
        //     <a href="https://ricette.giallozafferano.it/Pastiera-napoletana.html" ...>
        // Both the image link and the title link point to the same URL;
        // grabbing the first href in the article is reliable.
        std::string firstHref = extractBetween(article, "href=\"", "\"");
        if (firstHref.empty())
            continue;

        r.url = (firstHref.find("http") == 0)
                    ? firstHref
                    : ("https://www.giallozafferano.it" + firstHref);

        {
            size_t slash = r.url.rfind('/');
            size_t dot = r.url.rfind('.');
            if (slash != std::string::npos && dot != std::string::npos && dot > slash)
                r.id = r.url.substr(slash + 1, dot - slash - 1);
            else
                r.id = r.url;
        }

        // ── Title ───────────────────────────────────────────────────────────
        // Actual: <h2 class="gz-title"><a href="...">Pastiera napoletana</a></h2>
        r.name = stripTags(extractBetween(article, "gz-title\">", "</h2>"));
        if (r.name.empty())
            r.name = extractBetween(article, "title=\"", "\""); // <a title="..."> fallback

        // ── Image URL ───────────────────────────────────────────────────────
        // GZ search results use plain src= (no data-src lazy-loading on first
        // visible image; later pages use class="lazyload" with data-src).
        {
            size_t imgPos = article.find("<img ");
            if (imgPos != std::string::npos)
            {
                size_t imgEnd = article.find('>', imgPos);
                if (imgEnd != std::string::npos)
                {
                    std::string imgTag = article.substr(imgPos, imgEnd - imgPos + 1);
                    r.imageUrl = extractAttr(imgTag, "data-src"); // lazy-loaded pages
                    if (r.imageUrl.empty())
                        r.imageUrl = extractAttr(imgTag, "src"); // direct src
                }
            }
        }

        // ── Description ─────────────────────────────────────────────────────
        // Actual: <div class="gz-description">La pastiera è…</div>
        r.description = stripTags(extractBetween(article, "gz-description\">", "</div>"));

        // ── Author ──────────────────────────────────────────────────────────
        r.author = "Giallo Zafferano";

        // ── Difficulty & Time ───────────────────────────────────────────────
        // Actual structure — <ul class="gz-card-data bottom"> contains three <li>:
        //
        //   <li class="gz-single-data-recipe">
        //     <span class="gz-icon">
        //       <svg …><use xlink:href="…#difficolta-grey" /></svg>
        //     </span>
        //     Difficile          ← plain text after the closing </span>
        //   </li>
        //   <li …>  …#tempo-grey…   2 h 5 min  </li>
        //   <li …>  …#kcal-grey…    Kcal 580   </li>
        //
        // We identify the type by the SVG fragment name and read the trailing text.
        {
            size_t bottomStart = article.find("gz-card-data bottom");
            if (bottomStart != std::string::npos)
            {
                size_t ulEnd = article.find("</ul>", bottomStart);
                if (ulEnd == std::string::npos)
                    ulEnd = article.size();

                std::string list = article.substr(bottomStart, ulEnd - bottomStart);
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

                    bool isDiff = (li.find("difficolta") != std::string::npos);
                    bool isTime = (li.find("tempo") != std::string::npos);

                    // Text value sits after the last </span> in the <li>
                    size_t spanClose = li.rfind("</span>");
                    if (spanClose != std::string::npos)
                    {
                        std::string raw = li.substr(spanClose + 7);

                        // Stop at next HTML tag (e.g. </li>)
                        size_t tagStart = raw.find('<');
                        if (tagStart != std::string::npos)
                            raw = raw.substr(0, tagStart);

                        // Trim whitespace
                        size_t s = raw.find_first_not_of(" \t\r\n");
                        size_t e = raw.find_last_not_of(" \t\r\n");

                        if (s != std::string::npos)
                        {
                            std::string value = raw.substr(s, e - s + 1);
                            if (isDiff && !value.empty())
                                r.difficulty = value;
                            else if (isTime && r.totalTime.empty() && !value.empty())
                                r.totalTime = value;
                        }
                    }

                    liPos = liEnd + 5;
                }
            }
        }

        // ── Rating ──────────────────────────────────────────────────────────
        // Actual structure — <ul class="gz-card-data top"> contains a <li>
        // whose SVG uses fragment #voto-grey or #voto-orange, followed by
        // the rating as plain text with an Italian comma decimal: "4,2"
        {
            size_t topStart = article.find("gz-card-data top");
            if (topStart != std::string::npos)
            {
                size_t ulEnd = article.find("</ul>", topStart);
                if (ulEnd == std::string::npos)
                    ulEnd = article.size();

                std::string list = article.substr(topStart, ulEnd - topStart);
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

                    if (li.find("voto") != std::string::npos)
                    {
                        // Rating text is after the last </span> (or </a>)
                        size_t spanClose = li.rfind("</span>");
                        if (spanClose != std::string::npos)
                        {
                            std::string raw = li.substr(spanClose + 7);
                            // Also strip a possible enclosing </a>
                            size_t aClose = raw.find("</a>");
                            if (aClose != std::string::npos)
                                raw = raw.substr(0, aClose);

                            size_t s = raw.find_first_not_of(" \t\r\n");
                            size_t e = raw.find_last_not_of(" \t\r\n");
                            if (s != std::string::npos)
                            {
                                std::string ratingStr = raw.substr(s, e - s + 1);

                                // Italian decimal comma → dot
                                std::replace(ratingStr.begin(), ratingStr.end(), ',', '.');

                                // Parse safely
                                char *end = nullptr;
                                double value = std::strtod(ratingStr.c_str(), &end);

                                // Skip trailing spaces (optional but robust)
                                while (end && std::isspace(*end))
                                {
                                    ++end;
                                }

                                // Validate conversion
                                if (end != ratingStr.c_str() && *end == '\0')
                                {
                                    r.ratingValue = value;
                                }
                                else
                                {
                                    // fallback (choose what makes sense)
                                    r.ratingValue = 0.0;
                                }
                            }
                        }
                    }

                    liPos = liEnd + 5;
                }
            }
        }

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
    cfg.keep_alive_enable = false;

    // ── Debug: log every response header as it arrives ───────────────────────
    // Attach an event handler so we can see exactly what headers the server
    // sends back (including Location on a redirect).
    // Remove once redirect behaviour is confirmed.
    struct HeaderDumpCtx
    {
        const char *tag;
    };
    static HeaderDumpCtx dumpCtx{TAG};

    cfg.event_handler = [](esp_http_client_event_t *evt) -> esp_err_t
    {
        auto *ctx = static_cast<HeaderDumpCtx *>(evt->user_data);
        switch (evt->event_id)
        {
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(ctx->tag, "[HDR] %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(ctx->tag, "[EVT] HTTP_EVENT_REDIRECT fired");
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(ctx->tag, "[EVT] HTTP_EVENT_ERROR");
            break;
        default:
            break;
        }
        return ESP_OK;
    };
    cfg.user_data = &dumpCtx;
    // ─────────────────────────────────────────────────────────────────────────

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return {};

    esp_http_client_set_header(client, "User-Agent",
                               "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    esp_http_client_set_header(client, "Accept",
                               "text/html,application/xhtml+xml");
    esp_http_client_set_header(client, "Accept-Language", "it-IT,it;q=0.9");

    // esp_http_client's manual open/read path does NOT follow redirects
    // automatically (only esp_http_client_perform does).  Chase Location
    // headers ourselves for up to 3 hops.
    constexpr int kMaxRedirects = 3;
    for (int hop = 0; hop <= kMaxRedirects; ++hop)
    {
        if (esp_http_client_open(client, 0) != ESP_OK)
        {
            esp_http_client_cleanup(client);
            return {};
        }

        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);

        if (status != 301 && status != 302 && status != 307 && status != 308)
            break; // final destination reached (or an error — handled below)

        // Read the Location header before closing
        char *location = nullptr;
        esp_http_client_get_header(client, "Location", &location);

        if (!location || location[0] == '\0')
        {
            ESP_LOGE(TAG, "Redirect %d has no Location header", status);
            esp_http_client_cleanup(client);
            return {};
        }

        // Location may be relative ("/ricerca-ricette/…") or absolute
        std::string newUrl = location;
        if (newUrl[0] == '/')
            newUrl = "https://www.giallozafferano.it" + newUrl;

        ESP_LOGI(TAG, "Redirect %d → %s", status, newUrl.c_str());

        esp_http_client_close(client);
        esp_http_client_set_url(client, newUrl.c_str());

        if (hop == kMaxRedirects)
        {
            ESP_LOGE(TAG, "Too many redirects, giving up");
            esp_http_client_cleanup(client);
            return {};
        }
    }

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
        // if (html.size() > 161920)
        // {
        //     ESP_LOGW(TAG, "HTML cap reached (%d bytes), stopping early", (int)html.size());
        //     done = true;
        // }
        if (html.find("</main>") != std::string::npos)
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
    if (d == "easy")
        return "facile";
    if (d == "medium")
        return "media";
    if (d == "hard")
        return "difficile";
    return d; // pass through if already Italian or empty
}

/* =========================================================
 * PRIVATE — mapDiet
 * ========================================================= */
std::string RecipeGialloZafferanoService::mapDiet(const std::string &diet) const
{
    if (diet == "vegetarian")
        return "vegetariana";
    if (diet == "vegan")
        return "vegana";
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