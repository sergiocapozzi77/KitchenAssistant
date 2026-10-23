# Supabase migration — analysis

Date: 2026-10-23
Companion to: `2026-10-15-supabase-migration-design.md`
Scope: findings and reasoning behind the design. The design document is the
contract; this one records what was measured, what was rejected, and why.

---

## 1. What the firmware does today

### 1.1 Appwrite touchpoints

Every cloud call in the firmware goes through one of three HTTP paths, each of
which reimplements GET/POST/PATCH/DELETE independently.

| Area | Files | Appwrite surface used |
|---|---|---|
| Shared client | `HttpClientHelper.cpp` | `X-Appwrite-Project`, `X-Appwrite-Key`, `executeFunction()` |
| Singleton | `AppwriteClientInstance.cpp` | One `HttpClientHelper` built from `secrets.h` |
| Products | `ProductService.cpp` | `/tablesdb/{db}/tables/products/rows`, query DSL, `$id` |
| Barcodes | `ProductService::upsertBarcode` | `/tablesdb/{db}/tables/barcodes/rows` |
| Cookbooks | `CookbookService.cpp` | own copy of the four HTTP verbs |
| Recipes / favourites | `FavouriteService.cpp` | own copy of the four HTTP verbs, `cookbookIds` |
| Recipe search | `RecipeService.cpp` | `executeFunction(APPWRITE_FUNCTION_ID, {mode:"search"})` |
| Recipe detail | `RecipeDetailService.cpp` | `executeFunction(…, {mode:"detail", url})` |
| Step-by-step | `RecipeStepsAggregationService.cpp` | `executeFunction` async + `pollExecution` on `/executions/{id}` |
| Thumbnails | `thumbnail_manager.cpp` | `executeFunction(APPWRITE_IMAGE_RESIZE_FUNCTION_ID, {url,maxWidth,maxHeight})` |
| AI suggestions | `DeepSeekAIService.cpp` | DeepSeek direct — no Appwrite |
| AI recipe detail | `RecipeAIDetailService.cpp` | DeepSeek direct — no Appwrite |
| AI images | `LeonardoImageGenerator.cpp` | Leonardo direct — no Appwrite |

`APPWRITE_IMAGE_GENERATE_FUNCTION_ID` is defined but never referenced.

### 1.2 Structural observations

- **Three copies of the HTTP verbs.** `HttpClientHelper`, `FavouriteService` and
  `CookbookService` each carry their own `createHttpClient` / `httpGet` /
  `httpPost` / `httpPatch` / `httpDelete` / `urlEncode`. Only
  `HttpClientHelper` has `executeFunction`.
- **Appwrite's query DSL leaks into the API.** `ProductService::getProducts`
  takes `std::vector<std::string> queries` — raw Appwrite JSON query strings —
  and every caller builds them by hand.
- **A fresh TLS handshake per request.** `createHttpClient()` constructs and
  tears down an `esp_http_client` handle for every call.
- **`RecipeSuggestion` and `Favorite` are near-duplicates.**
  `ui_extensions_favourites.cpp:100-111` converts one into the other solely to
  call `showRecipeDetailScreen`.
- **`Favorite` is fetched in full, then re-fetched.** `detailsFetched` drives a
  lazy second request for ingredients and steps that the row already contains.

---

## 2. What the target backend actually is

Project: `kucina`, ref `ubsqrktgvlxzaujksran`.

| Table | Rows (all users) | Columns relevant to the device |
|---|---|---|
| `products` | 21 | `id`, `user_id`, `name`, `quantity`, `category`, `expiry` (date), `frozen`, `barcode` |
| `barcodes` | 72 | `id`, `user_id`, `barcode`, `name`, `category`, `image` |
| `cookbooks` | 14 | `id`, `user_id`, `name` |
| `recipes` | 42 | `id`, `user_id`, `url`, `name`, `image_url`, `image_url_big`, `description`, `difficulty`, `recipe_source`, `cookbook_ids` (text[]), `total_time`, `prep_time`, `cook_time`, `serving`, `ingredients` (text[]), `method_steps` (text[]) |
| `user_profiles` | 53 | not used by the device |

### 2.1 Row-level security

Every policy on the four tables the device needs is user-scoped:

```
products / barcodes / cookbooks / recipes:
  SELECT  USING (user_id = auth.uid())
  INSERT  WITH CHECK (user_id = auth.uid())
  UPDATE  USING (user_id = auth.uid()) WITH CHECK (user_id = auth.uid())
  DELETE  USING (user_id = auth.uid())
```

Consequence: **the anon key alone reads nothing.** Access requires either a
session belonging to the account or a key that bypasses RLS.

`products.user_id` has no default, so every insert must supply it.

### 2.2 Triggers

`set_updated_at` fires on insert and update for `products`, `barcodes`,
`cookbooks`, `recipes` and `user_profiles`. The firmware never needs to send
`updated_at`.

### 2.3 Stored JSON

`ingredients` and `method_steps` are single-element `text[]` columns whose one
element is a JSON string:

```
ingredients[1]  = '[{"name":"For the sauce","items":[
                      {"name":"olive oil","quantity":1,"unit":"tsp",
                       "rawIngredient":"1 tsp olive oil"}]}]'

method_steps[1] = '[{"name":"Make the sauce",
                     "steps":["Heat the oil","Add garlic"]}]'
```

`cookbook_ids` holds bare uuid strings, so membership is `cs.{<uuid>}`.

---

## 3. The pinned account

`RoastedMaple`, `user_id = c92ebb7f-457e-4d46-a9e8-ed7f7a67e7b2`.

| Fact | Value |
|---|---|
| `auth_provider` | `google` |
| `is_anonymous` | `true` |
| Has password | no |
| Has auth email | no |
| GoTrue identities | 1 |
| Products | 16 |
| Recipes | 39 |
| Cookbooks | 12 |
| Barcodes | 68 |

This account is representative of the whole project: of 82 accounts, **all 82
are anonymous and none has an email or a password**. The app signs in
anonymously and keeps a refresh token in Capacitor Preferences. Google sign-in
links the Google identity to the anonymous user through `user_profiles`
(`auth_provider`, `auth_id`) and the `merge-accounts` edge function — it does not
create email-based credentials.

**Therefore there is no credential the firmware could present to become this
user.** That is the constraint the entire authentication decision follows from.

---

## 4. How the Android app works (reference implementation)

| Concern | App behaviour |
|---|---|
| Client | `SupabaseClientService` — one `createClient(url, anonKey)` with `persistSession`, `autoRefreshToken` |
| Auth | anonymous sign-in, refresh token in Preferences, optional Google link at profile level |
| Products | `SupabaseProductsService` → `from('products')`, RLS does the filtering |
| Cookbooks | `SupabaseCookbookService` → 30 s cache, counts, preview images |
| Recipes | `SupabaseRecipesService` → `from('recipes')`, parses the JSON-in-text[] shape |
| Searching recipes | **local** — `recipe-search.page.ts` loads saved recipes and scores them against pantry products with `scoreRecipe` / `matchIngredient` |
| Importing recipes | client-side extractors over fetched HTML (JSON-LD, Next data, microdata, generic), then `deepseek-enrich` for gaps |
| Recipe detail | one scrolling page: hero, meta, ingredient sections, method sections. **No phase wizard** |
| Storage | bucket `images` for uploaded photos |

Two things follow:

1. The app's recipe search needs **no external scraper**. The ESP32's
   three-site scraper is a device-only feature.
2. The app already stores everything the device's detail screen needs. The
   `stepbystep` collection, the async scrape and the execution polling are all
   redundant against the app's data.

---

## 5. The identity problem, and the options rejected

| Option | Why not chosen |
|---|---|
| **Pairing code** — app shows a code, device redeems it for its own session | Correct and revocable, but needs a new edge function plus a new screen in the Android app. Not taken. |
| **Refresh token pasted into `secrets.h`** | Supabase rotates refresh tokens per session; two devices sharing one token intermittently sign each other out. |
| **Separate device account** | The device gets its own `user_id`, so it would show a different inventory from the phone. Contradicts the goal. |
| **Create an email+password for RoastedMaple** | Would make a scoped credential possible and keep RLS intact, but modifies a live account and risks disturbing the phone's session. |
| **Service key + pinned `user_id`** | **Chosen.** No account changes, least firmware code. The key is in flash and bypasses RLS. |

### 5.1 What the chosen option costs

