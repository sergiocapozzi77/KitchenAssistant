#include "diag.h"

#include "sdkconfig.h"

#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "esp_private/cache_utils.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include <cstring>

static const char *TAG = "DIAG";

// ── DIAGNOSTIC (temporary): post-mortem state in RTC RAM ─────────────────────
//
// See the comment at the top of diag.h for why this exists. Everything here is
// deliberately dependency-free: plain stores, no locks, no allocation, so the
// probes cannot be the thing that hangs.

#define DIAG_MAGIC 0x44494147u // "DIAG"
#define DIAG_VER 3

// Per core, so two cores can record concurrently without an atomic index.
#define DIAG_RING_LEN 48

// Tasks whose TCBs the tick probes can recognise (see diag_task_register).
#define DIAG_MAX_TASKS 12

// A core is reported as stalled once its tick counter has not moved for this
// long. The monitor samples every 500 ms, so detection takes 1.5-2 s.
#define DIAG_TICK_STALL_US 1500000LL
#define DIAG_MON_PERIOD_US 500000LL

typedef struct
{
    uint32_t ms; // esp_timer_get_time() / 1000, truncated (~49 day wrap)
    uint16_t code;
    uint16_t arg;
    uint8_t core;
    uint8_t pad[3];
} diag_entry_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t boot_seq;

    uint32_t ring_head[2];
    diag_entry_t ring[2][DIAG_RING_LEN];

    // Liveness monitor (esp_timer task, runs even when the FreeRTOS tick dies)
    uint32_t mon_count;
    int64_t mon_first_us;
    int64_t mon_prev_us;
    int64_t mon_last_us;
    uint32_t mon_last_period_ms;
    uint32_t mon_max_period_ms;
    int64_t mon_last_stall_log_us;

    // OS tick bookkeeping, sampled by the monitor
    uint32_t tick_count[2];
    int64_t tick_last_us[2]; // last sample at which the counter advanced
    int64_t tick_isr_us[2];  // written from the tick ISR itself
    uint32_t tick_isr_pc[2]; // interrupted PC (mepc) seen by the last tick
    uint32_t cur_task_id[2]; // diag_task_t running at the last tick
    uint32_t tick_cache_enabled[2]; // ext-mem cache state (global, sw-tracked) at the last tick
    uint32_t tick_stall_mask;
    int64_t tick_stall_us[2];
    int64_t tick_resume_us[2];

    // Application heartbeats (one task per core)
    int64_t hb_us[2];
    uint32_t hb_tick[2];

    // LVGL task beat
    int64_t lvgl_last_us;
    uint32_t lvgl_count;
    uint32_t lvgl_stall_logged;
} diag_rtc_t;

RTC_NOINIT_ATTR static diag_rtc_t s_rtc;

// FreeRTOS tick counters. Plain DRAM: readable with the flash cache disabled.
static volatile uint32_t s_tick_count[2] = {0, 0};

// Known task handles, so the tick probe can name the task each core was
// running. Tasks register themselves; the probe only reads the table. DRAM,
// not RTC: TCB pointers are only meaningful within one run.
static const void *volatile s_task_tcb[DIAG_MAX_TASKS];
static volatile uint8_t s_task_id[DIAG_MAX_TASKS];
static volatile uint32_t s_task_count = 0;

static const char *bc_name(uint16_t code);

// Both getters (freertos/idf_additions.h) return pxCurrentTCBs[] /
// xIdleTaskHandle[] directly and take no kernel lock, which matters because
// the tick probe runs in interrupt context.
void diag_task_register(const void *tcb, uint8_t id)
{
    if (tcb == nullptr || id == DIAG_TASK_UNKNOWN)
        return;

    const uint32_t n = s_task_count;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (s_task_tcb[i] == tcb)
        {
            s_task_id[i] = id;
            return;
        }
    }

    if (n >= DIAG_MAX_TASKS)
        return;

    s_task_tcb[n] = tcb;
    s_task_id[n] = id;
    s_task_count = n + 1;
}

