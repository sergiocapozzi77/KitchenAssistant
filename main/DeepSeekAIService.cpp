#include "DeepSeekAIService.h"
#include "esp_log.h"
#include "cJSON.h"
#include "secrets.h"
#include "esp_crt_bundle.h"
#include <sstream>
#include <iomanip>
#include <cctype>

static const char *TAG = "DeepSeekAIService";

// Safe JSON extraction helpers (similar to RecipeService)
static std::string safeJsonString(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

static int safeJsonInt(cJSON *obj, const char *key, int def = 0)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : def;
}

static std::string trimWhitespace(const std::string &str)
{
    size_t start = 0;
    size_t end = str.length();

    while (start < end && std::isspace(static_cast<unsigned char>(str[start])))
    {
        start++;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1])))
    {
        end--;
    }

    return str.substr(start, end - start);
}

// Helper function to perform HTTP POST to DeepSeek API with custom timeout
static std::string deepSeekHttpPost(const std::string &url, const std::string &body, int &status, int timeout_ms = 120000)
{
    ESP_LOGI(TAG, "POST to DeepSeek: %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = timeout_ms;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    cfg.skip_cert_common_name_check = false;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        status = -1;
        return {};
    }

    // Set DeepSeek headers
    esp_http_client_set_header(client, "Authorization", ("Bearer " + std::string(DEEPSEEK_API_KEY)).c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open error: %s", esp_err_to_name(err));
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    int bytes_written = esp_http_client_write(client, body.c_str(), body.size());
    if (bytes_written != body.size())
    {
        ESP_LOGE(TAG, "Write error: wrote %d of %d bytes", bytes_written, body.size());
        status = -1;
        esp_http_client_cleanup(client);
        return {};
    }

    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "DeepSeek POST Status: %d. Reading response", status);

    std::string response;
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, bytes_read);
    }

    ESP_LOGI(TAG, "DeepSeek POST Response length: %d", response.length());
    ESP_LOGI(TAG, "DeepSeek POST Response length: %s", response.c_str());
    esp_http_client_cleanup(client);
    return response;
}

DeepSeekAIService::DeepSeekAIService()
    : _httpClient("", "", "", 120000) // endpoint, projectId, apiKey empty, timeout 120s (unused)
      ,
      _apiKey(DEEPSEEK_API_KEY), _endpoint(DEEPSEEK_ENDPOINT)
{
}

std::vector<RecipeSuggestion> DeepSeekAIService::getRecipeSuggestions(
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
    ESP_LOGI(TAG, "Getting AI recipe suggestions with %d ingredients, mealType: %s", ingredients.size(), mealType.c_str());

    // Build the prompt
    std::string prompt = buildPrompt(ingredients, mealType, keywords, difficulty, totalTime, diet, cuisine, ratings, calories);

    // Build the JSON payload for DeepSeek chat completion
    cJSON *payload = cJSON_CreateObject();
    if (!payload)
    {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return {};
    }

    cJSON_AddStringToObject(payload, "model", "deepseek-chat");
    cJSON_AddNumberToObject(payload, "max_tokens", 2000);
    cJSON_AddNumberToObject(payload, "temperature", 0.7);

    cJSON *messages = cJSON_AddArrayToObject(payload, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", prompt.c_str());
    cJSON_AddItemToArray(messages, msg);

    char *payloadStr = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payloadStr)
    {
        ESP_LOGE(TAG, "Failed to stringify payload");
        return {};
    }

    std::string url = _endpoint + "/chat/completions";
    int status = 0;
    std::string response = deepSeekHttpPost(url, payloadStr, status, 120000);
    free(payloadStr);

    if (status < 0)
    {
        ESP_LOGE(TAG, "HTTP transport error");
        return {};
    }
    if (status != 200)
    {
        ESP_LOGE(TAG, "DeepSeek API error: HTTP %d, response: %s", status, response.c_str());
        return {};
    }
    if (response.empty())
    {
        ESP_LOGW(TAG, "Empty response from DeepSeek");
        return {};
    }

    // Parse the response and extract suggestions
    return parseAIResponse(response);
}

