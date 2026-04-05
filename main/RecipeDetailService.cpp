#include "RecipeDetailService.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <algorithm>
#include <cctype>

static const char *TAG = "RecipeDetail";

RecipeDetailService recipeDetailService;

// ── text helpers ─────────────────────────────────────────────────────────────

std::string RecipeDetailService::sanitizeText(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    bool in_tag = false;
    for (char c : text)
    {
        if (c == '<')
        {
            in_tag = true;
            continue;
        }
        if (c == '>')
        {
            in_tag = false;
            continue;
        }
        if (!in_tag)
            out += c;
    }
    std::string result;
    result.reserve(out.size());
    bool prev_space = false;
    for (char c : out)
    {
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c == ' ' && prev_space)
            continue;
        prev_space = (c == ' ');
        result += c;
    }
    size_t start = result.find_first_not_of(' ');
    if (start == std::string::npos)
        return {};
    size_t end = result.find_last_not_of(' ');
    return result.substr(start, end - start + 1);
}

// e.g. "PT30M" -> "30 mins"  |  "PT1H30M" -> "1 hr 30 mins"
std::string RecipeDetailService::parseIso8601Duration(const std::string &iso)
{
    if (iso.empty())
        return {};
    int hours = 0, minutes = 0;
    size_t i = 0;
    while (i < iso.size() && !isdigit((unsigned char)iso[i]))
        i++;
    while (i < iso.size())
    {
        int val = 0;
        while (i < iso.size() && isdigit((unsigned char)iso[i]))
            val = val * 10 + (iso[i++] - '0');
        if (i < iso.size())
        {
            char unit = iso[i++];
            if (unit == 'H')
                hours = val;
            else if (unit == 'M')
                minutes = val;
        }
    }
    std::string result;
    if (hours > 0)
    {
        result += std::to_string(hours) + " hr";
        if (hours > 1)
            result += "s";
    }
    if (minutes > 0)
    {
        if (!result.empty())
            result += " ";
        result += std::to_string(minutes) + " min";
        if (minutes > 1)
            result += "s";
    }
    return result;
}

// ── HTTP event handler capture ────────────────────────────────────────────────

// We stream the HTML looking for three possible script blocks, stopping as soon as we have enough.
struct HtmlCapture
{
    // Which blocks we're looking for / have captured
    std::string nextData;    // id="__NEXT_DATA__"
    std::string jsonLd;      // type="application/ld+json" containing "Recipe"
    std::string postContent; // id="__POST_CONTENT__"

    enum State
    {
        SEARCHING,
        IN_NEXT_DATA,
        IN_JSON_LD,
        IN_POST_CONTENT
    } state = SEARCHING;
    std::string *captureTarget = nullptr;

    // Two separate overlaps:
    // - window: large (for SEARCHING) — catches <script ...> opening tags split across chunks
    // - closeOverlap: tiny (for capture) — catches </script> split across chunks,
    //   WITHOUT re-feeding already-captured bytes
    std::string window;
    std::string closeOverlap;

    static const size_t WINDOW_SIZE = 512; // must exceed longest <script ...> opening tag
    static const size_t CLOSE_OV_LEN = 8;  // strlen("</script>") - 1 = 8
    static const size_t MAX_CAPTURE = 48 * 1024;

    bool done = false;

    void feed(const char *data, size_t len);

private:
    // newDataStart: first byte in chunk that has NOT been captured yet.
    // When entering in SEARCHING state this is 0 (safe to re-scan the window).
    // When entering in a capture state this is closeOverlap.size() (skip already-captured tail).
    void processChunk(const std::string &chunk, size_t newDataStart);
};

static const char *CLOSE_SCRIPT = "</script>";

void HtmlCapture::feed(const char *data, size_t len)
{
    if (done)
        return;

    std::string chunk;
    size_t newDataStart;

    if (state == SEARCHING)
    {
        chunk = window + std::string(data, len);
        newDataStart = 0;
    }
    else
    {
        chunk = closeOverlap + std::string(data, len);
        newDataStart = closeOverlap.size();
    }

    if (chunk.size() > WINDOW_SIZE)
        window = chunk.substr(chunk.size() - WINDOW_SIZE);
    else
        window = chunk;

    if (chunk.size() > CLOSE_OV_LEN)
        closeOverlap = chunk.substr(chunk.size() - CLOSE_OV_LEN);
    else
        closeOverlap = chunk;

    processChunk(chunk, newDataStart);
}

