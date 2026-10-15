# KitchenAssistant firmware — Appwrite to Supabase migration

Date: 2026-10-15
Status: approved for planning

## Goal

Move the ESP32-P4 firmware off Appwrite and onto the same Supabase project the
Android app already uses, so the kitchen display shows the same inventory and
cookbook data as the phone. Reduce the device's feature set to products,
cookbooks, the recipes inside a cookbook, and recipe detail.

## Non-goals

- No changes to the Android app or its backend.
- No changes to `main/ui/` generated files (`screens.c`, `styles.c`, `images.c`,
  `ui.c`). `main/ui/actions.cpp` is the designated manual-edit file and may
  change.
- No recipe search, no AI recipe generation.
- No build or flash from this work. The user builds and flashes manually.

## Decisions

| # | Decision | Rationale |
|---|---|---|
| 1 | Firmware authenticates with the Supabase **service-role key** and a pinned `SUPABASE_USER_ID` in `secrets.h` | Every table's RLS is `user_id = auth.uid()`, and all 82 accounts are anonymous with no email or password, so the device has no credential it could sign in with. Chosen over creating a password for the account (touches a live account) or an edge-function proxy (more work). |
| 2 | Pinned account: **RoastedMaple**, `user_id = c92ebb7f-457e-4d46-a9e8-ed7f7a67e7b2` | The single account the device mirrors. |
| 3 | **Drop recipe search.** No port of the `bbcgoodfood` / `giallozafferano` / `aniagotuje` scrapers | Decision made explicitly; the scraper source exists only inside Appwrite. |
| 4 | Recipe detail **matches the app**: hero image, meta card, Ingredients + Method tabs | The app has no phase concept. |
| 5 | Delete the RECIPE_PHASE screen and its step-progress wizard | Follows from decision 4. |
| 6 | **Delete all AI code** — DeepSeek, Leonardo, create-recipe panel, generate buttons | Device shows only what the phone has synced. |
| 7 | Thumbnails **download the URL directly** | The `image_url` / `image_url_big` columns already hold downscaled variants, so the server-side resize function is unnecessary. |
| 8 | Collapse `RecipeSuggestion` and `Favorite` into one `Recipe` struct | `ui_extensions_favourites.cpp:100-111` already converts between them purely to call `showRecipeDetailScreen`. With suggestions gone the second type has no reason to exist. |

## Verified backend facts

Project URL: `https://ubsqrktgvlxzaujksran.supabase.co`
REST base: `https://ubsqrktgvlxzaujksran.supabase.co/rest/v1`

### Tables

All RLS-enabled, all policies `user_id = auth.uid()` (or `id = auth.uid()` for
`user_profiles`, which the firmware does not touch).

| Table | Columns used by the firmware |
|---|---|
| `products` | `id` uuid, `user_id` uuid, `name` text, `quantity` int, `category` text, `expiry` **date** (nullable), `frozen` bool, `barcode` text (nullable), `created_at`, `updated_at` |
| `barcodes` | `id` uuid, `user_id` uuid, `barcode` text, `name` text, `category` text, `image` text (nullable), `created_at`, `updated_at` |
| `cookbooks` | `id` uuid, `user_id` uuid, `name` text, `created_at`, `updated_at` |
| `recipes` | `id` uuid, `user_id` uuid, `url` text, `name` text, `image_url` text, `image_url_big` text, `description` text, `cuisine` text, `difficulty` text, `recipe_source` text, `cookbook_ids` **text[]**, `total_time` text, `prep_time` text, `cook_time` text, `ingredients` text[], `method_steps` text[], `serving` int, `created_at`, `updated_at` |

`updated_at` is maintained by a `set_updated_at` trigger on every one of these
tables. The firmware never sends it.

`products.user_id` has **no default**. The client injects it on insert.

### Stored JSON shapes

`ingredients` and `method_steps` are single-element `text[]` columns whose one
element is a JSON string:

```
ingredients[1]  = '[{"name":"For the sauce","items":[
                      {"name":"olive oil","quantity":1,"unit":"tsp",
                       "rawIngredient":"1 tsp olive oil"}]}]'

method_steps[1] = '[{"name":"Make the sauce",
                     "steps":["Heat the oil","Add garlic"]}]'
```