// ── Tick probes ──────────────────────────────────────────────────────────────

#if defined(__riscv)
// The trap entry saves the interrupted PC (mepc) but leaves the CSR untouched
// until the trap return, so the tick probe can read it without any lock.
static inline uint32_t __attribute__((always_inline)) read_trap_pc(void)
{
    uint32_t pc = 0;
#if defined(CONFIG_SECURE_ENABLE_TEE)
    __asm__ volatile("csrr %0, uepc" : "=r"(pc));
#else
    __asm__ volatile("csrr %0, mepc" : "=r"(pc));
#endif
    return pc;
}
#else
static inline uint32_t __attribute__((always_inline)) read_trap_pc(void)
{
    return 0;
}
#endif

// Lock-free identification of the task a core was running when the tick
// arrived. Anything not registered is reported as unknown; the idle tasks are
// detected through the kernel's own handle so a frozen tick while idle is
// distinguishable from a frozen tick inside an unregistered system task.
static uint8_t IRAM_ATTR current_task_id(BaseType_t core)
{
    const TaskHandle_t cur = xTaskGetCurrentTaskHandleForCore(core);
    if (cur == nullptr)
        return DIAG_TASK_UNKNOWN;

    const uint32_t n = s_task_count;
    for (uint32_t i = 0; i < n; ++i)
    {
        if (s_task_tcb[i] == (const void *)cur)
            return s_task_id[i];
    }

    if (cur == xTaskGetIdleTaskHandleForCore(core))
        return DIAG_TASK_IDLE;

    return DIAG_TASK_UNKNOWN;
}

// IRAM so the hook still runs while the flash cache is disabled. esp_timer_get_time()
// is a lock-free systimer counter read and is IRAM as well, as are the two
// kernel task getters above and spi_flash_cache_enabled().
static void IRAM_ATTR tickProbeCpu0(void)
{
    s_tick_count[0] = s_tick_count[0] + 1;
    s_rtc.tick_isr_us[0] = esp_timer_get_time();
    s_rtc.tick_isr_pc[0] = read_trap_pc();
    s_rtc.cur_task_id[0] = current_task_id((BaseType_t)0);
    // spi_flash_cache_enabled() is IRAM (as is its callee in libhal), so this is
    // safe even if a flash operation has the cache disabled on this core.
    s_rtc.tick_cache_enabled[0] = spi_flash_cache_enabled() ? 1u : 0u;
}

static void IRAM_ATTR tickProbeCpu1(void)
{
    s_tick_count[1] = s_tick_count[1] + 1;
    s_rtc.tick_isr_us[1] = esp_timer_get_time();
    s_rtc.tick_isr_pc[1] = read_trap_pc();
    s_rtc.cur_task_id[1] = current_task_id((BaseType_t)1);
    s_rtc.tick_cache_enabled[1] = spi_flash_cache_enabled() ? 1u : 0u;
}

// ── Heartbeat tasks ──────────────────────────────────────────────────────────

// One per core. vTaskDelay needs OS ticks, so these stop logging the moment a
// core's tick stops — the monitor above is what still reports then.
static void diagHeartbeatTask(void *arg)
{
    const int core = (int)(intptr_t)arg;
    int64_t prev_us = esp_timer_get_time();
    uint32_t prev_tick[2] = {s_tick_count[0], s_tick_count[1]};

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        const int64_t now_us = esp_timer_get_time();
        const uint32_t tick0 = s_tick_count[0];
        const uint32_t tick1 = s_tick_count[1];

        s_rtc.hb_us[core] = now_us;
        s_rtc.hb_tick[core] = (core == 0) ? tick0 : tick1;

        // ESP_EARLY_LOG goes through esp_rom_printf instead of the logging
        // mutex, so the surviving core still reports if the other one stopped
        // while holding that mutex. The ROM printf has no long-long support,
        // hence the plain int/unsigned casts for every argument.
        ESP_EARLY_LOGW("HB", "core%d dt=%dms tick0=+%u tick1=+%u (total %u/%u)",
                       core, (int)((now_us - prev_us) / 1000),
                       (unsigned)(tick0 - prev_tick[0]),
                       (unsigned)(tick1 - prev_tick[1]),
                       (unsigned)tick0, (unsigned)tick1);

        prev_us = now_us;
        prev_tick[0] = tick0;
        prev_tick[1] = tick1;
    }
}