void HtmlCapture::processChunk(const std::string &chunk, size_t newDataStart)
{
    size_t pos = (state == SEARCHING) ? 0 : newDataStart;

    while (pos < chunk.size() && !done)
    {
        if (state == SEARCHING)
        {
            size_t scriptPos = chunk.find("<script", pos);
            if (scriptPos == std::string::npos)
                break;

            size_t gt = chunk.find('>', scriptPos);
            if (gt == std::string::npos)
            {
                pos = scriptPos;
                break;
            }

            std::string tag = chunk.substr(scriptPos, gt - scriptPos + 1);

            bool wantNext = nextData.empty() && tag.find("__NEXT_DATA__") != std::string::npos;
            bool wantLd = jsonLd.empty() && tag.find("application/ld+json") != std::string::npos;
            bool wantPost = postContent.empty() && tag.find("__POST_CONTENT__") != std::string::npos;

            if (wantNext)
            {
                state = IN_NEXT_DATA;
                captureTarget = &nextData;
                pos = gt + 1;
            }
            else if (wantLd)
            {
                state = IN_JSON_LD;
                captureTarget = &jsonLd;
                pos = gt + 1;
            }
            else if (wantPost)
            {
                state = IN_POST_CONTENT;
                captureTarget = &postContent;
                pos = gt + 1;
            }
            else
            {
                pos = gt + 1;
            }
        }
        else // capturing
        {
            size_t searchFrom = (pos >= CLOSE_OV_LEN) ? pos - CLOSE_OV_LEN : 0;
            size_t end = chunk.find(CLOSE_SCRIPT, searchFrom);

            if (end == std::string::npos)
            {
                if (captureTarget && captureTarget->size() < MAX_CAPTURE)
                    captureTarget->append(chunk, pos, chunk.size() - pos);
                break;
            }

            if (end > pos && captureTarget && captureTarget->size() < MAX_CAPTURE)
                captureTarget->append(chunk, pos, end - pos);

            if (state == IN_JSON_LD)
            {
                if (jsonLd.find("\"Recipe\"") == std::string::npos)
                    jsonLd.clear(); // not a Recipe block, discard and keep searching
            }

            state = SEARCHING;
            captureTarget = nullptr;
            pos = end + strlen(CLOSE_SCRIPT);

            // Stop only when all desired blocks are captured.
            // jsonLd is optional (not every page has it), so we stop as soon
            // as nextData and postContent are both in hand.
            done = !nextData.empty() && !postContent.empty() && !jsonLd.empty();
        }
    }
}

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    HtmlCapture *cap = (HtmlCapture *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && cap && !cap->done)
        cap->feed((const char *)evt->data, evt->data_len);
    return ESP_OK;
}

// ── HTTP fetch ────────────────────────────────────────────────────────────────

bool RecipeDetailService::fetchHtmlAndExtract(const std::string &url,
                                              std::string &nextData,
                                              std::string &jsonLd,
                                              std::string &postContent)
{
    ESP_LOGI(TAG, "Fetching: %s", url.c_str());
    ESP_LOGI(TAG, "Free heap: %" PRIu32, esp_get_free_heap_size());

    HtmlCapture cap;

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 4096;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.use_global_ca_store = false;
    cfg.event_handler = http_event_cb;
    cfg.user_data = &cap;
    // Follow redirects (e.g. trailing-slash normalisation on recipe pages)
    cfg.max_redirection_count = 5;
    cfg.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return false;

    // Match headers of the working RecipeGoodFoodService
    esp_http_client_set_header(client, "User-Agent",
                               "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                               "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    esp_http_client_set_header(client, "Accept",
                               "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    esp_http_client_set_header(client, "Accept-Language", "en-GB,en;q=0.9");
    // No Accept-Encoding — we can't decompress gzip on the ESP32 here
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    // BBC cookie-consent cookie to bypass the GDPR banner (no JS needed)
    esp_http_client_set_header(client, "Cookie",
                               "ckns_policy=111; ckns_explicit=1; bbccookies_status=accepted");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "perform failed: %s (status %d)", esp_err_to_name(err), status);
        return false;
    }

    ESP_LOGI(TAG, "HTTP %d — nextData: %u B, jsonLd: %u B, postContent: %u B",
             status,
             (unsigned)cap.nextData.size(),
             (unsigned)cap.jsonLd.size(),
             (unsigned)cap.postContent.size());

    nextData = std::move(cap.nextData);
    jsonLd = std::move(cap.jsonLd);
    postContent = std::move(cap.postContent);

    return !nextData.empty() || !jsonLd.empty() || !postContent.empty();
}