TypeScript counterparts in the app (`recipe-import/models.ts`):

```ts
interface IngredientSection { name?: string; items: Ingredient[] }
interface Ingredient { name: string; quantity?: number; unit?: string;
                       rawIngredient?: string; rawQuantity?: string }
interface InstructionSection { name?: string; steps: string[] }
```

The app's parse accepts three shapes: the single-element JSON string above, a
plain array of sections, and a bare string. Its fallback when JSON parsing
fails is `[{ steps: [rawString] }]`. The firmware must handle at least the first
shape and the fallback; matching all three costs little and avoids blank screens
on rows written by older app versions.

### `cookbook_ids`

A `text[]` of bare uuid strings, e.g. `["2db01766-5689-474e-8b99-504902d21381"]`.
"Recipes in cookbook X" is `cookbook_ids=cs.{<uuid>}`.

## Architecture

```
LVGL UI (main/ui — generated, untouched)
    |
ui_extensions_*.cpp          rendering + parsing only
    |
Managers                     ProductsManager, FavouritesManager, CookbookManager
    |                        (mutex-guarded caches, unchanged roles)
Services                     ProductService, FavouriteService, CookbookService
    |
SupabaseClient               base URL, headers, pinned user_id, PostgREST helpers
    |
esp_http_client  ->  PostgREST  /rest/v1
```

The concurrency model is unchanged: a FreeRTOS background task fetches and
writes through the managers under mutex; the LVGL tick at ~100 Hz reads under
mutex.

## SupabaseClient

```cpp
class SupabaseClient {
public:
    SupabaseClient(const std::string &baseUrl,
                   const std::string &serviceKey,
                   const std::string &userId,
                   int timeoutMs = 30000);

    // GET  /rest/v1/{table}?{filters}   — user_id filter always prepended
    std::string select(const std::string &table,
                       const std::string &filters, int &status) const;

    // POST /rest/v1/{table}             — user_id injected into the body
    // Prefer: return=representation     — response is the created row
    std::string insert(const std::string &table,
                       const std::string &jsonBody, int &status) const;

    // PATCH /rest/v1/{table}?{filters}  — user_id filter always appended
    std::string update(const std::string &table, const std::string &filters,
                       const std::string &jsonBody, int &status) const;

    // DELETE /rest/v1/{table}?{filters} — user_id filter always appended
    int remove(const std::string &table, const std::string &filters) const;

    static std::string urlEncode(const std::string &s);

private:
    std::string _baseUrl, _serviceKey, _userId;
    int _timeoutMs;

    std::string request(const char *method, const std::string &url,
                        const std::string &body, int &status) const;
};
```

Headers on every request:

```
apikey:        <serviceKey>
Authorization: Bearer <serviceKey>
Content-Type:  application/json      (requests with a body)
Prefer:        return=representation (insert; also applied to update so the
                                      caller can confirm the write)
```

Two safety properties, both enforced inside the client rather than at call sites:

- **Every insert carries `user_id`.** The client parses the body with cJSON,
  adds `user_id` if absent, and re-serialises.
- **Every update and delete carries `user_id=eq.<uuid>`.** Filters passed by
  callers are combined with it. A stale or wrong row id therefore cannot read
  or write another account's row.

An `Accept: application/vnd.pgrst.object+json` single-object mode is deliberately
**not** used: a missing row returns HTTP 406 with that header, which is a trap
for `maybeSingle`-style lookups. Callers parse arrays and take element 0.

Connection reuse: `_timeoutMs` and keep-alive are configured once. A screen load
issues several small REST calls in a burst; today each one pays a fresh TLS
handshake because `createHttpClient()` builds and tears down a handle per call.

## Data mapping

### Products

