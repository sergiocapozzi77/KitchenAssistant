#include "RecipeAIDetailService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "secrets.h"
#include "esp_crt_bundle.h"
#include <sstream>
#include <iomanip>
#include <cctype>

static const char *TAG = "RecipeAIDetail";

// Safe JSON extraction helpers (similar to other services)
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
    ESP_LOGI(TAG, "DeepSeek POST Response: %s", response.c_str());
    esp_http_client_cleanup(client);
    return response;
}

RecipeAIDetailService::RecipeAIDetailService()
    : _apiKey(DEEPSEEK_API_KEY), _endpoint(DEEPSEEK_ENDPOINT)
{
}

bool RecipeAIDetailService::fetchDetails(RecipeSuggestion &recipe,
                                         const std::vector<std::string> &ingredients,
                                         const filter_state_t *filterState)
{
    ESP_LOGI(TAG, "Fetching AI recipe details for: %s", recipe.name.c_str());

    // Build the prompt
    std::string prompt = buildPrompt(recipe, ingredients, filterState);

    // Build the JSON payload for DeepSeek chat completion
    cJSON *payload = cJSON_CreateObject();
    if (!payload)
    {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return false;
    }

    cJSON_AddStringToObject(payload, "model", "deepseek-chat");
    cJSON_AddNumberToObject(payload, "max_tokens", 3000); // Larger for full recipe
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
        return false;
    }

    std::string url = _endpoint + "/chat/completions";
    int status = 0;
    std::string response = deepSeekHttpPost(url, payloadStr, status, 120000);
    free(payloadStr);

    if (status < 0)
    {
        ESP_LOGE(TAG, "HTTP transport error");
        return false;
    }
    if (status != 200)
    {
        ESP_LOGE(TAG, "DeepSeek API error: HTTP %d, response: %s", status, response.c_str());
        return false;
    }
    if (response.empty())
    {
        ESP_LOGW(TAG, "Empty response from DeepSeek");
        return false;
    }

    // Parse the response and populate recipe details
    return parseAIResponse(response, recipe);
}

std::string RecipeAIDetailService::buildPrompt(const RecipeSuggestion &recipe,
                                               const std::vector<std::string> &ingredients,
                                               const filter_state_t *filterState) const
{
    std::ostringstream oss;
    oss << "You are a recipe generator.\n\n";
    oss << "I need a full recipe for: " << recipe.name << "\n";
    if (!recipe.description.empty())
    {
        oss << "Description: " << recipe.description << "\n";
    }
    oss << "\n";

    oss << "Available ingredients: ";
    if (ingredients.empty())
    {
        oss << "any";
    }
    else
    {
        for (size_t i = 0; i < ingredients.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << ingredients[i];
        }
    }
    oss << "\n";

    if (filterState)
    {
        if (filterState->meal_type && filterState->meal_type[0])
            oss << "- Dish type: " << filterState->meal_type << "\n";
        if (filterState->difficulty && filterState->difficulty[0])
            oss << "- Difficulty: " << filterState->difficulty << "\n";
        if (filterState->total_time && filterState->total_time[0])
            oss << "- Total time: " << filterState->total_time << "\n";
        if (filterState->diet && filterState->diet[0])
            oss << "- Diet: " << filterState->diet << "\n";
        if (filterState->cuisine && filterState->cuisine[0])
            oss << "- Cuisine: " << filterState->cuisine << "\n";
        if (!filterState->keywords.empty())
        {
            oss << "- Keywords: ";
            for (size_t i = 0; i < filterState->keywords.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << filterState->keywords[i];
            }
            oss << "\n";
        }
    }

    oss << "\n";
    oss << "Rules:\n";
    oss << "- Generate a complete recipe with ingredient list and step-by-step method.\n";
    oss << "- Prefer to use as many of the available ingredients as possible.\n";
    oss << "- It is OK to include extra ingredients not listed.\n";
    oss << "- Provide exact quantities for ingredients where possible.\n";
    oss << "- Provide clear, numbered steps for the method.\n";
    oss << "- Estimate preparation time and cooking time, and total time.\n";
    oss << "- Estimate difficulty (easy, medium, hard).\n";
    oss << "- Estimate servings.\n";
    oss << "\n";
    oss << "Output ONLY valid JSON — no explanations, no text before or after.\n";
    oss << "Do not wrap the JSON in markdown code blocks (no ```json or ```).\n";
    oss << "\n";
    oss << "Format:\n";
    oss << "{\n";
    oss << "  \"name\": \"Recipe name\",\n";
    oss << "  \"description\": \"Optional description\",\n";
    oss << "  \"ingredients\": [\"1 cup flour\", \"2 eggs\", ...],\n";
    oss << "  \"methodSteps\": [\"Step 1 description\", \"Step 2 description\", ...],\n";
    oss << "  \"prepTime\": \"15 mins\",\n";
    oss << "  \"cookTime\": \"30 mins\",\n";
    oss << "  \"totalTime\": \"45 mins\",\n";
    oss << "  \"servings\": \"4\",\n";
    oss << "  \"difficulty\": \"easy\"\n";
    oss << "}\n";
    return oss.str();
}