// ── __NEXT_DATA__ parser for recipe detail page ───────────────────────────────
// BBC Good Food recipe pages have their data at several possible paths inside
// __NEXT_DATA__.props.pageProps — try them in order.

bool RecipeDetailService::parseNextData(const std::string &json, RecipeSuggestion &recipe)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (!root)
    {
        ESP_LOGW(TAG, "cJSON_Parse __NEXT_DATA__ failed");
        return false;
    }

    auto str = [](cJSON *o, const char *k) -> std::string
    {
        if (!o)
            return {};
        cJSON *v = cJSON_GetObjectItem(o, k);
        return (v && cJSON_IsString(v)) ? v->valuestring : "";
    };

    // Navigate to props.pageProps
    cJSON *props = cJSON_GetObjectItem(root, "props");
    cJSON *pp = props ? cJSON_GetObjectItem(props, "pageProps") : nullptr;

    if (!pp)
    {
        cJSON_Delete(root);
        return false;
    }

    // ── Try: pageProps.schema (BBC Good Food embeds the Recipe JSON-LD here) ──
    cJSON *schema = cJSON_GetObjectItem(pp, "schema");
    bool fromSchema = false;
    if (schema)
    {
        // May be an array
        if (cJSON_IsArray(schema))
        {
            cJSON *item;
            cJSON_ArrayForEach(item, schema)
            {
                cJSON *t = cJSON_GetObjectItem(item, "@type");
                if (t && cJSON_IsString(t) && std::string(t->valuestring) == "Recipe")
                {
                    schema = item;
                    break;
                }
            }
        }
        if (cJSON_IsObject(schema))
        {
            if (recipe.prepTime.empty())
                recipe.prepTime = parseIso8601Duration(str(schema, "prepTime"));
            if (recipe.cookTime.empty())
                recipe.cookTime = parseIso8601Duration(str(schema, "cookTime"));
            if (recipe.servings.empty())
                recipe.servings = sanitizeText(str(schema, "recipeYield"));

            cJSON *ings = cJSON_GetObjectItem(schema, "recipeIngredient");
            if (ings && cJSON_IsArray(ings) && recipe.ingredients.empty())
            {
                cJSON *i;
                cJSON_ArrayForEach(i, ings) if (cJSON_IsString(i)) recipe.ingredients.push_back(sanitizeText(i->valuestring));
            }

            cJSON *instr = cJSON_GetObjectItem(schema, "recipeInstructions");
            if (instr && cJSON_IsArray(instr) && recipe.methodSteps.empty())
            {
                cJSON *step;
                cJSON_ArrayForEach(step, instr)
                {
                    std::string t2 = str(step, "text");
                    if (t2.empty())
                        t2 = str(step, "name");
                    if (t2.empty() && cJSON_IsString(step))
                        t2 = step->valuestring;
                    if (!t2.empty() && t2.length() < 5)
                        recipe.methodSteps.push_back(sanitizeText(t2));
                }
            }
            fromSchema = !recipe.ingredients.empty() || !recipe.methodSteps.empty();
        }
    }

    // ── Try: pageProps directly (some versions embed title/ingredients/method here) ──
    // Fields: title, servings, skillLevel, ingredients (array of sections), method
    if (recipe.name.empty())
        recipe.name = sanitizeText(str(pp, "title"));
    if (recipe.servings.empty())
        recipe.servings = sanitizeText(str(pp, "servings"));
    if (recipe.difficulty.empty())
        recipe.difficulty = sanitizeText(str(pp, "skillLevel"));

    cIngredientsMethod(pp, recipe);

    cJSON_Delete(root);

    bool ok = !recipe.ingredients.empty() || !recipe.methodSteps.empty();
    ESP_LOGI(TAG, "parseNextData: ings=%d steps=%d", (int)recipe.ingredients.size(), (int)recipe.methodSteps.size());
    return ok;
}