| Appwrite | Supabase |
|---|---|
| `GET /tablesdb/{db}/tables/products/rows?queries[n]={"method":"equal",…}` | `GET /rest/v1/products?user_id=eq.<uuid>&<col>=eq.<v>` |
| `{"method":"lessThanEqual","attribute":"expiry","values":[…]}` | `expiry=lte.<YYYY-MM-DD>` |
| `{"method":"limit"/"offset"}` | `limit=<n>&offset=<n>` |
| `POST …/rows` body `{rowId, data:{…}}` | `POST /rest/v1/products` body `{name,quantity,category,expiry,frozen,barcode}` |
| `PATCH …/rows/{id}` body `{data:{…}}` | `PATCH /rest/v1/products?id=eq.<uuid>` flat body |
| `DELETE …/rows/{id}` | `DELETE /rest/v1/products?id=eq.<uuid>` |
| response `{"total":n,"rows":[{"$id":…}]}` | response is a bare array; no `total` |
| `expiry` ISO datetime `2026-03-28T00:00:00Z` | `date`, bare `2026-03-28` |

Consequences:

- `extractDatePart()` is deleted. `expiry` arrives as `YYYY-MM-DD` already.
- `getExpiringProducts()` builds the cutoff with `strftime("%Y-%m-%d")` and one
  day of `tm_mday` arithmetic, instead of `%Y-%m-%dT%H:%M:%SZ` strings.
- Pagination: the old loop fetched 25 rows per request and tracked `total` from
  the response body. PostgREST caps a response at the project's max rows
  (1000 by default) and returns no total. The new code loops `limit`/`offset`
  until a short page comes back, with the same safety counter.
- `Product.rowId` is renamed `Product.id`.

### Barcodes

`upsertBarcode()` keeps its current two-step shape: select by `barcode=eq.<v>`
then update or insert. The barcode value must be URL-encoded in the filter.

### Cookbooks and recipes

| Device operation | Supabase |
|---|---|
| `getCookbooks()` | `GET /rest/v1/cookbooks?order=name.asc` |
| `createCookbook(name)` | `POST /rest/v1/cookbooks` body `{name}`, read `id` from the returned row |
| `deleteCookbook(id)` | `DELETE /rest/v1/cookbooks?id=eq.<uuid>` |
| `getFavourites()` | `GET /rest/v1/recipes?order=name.asc` |
| `getFavouritesByCookbook(id)` | `GET /rest/v1/recipes?cookbook_ids=cs.{<uuid>}` |
| `isFavourite(url)` | `GET /rest/v1/recipes?url=eq.<encoded>&limit=1` |
| `addFavourite(recipe)` | `POST /rest/v1/recipes` with the full row |
| `removeFavourite(url)` | `DELETE /rest/v1/recipes?url=eq.<encoded>` |
| `addFavouriteToCookbook(favId, cbId)` | `PATCH /rest/v1/recipes?id=eq.<favId>` body `{"cookbook_ids":[…]}` with the id appended |
| `removeCookbookFromFavourites(cbId)` | `GET /rest/v1/recipes?cookbook_ids=cs.{<uuid>}&select=id,cookbook_ids`, then one `PATCH` per returned row with that id filtered out of its `cookbook_ids`. The select-then-patch loop replaces the current implementation, which does the same thing one document at a time. |

`recipes.url` is not unique in the schema, so `isFavourite` and the toggle path
take element 0 of the result. Recipe detail is served entirely from the row the
list already fetched — no second request, and no `detailsFetched` lazy-load.

### Thumbnails

List cards use `image_url`; the detail hero uses `image_url_big` (falling back to
`image_url` when empty). `thumbnail_manager` downloads the URL directly and
decodes locally with the existing JPEG decoder. `fetch_resized_base64()` and
`APPWRITE_IMAGE_RESIZE_FUNCTION_ID` are deleted.

Risk: a `image_url` that is not actually a small variant could pressure PSRAM
during decode. Mitigation: keep the existing SPIFFS thumbnail cache, and add a
response-size guard that skips decode above a fixed byte budget and shows the
placeholder instead of failing the whole list.

## Models

