# Cookbooks — Organize Favourites into Collections

**Date:** 2026-05-18
**Status:** Approved Design

## Overview

Replace the flat favourites list with a cookbook-based organization system. Users create cookbooks (collections), add recipes to them, and browse them hierarchically — cookbook list first, then favourites within a cookbook.

## Data Model

### Appwrite Schema

**New collection: `cookbooks`**

| Attribute | Type   | Description                |
|-----------|--------|----------------------------|
| `$id`     | string | Auto-generated document ID |
| `name`    | string | Cookbook name (user-defined) |

**Modified collection: `favourites`**

Add attribute: `cookbookIds` (string array) — contains `$id`s of cookbooks this favourite belongs to.

### C++ Structs

```cpp
// New — main/models.h
struct Cookbook {
    std::string id;    // Appwrite $id
    std::string name;
};

// Modified — Favorite gets new field
struct Favorite {
    // ... existing fields ...
    std::vector<std::string> cookbookIds;  // NEW
};
```

### Key Queries

- **Favourites in a cookbook:** `equal("cookbookIds", ["<cookbookId>"])` on favourites collection
- **All cookbooks:** no filter, list all rows in cookbooks collection

## Service Layer

### New: `CookbookService`

CRUD for the `cookbooks` Appwrite collection. Follows same HTTP+Appwrite pattern as `FavouriteService`.

| Method                          | Description                                     |
|--------------------------------|-------------------------------------------------|
| `getCookbooks()`               | Fetch all cookbooks from Appwrite               |
| `createCookbook(name)`         | Create a new cookbook, returns its `$id`        |
| `deleteCookbook(id)`           | Delete cookbook document                        |

### Modified: `FavouriteService`

New/changed methods:

| Method                                               | Description                                                    |
|------------------------------------------------------|----------------------------------------------------------------|
| `addFavourite(Favorite)`                             | Unchanged — creates the favourite document                     |
| `addFavouriteToCookbook(favouriteId, cookbookId)`    | PATCH the favourite's `cookbookIds` array to append cookbookId |
| `getFavouritesByCookbook(cookbookId)`                | Query favourites where `cookbookIds` contains cookbookId       |
| `removeCookbookFromFavourites(cookbookId)`            | Find all favourites with that cookbookId and delete them       |
| `removeFavourite(url)`                               | Unchanged                                                      |

### New: `CookbookManager`

Mutex-protected in-memory cache (same pattern as `FavouritesManager`).

| Method                       | Description                         |
|------------------------------|-------------------------------------|
| `fetchCookbooks()`           | Load cookbooks from Appwrite        |
| `getCookbooks()`             | Thread-safe copy of cached cookbooks |
| `addCookbook(Cookbook)`      | Add to cache                        |
| `removeCookbook(id)`         | Remove from cache                   |

### Concurrency

- Background task fetches cookbooks alongside favourites
- Same pattern: FreeRTOS task, waits for WiFi + SNTP, writes under mutex
- Mutex on `CookbookManager::_cookbooks`

## UI Design

### Adding a Favourite (Recipe Detail Screen)

When the user taps the heart button on a recipe detail:

1. A modal overlay appears showing existing cookbooks as a list + "Create new cookbook" option
2. Selecting a cookbook → adds the recipe to that cookbook (POST favourite + PATCH cookbookIds)
3. Selecting "Create new" → a text input appears for naming the new cookbook → creates it → adds the recipe
4. After adding, the modal closes and the heart shows "favourited" state

### Favourites Panel (Tab 2)

Two states managed by a state variable in `ui_extensions_favourites.cpp`:

**State: Cookbook List (default)**
- `favourites_list` uses a 2-column flex flow (`LV_FLEX_FLOW_ROW_WRAP`)
- Each cookbook = a card with the name centered + count badge
- Delete button (X) on each card → confirmation dialog → deletes cookbook + all its favourites
- Search filters cookbooks by name
- Pagination buttons repurposed or hidden

**State: Favourites in Cookbook (drill-in)**
- Back button dynamically created on `favourites_header_pnl`
- `favourites_list` shows favourite cards for the selected cookbook (same card layout as current implementation)
- Search filters within this cookbook's favourites
- Back button returns to cookbook list (cleans up dynamic button)

### Delete Cookbook Flow

1. User taps X on a cookbook card
2. Confirmation snackbar/dialog: "Delete [name] and all its favourites?"
3. On confirm:
   - `CookbookService::deleteCookbook(id)` — removes cookbook doc
   - `FavouriteService::removeCookbookFromFavourites(id)` — queries + deletes all favourites with that cookbookId
   - Update local caches
   - Refresh UI

## Files to Create

| File                          | Description                        |
|-------------------------------|------------------------------------|
| `main/CookbookService.cpp/h`  | Appwrite CRUD for cookbooks        |
| `main/CookbookManager.cpp/h`  | In-memory cache for cookbooks      |

## Files to Modify

| File                               | Changes                                              |
|------------------------------------|------------------------------------------------------|
| `main/models.h`                    | Add `Cookbook` struct, `cookbookIds` to `Favorite`   |
| `main/FavouriteService.cpp/h`      | Add `addFavouriteToCookbook`, `getFavouritesByCookbook`, `removeCookbookFromFavourites` |
| `main/FavouritesManager.cpp/h`     | Wire cookbook-aware fetching/deletion                |
| `main/ui_extensions_favourites.cpp`| Cookbook grid view, drill-in, back button, delete    |
| `main/ui_extensions.h`             | Declare new population functions                     |
| `main/ui_extensions_recipe_detail.cpp` | Cookbook selection modal when adding favourite   |
| `main/ui/actions.cpp`              | New event callbacks for cookbook actions             |
| `main/app.cpp`                     | Initialize `CookbookManager` fetch task              |
| `main/secrets.h`                   | Add `COOKBOOKS_COLLECTION_ID`                        |

## Out of Scope

- Cookbook reordering
- Cookbook sharing between users
- Search across all cookbook favourites at once
