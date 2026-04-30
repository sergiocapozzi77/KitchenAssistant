# KitchenAssistant

An ESP32-P4 embedded kitchen inventory manager with a touchscreen UI, Appwrite cloud sync, and multi-source recipe suggestions.

Track product expiry dates, get recipe ideas based on what you have, and save favourites — all on an 800×1280 LCD driven by LVGL 9.x.

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

Create `main/secrets.h` with the following defines (example values shown — replace with your own):

```c
#define APPWRITE_API_KEY "your-appwrite-api-key"
#define APPWRITE_ENDPOINT "https://fra.cloud.appwrite.io/v1"
#define APPWRITE_PROJECT_ID "your-project-id"
#define FAVOURITES_COLLECTION_ID "favourites"
#define RECIPES_COLLECTION_ID "recipes"
#define APPWRITE_FUNCTION_ID "your-function-id"
#define APPWRITE_IMAGE_RESIZE_FUNCTION_ID "your-image-resize-function-id"
#define DEEPSEEK_API_KEY "your-deepseek-api-key"
#define DEEPSEEK_ENDPOINT "https://api.deepseek.com"
#define LEONARDO_API_KEY "your-leonardo-api-key"
#define LEONARDO_ENDPOINT "https://cloud.leonardo.ai/api/rest/v1"
#define LEONARDO_IMAGE_MODEL "your-model-id"
```

WiFi credentials are stored on SPIFFS (set via the on-device settings UI at first boot).

## Features

- **Inventory management** — Scan or add products with expiry dates, quantities, and categories. Products expiring soon are highlighted.
- **Recipe suggestions** — Fetches recipes from BBC GoodFood, Giallo Zafferano, and Ania Gotuje based on available ingredients.
- **AI recipes** — Generate custom recipes from your ingredients using DeepSeek AI.
- **AI-generated images** — Recipe thumbnails and headers generated via Leonardo AI.
- **Favourites** — Save recipes with full ingredients and method steps, synced to Appwrite.
- **Recipe steps view** — Step-by-step cooking mode with progress tracking.
- **Offline-capable thumbnail cache** — JPEG thumbnails cached on SPIFFS with LRU eviction.

## Architecture

```
LVGL UI (EEZ Studio generated: main/ui/)
    ↕
UI Extensions (main/ui_extensions*.cpp) — populates LVGL widgets from data
    ↕
Services (ProductService, RecipeService, RecipeDetailService, FavouriteService, WiFiManager)
    ↕
ESP-IDF HTTP client + cJSON + FreeRTOS
```

### Key Source Files

| File | Purpose |
|------|---------|
| `main/app.cpp` | Initialisation pipeline, main loop |
| `main/ui_extensions.cpp` | UI styles, helpers, recipe card builder |
| `main/ui_extensions_products.cpp` | Product list UI |
| `main/ui_extensions_recipes.cpp` | Recipe list UI |
| `main/ui_extensions_recipe_detail.cpp` | Recipe detail screen + heart button |
| `main/ui_extensions_favourites.cpp` | Favourites list |
| `main/ui_extensions_recipe_steps.cpp` | Step-by-step cooking mode |
| `main/ProductService.cpp` | Appwrite CRUD for products |
| `main/RecipeService.cpp` | Recipe fetch from GoodFood, Giallo Zafferano, Ania Gotuje |
| `main/RecipeDetailService.cpp` | On-demand recipe detail scraping |
| `main/RecipeAIDetailService.cpp` | AI-powered recipe generation (DeepSeek) |
| `main/FavouriteService.cpp` | Appwrite favourites sync |
| `main/FavouritesManager.cpp` | In-memory favourites cache with mutex |
| `main/ProductsManager.cpp` | In-memory product cache with mutex |
| `main/DeepSeekAIService.cpp` | DeepSeek AI integration |
| `main/LeonardoImageGenerator.cpp` | AI image generation via Leonardo |
| `main/thumbnail_cache.cpp` | SPIFFS-backed LRU thumbnail cache |
| `main/thumbnail_manager.cpp` | Async thumbnail fetch, decode, and LVGL integration |
| `main/HttpClientHelper.cpp` | Shared HTTP client for Appwrite functions |
| `main/WiFiManager.cpp` | WiFi connection management |
| `main/models.h` | `Product`, `RecipeSuggestion`, `Favorite` structs |
| `main/secrets.h` | API keys (not committed) |

### EEZ Studio UI (`main/ui/`)

The UI screens are designed in [EEZ Studio](https://www.envox.eu/eez-studio/eez-studio-introduction/) (`KitchenHelp.eez-project`). The following files are auto-generated and should not be manually edited: `screens.c`, `styles.c`, `images.c`. The file `actions.cpp` is the designated manual-edit file for event handlers.

### Concurrency Model

- **LVGL main loop**: runs at ~100 Hz, handles rendering and input.
- **`fetchProductsTask`**: FreeRTOS background task that fetches products from Appwrite and writes to `ProductsManager` under mutex.
- **`thumb_worker_task`**: processes thumbnail download/decode queue, limited to 1 concurrent HTTP request via counting semaphore.
- **`cache_io_task`**: SPIFFS I/O for thumbnail cache — runs with internal DRAM stack (critical for SPIFFS flash cache safety) pinned to core 0.
- **Render threads**: LVGL 9.x multi-threaded software renderer with 2 draw units.

### Flash Partitions

| Name | Size | Purpose |
|------|------|---------|
| NVS | 24 KB | Key-value storage |
| factory | 8 MB | Application binary |
| storage | 7 MB | SPIFFS filesystem |

## Components

- **`components/`** — Local custom drivers (BSP, LCD JD9365, touchscreen GSL3680). Editable.
- **`managed_components/`** — Auto-downloaded from the ESP Component Registry. Do not edit.

## Licence

MIT