```cpp
struct Product {
    std::string id, name, category, expiry, barcode;
    int quantity = 0;
    bool frozen = false;
};

struct Cookbook {
    std::string id, name;
    int recipeCount = 0;   // computed from one recipes query, not a request per card
};

struct Ingredient {
    std::string name, quantity, unit, rawIngredient;
};

struct IngredientSection {
    std::string name;
    std::vector<Ingredient> items;
};

struct InstructionSection {
    std::string name;
    std::vector<std::string> steps;
};

struct Recipe {
    std::string id, name, url, imageUrl, imageUrlBig, description;
    std::string difficulty, totalTime, prepTime, cookTime, recipeSource;
    int serving = 0;
    std::vector<std::string> cookbookIds;
    std::vector<IngredientSection> ingredients;
    std::vector<InstructionSection> methodSteps;
};
```

`RecipeSuggestion` and `Favorite` are deleted. `RecipeIngredient`,
`RecipePhase`, `RecipeImageRef` and the whole `Recipe` in
`RecipeStepsAggregationService.h` are deleted with that service.

Ingredient display text is built as `rawIngredient` when present, otherwise
`quantity + " " + unit + " " + name` — the device renders strings, whereas the
app keeps the numeric quantity for servings adjustment. `quantity` is a JSON
number in the database and a string in the firmware, so parsing uses
`valuedouble` and formats to a trimmed string.

## File plan

### New

| File | Contents |
|---|---|
| `main/SupabaseClient.h` / `.cpp` | The client described above. |

### Rewritten

| File | Change |
|---|---|
| `main/secrets.h`, `main/secrets.h.example` | `SUPABASE_URL`, `SUPABASE_SERVICE_KEY`, `SUPABASE_USER_ID`. Appwrite, DeepSeek and Leonardo defines removed. |
| `main/models.h` | New model set above. |
| `main/ProductService.{h,cpp}` | PostgREST products + barcodes. `getProducts(const std::vector<std::string>&, int&)` replaced by a typed `ProductFilter` (expiry cutoff, name search, limit/offset). |
| `main/FavouriteService.{h,cpp}` | `recipes` table. Private `createHttpClient`/`httpGet`/`httpPost`/`httpPatch`/`httpDelete`/`urlEncode`/`generateId` deleted; `buildFavouriteJson`/`parseFavouriteFromJson` rewritten for snake_case columns and the section JSON. |
| `main/CookbookService.{h,cpp}` | `cookbooks` table; same private-helper deletion. |
| `main/thumbnail_manager.cpp` | `fetch_resized_base64` deleted; direct download. |
| `main/ui_extensions_recipe_detail.cpp` | Renders `Recipe` sections directly. `recipeDetailService` and `recipeAIDetailService` calls removed. |
| `main/ui_extensions_favourites.cpp` | `Favorite` -> `Recipe`; conversion shim removed. |
| `main/ui_extensions.cpp`, `ui_extensions_products.cpp`, `ui_extensions.h`, `ui_extensions_internal.h` | Signatures updated from `RecipeSuggestion`/`Favorite` to `Recipe`; `populateRecipeList` (suggestions) removed. |
| `main/filters_ui.cpp` | Recipe `source` dropdown and the recipe filter panel removed; product filters kept. |
| `main/ProductsManager.cpp`, `FavouritesManager.cpp`, `CookbookManager.cpp` | Call sites and log strings. |
| `main/ui/actions.cpp` | Handlers listed under "EEZ constraint" reduced to no-ops; deleted-service includes removed. |
| `main/CMakeLists.txt` | Removed sources dropped, `SupabaseClient.cpp` added. |
| `main/app.cpp` | Task wiring unchanged in shape; log strings and removed includes. |
| `README.md`, `CLAUDE.md` | Configuration section, architecture, feature list. |

### Deleted

`AppwriteClientInstance.{h,cpp}`, `HttpClientHelper.{h,cpp}`,
`RecipeService.{h,cpp}`, `RecipeDetailService.{h,cpp}`,
`RecipeStepsAggregationService.{h,cpp}`, `RecipeAIDetailService.{h,cpp}`,
`DeepSeekAIService.{h,cpp}`, `LeonardoImageGenerator.{h,cpp}`,
`RecipeSuggestionsManager.{h,cpp}`, `ui_extensions_recipe_steps.{h,cpp}`,
`ui_extensions_recipes.cpp`, `StepProgressBar.h`.