// ── Liveness monitor ─────────────────────────────────────────────────────────

// Runs in the esp_timer task, driven by the ESPTIMER systimer unit. Unlike the
// heartbeat tasks it does not need FreeRTOS ticks, so its continued execution
// is what separates "the OS tick stopped" from "interrupts stopped globally".
static void diagMonitorCb(void *arg)
{
    (void)arg;

    // This callback runs in the esp_timer task; name it for the tick probes.
    static bool s_timer_task_registered = false;
    if (!s_timer_task_registered)
    {
        s_timer_task_registered = true;
        diag_task_register(xTaskGetCurrentTaskHandle(), DIAG_TASK_TIMER);
    }

    const int64_t now = esp_timer_get_time();

    const int64_t prev = s_rtc.mon_prev_us;
    s_rtc.mon_prev_us = now;
    s_rtc.mon_last_us = now;
    s_rtc.mon_count++;

    if (s_rtc.mon_count == 1)
    {
        s_rtc.mon_first_us = now;
        s_rtc.tick_count[0] = s_tick_count[0];
        s_rtc.tick_count[1] = s_tick_count[1];
        s_rtc.tick_last_us[0] = now;
        s_rtc.tick_last_us[1] = now;
    }
    else
    {
        const int64_t period_us = now - prev;
        const uint32_t period_ms = (uint32_t)(period_us / 1000);
        s_rtc.mon_last_period_ms = period_ms;
        if (period_ms > s_rtc.mon_max_period_ms)
            s_rtc.mon_max_period_ms = period_ms;

        // The flash cache, a long critical section or a stalled core makes the
        // callback late. A late callback alone is normal during SPIFFS writes;
        // seconds of lateness are not.
        if (period_us > 1500000)
        {
            ESP_EARLY_LOGW(TAG, "monitor callback late by %d ms",
                           (int)((period_us - DIAG_MON_PERIOD_US) / 1000));
            diag_bc(DIAG_MON_LATE, period_ms > 60000 ? 60000 : (uint16_t)period_ms);
        }
    }

    const uint32_t ticks[2] = {s_tick_count[0], s_tick_count[1]};
    for (int c = 0; c < 2; ++c)
    {
        if (ticks[c] != s_rtc.tick_count[c])
        {
            s_rtc.tick_count[c] = ticks[c];
            s_rtc.tick_last_us[c] = now;
            if (s_rtc.tick_stall_mask & (1u << c))
            {
                s_rtc.tick_stall_mask &= ~(1u << c);
                s_rtc.tick_resume_us[c] = now;
                ESP_EARLY_LOGW(TAG, "core%d OS tick resumed after %d ms", c,
                               (int)((now - s_rtc.tick_stall_us[c]) / 1000));
                diag_bc(DIAG_MON_TICK_RESUME, (uint16_t)c);
            }
        }
        else if (!(s_rtc.tick_stall_mask & (1u << c)) &&
                 (now - s_rtc.tick_last_us[c]) > DIAG_TICK_STALL_US)
        {
            s_rtc.tick_stall_mask |= (1u << c);
            s_rtc.tick_stall_us[c] = now;
            s_rtc.mon_last_stall_log_us = now;
            ESP_EARLY_LOGW(TAG,
                           "core%d OS tick stalled for %d ms (tick0=%u tick1=%u, esp_timer alive)",
                           c, (int)((now - s_rtc.tick_last_us[c]) / 1000),
                           (unsigned)ticks[0], (unsigned)ticks[1]);
            diag_bc(DIAG_MON_TICK_STALL, (uint16_t)c);
        }
    }

    // The LVGL task runs lv_timer_handler from its own task; if it stops while
    // the monitor still runs, the UI side is wedged but the system is not.
    if (s_rtc.lvgl_last_us != 0 && !s_rtc.lvgl_stall_logged &&
        (now - s_rtc.lvgl_last_us) > 2000000)
    {
        s_rtc.lvgl_stall_logged = 1;
        ESP_EARLY_LOGW(TAG, "LVGL task has not run for %d ms",
                       (int)((now - s_rtc.lvgl_last_us) / 1000));
        diag_bc(DIAG_MON_LVGL_STALL, 0);
    }

    // Keep printing while a stall persists: the last of these lines before a
    // reset tells how far the panic path got before the timer groups fired.
    if (s_rtc.tick_stall_mask != 0 &&
        (now - s_rtc.mon_last_stall_log_us) > 1000000)
    {
        s_rtc.mon_last_stall_log_us = now;
        ESP_EARLY_LOGW(TAG,
                       "STALL persists: core0 %s (last tick %d ms ago), core1 %s (last tick %d ms ago)",
                       (s_rtc.tick_stall_mask & 1u) ? "FROZEN" : "ok",
                       (int)((now - s_rtc.tick_last_us[0]) / 1000),
                       (s_rtc.tick_stall_mask & 2u) ? "FROZEN" : "ok",
                       (int)((now - s_rtc.tick_last_us[1]) / 1000));
    }
}