bool RecipeAIDetailService::parseAIResponse(const std::string &jsonResponse, RecipeSuggestion &recipe) const
{
    cJSON *root = cJSON_Parse(jsonResponse.c_str());
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse DeepSeek response JSON");
        return false;
    }

    // DeepSeek response format: {"choices":[{"message":{"content":"..."}}]}
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0)
    {
        ESP_LOGE(TAG, "No choices array in DeepSeek response");
        cJSON_Delete(root);
        return false;
    }

    cJSON *firstChoice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(firstChoice, "message");
    if (!message)
    {
        ESP_LOGE(TAG, "No message in choice");
        cJSON_Delete(root);
        return false;
    }

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!cJSON_IsString(content))
    {
        ESP_LOGE(TAG, "No content string in message");
        cJSON_Delete(root);
        return false;
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

    // Parse the inner JSON object
    cJSON *obj = cJSON_Parse(cleanJson.c_str());
    if (!cJSON_IsObject(obj))
    {
        ESP_LOGE(TAG, "Content is not a JSON object. Cleaned JSON: %s", cleanJson.c_str());
        return false;
    }

    // Extract fields, overwriting existing ones
    recipe.name = safeJsonString(obj, "name");
    recipe.description = safeJsonString(obj, "description");
    recipe.prepTime = safeJsonString(obj, "prepTime");
    recipe.cookTime = safeJsonString(obj, "cookTime");
    recipe.totalTime = safeJsonString(obj, "totalTime");
    recipe.servings = safeJsonString(obj, "servings");
    recipe.difficulty = safeJsonString(obj, "difficulty");

    // ingredients array
    recipe.ingredients.clear();
    cJSON *ings = cJSON_GetObjectItem(obj, "ingredients");
    if (ings && cJSON_IsArray(ings))
    {
        cJSON *item;
        cJSON_ArrayForEach(item, ings)
        {
            if (cJSON_IsString(item) && item->valuestring[0] != '\0')
                recipe.ingredients.emplace_back(item->valuestring);
        }
    }

    // methodSteps array
    recipe.methodSteps.clear();
    cJSON *steps = cJSON_GetObjectItem(obj, "methodSteps");
    if (steps && cJSON_IsArray(steps))
    {
        cJSON *item;
        cJSON_ArrayForEach(item, steps)
        {
            if (cJSON_IsString(item) && item->valuestring[0] != '\0')
                recipe.methodSteps.emplace_back(item->valuestring);
        }
    }

    cJSON_Delete(obj);

    bool ok = !recipe.ingredients.empty() && !recipe.methodSteps.empty();
    ESP_LOGI(TAG, "parseAIResponse: ings=%d steps=%d name='%s'",
             (int)recipe.ingredients.size(),
             (int)recipe.methodSteps.size(),
             recipe.name.c_str());
    return ok;
}

// Global instance
RecipeAIDetailService recipeAIDetailService;