std::string DeepSeekAIService::buildPrompt(
    const std::vector<std::string> &ingredients,
    const std::string &mealType,
    const std::vector<std::string> &keywords,
    const std::string &difficulty,
    const std::string &totalTime,
    const std::string &diet,
    const std::string &cuisine,
    const std::string &ratings,
    const std::string &calories) const
{
    std::ostringstream oss;
    oss << "You are a recipe generator.\n\n";
    oss << "Use the following inputs:\n";
    oss << "- Ingredients: " << (ingredients.empty() ? "any" : "");
    for (size_t i = 0; i < ingredients.size(); ++i)
    {
        if (i > 0)
            oss << ", ";
        oss << ingredients[i];
    }
    oss << "\n";
    if (!mealType.empty())
        oss << "- Dish type: " << mealType << "\n";
    if (!keywords.empty())
    {
        oss << "- Keywords: ";
        for (size_t i = 0; i < keywords.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << keywords[i];
        }
        oss << "\n";
    }
    if (!difficulty.empty())
        oss << "- Difficulty: " << difficulty << "\n";
    if (!totalTime.empty())
        oss << "- Total time: " << totalTime << "\n";
    if (!diet.empty())
        oss << "- Diet: " << diet << "\n";
    if (!cuisine.empty())
        oss << "- Cuisine: " << cuisine << "\n";
    if (!calories.empty())
        oss << "- Calories: " << calories << "\n";
    oss << "\n";
    oss << "Rules:\n";
    oss << "- Prefer recipes that use as many of the listed ingredients as possible.\n";
    oss << "- It is OK if the recipe uses extra ingredients.\n";
    oss << "- Return exactly 5 recipes.\n";
    oss << "- You may search the web to find real recipes and links.\n";
    oss << "- For each recipe, extract the preparation time (in minutes) and difficulty, and normalize difficulty to: \"easy\", \"medium\", or \"hard\".\n";
    oss << "\n";
    oss << "Output ONLY valid JSON — no explanations, no text before or after.\n";
    oss << "Do not wrap the JSON in markdown code blocks (no ```json or ```).\n";
    oss << "\n";
    oss << "Format:\n";
    oss << "[\n";
    oss << "  {\n";
    oss << "    \"name\": \"Dish name\",\n";
    oss << "    \"description\": \"Recipe description\",\n";
    oss << "    \"prep_time\": 0,\n";
    oss << "    \"difficulty\": \"easy\"\n";
    oss << "  }\n";
    oss << "]\n";
    return oss.str();
}

std::vector<RecipeSuggestion> DeepSeekAIService::parseAIResponse(const std::string &jsonResponse) const
{
    std::vector<RecipeSuggestion> suggestions;
    cJSON *root = cJSON_Parse(jsonResponse.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse DeepSeek response JSON");
        return suggestions;
    }

    // DeepSeek response format: {"choices":[{"message":{"content":"..."}}]}
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0)
    {
        ESP_LOGE(TAG, "No choices array in DeepSeek response");
        cJSON_Delete(root);
        return suggestions;
    }

    cJSON *firstChoice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(firstChoice, "message");
    if (!message)
    {
        ESP_LOGE(TAG, "No message in choice");
        cJSON_Delete(root);
        return suggestions;
    }

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!cJSON_IsString(content))
    {
        ESP_LOGE(TAG, "No content string in message");
        cJSON_Delete(root);
        return suggestions;
    }

    std::string innerJson = content->valuestring;
    cJSON_Delete(root);

    // Clean markdown code block if present
    size_t start = 0;
    size_t end = innerJson.length();

    // Remove leading ```json or ``` markers
    if (innerJson.substr(0, 7) == "```json")
    {
        start = innerJson.find('\n', 7);
        if (start != std::string::npos)
        {
            start++; // move past newline
        }
        else
        {
            start = 7; // no newline after ```json
        }
    }
    else if (innerJson.substr(0, 3) == "```")
    {
        start = innerJson.find('\n', 3);
        if (start != std::string::npos)
        {
            start++; // move past newline
        }
        else
        {
            start = 3; // no newline after ```
        }
    }

    // Remove trailing ``` markers
    if (innerJson.substr(end - 3, 3) == "```")
    {
        end = end - 3;
        // Also remove any trailing whitespace/newline before ```
        while (end > start && (innerJson[end - 1] == '\n' || innerJson[end - 1] == '\r' || innerJson[end - 1] == ' '))
        {
            end--;
        }
    }

    std::string cleanJson = innerJson.substr(start, end - start);
    cleanJson = trimWhitespace(cleanJson);

    // Parse the inner JSON array
    cJSON *array = cJSON_Parse(cleanJson.c_str());
    if (!cJSON_IsArray(array))
    {
        ESP_LOGE(TAG, "Content is not a JSON array. Cleaned JSON: %s", cleanJson.c_str());
        return suggestions;
    }

    int count = cJSON_GetArraySize(array);
    ESP_LOGI(TAG, "Parsed %d AI recipe suggestions", count);

    for (int i = 0; i < count; i++)
    {
        cJSON *item = cJSON_GetArrayItem(array, i);
        if (!item)
            continue;

        RecipeSuggestion r;
        r.name = safeJsonString(item, "name");
        r.url = safeJsonString(item, "url");
        r.description = safeJsonString(item, "description");
        int prepTime = safeJsonInt(item, "prep_time", 0);
        if (prepTime > 0)
        {
            r.totalTime = std::to_string(prepTime) + " mins";
        }
        else
        {
            r.totalTime = "";
        }
        r.difficulty = safeJsonString(item, "difficulty");
        // Default values for other fields
        r.imageUrl = "";
        r.recipeSource = "ai-deepseek";
        r.author = "";
        r.ratingValue = 0.0;
        r.ratingCount = 0;
        r.isPremium = false;
        r.contentType = "recipe";
        r.id = "";

        suggestions.push_back(r);
    }

    cJSON_Delete(array);
    return suggestions;
}

// Global instance
DeepSeekAIService deepSeekAIService;