// ── Breadcrumbs ──────────────────────────────────────────────────────────────

void diag_bc(uint16_t code, uint16_t arg)
{
    if (s_rtc.magic != DIAG_MAGIC)
        return; // called before diag_init() — should not happen

    const int core = esp_cpu_get_core_id() & 1;
    diag_entry_t *e = &s_rtc.ring[core][s_rtc.ring_head[core] % DIAG_RING_LEN];

    e->ms = (uint32_t)(esp_timer_get_time() / 1000);
    e->code = code;
    e->arg = arg;
    e->core = (uint8_t)core;
    e->pad[0] = e->pad[1] = e->pad[2] = 0;

    s_rtc.ring_head[core] = s_rtc.ring_head[core] + 1;
}

void diag_lvgl_beat(void)
{
    // The beat is an LVGL timer callback, so this runs in the LVGL port task.
    static bool s_lvgl_task_registered = false;
    if (!s_lvgl_task_registered)
    {
        s_lvgl_task_registered = true;
        diag_task_register(xTaskGetCurrentTaskHandle(), DIAG_TASK_LVGL);
    }

    s_rtc.lvgl_last_us = esp_timer_get_time();
    s_rtc.lvgl_count = s_rtc.lvgl_count + 1;
    s_rtc.lvgl_stall_logged = 0;
}

// ── Boot-time dump of the previous run ───────────────────────────────────────

