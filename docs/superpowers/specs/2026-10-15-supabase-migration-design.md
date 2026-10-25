# KitchenAssistant firmware — Supabase migration and app-like UI

Date: 2026-10-15, revised 2026-10-25
Status: draft — awaiting review

## Goal

Move the ESP32-P4 firmware off Appwrite and onto the same Supabase project the
Android app already uses, and replace the EEZ-generated UI with a hand-written
LVGL UI that follows the app's layout: an Inventory page, a Cookbooks page, a
cookbook detail page, and recipe detail. The device mirrors one account
(RoastedMaple) and shows the same inventory and cookbooks as the phone.

## Non-goals

- No changes to the Android app or its backend.
- No recipe search, no AI recipe generation, no recipe import.
- No product creation on the device. Products can be edited and deleted only.
- No cookbook management on the device (no create, rename, or delete).
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
| 9 | **Replace the EEZ Studio output with a hand-written LVGL UI.** `main/ui/screens.c`, `styles.c` and the EEZ runtime indirection are deleted; screens are constructed in C++ | EEZ Studio is no longer used. The generated file is 2 682 lines with an `obj0..obj45` handle numbering scheme and 32 pointer-registered callbacks; three of its five screens/widgets are to be deleted. Hand-writing the remaining two screens plus the product modal is smaller, styleable, and maintainable. |
| 10 | Follow the app's **light Material 3 palette** | The app ships both a dark default and a light theme. Light was chosen for a kitchen display: the LCD sits in a bright room and #F8F6FC is more legible there than #0D0D2B. |
| 11 | **Three tabs — Inventory, Cookbooks, Settings** — rendered by an `lv_tabview` docked at the bottom | Matches the app's navigation. The Recipes tab is removed with search; the Favourites tab becomes Cookbooks. |
| 12 | Cookbook covers show **the first recipe's image**, not a photo mosaic | Explicit instruction. The mosaic is the one place the device deliberately departs from the app's card design. |
| 13 | Cookbooks and recipes are **read-only** on the device | Every recipe already belongs to a cookbook, so the device never needs to create, import, move, or delete recipes. Cookbook management stays on the phone. |
| 14 | The Inventory list is a **single column of collapsible category groups**, replacing the category sidebar | Matches the app. On an 800 px-wide screen the sidebar's fast switching is no longer worth the width it consumes, and keeping it would be the one structural difference on the page the user asked to mirror. |
| 15 | Recipe **bodies load when a cookbook is opened**, not up front. The cookbook grid is built from one light metadata query (`id`, `image_url`, `cookbook_ids`) | The device never shows a recipe outside a cookbook, so holding all 119 kB of recipe text (39 rows, ingredient and method JSON included) serves no screen. The metadata query is ~10 kB and still yields every count and cover in a single request; full rows are fetched per cookbook, 1–7 rows at a time, and cached. |

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

### Account shape

Measured on RoastedMaple, 2026-10-25:

| Fact | Value | Consequence |
|---|---|---|
| Products | 16 | One page; pagination is still implemented because the schema has no cap |
| Cookbooks | 12 | All non-empty; recipe counts 1–7 |
| Recipes | 39 | **All 39 belong to at least one cookbook**, so dropping the Recipes tab loses nothing |
| Recipe images | all 39 have `image_url` | A cookbook cover always resolves |
| Recipe payload size | 9.7 kB as metadata (`id`, `name`, `image_url`, `cookbook_ids`); 119 kB with bodies, largest section JSON 6.1 kB | The cookbook grid fetches metadata only; bodies load per cookbook (decision 15) |

## Architecture

```
main/ui/            hand-written LVGL: screens, theme, actions, modal
    |
ui_extensions_*.cpp      list/grid builders: rows, cards, detail sections
    |
Managers                 ProductsManager, RecipesManager, CookbooksManager
    |                    (mutex-guarded caches; same concurrency roles)
Services                 ProductService, RecipeService, CookbookService
    |
SupabaseClient           base URL, headers, pinned user_id, PostgREST helpers
    |
esp_http_client  ->  PostgREST  /rest/v1
```

