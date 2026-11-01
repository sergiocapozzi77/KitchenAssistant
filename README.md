# KitchenAssistant

An ESP32-P4 kitchen display that mirrors one Supabase account: it shows the
inventory and cookbooks that the KitchenAssistant phone app syncs, on an
800×1280 LCD driven by LVGL 9.5.

The device is read-only for cookbooks: it edits and deletes products, and
browses cookbooks, their recipes and the full recipe detail. Cookbook
management stays on the phone.

## Hardware

- [ESP32-P4-Function-EV board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html)
- 800×1280 LCD (JD9365 driver) with touch (GSL3680)
- SPIFFS flash partition (7 MB) for WiFi credentials and thumbnail cache

## Software Requirements

- [ESP-IDF v5.5.2](https://github.com/espressif/esp-idf) installed at `C:\Users\sergi\esp\v5.5.2\esp-idf`
- Python 3.x (shipped with ESP-IDF)

## Quick Start

```bash
# Set target (one-time)
idf.py set-target esp32p4

# Build
idf.py build

# Flash and monitor
idf.py -p COM10 flash monitor
```

WiFi credentials and other settings are configured via:

```bash
idf.py menuconfig  # look under "Kitchen Assistant Configuration"
```

## Configuration

Create `main/secrets.h` from `main/secrets.h.example` and fill in the
service-role key:

```c
#define SUPABASE_URL "https://ubsqrktgvlxzaujksran.supabase.co"
#define SUPABASE_SERVICE_KEY "replace-with-service-role-key"
#define SUPABASE_USER_ID "c92ebb7f-457e-4d46-a9e8-ed7f7a67e7b2"
```

`SUPABASE_USER_ID` pins the mirrored account. The service-role key bypasses
row-level security, so it must never be committed; `main/secrets.h` is
excluded by `.gitignore`.

WiFi credentials are stored on SPIFFS and are set in the on-device Settings
tab at first boot.

## Features

- **Inventory management** — Products grouped by category with expiry badges,
  All / Expiring soon / Expired filters, A-Z / By expiry sorting, name search,
  and an edit modal for name, expiry, category, quantity and frozen state.
  Products can be edited or deleted on the device, never created.
- **Cookbooks** — A two-column grid built from one light metadata query; each
  card shows the first recipe's cover, the cookbook name and its recipe count.
- **Cookbook recipes** — Opening a cookbook fetches exactly that cookbook's
  rows, renders a card per recipe with time, difficulty and cuisine badges,
  and caches the result so a revisit is instant.
- **Recipe detail** — Hero image, title, source, total time, difficulty and
  Ingredients / Method tabs; ingredient rows can be ticked off with a
  strike-through while cooking.
- **Offline-capable thumbnail cache** — JPEG thumbnails are downloaded
  directly from the stored URL and cached on SPIFFS with LRU eviction.

## Architecture

```
Hand-written LVGL UI (main/ui)
    ↕
UI Extensions (main/ui_extensions*.cpp) — populates LVGL widgets from data
    ↕
Services and managers (ProductService, CookbookService, RecipeService,
ProductsManager, CookbooksManager, RecipesManager, WiFiManager)
    ↕
SupabaseClient (PostgREST) + cJSON + FreeRTOS
```

### Key Source Files

| File                                   | Purpose                                                    |
| -------------------------------------- | ---------------------------------------------------------- |
| `main/app.cpp`                         | Initialisation pipeline, main loop                         |
| `main/SupabaseClient.cpp`              | PostgREST client with service-role auth and user scoping   |
| `main/ProductService.cpp`              | Products and barcodes CRUD on Supabase                     |
| `main/ProductsManager.cpp`             | In-memory product cache with mutex                         |
| `main/CookbookService.cpp`             | Read-only cookbook fetch                                   |
| `main/CookbooksManager.cpp`            | Cookbook cache with derived recipe counts and covers       |
| `main/RecipeService.cpp`               | Recipe summaries and per-cookbook recipe bodies            |
| `main/RecipesManager.cpp`              | Summary list plus per-cookbook recipe caches with mutex    |
| `main/ui/screens.cpp`                  | Hand-written construction of every screen and the modal    |
| `main/ui/actions.cpp`                  | Event handlers and background fetch tasks                  |
| `main/ui_theme.cpp`                    | Palette, shared styles, fonts and widget helpers           |
| `main/ui_extensions.cpp`               | Snackbar, spinner and thumbnail queue entry point          |
| `main/ui_extensions_products.cpp`      | Inventory list, grouped rows and product modal population  |
| `main/ui_extensions_cookbooks.cpp`     | Cookbook grid, recipe cards and recipe detail rendering    |
| `main/thumbnail_cache.cpp`             | SPIFFS-backed LRU thumbnail cache                          |
| `main/thumbnail_manager.cpp`           | Async thumbnail fetch, decode and LVGL integration         |
| `main/WiFiManager.cpp`                 | WiFi connection management                                 |
| `main/models.h`                        | `Product`, `Cookbook`, `RecipeSummary`, `Recipe` structs   |
| `main/secrets.h`                       | Supabase URL, service key and user id (not committed)      |

### UI

The UI is hand-written C++ in `main/ui`. `screens.cpp` builds the three screens
(Inventory, Cookbooks, Settings), the product edit modal and the two detail
screens. `actions.cpp` holds the event handlers. There is no UI generator;
`main/ui` is edited directly.

### Concurrency Model

- **LVGL main loop**: runs at ~100 Hz, handles rendering and input.
- **`combinedFetchTask`**: FreeRTOS task that waits for WiFi and SNTP, fetches
  products from Supabase, and writes to `ProductsManager` under mutex.
- **Cookbook loads**: the grid is fetched once when the Cookbooks tab is first
  activated; a cookbook's recipe bodies are fetched when it is opened, with a
  per-cookbook in-flight guard so a late result is discarded after Back.
- **`thumb_worker_task`**: processes the thumbnail queue, one HTTP request at a
  time, under the LVGL lock when it touches widgets.
- **`cache_io_task`**: SPIFFS I/O for the thumbnail cache, with an internal DRAM
  stack pinned to core 0.
- Every background task takes `lv_lock()` before touching `objects` or widgets.

### Flash Partitions

| Name    | Size  | Purpose            |
| ------- | ----- | ------------------ |
| NVS     | 24 KB | Key-value storage  |
| factory | 8 MB  | Application binary |
| storage | 7 MB  | SPIFFS filesystem  |

## Components

- **`components/`** — Local custom drivers (BSP, LCD JD9365, touchscreen GSL3680). Editable.
- **`managed_components/`** — Auto-downloaded from the ESP Component Registry. Do not edit.

## Licence

MIT