static const char *bc_name(uint16_t code)
{
    switch (code)
    {
    case DIAG_NONE:
        return "NONE";
    case DIAG_APP_BOOT:
        return "APP_BOOT";
    case DIAG_APP_HW_READY:
        return "APP_HW_READY";
    case DIAG_APP_QUEUES_READY:
        return "APP_QUEUES_READY";
    case DIAG_APP_LOOP:
        return "APP_LOOP";
    case DIAG_APP_TASKS_ENTER:
        return "APP_TASKS_ENTER";
    case DIAG_APP_TASKS_ALLOC:
        return "APP_TASKS_ALLOC";
    case DIAG_APP_TASKS_CREATED:
        return "APP_TASKS_CREATED";
    case DIAG_APP_FETCH_ENTER:
        return "APP_FETCH_ENTER";
    case DIAG_APP_FETCH_UI:
        return "APP_FETCH_UI";
    case DIAG_UI_DETAIL_ENTER:
        return "UI_DETAIL_ENTER";
    case DIAG_UI_DETAIL_CANCELLED:
        return "UI_DETAIL_CANCELLED";
    case DIAG_UI_HERO_ENTER:
        return "UI_HERO_ENTER";
    case DIAG_UI_HERO_DELETED:
        return "UI_HERO_DELETED";
    case DIAG_UI_HERO_CREATED:
        return "UI_HERO_CREATED";
    case DIAG_UI_HERO_IMAGED:
        return "UI_HERO_IMAGED";
    case DIAG_UI_DETAIL_TEXT:
        return "UI_DETAIL_TEXT";
    case DIAG_UI_DETAIL_TAB:
        return "UI_DETAIL_TAB";
    case DIAG_UI_POP_ING_ENTER:
        return "UI_POP_ING_ENTER";
    case DIAG_UI_POP_ING_DONE:
        return "UI_POP_ING_DONE";
    case DIAG_UI_POP_METH_ENTER:
        return "UI_POP_METH_ENTER";
    case DIAG_UI_POP_METH_DONE:
        return "UI_POP_METH_DONE";
    case DIAG_UI_DETAIL_ANIM:
        return "UI_DETAIL_ANIM";
    case DIAG_UI_PUSH_THUMB:
        return "UI_PUSH_THUMB";
    case DIAG_UI_DETAIL_DONE:
        return "UI_DETAIL_DONE";
    case DIAG_APP_FETCH_WAIT:
        return "APP_FETCH_WAIT";
    case DIAG_APP_FETCH_DONE:
        return "APP_FETCH_DONE";
    case DIAG_APP_MAINLOOP:
        return "APP_MAINLOOP";
    case DIAG_THUMB_PUSH:
        return "THUMB_PUSH";
    case DIAG_THUMB_CANCEL:
        return "THUMB_CANCEL";
    case DIAG_THUMB_RECV:
        return "THUMB_RECV";
    case DIAG_THUMB_STALE:
        return "THUMB_STALE";
    case DIAG_THUMB_FETCH_ENTER:
        return "THUMB_FETCH_ENTER";
    case DIAG_THUMB_CACHE_GET_ENTER:
        return "THUMB_CACHE_GET_ENTER";
    case DIAG_THUMB_CACHE_GET_DONE:
        return "THUMB_CACHE_GET_DONE";
    case DIAG_THUMB_NET_ENTER:
        return "THUMB_NET_ENTER";
    case DIAG_THUMB_NET_DONE:
        return "THUMB_NET_DONE";
    case DIAG_THUMB_CACHE_PUT:
        return "THUMB_CACHE_PUT";
    case DIAG_THUMB_DECODE_ENTER:
        return "THUMB_DECODE_ENTER";
    case DIAG_THUMB_DECODE_DONE:
        return "THUMB_DECODE_DONE";
    case DIAG_THUMB_APPLY_ENTER:
        return "THUMB_APPLY_ENTER";
    case DIAG_THUMB_APPLY_DONE:
        return "THUMB_APPLY_DONE";
    case DIAG_THUMB_WAIT:
        return "THUMB_WAIT";
    case DIAG_WIFI_CONNECT_ENTER:
        return "WIFI_CONNECT_ENTER";
    case DIAG_WIFI_CMD:
        return "WIFI_CMD";
    case DIAG_WIFI_CONNECT_SENT:
        return "WIFI_CONNECT_SENT";
    case DIAG_WIFI_WAIT:
        return "WIFI_WAIT";
    case DIAG_WIFI_EVENT:
        return "WIFI_EVENT";
    case DIAG_IP_EVENT:
        return "IP_EVENT";
    case DIAG_IO_JOB:
        return "IO_JOB";
    case DIAG_IO_LOAD_ENTER:
        return "IO_LOAD_ENTER";
    case DIAG_IO_LOAD_DONE:
        return "IO_LOAD_DONE";
    case DIAG_IO_PURGE_ENTER:
        return "IO_PURGE_ENTER";
    case DIAG_IO_PURGE_DONE:
        return "IO_PURGE_DONE";
    case DIAG_IO_GET_LOOKUP:
        return "IO_GET_LOOKUP";
    case DIAG_IO_GET_LOOKUP_DONE:
        return "IO_GET_LOOKUP_DONE";
    case DIAG_IO_GET_WAIT:
        return "IO_GET_WAIT";
    case DIAG_IO_GET_OPEN:
        return "IO_GET_OPEN";
    case DIAG_IO_GET_READ:
        return "IO_GET_READ";
    case DIAG_IO_GET_DONE:
        return "IO_GET_DONE";
    case DIAG_IO_PUT_ENTER:
        return "IO_PUT_ENTER";
    case DIAG_IO_PUT_SPACE:
        return "IO_PUT_SPACE";
    case DIAG_IO_PUT_OPEN:
        return "IO_PUT_OPEN";
    case DIAG_IO_PUT_WRITE:
        return "IO_PUT_WRITE";
    case DIAG_IO_PUT_CLOSE:
        return "IO_PUT_CLOSE";
    case DIAG_IO_PUT_INDEX:
        return "IO_PUT_INDEX";
    case DIAG_IO_PUT_DONE:
        return "IO_PUT_DONE";
    case DIAG_IO_IDX_BUILD:
        return "IO_IDX_BUILD";
    case DIAG_IO_IDX_OPEN:
        return "IO_IDX_OPEN";
    case DIAG_IO_IDX_WRITE:
        return "IO_IDX_WRITE";
    case DIAG_IO_IDX_FLUSH:
        return "IO_IDX_FLUSH";
    case DIAG_IO_IDX_CLOSE:
        return "IO_IDX_CLOSE";
    case DIAG_IO_IDX_UNLINK:
        return "IO_IDX_UNLINK";
    case DIAG_IO_IDX_RENAME:
        return "IO_IDX_RENAME";
    case DIAG_IO_IDX_DONE:
        return "IO_IDX_DONE";
    case DIAG_IO_WAIT:
        return "IO_WAIT";
    case DIAG_MON:
        return "MON";
    case DIAG_MON_LATE:
        return "MON_LATE";
    case DIAG_MON_TICK_STALL:
        return "MON_TICK_STALL";
    case DIAG_MON_TICK_RESUME:
        return "MON_TICK_RESUME";
    case DIAG_MON_LVGL_STALL:
        return "MON_LVGL_STALL";
    case DIAG_LVGL_BEAT:
        return "LVGL_BEAT";
    default:
        return "?";
    }
}