The concurrency model is unchanged: a FreeRTOS background task fetches and
writes through the managers under mutex; the LVGL tick at ~100 Hz reads under
mutex. The UI still builds widgets only from the LVGL task, yielding the lock
every few rows on long lists.

`FavouritesManager` becomes `RecipesManager` and `CookbookManager` becomes
`CookbooksManager`; "favourite" stops being a concept once search is gone.

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

The device always fetches the whole inventory (16 rows) and filters on the
device — the app does the same. Consequences:

- `extractDatePart()` is deleted. `expiry` arrives as `YYYY-MM-DD` already.
- `getExpiringProducts()` is deleted: it has no callers, and the Expiring/Expired
  filters are applied in memory.
- `addProduct()` is deleted: the device cannot create products (decision 13).
- Pagination: the old loop fetched 25 rows per request and tracked `total` from
  the response body. PostgREST caps a response at the project's max rows
  (1000 by default) and returns no total. The new code loops `limit`/`offset`
  until a short page comes back, with the same safety counter.
- `Product.rowId` is renamed `Product.id`.

### Barcodes

`upsertBarcode()` keeps its current two-step shape: select by `barcode=eq.<v>`
then update or insert. The barcode value must be URL-encoded in the filter. It
still runs when a product is saved with a barcode, so the phone inherits the
barcode-to-category mapping.

### Cookbooks and recipes

The cookbook grid needs counts and covers; the cookbook detail page needs full
recipe rows. They are two different queries, and the device asks for each only
when the screen that needs it is shown.

| Device operation | Supabase |
|---|---|
| `CookbooksManager::fetch()` | `GET /rest/v1/cookbooks?select=id,name&order=name.asc` |
| `RecipesManager::fetchSummaries()` | `GET /rest/v1/recipes?select=id,image_url,cookbook_ids&order=name.asc` (~10 kB) |
| Cookbook recipe count | count of summaries whose `cookbook_ids` contains the cookbook id |
| Cookbook cover | `image_url` of the **first** summary (name ascending) containing the cookbook id |
| `RecipesManager::fetchForCookbook(id)` | `GET /rest/v1/recipes?cookbook_ids=cs.{<uuid>}&select=*&order=name.asc` |
| Recipes in a cookbook | the full rows from that fetch, cached per cookbook id |
| Refresh (header button) | clears the summary list and every cached cookbook |
| `updateProduct(product)` | `PATCH /rest/v1/products?id=eq.<uuid>` with the flat row |
| `deleteProduct(id)` | `DELETE /rest/v1/products?id=eq.<uuid>` |

`order=name.asc` on the summary query is what makes the cover deterministic:
filtering the ordered summaries per cookbook preserves name order, so "first"
means the same thing on every load.

`createCookbook`, `deleteCookbook`, `getFavouritesByCookbook`, `isFavourite`,
`addFavourite`, `removeFavourite`, `addFavouriteToCookbook` and
`removeCookbookFromFavourites` are all deleted: nothing on the device calls
them (decision 13). `removeCookbookFromFavourites`' select-then-patch loop
disappears with them.

Recipe detail is served entirely from the cached cookbook rows — no second
request, and no `detailsFetched` lazy-load. A recipe that belongs to two
cookbooks is fetched and held twice; there are none in the current data, and
the cache is cleared on refresh.

### Thumbnails

Inventory has no images. Cookbook covers use `image_url` at 366×280; the
cookbook detail list uses `image_url` at 260×200; the recipe hero uses
`image_url_big` at 800×420, falling back to `image_url` when empty.
`thumbnail_manager` downloads the URL directly and decodes locally with the
existing JPEG decoder. `fetch_resized_base64()` and
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
    std::string coverImageUrl;   // from the summary list, empty if none
    int recipeCount = 0;         // from the summary list, not a request per card
};

