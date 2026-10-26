#pragma once

#include <stdint.h>

// ── DIAGNOSTIC (temporary) ───────────────────────────────────────────────────
//
// Crash forensics for the watchdog resets that leave no Guru output.
//
// Every reset the device produces is an INT-WDT reset: rst:0x7
// (HP_SYS_HP_WDT_RESET) when the panic handler never disables the timer-group
// watchdogs, rst:0x10 (CHIP_LP_WDT_RESET) when the panic handler itself wedges.
// The suspended PCs show both cores inside the panic entry, but the console
// shows no panic text, so by the time the panic starts the interesting part is
// already over.
//
// This module records state into RTC (LP) RAM, which survives the reset, and
// logs it on the next boot:
//
//   * a breadcrumb ring (code + argument + core + time) written by the UI,
//     thumbnail worker and cache IO task,
//   * per-core FreeRTOS tick counters and the last tick-ISR timestamp,
//   * an esp_timer-driven monitor that runs even when the FreeRTOS tick stops
//     and logs live whenever a core stops ticking while esp_timer still
//     advances.
//
// The distinction the monitor makes is the decisive one: if it keeps logging
// during the silence, interrupts and the scheduler are alive and only the
// OS-tick alarm stopped (the failure GH espressif/esp-idf#18899 describes). If
// its logs stop too, interrupts are globally blocked or both systimer
// timebases froze, which points at the flash cache-disable / cross-core stall
// path instead.
//
// Remove this module and its call sites once the resets are understood.

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        DIAG_NONE = 0,

        // Application lifecycle
        DIAG_APP_BOOT = 1,
        DIAG_APP_HW_READY,
        DIAG_APP_QUEUES_READY,
        DIAG_APP_LOOP,
        DIAG_APP_TASKS_ENTER,
        DIAG_APP_TASKS_ALLOC,
        DIAG_APP_TASKS_CREATED,
        DIAG_APP_FETCH_ENTER,
        DIAG_APP_FETCH_UI,

        // Recipe detail screen construction (LVGL task)
        DIAG_UI_DETAIL_ENTER = 10,
        DIAG_UI_DETAIL_CANCELLED,
        DIAG_UI_HERO_ENTER,
        DIAG_UI_HERO_DELETED,
        DIAG_UI_HERO_CREATED,
        DIAG_UI_HERO_IMAGED,
        DIAG_UI_DETAIL_TEXT,
        DIAG_UI_DETAIL_TAB,
        DIAG_UI_POP_ING_ENTER,
        DIAG_UI_POP_ING_DONE,
        DIAG_UI_POP_METH_ENTER,
        DIAG_UI_POP_METH_DONE,
        DIAG_UI_DETAIL_ANIM,
        DIAG_UI_PUSH_THUMB,
        DIAG_UI_DETAIL_DONE,

        // Application lifecycle (continued; free slots after the UI group)
        DIAG_APP_FETCH_WAIT = 25,
        DIAG_APP_FETCH_DONE,
        DIAG_APP_MAINLOOP,

        // Thumbnail worker (core 1)
        DIAG_THUMB_PUSH = 40,
        DIAG_THUMB_CANCEL,
        DIAG_THUMB_RECV,
        DIAG_THUMB_STALE,
        DIAG_THUMB_FETCH_ENTER,
        DIAG_THUMB_CACHE_GET_ENTER,
        DIAG_THUMB_CACHE_GET_DONE,
        DIAG_THUMB_NET_ENTER,
        DIAG_THUMB_NET_DONE,
        DIAG_THUMB_CACHE_PUT,
        DIAG_THUMB_DECODE_ENTER,
        DIAG_THUMB_DECODE_DONE,
        DIAG_THUMB_APPLY_ENTER,
        DIAG_THUMB_APPLY_DONE,
        DIAG_THUMB_WAIT,

        // WiFi manager (wifi_task, esp_event task)
        DIAG_WIFI_CONNECT_ENTER = 60,
        DIAG_WIFI_CMD,
        DIAG_WIFI_CONNECT_SENT,
        DIAG_WIFI_WAIT,
        DIAG_WIFI_EVENT,
        DIAG_IP_EVENT,

        // cache_io task (core 0)
        DIAG_IO_JOB = 70,
        DIAG_IO_LOAD_ENTER,
        DIAG_IO_LOAD_DONE,
        DIAG_IO_PURGE_ENTER,
        DIAG_IO_PURGE_DONE,
        DIAG_IO_GET_LOOKUP,
        DIAG_IO_GET_LOOKUP_DONE,
        DIAG_IO_GET_WAIT,
        DIAG_IO_GET_OPEN,
        DIAG_IO_GET_READ,
        DIAG_IO_GET_DONE,
        DIAG_IO_PUT_ENTER,
        DIAG_IO_PUT_SPACE,
        DIAG_IO_PUT_OPEN,
        DIAG_IO_PUT_WRITE,
        DIAG_IO_PUT_CLOSE,
        DIAG_IO_PUT_INDEX,
        DIAG_IO_PUT_DONE,
        DIAG_IO_IDX_BUILD,
        DIAG_IO_IDX_OPEN,
        DIAG_IO_IDX_WRITE,
        DIAG_IO_IDX_FLUSH,
        DIAG_IO_IDX_CLOSE,
        DIAG_IO_IDX_UNLINK,
        DIAG_IO_IDX_RENAME,
        DIAG_IO_IDX_DONE,
        DIAG_IO_WAIT,

        // Liveness monitor / LVGL
        DIAG_MON = 100,
        DIAG_MON_LATE,
        DIAG_MON_TICK_STALL,
        DIAG_MON_TICK_RESUME,
        DIAG_MON_LVGL_STALL,
        DIAG_LVGL_BEAT,
    } diag_code_t;

    // Dumps the previous run's RTC log, then starts the tick probes, heartbeat
    // tasks and liveness monitor. Call once, early, from app_main.
    void diag_init(void);

    // Records one breadcrumb. Cheap, task-safe, callable from any task.
    void diag_bc(uint16_t code, uint16_t arg);

    // Called from a periodic LVGL timer so the log shows whether the LVGL task
    // kept running while everything else stalled.
    void diag_lvgl_beat(void);

    // Task identities recorded by the tick probes. Every tick writes the id and
    // FreeRTOS priority of the task each core was running, so the next boot
    // shows what was executing when the ticks stopped. Only tasks registered
    // here can be identified; anything else is reported as unknown.
    typedef enum
    {
        DIAG_TASK_UNKNOWN = 0,
        DIAG_TASK_MAIN,
        DIAG_TASK_HB0,
        DIAG_TASK_HB1,
        DIAG_TASK_IO,
        DIAG_TASK_THUMB,
        DIAG_TASK_FETCH,
        DIAG_TASK_WIFI,
        DIAG_TASK_LVGL,
        DIAG_TASK_TIMER,
        DIAG_TASK_EVENT,
        DIAG_TASK_IDLE = 15,
    } diag_task_t;

    void diag_task_register(const void *tcb, uint8_t id);

#ifdef __cplusplus
}
#endif