static const char *task_name(uint32_t id)
{
    switch (id)
    {
    case DIAG_TASK_MAIN:
        return "main";
    case DIAG_TASK_HB0:
        return "hb0";
    case DIAG_TASK_HB1:
        return "hb1";
    case DIAG_TASK_IO:
        return "cache_io";
    case DIAG_TASK_THUMB:
        return "thumb_worker";
    case DIAG_TASK_FETCH:
        return "combinedFetch";
    case DIAG_TASK_WIFI:
        return "wifi_task";
    case DIAG_TASK_LVGL:
        return "lvgl";
    case DIAG_TASK_TIMER:
        return "esp_timer";
    case DIAG_TASK_EVENT:
        return "esp_event";
    case DIAG_TASK_IDLE:
        return "idle";
    default:
        return "unknown";
    }
}

static void dumpPreviousRun(int reset_reason)
{
    // The ROM printf behind ESP_EARLY_LOG has no long-long support, so every
    // value here is printed as a 32-bit int/unsigned (all are milliseconds).
    ESP_EARLY_LOGW(TAG, "===== previous run #%u ended: reset_reason=%d =====",
                   (unsigned)s_rtc.boot_seq, reset_reason);
    ESP_EARLY_LOGW(TAG,
                   "monitor: %u callbacks, first %u ms, last %u ms, gap last/max %u/%u ms",
                   (unsigned)s_rtc.mon_count,
                   (unsigned)(s_rtc.mon_first_us / 1000),
                   (unsigned)(s_rtc.mon_last_us / 1000),
                   (unsigned)s_rtc.mon_last_period_ms,
                   (unsigned)s_rtc.mon_max_period_ms);

    for (int c = 0; c < 2; ++c)
    {
        ESP_EARLY_LOGW(TAG,
                       "core%d: last tick ISR @%u ms, last monitor tick @%u ms (count %u), "
                       "stall_mask=%u stall@%u ms resume@%u ms, last HB @%u ms",
                       c,
                       (unsigned)(s_rtc.tick_isr_us[c] / 1000),
                       (unsigned)(s_rtc.tick_last_us[c] / 1000),
                       (unsigned)s_rtc.tick_count[c],
                       (unsigned)((s_rtc.tick_stall_mask >> c) & 1u),
                       (unsigned)(s_rtc.tick_stall_us[c] / 1000),
                       (unsigned)(s_rtc.tick_resume_us[c] / 1000),
                       (unsigned)(s_rtc.hb_us[c] / 1000));

        // What the core was running when its last tick arrived, and the PC that
        // tick interrupted. In a spinlock/critical-section deadlock these point
        // at the code that stopped the ticks (symbolize with addr2line).
        ESP_EARLY_LOGW(TAG, "core%d: last tick task=%s, cache=%u, interrupted pc=0x%x",
                       c, task_name(s_rtc.cur_task_id[c]),
                       (unsigned)s_rtc.tick_cache_enabled[c],
                       (unsigned)s_rtc.tick_isr_pc[c]);
    }

    ESP_EARLY_LOGW(TAG, "lvgl: %u beats, last @%u ms",
                   (unsigned)s_rtc.lvgl_count,
                   (unsigned)(s_rtc.lvgl_last_us / 1000));

    // Merge both per-core rings and print the tail in chronological order.
    // Static rather than on the stack: diag_init() runs on the small app_main
    // task stack and the entries stay live across the logging below.
    static diag_entry_t merged[2 * DIAG_RING_LEN];
    size_t n = 0;
    for (int c = 0; c < 2; ++c)
    {
        const uint32_t head = s_rtc.ring_head[c];
        const uint32_t count = head < DIAG_RING_LEN ? head : DIAG_RING_LEN;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t slot = (head - count + i) % DIAG_RING_LEN;
            merged[n++] = s_rtc.ring[c][slot];
        }
    }

    // Insertion sort by millisecond stamp: a few dozen entries at boot, no
    // std::sort to keep this file free of library dependencies.
    for (size_t i = 1; i < n; ++i)
    {
        diag_entry_t key = merged[i];
        size_t j = i;
        while (j > 0 && merged[j - 1].ms > key.ms)
        {
            merged[j] = merged[j - 1];
            --j;
        }
        merged[j] = key;
    }

    const size_t print_from = n > 64 ? n - 64 : 0;
    for (size_t i = print_from; i < n; ++i)
    {
        ESP_EARLY_LOGW(TAG, "  %u ms core%u %s arg=%u",
                       (unsigned)merged[i].ms, (unsigned)merged[i].core,
                       bc_name(merged[i].code), (unsigned)merged[i].arg);
    }
    ESP_EARLY_LOGW(TAG, "===== end of previous run =====");
}