// Light row for the cookbook grid. No ingredients, no method, no description.
struct RecipeSummary {
    std::string id, imageUrl;
    std::vector<std::string> cookbookIds;
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
    std::string cuisine, difficulty, totalTime, prepTime, cookTime, recipeSource;
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

## UI design

Screen is 800×1280. Sizes below are device pixels; the app's phone layout is
scaled roughly 2×.

### Palette (light Material 3, from the app)

| Token | Hex | Use |
|---|---|---|
| `PAGE_BG` | `#F8F6FC` | Screen background |
| `SURFACE` | `#FFFFFF` | Modal, cards |
| `SURFACE_HIGH` | `#F0EDF7` | Pills, recipe cards |
| `SURFACE_HIGHEST` | `#E8E4F0` | Active chip, icon-button fill |
| `PRODUCT_ROW` | `#EEEBF5` | Product row background |
| `FILTER_BG` | `#F5F2FC` | Chip group background |
| `FILTER_STROKE` | `#D8D0E8` | Chip group border |
| `CARD_BORDER` | `#E5DCF2` | Recipe/cookbook card border (accent at 15 % over white) |
| `TEXT_PRIMARY` | `#1A1A3E` | Titles, names |
| `TEXT_SECONDARY` | `#4A4A6A` | Pill labels |
| `TEXT_MUTED` | `#8888AA` | Counts, sources, placeholders |
| `ACCENT` | `#7C5CBF` | Active tab, focus border, Save button |
| `ERROR_SURFACE` | `#FFE8E8` | Expired badge / delete button |
| `ERROR_TEXT` | `#CC3333` | Expired badge / delete icon |
| `TODAY_SURFACE` | `#FFE0E0` | Today badge |
| `TODAY_TEXT` | `#CC3333` | Today badge |
| `DAYSLEFT_SURFACE` | `#FFF5E0` | "N d left" badge |
| `DAYSLEFT_TEXT` | `#8B7A00` | "N d left" badge |
| `BADGE_TIME_BG` / `_TEXT` | `#FFF3D6` / `#8B6E00` | Time badge |
| `BADGE_DIFF_BG` / `_TEXT` | `#E8F3E9` / `#2E7D32` | Difficulty badge |
| `BADGE_CUISINE_BG` / `_TEXT` | `#F3E6F5` / `#7B1FA2` | Cuisine badge |

Icons are LVGL's built-in Montserrat symbols (`LV_SYMBOL_*`), not Material
Icons: the generated fonts carry no Material glyphs. This is the second
deliberate visual departure; symbol shapes differ from the app's icons but the
positions and hit areas match.

Fonts available: `ui_font_ext_font_montserrat_18`, `_26`, `_36`, plus LVGL's
built-in 14 and 16. Display titles 36, card titles and row names 26, meta and
secondary text 18.

### Navigation

`lv_tabview` docked at the **bottom**, height 108, three tabs:

| Index | Label | Content |
|---|---|---|
| 0 | Inventory | Product groups |
| 1 | Cookbooks | Cookbook grid |
| 2 | Settings | WiFi status and connect button |

`COOKBOOK_DETAIL` and `RECIPE_DETAIL` are separate screens pushed with
`lv_screen_load_anim(..., LV_SCR_LOAD_ANIM_MOVE_LEFT, 300)`. Back animates
right: `COOKBOOK_DETAIL` → `MAIN` (Cookbooks tab still active),
`RECIPE_DETAIL` → the screen it was opened from.

`app.cpp`'s startup redirect for missing WiFi credentials changes from tab
index 3 to 2. `loadScreen()`'s pointer-arithmetic cast over `objects` is
replaced by an explicit switch, because the struct is no longer positional.
`ui_tick()` stays as an empty function so the main loop is unchanged.

### Screen: Inventory (tab 0)

```
┌──────────────────────────────────────────────┐
│ Your Pantry & Fridge                  [⟳]    │  36 px title, reload 72×72
│ [🔍] [Filter: All] [Sort: A-Z]               │  pills, 64 high
│ ┌ All │ Expiring soon │ Expired ┐            │  chips, only when Filter open
│ ──────────────────────────────────────────── │
│ DAIRY · 4 items                          ⌄   │  group header, tap to collapse
│ ┌──────────────────────────────────────────┐ │
│ │ Milk         ❄        [2d left] [✎] [🗑] │ │  96 high
│ └──────────────────────────────────────────┘ │
│ MEAT · 3 items                           ⌄   │
│ …                                            │
└──────────────────────────────────────────────┘
```

- Header panel is fixed; the list below scrolls (`flex_grow`).
- Search pill expands into a 64-high textarea with placeholder
  "Search products…". The filter is applied at 3 characters or more, as today,
  after a 300 ms debounce timer.
- Filter pill opens the chip group with **All / Expiring soon / Expired**;
  the pill label shows the active filter and gains an accent dot when it is not
  All. Sort pill opens **A-Z / By expiry**.
- Groups are the categories present after filtering, sorted by name, each
  expanded by default; collapse state is kept for the session.
- Product rows show: name (26, ellipsis), frozen icon 40×40, expiry badge,
  edit button, delete button. Row tap opens the edit modal; the delete button
  deletes immediately and shows the snackbar "Deleted".
- Expiry badge follows the app exactly: shown only at **≤ 3 days**. `< 0` →
  "Expired Nd" in error colours; `0` → "Today"; `1–3` → "Nd left" in days-left
  colours. Frozen products add the app's frozen extension to the date.
- Empty state: "No products yet" / "Add items from the Kitchen Assistant app".

### Screen: Cookbooks (tab 1)

```
Cookbooks                                     36 px title, reload 72×72
┌────────────────────┐  ┌────────────────────┐
│                    │  │                    │  cover 366×280, radius 24
│     (first         │  │                    │
│      recipe)       │  │                    │
├────────────────────┤  ├────────────────────┤
│ Seconds Chicken    │  │ BBQ                │  name 26, 2 lines
│ 7 recipes          │  │ 7 recipes          │  count 18 muted
└────────────────────┘  └────────────────────┘
```

- Grid: `LV_FLEX_FLOW_ROW_WRAP`, padding 24, gap 20, giving 366-wide cards.
- Counts and covers come from the summary list, so the grid renders without
  fetching any recipe body.
- Cover is the first recipe's image (decision 12). A cookbook with no recipe
  shows a `SURFACE_HIGH` block with "No recipes" instead. No mosaic.
- Tap opens `COOKBOOK_DETAIL`.
- Empty state: "No cookbooks yet" / "Create one in the app".

### Screen: COOKBOOK_DETAIL

- Top bar: back button 72×72 (`LV_SYMBOL_LEFT`, `SURFACE_HIGH`), cookbook name
  at 36, "N recipes" at 18 muted underneath. The count is known from the
  summary list, so it shows immediately.
- On open, if this cookbook has no cached rows, the body shows a centred
  spinner while `fetchForCookbook(id)` runs. Back stays enabled; a result that
  arrives after leaving is discarded.
- Scrollable list of recipe cards, 752×200, radius 24, `SURFACE_HIGH`
  background, 1 px `CARD_BORDER`: image 260×200 on the left; name at 26
  (2 lines, ellipsis), `recipe_source` at 18 muted, then the time / difficulty /
  cuisine badges at 44 high, 18 font.
- Tap opens `RECIPE_DETAIL`.
- Empty state: "No recipes in this cookbook". Failure state: "Couldn't load
  recipes" with a Retry button; the cached entry is left unset so Retry refetches.

### Screen: RECIPE_DETAIL

Keeps the current structure, restyled and stripped of the favourite and
create-steps controls:

- Hero image 800×420 from `image_url_big`, back button floating top-left.
- Title at 36.
- Meta card: total time and difficulty, each a 18 muted label over a 26 value.
- `lv_tabview` (top tabs, height 92) with **Ingredients** and **Method**.
- Ingredients: section names at 26, items at 18 with the existing checkbox and
  strike-through. Method: section names at 26, numbered steps at 18.
- No favourite add/remove buttons: every recipe is already saved, so the
  toggle has no meaning.

### Modal: product edit

Overlay over `MAIN`: full-screen black at 40 % opacity, card 640 wide,
auto height, radius 24, `SURFACE`, padding 24, column flex, 16 gap.

Fields, all 64 high: name (textarea), expiry (textarea, `YYYY-MM-DD`
placeholder), category (dropdown, the existing 21 categories), quantity
(`−  value  +` stepper), frozen (checkbox). Buttons: Delete (error colours),
Cancel, Save (accent fill, white text).

Quantity is added because it is currently carried in the model and written on
save but has no editor anywhere — the dead `QtyContext` in
`ui_extensions_products.cpp` is the remains of one.

### Settings (tab 2)

`WiFiSettingsUI` is reused unchanged apart from object names and the page
background colour. Not otherwise restyled.

## UI code structure

```
main/ui/
  screens.h        screen ids, trimmed objects_t with named handles, API
  screens.cpp      hand-written construction of all three screens + modal
  ui_theme.h       palette constants, shared lv_style_t, small widget helpers
  ui_theme.cpp
  actions.h        event handler declarations
  actions.cpp      event handlers
  ui.h / ui.c      ui_init, loadScreen switch, empty ui_tick
  fonts.h, images.h/.c, ui_font_*.c, ui_image_*.c   assets, kept
```

`styles.c`/`styles.h`, `structs.h`, `vars.h` and the EEZ build metadata are
deleted with `screens.c`. `objects_t` drops from 162 members to roughly 45
named handles; `obj0..obj45` do not survive. Unused image assets are left in
place rather than culled.

List builders stay in `main/ui_extensions_*.cpp`; they are ordinary C++ now,
not generator-adjacent files.

## File plan

### New

| File | Contents |
|---|---|
| `main/SupabaseClient.{h,cpp}` | The client described above. |
| `main/ui/ui_theme.{h,cpp}` | Palette, shared styles, widget helpers. |
| `main/RecipeService.{h,cpp}` | `recipes` table, replaces `FavouriteService`. |
| `main/RecipesManager.{h,cpp}` | Recipe cache, replaces `FavouritesManager`. |
| `main/CookbooksManager.{h,cpp}` | Cookbook cache plus derived counts and covers, replaces `CookbookManager`. |
| `main/ui_extensions_cookbooks.cpp` | Cookbook grid and cookbook detail list; replaces `ui_extensions_favourites.cpp`. Added in checkpoint B. |

### Rewritten or replaced

| File | Change |
|---|---|
| `main/secrets.h`, `main/secrets.h.example` | `SUPABASE_URL`, `SUPABASE_SERVICE_KEY`, `SUPABASE_USER_ID`. Appwrite, DeepSeek and Leonardo defines removed. |
| `main/models.h` | New model set above. |
| `main/ProductService.{h,cpp}` | PostgREST products + barcodes; `getProducts(int&)`, `updateProduct`, `deleteProduct`, `upsertBarcode`. |
| `main/ProductsManager.{h,cpp}` | Selection state and `addProduct` paths removed; list callback points at the new builder. |
| `main/CookbookService.{h,cpp}` | Read-only `cookbooks` fetch; private HTTP helpers deleted. |
| `main/thumbnail_manager.cpp` | `fetch_resized_base64` deleted; direct download with size guard. |
| `main/ui/screens.h`, `main/ui/screens.cpp` | Hand-written screens replacing `screens.c`. |
| `main/ui/actions.{h,cpp}` | Handlers for tabs, pills, chips, rows, modal, back buttons. |
| `main/ui/ui.{h,c}` | Explicit `loadScreen` switch; no-op `ui_tick`. |
| `main/ui_extensions.{h,cpp}` | Shared list helpers, snackbar, spinner; style ownership moves to `ui_theme`. |
| `main/ui_extensions_products.cpp` | Grouped inventory list and product rows. |
| `main/ui_extensions_recipe_detail.cpp` | Renders `Recipe` sections; no service calls. |
| `main/WiFiSettingsUI.cpp` | Object names only. |
| `main/app.cpp` | Tab index 2, includes, log strings. |
| `main/CMakeLists.txt` | Removed sources dropped; new sources added. |
| `README.md`, `CLAUDE.md` | Configuration, architecture, feature list. |

### Deleted

`AppwriteClientInstance.{h,cpp}`, `HttpClientHelper.{h,cpp}`,
`RecipeService.{h,cpp}` (old, replaced by the new one of the same name),
`RecipeDetailService.{h,cpp}`, `RecipeStepsAggregationService.{h,cpp}`,
`RecipeAIDetailService.{h,cpp}`, `DeepSeekAIService.{h,cpp}`,
`LeonardoImageGenerator.{h,cpp}`, `RecipeSuggestionsManager.{h,cpp}`,
`FavouriteService.{h,cpp}`, `FavouritesManager.{h,cpp}`,
`CookbookManager.{h,cpp}`, `filters_ui.{h,cpp}`,
`ui_extensions_recipes.cpp`, `ui_extensions_recipe_steps.{h,cpp}`,
`StepProgressBar.h`, `main/ui/screens.c`, `main/ui/styles.{c,h}`,
`main/ui/structs.h`, `main/ui/vars.h`, `main/ui/.eez-project-build`.

## Sequencing

Three checkpoints. The tree does not compile *between* them; it compiles and
runs at each one. The user can flash after A and B to see progress.

**Checkpoint A — Supabase + Inventory, and everything else deleted.**
Adds `SupabaseClient`, `secrets.h`, the full new model set, the products path
(`ProductService`, `ProductsManager`), `ui_theme`, and a hand-written
`main/ui` holding a two-tab tabview (Inventory, Settings), the inventory list
and the product modal. Deletes every file that cannot compile without the
generated UI or that belongs to a removed feature: the whole old `main/ui`
generation, `filters_ui`, all search/AI/step services, and the old
favourites/cookbook services and managers. B rewrites the cookbook and recipe
layer from scratch, so nothing is kept dangling.

This is the largest deletion and the smallest addition, which is deliberate:
what remains after A is a small tree whose only cloud path is products.

**Checkpoint B — Cookbooks and recipes.**
`RecipeService`, `RecipesManager`, `CookbookService`, `CookbooksManager`, the
Cookbooks tab, `COOKBOOK_DETAIL`, `RECIPE_DETAIL`, and their list builders.

**Checkpoint C — Documentation and cleanup.**
README, CLAUDE.md, CMake source list, dead includes, unused defines.

## Verification

The firmware cannot be built or flashed from this work (project rule). The
verification plan is therefore:

**Before flashing — REST contract checks.** Each URL shape the firmware will
issue gets exercised directly against PostgREST with the service key, confirming
status 200 and the expected JSON before any firmware depends on it:

```
GET    /rest/v1/products?user_id=eq.<uuid>&order=name.asc&limit=1000
GET    /rest/v1/cookbooks?user_id=eq.<uuid>&select=id,name&order=name.asc
GET    /rest/v1/recipes?user_id=eq.<uuid>&select=id,image_url,cookbook_ids&order=name.asc
GET    /rest/v1/recipes?user_id=eq.<uuid>&cookbook_ids=cs.%7B<uuid>%7D&select=*&order=name.asc
GET    /rest/v1/barcodes?user_id=eq.<uuid>&barcode=eq.<value>
PATCH  /rest/v1/products?id=eq.<uuid>&user_id=eq.<uuid>
DELETE /rest/v1/products?id=eq.<uuid>&user_id=eq.<uuid>
```

The `cs.{}` filter must be percent-encoded in the URL (`%7B`, `%7D`) or
`esp_http_client` truncates it; the check confirms the encoded form returns the
same 1–7 rows per cookbook as the SQL query.

Each write check runs against a throwaway row that is deleted in the same
session, so no real inventory is touched. The delete is then confirmed with a
select that must return zero rows.

**After flashing — on-device checklist.**

1. Inventory lists all 16 products, grouped by category, group headers showing
   `CATEGORY · N items`.
2. Collapsing a group hides its rows; the state survives a filter change.
3. Filter All / Expiring soon / Expired and sort A-Z / By expiry each change
   the list; pill labels reflect the active choice.
4. Typing 3 or more characters filters by name; clearing restores the list.
5. A product expiring today, in 2 days, and one already expired show
   "Today", "2d left" and "Expired Nd" in the right colours; a product expiring
   in 5 days shows no badge.
6. Editing name, expiry, category, quantity and frozen round-trips and the list
   refreshes.
7. Deleting a product removes it and shows the snackbar.
8. Saving a product with a barcode upserts `barcodes`.
9. Cookbooks shows 12 cards in two columns with a distinct cover each, name and
   "N recipes"; counts match the database, and the tab renders without any
   recipe body having been fetched.
10. Opening a cookbook fetches its recipes (spinner first), then shows exactly
    those recipes and the count in its header; reopening it does not refetch.
    Killing WiFi before opening shows the failure state and a working Retry.
11. Opening a recipe shows hero, title, total time, difficulty, and populated
    Ingredients and Method tabs.
12. Back from recipe detail returns to the cookbook; back from the cookbook
    returns to the Cookbooks tab.
13. Thumbnails render in all three places without PSRAM exhaustion or watchdog
    resets.
14. A recipe with empty `method_steps` or malformed section JSON renders an
    empty tab rather than crashing.
15. With no saved WiFi credentials the device opens the Settings tab.

## Risks

| Risk | Mitigation |
|---|---|
| The service key in flash grants full read/write to all users' data, and cannot be scoped or rotated without reflashing | Accepted by decision 1. Client-side `user_id` injection on insert/update/delete bounds accidental damage; the key can be rotated in the dashboard at the cost of a reflash. |
| A full UI rewrite cannot be compiled here, so type and geometry errors surface only on the user's first build | Checkpoint A adds only one screen's content, one list and one modal, and deletes everything else so there is no half-migrated code to reason about. The theme lives in one header, so palette mistakes are a one-file fix. |
| The device and phone write the same rows concurrently | Both write whole-row values, so last-write-wins is the same behaviour the Appwrite version had. No new conflict class. |
| Recipe image URLs may be larger than the old resized payloads | Response-size guard plus the existing SPIFFS thumbnail cache. |
| Rows written by older app versions may hold a different `ingredients` shape | Parser accepts the single-element JSON string, a plain array, and a bare string, mirroring the app. |
| Opening a cookbook now costs a request, so the list appears after a spinner rather than instantly | The fetch is 1–7 rows; the result is cached per cookbook so a revisit is instant, and the header count is already known from the summary list. |
| The summary list can go stale if the phone edits cookbook membership while the device runs | The reload button clears the summary list and every cached cookbook. The all-recipes cache had the same staleness window. |
| LVGL has no Material icon font, so icons differ from the app | Accepted; symbol glyphs are positioned and sized like the app's icons. |

## Out of scope

- Schema changes. The migration adapts the firmware to the existing schema; no
  migration files, no new columns, no policy changes.
- Anything in `KitchenAssistantApp`.
- Restoring recipe search, recipe import, the phase wizard, or AI features.
- Creating cookbooks or products on the device.
- A settings redesign beyond reusing the existing WiFi controls.