## EEZ constraint and sequencing

`main/ui/screens.c` is generated and must not be edited. It registers 32
callbacks by pointer, including handlers for the features being removed:

`action_create_recipe_steps_click`, `action_recipe_phase_next`,
`action_recipe_phase_prev`, `action_ingredients_factor`,
`action_generate_ai_recipes_click`, `action_generate_recipe_click`,
`action_create_recipe_close_click`, `action_recipe_suggestion_next`,
`action_recipe_suggestion_prev`, `action_recipes_filter_panel_toggle`,
`action_update_recipes_from_filter_panel`.

Deleting those functions breaks the link, so this work is split:

**Phase 1 (this migration).** Gut the bodies of those handlers to no-ops that log
"feature removed", keeping the symbols. Their underlying services are deleted
because nothing calls into them any more. Build stays green.

**Phase 2 (follow-up, after an EEZ regeneration).** The user removes the Recipes
tab, the RECIPE_PHASE screen, the create-recipe panel and the AI buttons in EEZ
Studio and regenerates. Then the now-unreferenced handlers, the `source`
dropdown wiring, and the unused `objects.*` members are removed.

Phase 2 is tracked separately; it is not part of this migration's definition of
done.

## Verification

The firmware cannot be built or flashed from this work (project rule). The
verification plan is therefore:

**Before flashing — REST contract checks.** Each URL shape the firmware will
issue gets exercised directly against PostgREST with the service key, confirming
status 200 and the expected JSON before any firmware depends on it:

```
GET    /rest/v1/products?user_id=eq.<uuid>&expiry=lte.<YYYY-MM-DD>&select=*
GET    /rest/v1/cookbooks?user_id=eq.<uuid>&order=name.asc
GET    /rest/v1/recipes?user_id=eq.<uuid>&cookbook_ids=cs.{<uuid>}
POST   /rest/v1/products            (with Prefer: return=representation)
PATCH  /rest/v1/products?id=eq.<uuid>&user_id=eq.<uuid>
DELETE /rest/v1/products?id=eq.<uuid>&user_id=eq.<uuid>
```

Each write check runs against a throwaway row that is deleted in the same
session, so no real inventory is touched. The delete is then confirmed with a
select that must return zero rows.

**After flashing — on-device checklist.**

1. Products tab lists all 16 products with correct names, quantities, categories.
2. An expired or today-expiring product shows the correct badge, and `frozen`
   products show the extended date.
3. Add, edit and delete a product; each round-trips and the list refreshes.
4. Barcode upsert: scanning a known barcode updates rather than duplicates.
5. Favourites tab lists cookbook cards; opening one shows exactly that
   cookbook's recipes.
6. Opening a recipe shows hero image, title, time, difficulty, and populated
   Ingredients and Method tabs.
7. Thumbnails render in both lists without PSRAM exhaustion or watchdog resets.
8. A recipe with an empty `method_steps` or malformed section JSON renders an
   empty tab rather than crashing.

## Risks

| Risk | Mitigation |
|---|---|
| The service key in flash grants full read/write to all users' data, and cannot be scoped or rotated without reflashing | Accepted by decision 1. Client-side `user_id` injection on insert/update/delete bounds accidental damage; the key can be rotated in the dashboard at the cost of a reflash. |
| The device and phone write the same rows concurrently | Both write whole-row values, so last-write-wins is the same behaviour the Appwrite version had. No new conflict class. |
| Recipe image URLs may be larger than the old resized payloads | Response-size guard plus the existing SPIFFS thumbnail cache. |
| Rows written by older app versions may hold a different `ingredients` shape | Parser accepts the single-element JSON string, a plain array, and a bare string, mirroring the app. |
| `recipes.url` is not unique, so a lookup can return several rows | Take element 0, matching current toggle behaviour. |

## Out of scope

- Schema changes. The migration adapts the firmware to the existing schema; no
  migration files, no new columns, no policy changes.
- Anything in `KitchenAssistantApp`.
- Restoring recipe search or the phase wizard.
