#include "RecipeService.h"

#include "SupabaseClient.h"

#include "cJSON.h"
#include "esp_log.h"

#include <cstdio>
#include <utility>

static const char *TAG = "RECIPES";

namespace
{

std::string jsonString(cJSON *obj, const char *key)
{
    if (!obj)
        return "";
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(value) && value->valuestring)
        return value->valuestring;
    return "";
}

// Some rows use the capitalised names from the import tooling.
std::string firstString(cJSON *obj, const char *lower, const char *upper)
{
    std::string value = jsonString(obj, lower);
    return value.empty() ? jsonString(obj, upper) : value;
}

int jsonInt(cJSON *obj, const char *key, int fallback = 0)
{
    if (!obj)
        return fallback;
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(value) ? (int)value->valuedouble : fallback;
}

// The database stores ingredient quantity as a JSON number; the firmware
// carries display text. %g trims trailing zeros (1, 0.5, 1.5).
std::string scalarText(cJSON *value)
{
    if (cJSON_IsString(value) && value->valuestring)
        return value->valuestring;
    if (cJSON_IsNumber(value))
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", value->valuedouble);
        return buffer;
    }
    return "";
}

std::vector<std::string> stringArray(cJSON *value)
{
    std::vector<std::string> out;
    if (!cJSON_IsArray(value))
        return out;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, value)
    {
        if (cJSON_IsString(item) && item->valuestring)
            out.emplace_back(item->valuestring);
    }
    return out;
}

std::string joinParts(const std::string &quantity, const std::string &unit,
                      const std::string &name)
{
    std::string out;
    for (const std::string *part : {&quantity, &unit, &name})
    {
        if (part->empty())
            continue;
        if (!out.empty())
            out += ' ';
        out += *part;
    }
    return out;
}

Ingredient parseIngredient(cJSON *item)
{
    Ingredient ingredient;
    if (cJSON_IsString(item) && item->valuestring)
    {
        ingredient.name = item->valuestring;
        ingredient.rawIngredient = item->valuestring;
        return ingredient;
    }
    if (!cJSON_IsObject(item))
        return ingredient;

    ingredient.name = firstString(item, "name", "Name");
    ingredient.quantity = scalarText(
        cJSON_GetObjectItemCaseSensitive(item, "quantity"));
    ingredient.unit = firstString(item, "unit", "Unit");
    ingredient.rawIngredient = firstString(item, "rawIngredient", "RawIngredient");
    if (ingredient.rawIngredient.empty())
        ingredient.rawIngredient = joinParts(ingredient.quantity,
                                             ingredient.unit, ingredient.name);
    return ingredient;
}

std::vector<IngredientSection> parseIngredientSections(cJSON *row)
{
    std::vector<IngredientSection> sections;
    cJSON *value = cJSON_GetObjectItemCaseSensitive(row, "ingredients");
    if (!value)
        return sections;

    cJSON *parsed = nullptr;
    cJSON *array = value;

    if (cJSON_IsArray(value) && cJSON_GetArraySize(value) == 1 &&
        cJSON_IsString(cJSON_GetArrayItem(value, 0)))
    {
        parsed = cJSON_Parse(cJSON_GetArrayItem(value, 0)->valuestring);
        if (!cJSON_IsArray(parsed))
        {
            cJSON_Delete(parsed);
            return sections;
        }
        array = parsed;
    }
    else if (cJSON_IsString(value) && value->valuestring)
    {
        parsed = cJSON_Parse(value->valuestring);
        if (!cJSON_IsArray(parsed))
        {
            cJSON_Delete(parsed);
            return sections;
        }
        array = parsed;
    }

    if (cJSON_IsArray(array))
    {
        cJSON *section = nullptr;
        cJSON_ArrayForEach(section, array)
        {
            if (!cJSON_IsObject(section))
                continue;

            IngredientSection out;
            out.name = firstString(section, "name", "Name");

            cJSON *items = cJSON_GetObjectItemCaseSensitive(section, "items");
            if (!cJSON_IsArray(items))
                items = cJSON_GetObjectItemCaseSensitive(section, "Items");
            if (cJSON_IsArray(items))
            {
                cJSON *item = nullptr;
                cJSON_ArrayForEach(item, items)
                {
                    Ingredient parsed_item = parseIngredient(item);
                    if (!parsed_item.name.empty() ||
                        !parsed_item.rawIngredient.empty())
                        out.items.push_back(std::move(parsed_item));
                }
            }

            if (!out.name.empty() || !out.items.empty())
                sections.push_back(std::move(out));
        }
    }

    cJSON_Delete(parsed);
    return sections;
}