// Shared helper: parse ingredients/method from a node that has
// {"ingredients":[{section}...], "method":[{steps}...]} structure (__POST_CONTENT__ format)
void RecipeDetailService::cIngredientsMethod(cJSON *node, RecipeSuggestion &recipe)
{
    if (!node)
        return;

    auto str = [](cJSON *o, const char *k) -> std::string
    {
        if (!o)
            return {};
        cJSON *v = cJSON_GetObjectItem(o, k);
        return (v && cJSON_IsString(v)) ? v->valuestring : "";
    };

    // Ingredients
    if (recipe.ingredients.empty())
    {
        cJSON *ingSections = cJSON_GetObjectItem(node, "ingredients");
        if (ingSections && cJSON_IsArray(ingSections))
        {
            cJSON *section;
            cJSON_ArrayForEach(section, ingSections)
            {
                cJSON *items = cJSON_GetObjectItem(section, "ingredients");
                if (!items)
                { // flat array of strings
                    if (cJSON_IsString(section))
                        recipe.ingredients.push_back(sanitizeText(section->valuestring));
                    continue;
                }
                cJSON *item;
                cJSON_ArrayForEach(item, items)
                {
                    std::string qty = str(item, "quantityText");
                    std::string ing = str(item, "ingredientText");
                    std::string note = str(item, "note");
                    std::string line = qty;
                    if (!ing.empty())
                    {
                        if (!line.empty())
                            line += " ";
                        line += ing;
                    }
                    if (!note.empty())
                    {
                        if (!line.empty())
                            line += ", ";
                        line += note;
                    }
                    line = sanitizeText(line);
                    if (!line.empty())
                        recipe.ingredients.push_back(line);
                }
            }
        }
    }

    // Method
    if (recipe.methodSteps.empty())
    {
        cJSON *methodSections = cJSON_GetObjectItem(node, "method");
        if (methodSections && cJSON_IsArray(methodSections))
        {
            cJSON *section;
            cJSON_ArrayForEach(section, methodSections)
            {
                cJSON *steps = cJSON_GetObjectItem(section, "steps");
                if (!steps)
                    continue;
                cJSON *step;
                cJSON_ArrayForEach(step, steps)
                {
                    std::string text = str(step, "description");
                    if (text.empty())
                        text = str(step, "text");
                    text = sanitizeText(text);
                    if (!text.empty())
                        recipe.methodSteps.push_back(text);
                }
            }
        }
    }
}

// ── JSON-LD parser ────────────────────────────────────────────────────────────

