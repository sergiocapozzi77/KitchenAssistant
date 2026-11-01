// LVGL object trees are the largest consumer of internal RAM during screen
// builds: every lv_obj, label, style and event node is a small heap block, and
// a populated cookbook screen keeps tens of kilobytes alive. On this board the
// internal RAM has to stay free for the esp_hosted SDIO DMA buffers, which can
// be requested at any moment (a 1.5-3 KB DMA block) and cannot be served from
// PSRAM. So LVGL's allocator prefers PSRAM and falls back to internal memory
// only when PSRAM is exhausted.
//
// This file supplies the hooks selected by CONFIG_LV_USE_CUSTOM_MALLOC
// (LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM). The signatures mirror the CLIB
// core implementation in lvgl/src/stdlib/clib/lv_mem_core_clib.c.

#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "LV_MEM";

static void *lv_psram_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return ptr ? ptr : heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}

extern "C" {

void lv_mem_init(void)
{
    ESP_LOGI(TAG, "LVGL allocator active: PSRAM first, internal fallback");
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    // Pools are a built-in-allocator concept; nothing to add here.
    (void)mem;
    (void)bytes;
    return nullptr;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    (void)pool;
}

void *lv_malloc_core(size_t size)
{
    return lv_psram_malloc(size);
}

void *lv_realloc_core(void *ptr, size_t new_size)
{
    if (!ptr)
        return lv_psram_malloc(new_size);

    // heap_caps_realloc migrates between regions when the caps differ, so a
    // block that originally fell back to internal memory can move to PSRAM.
    void *res = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!res && new_size != 0)
        res = heap_caps_realloc(ptr, new_size, MALLOC_CAP_DEFAULT);
    return res;
}

void lv_free_core(void *ptr)
{
    heap_caps_free(ptr);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    if (!mon_p)
        return;

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    mon_p->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon_p->free_cnt = info.free_blocks;
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->used_cnt = info.allocated_blocks;
    mon_p->max_used = 0;
    mon_p->used_pct = mon_p->total_size
                          ? (uint8_t)(((uint64_t)(mon_p->total_size - mon_p->free_size) * 100) /
                                      mon_p->total_size)
                          : 0;
    mon_p->frag_pct = mon_p->free_size
                          ? (uint8_t)(100 - ((uint64_t)mon_p->free_biggest_size * 100) /
                                                mon_p->free_size)
                          : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

} // extern "C"
