# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

KitchenAssistant is an ESP32-P4 embedded application for kitchen inventory management. It mirrors one Supabase account (`RoastedMaple`): the device lists products, edits and deletes them, and browses the account's cookbooks, their recipes and the full recipe detail. The UI is hand-written C++ on an 800x1280 LCD using LVGL 9.5.

## Build Commands

Requires ESP-IDF v5.5.2 installed at `C:\Users\sergi\esp\v5.5.2\esp-idf`.

```bash
idf.py set-target esp32p4   # one-time setup
idf.py build
idf.py -p COM10 flash
idf.py -p COM10 monitor
idf.py -p COM10 flash monitor  # flash and immediately monitor
idf.py fullclean              # clean all build artifacts
```

WiFi credentials and other settings are configured via:

```bash
idf.py menuconfig  # look under "Kitchen Assistant Configuration"
```

## Architecture

### Layer Stack

```
Hand-written LVGL UI (main/ui)
    ↕
UI Extensions (main/ui_extensions*.cpp) — populates LVGL widgets from data
    ↕
Services (ProductService, CookbookService, RecipeService, WiFiManager)
Managers (ProductsManager, CookbooksManager, RecipesManager)
    ↕
SupabaseClient (PostgREST) + cJSON + FreeRTOS
```

### Key Components

**`main/app.cpp`** — Initialization pipeline and main loop:

1. `initNVS()` → NVS storage
2. `initHardware()` → WiFi, display, LVGL port
3. `initTasks()` → spawns `combinedFetchTask`
4. `mainLoop()` → LVGL tick at ~100Hz

**`main/SupabaseClient.cpp`** — PostgREST client for the `kucina` project. Authenticates with the service-role key and pins `SUPABASE_USER_ID`; every select/update/delete is filtered by `user_id=eq.<uuid>` and every insert injects `user_id`. `SUPABASE_URL`, `SUPABASE_SERVICE_KEY` and `SUPABASE_USER_ID` live in `main/secrets.h` (not committed).

**`main/ProductService.cpp`** — Products and barcodes CRUD against `/rest/v1/products` and `/rest/v1/barcodes`. Uses `esp_http_client` and `cJSON`.

**`main/ProductsManager.cpp`** — Mutex-protected in-memory cache of products. Background task writes here; the LVGL task reads snapshots here.

**`main/CookbookService.cpp`** — Read-only `cookbooks` fetch (`id`, `name`).

**`main/CookbooksManager.cpp`** — Cookbook cache; derives each cookbook's recipe count and cover URL from `RecipesManager`'s summary list, and clears every cached recipe body on refresh.

**`main/RecipeService.cpp` / `main/RecipeService.h`** — Fetches recipe metadata (`id`, `image_url`, `cookbook_ids`) once, and a cookbook's full rows on demand with `cookbook_ids=cs.{uuid}`. Parses the app's stored section JSON: a single-element JSON string, a plain section array, a bare string, and capitalized `Name`/`Steps` fields.

**`main/RecipesManager.cpp`** — Summary list plus a per-cookbook cache with `loaded`/`failed` state. Counts and covers come from the summaries; failed cookbook fetches are retried on demand.

**`main/ui/screens.cpp` / `main/ui/actions.cpp`** — Hand-written construction of the Inventory, Cookbooks and Settings tabs, the product edit modal, the cookbook detail screen and the recipe detail screen, plus all event handlers and background fetch tasks.

**`main/ui_theme.cpp`** — Light Material 3 palette, shared styles, fonts and small widget helpers.

**`main/ui_extensions.cpp`** — Snackbar, spinner and `push_thumbnail`, which queues a direct image download for an already-sized `lv_image`.

**`main/ui_extensions_products.cpp`** — Grouped inventory list, expiry badges and product modal population.

**`main/ui_extensions_cookbooks.cpp`** — Cookbook grid, cookbook recipe cards and recipe detail rendering (hero, meta badges, ingredient check rows, numbered method steps).

**`main/thumbnail_manager.cpp` / `main/thumbnail_cache.cpp`** — Async thumbnail download/decode with a 2 MB stream guard, and a SPIFFS-backed LRU cache.

### Concurrency Model

- `combinedFetchTask` (FreeRTOS task): waits for WiFi + SNTP sync, then fetches products from Supabase and writes to `ProductsManager` under mutex.
- LVGL main loop runs on the main task at 100Hz and reads manager snapshots.
- The cookbook grid loads lazily when the Cookbooks tab is first activated. A cookbook's recipe bodies are fetched when it is opened, with a per-cookbook in-flight set; a result whose cookbook is no longer open is discarded.
- `thumb_worker_task` processes the thumbnail queue. `cache_io_task` runs SPIFFS I/O on internal DRAM, pinned to core 0.
- Every background task takes `lv_lock()` before touching `objects` or LVGL widgets.

### Flash Partitions (`partitions.csv`)

| Name    | Size | Purpose            |
| ------- | ---- | ------------------ |
| NVS     | 24KB | Key-value storage  |
| factory | 8MB  | Application binary |
| storage | 7MB  | SPIFFS filesystem  |

### Managed vs Local Components

- **`managed_components/`** — auto-downloaded from ESP Component Registry (declared in `main/idf_component.yml`, pinned in `dependencies.lock`). Do not edit manually.
- **`components/`** — local custom drivers (BSP, LCD JD9365, touchscreen GSL3680). These are editable.

## Important Files

| File                 | Purpose                                             |
| -------------------- | --------------------------------------------------- |
| `main/secrets.h`     | Supabase URL, service key and user id — not in git  |
| `main/models.h`      | `Product`, `Cookbook`, `RecipeSummary`, `Recipe`    |
| `main/CMakeLists.txt`| Source list for the firmware component              |
| `sdkconfig`          | Active SDK config (WiFi creds embedded here)        |
| `sdkconfig.defaults` | Template for fresh setups                           |

# Project Rules

- **CRITICAL**: Never run `idf.py build` or any ESP-IDF build commands.
- Do not attempt to compile or build the firmware.
- Focus only on code modifications; I will handle all builds manually.
- `main/ui` is hand-written C++ and is edited directly; there is no UI generator.
- Never commit `main/secrets.h` or the service-role key.