std::vector<InstructionSection> parseInstructionSections(cJSON *row)
{
    std::vector<InstructionSection> sections;
    cJSON *value = cJSON_GetObjectItemCaseSensitive(row, "method_steps");
    if (!value)
        return sections;

    cJSON *parsed = nullptr;
    cJSON *array = value;
    std::string raw_fallback;
    bool parse_failed = false;

    if (cJSON_IsArray(value) && cJSON_GetArraySize(value) == 1 &&
        cJSON_IsString(cJSON_GetArrayItem(value, 0)))
    {
        raw_fallback = cJSON_GetArrayItem(value, 0)->valuestring;
        parsed = cJSON_Parse(raw_fallback.c_str());
        if (cJSON_IsArray(parsed))
        {
            array = parsed;
        }
        else
        {
            cJSON_Delete(parsed);
            parsed = nullptr;
            parse_failed = true;
        }
    }
    else if (cJSON_IsString(value) && value->valuestring)
    {
        raw_fallback = value->valuestring;
        parsed = cJSON_Parse(raw_fallback.c_str());
        if (cJSON_IsArray(parsed))
        {
            array = parsed;
        }
        else
        {
            cJSON_Delete(parsed);
            parsed = nullptr;
            parse_failed = true;
        }
    }

    if (parse_failed)
    {
        // Mirrors the app: a row that is not section JSON still shows its
        // text as one step instead of an empty Method tab.
        if (!raw_fallback.empty())
            sections.push_back(InstructionSection{"", {raw_fallback}});
        return sections;
    }

    if (cJSON_IsArray(array))
    {
        int count = cJSON_GetArraySize(array);
        if (count > 0)
        {
            bool all_strings = true;
            cJSON *item = nullptr;
            cJSON_ArrayForEach(item, array)
            {
                if (!cJSON_IsString(item))
                {
                    all_strings = false;
                    break;
                }
            }
            if (all_strings)
            {
                InstructionSection out;
                cJSON_ArrayForEach(item, array)
                    out.steps.emplace_back(item->valuestring);
                sections.push_back(std::move(out));
                cJSON_Delete(parsed);
                return sections;
            }
        }

        cJSON *section = nullptr;
        cJSON_ArrayForEach(section, array)
        {
            if (!cJSON_IsObject(section))
                continue;

            InstructionSection out;
            out.name = firstString(section, "name", "Name");

            cJSON *steps = cJSON_GetObjectItemCaseSensitive(section, "steps");
            if (!steps)
                steps = cJSON_GetObjectItemCaseSensitive(section, "Steps");
            if (cJSON_IsString(steps) && steps->valuestring)
            {
                out.steps.emplace_back(steps->valuestring);
            }
            else if (cJSON_IsArray(steps))
            {
                cJSON *step = nullptr;
                cJSON_ArrayForEach(step, steps)
                {
                    if (cJSON_IsString(step) && step->valuestring)
                        out.steps.emplace_back(step->valuestring);
                }
            }

            if (!out.name.empty() || !out.steps.empty())
                sections.push_back(std::move(out));
        }
    }

    cJSON_Delete(parsed);
    return sections;
}

Recipe parseRecipe(cJSON *row)
{
    Recipe recipe;
    recipe.id = jsonString(row, "id");
    recipe.name = jsonString(row, "name");
    recipe.url = jsonString(row, "url");
    recipe.imageUrl = jsonString(row, "image_url");
    recipe.imageUrlBig = jsonString(row, "image_url_big");
    recipe.description = jsonString(row, "description");
    recipe.cuisine = jsonString(row, "cuisine");
    recipe.difficulty = jsonString(row, "difficulty");
    recipe.totalTime = jsonString(row, "total_time");
    recipe.prepTime = jsonString(row, "prep_time");
    recipe.cookTime = jsonString(row, "cook_time");
    recipe.recipeSource = jsonString(row, "recipe_source");
    recipe.serving = jsonInt(row, "serving");
    recipe.cookbookIds = stringArray(
        cJSON_GetObjectItemCaseSensitive(row, "cookbook_ids"));
    recipe.ingredients = parseIngredientSections(row);
    recipe.methodSteps = parseInstructionSections(row);
    return recipe;
}

} // namespace

RecipeService::RecipeService() : _client(getSupabaseClient())
{
}

bool RecipeService::fetchSummaries(std::vector<RecipeSummary> &out)
{
    out.clear();

    int status = -1;
    std::string body = _client.select(
        "recipes", "select=id,image_url,cookbook_ids&order=name.asc", status);
    if (status != 200)
    {
        ESP_LOGE(TAG, "recipe summaries fetch failed: status=%d", status);
        return false;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!cJSON_IsArray(root))
    {
        ESP_LOGE(TAG, "recipe summaries response is not an array");
        cJSON_Delete(root);
        return false;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (!cJSON_IsObject(item))
            continue;

        RecipeSummary summary;
        summary.id = jsonString(item, "id");
        if (summary.id.empty())
            continue;

        summary.imageUrl = jsonString(item, "image_url");
        summary.cookbookIds = stringArray(
            cJSON_GetObjectItemCaseSensitive(item, "cookbook_ids"));
        out.push_back(std::move(summary));
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Fetched %u recipe summaries", (unsigned)out.size());
    return true;
}

bool RecipeService::fetchForCookbook(const std::string &cookbookId,
                                     std::vector<Recipe> &out)
{
    out.clear();
    if (cookbookId.empty())
        return false;

    // Braces must be percent-encoded or esp_http_client truncates the query.
    std::string filters =
        "cookbook_ids=cs." +
        SupabaseClient::urlEncode("{" + cookbookId + "}") +
        "&select=*&order=name.asc";

    int status = -1;
    std::string body = _client.select("recipes", filters, status);
    if (status != 200)
    {
        ESP_LOGE(TAG, "cookbook recipes fetch failed: status=%d", status);
        return false;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!cJSON_IsArray(root))
    {
        ESP_LOGE(TAG, "cookbook recipes response is not an array");
        cJSON_Delete(root);
        return false;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (!cJSON_IsObject(item))
            continue;

        Recipe recipe = parseRecipe(item);
        if (recipe.id.empty())
            continue;
        out.push_back(std::move(recipe));
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Fetched %u recipes for cookbook %.36s", (unsigned)out.size(),
             cookbookId.c_str());
    return true;
}

RecipeService recipeService;