The service key grants full read/write to every table for all 82 accounts. It
cannot be scoped down; rotating it requires reflashing. The design bounds
*accidental* damage inside `SupabaseClient` — `user_id` is injected on insert
and appended to every update and delete — but it does not bound deliberate
extraction of the key from the device.

---

## 6. Server-side function dependencies

Appwrite supplies four function IDs. Two are live:

- **`APPWRITE_FUNCTION_ID`** serves three modes: `search` (three site scrapers),
  `detail` (`{url}` → ingredients/steps), and step aggregation (async, polled for
  up to 6 minutes, caching into the `stepbystep` collection keyed by
  `sourceUrl`).
- **`APPWRITE_IMAGE_RESIZE_FUNCTION_ID`** fetches an external image, downscales
  it and returns base64.

`APPWRITE_IMAGE_GENERATE_FUNCTION_ID` is empty and unused; Leonardo is called
directly.

**The source for these functions is not in either repository.** Porting them
means rewriting the scrapers from scratch, so "port" is not a mechanical move.

### 6.1 Decisions

- **Search: not ported.** Explicit decision. The three source options in the
  recipe filter become dead, leaving `ai-deepseek` as the only source — and the
  AI path is then deleted too, so the Recipes tab has no source at all.
- **Resize: not ported.** `image_url` and `image_url_big` already hold
  downscaled variants, so the device downloads the URL directly and decodes
  locally with the decoder it already has.
- **Detail and step aggregation: not ported.** They only ever ran for
  site-scraped suggestions. With search gone, the remaining recipes are the
  user's saved ones, which already carry `ingredients` and `method_steps`.
  `RecipeDetailService` and `RecipeStepsAggregationService` are deleted.

Net result: the firmware ends up with **no Appwrite dependency at all**.

---

## 7. Feature scope

Kept: products (incl. barcodes, expiry badges), cookbooks, the recipes inside a
cookbook, recipe detail rendered like the app.

Deleted: recipe search, the suggestions screen, the phase wizard, all AI
(DeepSeek suggestions, AI recipe detail, Leonardo images, create-recipe panel,
generate buttons).

The UI already implements the kept flow — `ui_extensions.h:25-26` exposes
`populateCookbooksList()` / `showCurrentPageCookbooks()`; clicking a cookbook
sets `g_activeCookbookId` and drills into `getFavouritesByCookbook()`; clicking a
recipe opens `showRecipeDetailScreen()`. Nothing new is needed in LVGL.

---

## 8. Consequences worth flagging

1. **The key is a master credential.** See 5.1. Accepted trade-off; the
   alternative that preserves RLS costs app changes or a live-account edit.
2. **Phase 1 leaves the Recipes tab visible but inert.** `main/ui/screens.c` is
   generated and registers 32 callbacks by pointer, including the search, phase
   and AI handlers. Deleting those functions breaks the link, so their bodies
   become no-ops and the services underneath are deleted. The tab, the phase
   screen and the AI panels stay on screen until an EEZ Studio regeneration — an
   explicit Phase 2, not a hidden gap.
3. **`updated_at` needs no client handling** — triggers maintain it (2.2).
4. **Favourites remain unfiltered by cookbook.** `getFavourites()` returns all
   saved recipes and cookbooks are a drill-down, matching today's behaviour. If
   the device should only ever show recipes that belong to a cookbook, that is
   a one-line filter (`cookbook_ids=not.is.null`) and a different empty state.
5. **Thumbnails grow.** The old path downloaded server-resized payloads; the new
   path downloads `image_url` directly. Mitigated with a response-size guard and
   the existing SPIFFS cache, but it is a real change in PSRAM pressure during
   list scrolling.
6. **`recipes.url` is not unique**, so lookups take element 0, matching the
   current favourite-toggle behaviour.
7. **Three HTTP implementations collapse into one.** `HttpClientHelper` and the
   private helpers in `FavouriteService` / `CookbookService` are deleted in
   favour of `SupabaseClient`.

---

## 9. Open items

- **Phase 2 (EEZ).** Remove the Recipes tab, the RECIPE_PHASE screen, the
  create-recipe panel and the AI buttons in EEZ Studio; regenerate; then delete
  the remaining no-op handlers in `actions.cpp`. Not part of this migration's
  definition of done.
- **REST contract checks before flashing.** PostgREST URL shapes are exercised
  directly (including a throwaway write/read/delete round-trip) before the
  firmware depends on them, because the firmware cannot be built here.