// ── Init ─────────────────────────────────────────────────────────────────────

void diag_init(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const int reset_reason = (int)esp_reset_reason();

    const bool have_prev = (s_rtc.magic == DIAG_MAGIC) &&
                           (s_rtc.version == DIAG_VER) &&
                           (s_rtc.boot_seq != 0);

    if (have_prev)
    {
        dumpPreviousRun(reset_reason);
        s_rtc.boot_seq = s_rtc.boot_seq + 1;
    }
    else
    {
        memset(&s_rtc, 0, sizeof(s_rtc));
        s_rtc.magic = DIAG_MAGIC;
        s_rtc.version = DIAG_VER;
        s_rtc.boot_seq = 1;
        ESP_EARLY_LOGW(TAG, "RTC diag log initialised (reset_reason=%d)", reset_reason);
    }

    // Fresh per-run state; the rings are overwritten as new breadcrumbs land.
    s_rtc.ring_head[0] = 0;
    s_rtc.ring_head[1] = 0;
    s_rtc.mon_count = 0;
    s_rtc.mon_first_us = 0;
    s_rtc.mon_prev_us = esp_timer_get_time();
    s_rtc.mon_last_us = 0;
    s_rtc.mon_last_period_ms = 0;
    s_rtc.mon_max_period_ms = 0;
    s_rtc.mon_last_stall_log_us = 0;
    s_rtc.tick_stall_mask = 0;
    s_rtc.lvgl_last_us = 0;
    s_rtc.lvgl_count = 0;
    s_rtc.lvgl_stall_logged = 0;
    for (int c = 0; c < 2; ++c)
    {
        s_rtc.tick_count[c] = 0;
        s_rtc.tick_last_us[c] = 0;
        s_rtc.tick_isr_us[c] = 0;
        s_rtc.tick_isr_pc[c] = 0;
        s_rtc.cur_task_id[c] = DIAG_TASK_UNKNOWN;
        s_rtc.tick_cache_enabled[c] = 1; // cache on until proven otherwise
        s_rtc.tick_stall_us[c] = 0;
        s_rtc.tick_resume_us[c] = 0;
        s_rtc.hb_us[c] = 0;
        s_rtc.hb_tick[c] = 0;
    }
    s_tick_count[0] = 0;
    s_tick_count[1] = 0;

    if (esp_register_freertos_tick_hook_for_cpu(tickProbeCpu0, 0) != ESP_OK ||
        esp_register_freertos_tick_hook_for_cpu(tickProbeCpu1, 1) != ESP_OK)
    {
        ESP_LOGE(TAG, "tick probe registration failed");
    }

    for (int core = 0; core < 2; ++core)
    {
        TaskHandle_t hb_handle = nullptr;
        if (xTaskCreatePinnedToCore(diagHeartbeatTask, "hb", 4096,
                                    (void *)(intptr_t)core, 1, &hb_handle, core) != pdPASS)
        {
            ESP_LOGE(TAG, "heartbeat task for core %d failed", core);
        }
        else
        {
            diag_task_register(hb_handle, core == 0 ? DIAG_TASK_HB0 : DIAG_TASK_HB1);
        }
    }

    // app_main runs in the main task; name it for the tick probes.
    diag_task_register(xTaskGetCurrentTaskHandle(), DIAG_TASK_MAIN);

    esp_timer_create_args_t args = {};
    args.callback = diagMonitorCb;
    args.arg = nullptr;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "diag_mon";
    args.skip_unhandled_events = true;

    esp_timer_handle_t monitor = nullptr;
    if (esp_timer_create(&args, &monitor) == ESP_OK)
    {
        if (esp_timer_start_periodic(monitor, DIAG_MON_PERIOD_US) != ESP_OK)
            ESP_LOGE(TAG, "monitor timer start failed");
    }
    else
    {
        ESP_LOGE(TAG, "monitor timer create failed");
    }

    ESP_EARLY_LOGW(TAG, "diag v%d: %s rev v%d.%d, %d core(s), reset_reason=%d",
                   DIAG_VER, CONFIG_IDF_TARGET, chip.revision / 100,
                   chip.revision % 100, chip.cores, reset_reason);

    diag_bc(DIAG_APP_BOOT, 0);
}