bool RecipeDetailService::parseJsonLd(const std::string &json, RecipeSuggestion &recipe)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (!root)
        return false;

    cJSON *schema = root;
    if (cJSON_IsArray(root))
    {
        cJSON *item;
        cJSON_ArrayForEach(item, root)
        {
            cJSON *t = cJSON_GetObjectItem(item, "@type");
            if (t && cJSON_IsString(t) && std::string(t->valuestring) == "Recipe")
            {
                schema = item;
                break;
            }
        }
    }

    auto str = [](cJSON *o, const char *k) -> std::string
    {
        cJSON *v = cJSON_GetObjectItem(o, k);
        return (v && cJSON_IsString(v)) ? v->valuestring : "";
    };

    if (recipe.name.empty())
        recipe.name = sanitizeText(str(schema, "name"));
    if (recipe.servings.empty())
        recipe.servings = sanitizeText(str(schema, "recipeYield"));
    if (recipe.prepTime.empty())
        recipe.prepTime = parseIso8601Duration(str(schema, "prepTime"));
    if (recipe.cookTime.empty())
        recipe.cookTime = parseIso8601Duration(str(schema, "cookTime"));

    cJSON *ings = cJSON_GetObjectItem(schema, "recipeIngredient");
    if (ings && cJSON_IsArray(ings) && recipe.ingredients.empty())
    {
        cJSON *ing;
        cJSON_ArrayForEach(ing, ings) if (cJSON_IsString(ing)) recipe.ingredients.push_back(sanitizeText(ing->valuestring));
    }

    cJSON *instr = cJSON_GetObjectItem(schema, "recipeInstructions");
    if (instr && cJSON_IsArray(instr) && recipe.methodSteps.empty())
    {
        cJSON *step;
        cJSON_ArrayForEach(step, instr)
        {
            std::string text = str(step, "text");
            if (text.empty())
                text = str(step, "name");
            if (text.empty() && cJSON_IsString(step))
                text = step->valuestring;
            if (!text.empty())
                recipe.methodSteps.push_back(sanitizeText(text));
        }
    }

    cJSON_Delete(root);
    bool ok = !recipe.ingredients.empty() || !recipe.methodSteps.empty();
    ESP_LOGI(TAG, "parseJsonLd: ings=%d steps=%d", (int)recipe.ingredients.size(), (int)recipe.methodSteps.size());
    return ok;
}

// ── __POST_CONTENT__ parser ───────────────────────────────────────────────────

bool RecipeDetailService::parsePostContent(const std::string &json, RecipeSuggestion &recipe)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (!root)
    {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGW(TAG, "parsePostContent: cJSON_Parse failed near: %.40s", err ? err : "(null)");
        return false;
    }

    auto str = [](cJSON *o, const char *k) -> std::string
    {
        cJSON *v = cJSON_GetObjectItem(o, k);
        return (v && cJSON_IsString(v)) ? v->valuestring : "";
    };

    if (recipe.name.empty())
        recipe.name = sanitizeText(str(root, "title"));
    if (recipe.servings.empty())
        recipe.servings = sanitizeText(str(root, "servings"));
    if (recipe.difficulty.empty())
        recipe.difficulty = sanitizeText(str(root, "skillLevel"));

    cJSON *schemaObj = cJSON_GetObjectItem(root, "schema");
    if (schemaObj)
    {
        if (recipe.prepTime.empty())
            recipe.prepTime = parseIso8601Duration(str(schemaObj, "prepTime"));
        if (recipe.cookTime.empty())
            recipe.cookTime = parseIso8601Duration(str(schemaObj, "cookTime"));
    }

    cIngredientsMethod(root, recipe);

    cJSON_Delete(root);
    bool ok = !recipe.ingredients.empty() || !recipe.methodSteps.empty();
    ESP_LOGI(TAG, "parsePostContent: ings=%d steps=%d", (int)recipe.ingredients.size(), (int)recipe.methodSteps.size());
    return ok;
}

// ── public API ────────────────────────────────────────────────────────────────

bool RecipeDetailService::fetchDetails(RecipeSuggestion &recipe)
{
    if (recipe.url.empty())
        return false;

    selectedRecipe = recipe;

    std::string nextData, jsonLd, postContent;
    if (!fetchHtmlAndExtract(recipe.url, nextData, jsonLd, postContent))
    {
        ESP_LOGW(TAG, "Nothing extracted for %s", recipe.url.c_str());
        return false;
    }

    bool ok = false;

    // Priority: __NEXT_DATA__ (most complete), then JSON-LD, then __POST_CONTENT__
    if (!nextData.empty())
        ok = parseNextData(nextData, recipe);

    if (!ok && !jsonLd.empty())
        ok = parseJsonLd(jsonLd, recipe);

    if (!ok && !postContent.empty())
        ok = parsePostContent(postContent, recipe);

    if (ok)
        recipe.detailsFetched = true;
    return ok;
}
