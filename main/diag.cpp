#include "diag.h"

#include "sdkconfig.h"

#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_freertos_hooks.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/cache_utils.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/assist_debug_hal.h"

#if CONFIG_IDF_TARGET_ESP32P4
// LP_CLKRST_RESET_CAUSE_REG latches the raw cause of the last reset per core.
// IDF never reads or clears it, so it still describes the previous run here.
#include "soc/lp_clkrst_reg.h"
#include "soc/soc.h"
#endif

#include <cstdio>
#include <cstring>

// The draw guard below needs LVGL's draw task, layer and blend descriptor
// layouts, so it includes the same private headers LVGL compiles against. The
// lvgl component exports both its root and its `src` directory as public
// include dirs, so these paths resolve from the app.
#include "lvgl.h"
#include "draw/lv_draw_buf.h"
#include "draw/lv_draw_private.h"
#include "draw/sw/blend/lv_draw_sw_blend.h"
#include "draw/sw/blend/lv_draw_sw_blend_private.h"

static const char *TAG = "DIAG";

// ── DIAGNOSTIC (temporary): post-mortem state in RTC RAM ─────────────────────
//
// See the comment at the top of diag.h for why this exists. Everything here is
// deliberately dependency-free: plain stores, no locks, no allocation, so the
// probes cannot be the thing that hangs.

#define DIAG_MAGIC 0x44494147u // "DIAG"
// v12: store watchpoints removed (heap poisoning tripped them); layout report
//      kept.
// v11: draw-buffer free quarantine, boot layout report and store watchpoints.
// v10: TCM/RTC region acceptance, per-core last-blend snapshots, heap log.
#define DIAG_VER 12

// Set to 1 to log panic entries live from the probe. Off in v7: in v6 the
// live log call wedged right after emitting "W (25913) DIAG: p", so the panic
// handler never armed the RTC WDT, never disabled the TIMG WDTs and never
// printed the Guru backtrace; 12 s later the TIMG stage-1 reset fired. The RTC
// records below survive that reset and are dumped on the next boot, and with
// this quiet the real panic handler gets its own chance to print.
#define DIAG_LIVE_PANIC_LOG 0

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

// Recorded around every esp_cache_msync() call by the linker-wrapped entry
// point below. esp_cache_msync holds a spinlock with interrupts disabled for
// the whole writeback, and the DPI flush in LVGL direct mode asks it to write
// back a full 800x1280 RGB565 framebuffer (~2 MB) every frame, so these
// numbers are the length of the interrupt-free windows the system runs in.
typedef struct
{
    uint32_t in_flight; // 1 from wrapper entry until the real call returns
    uint32_t size;      // bytes of the in-flight/last call
    uint32_t flags;
    uint32_t count;    // completed calls since boot
    uint32_t last_us;  // duration of the last completed call
    uint32_t max_us;   // longest completed call
    uint32_t max_size; // size of the longest completed call
    uint32_t max_flags;
    int64_t enter_us; // start of the in-flight/last call
    int64_t exit_us;  // end of the last completed call
} diag_msync_t;

// Written by the linker-wrapped esp_restart()/esp_restart_noos() entry points
// (main/CMakeLists.txt) just before the chip goes down. A watchdog panic sets a
// reset-reason hint, so the next boot reports ESP_RST_PANIC/INT_WDT/TASK_WDT; a
// boot that reports ESP_RST_SW together with this record is a deliberate
// software restart, and caller_pc names the code that asked for it (symbolize
// with addr2line against the ELF of that build).
typedef struct
{
    uint32_t valid;       // 0 none, 2 esp_restart(), 3 esp_restart_noos()
    uint32_t core;        // core that called it
    uint32_t caller_pc;   // return address inside the caller
    uint32_t caller_task; // diag_task_t id of the calling task, when known
    int64_t at_us;        // esp_timer time of the call
} diag_restart_t;

// Panic-handler entries, recorded by the wrapped entry-count function before
// it increments the real counter. Entry 1 is the root fault; a second or third
// entry means the panic handler itself faulted and re-entered, which ends in
// "Panic handler entered multiple times" and a reset without a reset-reason
// hint (so the next boot reports ESP_RST_SW, not ESP_RST_PANIC).
//
// mepc is the PC of the faulting instruction of that entry, mcause the RISC-V
// trap cause (bit 31 set = interrupt, otherwise exception) and mtval its trap
// value (faulting address for load/store faults, instruction for illegal
// instructions, and so on).
#define DIAG_PANIC_SLOTS 4

typedef struct
{
    uint32_t seq;      // 1-based panic-handler entry number on this core
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtval;
    uint32_t task_id;  // diag_task_t interrupted by the entry
    uint32_t cache_on; // ext-mem cache state at the entry
    int64_t at_us;
} diag_panic_entry_t;

// Register order of the trap frame the RISC-V exception vector builds and
// hands to xt_unhandled_exception() (components/riscv/include/riscv/rvruntime-frames.h).
//   0 mepc   1 ra     2 sp     3 gp     4 tp     5 t0     6 t1     7 t2
//   8 s0     9 s1    10 a0    11 a1    12 a2    13 a3    14 a4    15 a5
//  16 a6    17 a7    18 s2    19 s3    20 s4    21 s5    22 s6    23 s7
//  24 s8    25 s9    26 s10   27 s11   28 t3    29 t4    30 t5    31 t6
//  32 mstatus 33 mtvec 34 mcause 35 mtval 36 mhartid
#define DIAG_EXC_FRAME_WORDS 37

// Words of the faulting task's stack copied from frame->sp upwards. Stacks grow
// down, so [sp, pxEndOfStack) is the used part: the innermost frames and the
// saved return addresses of the current call chain. 640 bytes is roughly the
// first 6-10 frames.
#define DIAG_EXC_STACK_WORDS 160
#define DIAG_EXC_NAME_LEN 16

// Captured in the linker-wrapped xt_unhandled_exception(), the port entry for
// synchronous faults, before panic_enable_cache() and the panic handler run.
// Only the first fault per core is kept; the panic path itself may fault, and
// overwriting the root frame with a secondary one would hide the cause.
typedef struct
{
    uint32_t valid;
    uint32_t core;
    uint32_t cache_on;
    uint32_t frame_ptr; // address of the RvExcFrame handed to the exception path
    uint32_t tcb;
    uint32_t task_id;
    char name[DIAG_EXC_NAME_LEN]; // FreeRTOS task name copied from the TCB
    uint32_t sp_min;              // HW stack-guard bounds for this core
    uint32_t sp_max;
    uint32_t frame[DIAG_EXC_FRAME_WORDS];
    uint32_t stack_sp;      // frame->sp, the pre-trap stack pointer
    uint32_t stack_words;   // words copied into stack[]
    uint32_t stack_clamped; // 0 copied, 1 sp outside the guard bounds, 2 cache off (not attempted)
    uint32_t stack[DIAG_EXC_STACK_WORDS];
    int64_t at_us;
} diag_exception_t;

// Why the draw guard in blendGuard() rejected a blend. Several bits can be
// set at once; the combination says whether the draw buffer was replaced or
// its fields were modified (NO_ENTRY/MUTATED/BAD_MAGIC) or whether the blend
// area no longer maps inside the buffer (COORD/NEG/RANGE).
#define DIAG_BLEND_R_NULL 0x0001u      // layer, draw_buf or descriptor not in RAM
#define DIAG_BLEND_R_BAD_MAGIC 0x0002u // draw_buf header magic missing
#define DIAG_BLEND_R_NO_ENTRY 0x0004u  // draw_buf was never created by LVGL
#define DIAG_BLEND_R_MUTATED 0x0008u   // draw_buf fields changed since creation
#define DIAG_BLEND_R_COORD 0x0010u     // x/y outside the buffer's own geometry
#define DIAG_BLEND_R_NEG 0x0020u       // area starts before the buffer
#define DIAG_BLEND_R_RANGE 0x0040u     // area ends past the buffer's allocation
#define DIAG_BLEND_R_DESC 0x0080u      // the draw task or blend descriptor itself is not in RAM
#define DIAG_BLEND_R_SRC 0x0100u       // the source/mask read window leaves mapped memory

// First software blend whose destination was not inside a draw buffer LVGL had
// created. Written by the wrapped lv_draw_sw_blend() *before* the store runs,
// so it survives even when the skipped blend was the one that used to corrupt
// lv_global or the swdraw stack. Dumped on the next boot.
typedef struct
{
    uint32_t valid;
    uint32_t count;
    uint32_t reason; // DIAG_BLEND_R_*
    uint32_t ms;
    uint32_t core;
    uint32_t task_id;
    uint32_t ra;  // return address into the lv_draw_sw_* caller
    uint32_t dsc; // lv_draw_sw_blend_dsc_t* (kept raw: it may be the bad pointer)
    uint32_t t;   // lv_draw_task_t*
    uint32_t t_type;
    uint32_t t_area[4];
    uint32_t t_clip[4];
    uint32_t layer; // lv_layer_t*
    uint32_t layer_parent;
    uint32_t layer_buf_area[4];
    uint32_t layer_cf; // layer->color_format; selects the blender, must equal db_cf
    uint32_t draw_buf; // lv_draw_buf_t*
    uint32_t db_magic;
    uint32_t db_data; // draw_buf fields as they are at blend time
    uint32_t db_size;
    uint32_t db_stride;
    uint32_t db_w;
    uint32_t db_h;
    uint32_t db_cf;
    uint32_t reg_data; // draw_buf fields as recorded when LVGL created it
    uint32_t reg_size;
    uint32_t reg_stride;
    uint32_t reg_w;
    uint32_t reg_h;
    uint32_t reg_cf;
    uint32_t found;
    uint32_t reg_over; // registry slots evicted while still in use (see bufRecAdd)
    uint32_t blend_area[4];
    uint32_t opa;
    uint32_t color;
    uint32_t src_buf;
    uint32_t mask_buf;
    uint32_t dx;
    uint32_t dy;
    uint32_t dest_w;
    uint32_t dest_h;
    uint32_t start_hi;
    uint32_t start_lo;
    uint32_t end_hi;
    uint32_t end_lo;
    uint32_t dest; // data + start, when start >= 0
} diag_blend_t;

// The last blend the guard *accepted*, kept per core in DRAM and copied into
// RTC RAM when the fault is captured (or by the monitor, so it also survives a
// plain hang). A fault inside a fill names the destination it was writing, but
// the trap frame alone cannot say which buffer that destination belonged to;
// this record can: data/size/stride and dx/dy come from the same validation
// that let the blend run.
typedef struct
{
    uint32_t seq; // accepted blends on this core when the record was written
    uint32_t ms;
    uint32_t core;
    uint32_t task_id;
    uint32_t ra;
    uint32_t t;
    uint32_t layer;
    uint32_t draw_buf;
    uint32_t data; // draw_buf->data as validated
    uint32_t size; // registry data_size (the allocation the guard checked)
    uint32_t stride;
    uint32_t dx;
    uint32_t dy;
    uint32_t w;
    uint32_t h;
    uint32_t start_hi;
    uint32_t start_lo;
    uint32_t end_hi;
    uint32_t end_lo;
    uint32_t dest; // data + start
    uint32_t src_buf;
    uint32_t mask_buf;
    uint32_t opa;
    uint32_t color;
} diag_last_blend_t;

// Blend-guard counters, per core, in DRAM. Copied into RTC RAM together with
// the records above; a rising src_bad count with a crash is the source-side
// analogue of the destination guard.
#define DIAG_BLEND_STAT_ACC 0     // blends that passed validation and ran
#define DIAG_BLEND_STAT_REJ 1     // blends rejected and skipped
#define DIAG_BLEND_STAT_SRC 2     // rejected because of the source/mask window
#define DIAG_BLEND_STAT_DESC 3    // rejected because t/dsc were not in RAM
typedef struct
{
    uint32_t valid;
    diag_last_blend_t last[2];
    diag_blend_t last_rej[2];
    uint32_t stats[2][4];
} diag_blend_snap_t;

// Draw-buffer free quarantine state (see quar_park below). A hit records the
// buffer that was painted after destroy and the last blend each core had
// accepted when the buffer was parked — the blend that was in flight at the
// destroy, if the race is real.
typedef struct
{
    uint32_t valid;    // 1 once the quarantine has parked or retired anything
    uint32_t parked;   // buffers parked since boot
    uint32_t evicted;  // parked buffers scanned and really freed
    uint32_t hits;     // evicted buffers that contained the page background
    uint32_t skipped;  // buffers that could not be parked
    uint32_t active;   // parked slots at the last update
    uint32_t bytes;    // parked data bytes at the last update
    uint32_t last_buf;
    uint32_t last_data;
    uint32_t last_size;
    uint32_t last_stride;
    uint32_t last_w;
    uint32_t last_h;
    uint32_t last_cf;
    uint32_t last_off;  // byte offset of the first page-background word
    uint32_t last_hits; // page-background words found
    uint32_t last_dirty_off;   // byte offset of the first word not holding the fill
    uint32_t last_dirty_val;   // that word's value
    uint32_t last_dirty_count; // words that no longer hold the 0xA5 fill
    uint32_t last_ms;   // when the hit was found (eviction time)
    uint32_t last_core; // core that found it
    uint32_t last_task;
    diag_last_blend_t last_blend[2]; // blends at destroy time
} diag_quar_t;

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

    // RTC (LP timer) wall clock sampled by the monitor. Unlike esp_timer this
    // is not derived from the systimer, so it keeps counting even if the
    // systimer timebase freezes, and it lives in the LP domain that stays
    // powered through a reset. The next boot can therefore measure how long
    // after the last monitor callback the watchdog reset actually happened.
    int64_t rtc_last_us;
    uint32_t rtc_valid; // 1 once esp_clk_rtc_time() returned a usable value

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

    // esp_cache_msync() timing, per core
    diag_msync_t msync[2];

    // Who called esp_restart()/esp_restart_noos() on the run that just ended
    diag_restart_t restart;

    // Panic-handler entries on the run that just ended, per core
    diag_panic_entry_t panic[2][DIAG_PANIC_SLOTS];
    uint32_t panic_entry_count[2];

    // Synchronous fault frames captured before the panic handler runs
    diag_exception_t exc[2];
    uint32_t exc_extra[2]; // further faults on that core (panic path re-faulting)

    // First rejected software blend (see blendGuard below)
    diag_blend_t blend;

    // Blend guard state at the last fault/monitor sample: the last blend that
    // ran and the last one that was rejected, per core, plus the counters.
    diag_blend_snap_t blend_snap;

    // Draw-buffer free quarantine: counts and the last buffer that contained
    // the page background after it was destroyed.
    diag_quar_t quar;

    // Internal/PSRAM heap headroom sampled by the monitor. free_min and
    // largest_min are the minima since boot; a zero/very low value at the time
    // of the crash says the failure happened under allocation pressure.
    uint32_t heap_int_free;
    uint32_t heap_int_free_min;
    uint32_t heap_int_largest;
    uint32_t heap_int_largest_min;
    uint32_t heap_psram_free;
    uint32_t heap_psram_free_min;

    // How far each core got down the panic path, see the stage wrappers
    uint32_t panic_stage[2]; // bit 0 RTC WDT enable, bit 1 TIMG WDTs disabled, bit 2 panic handler
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
static void blendLog(const diag_blend_t *b);
static const char *guard_region(uint32_t addr);

// ── Blend guard live state (plain DRAM) ──────────────────────────────────────
//
// Kept out of RTC RAM because these are written on every blend, and RTC RAM
// accesses are far slower than cached DRAM stores. The draw threads write only
// their own core's slot, so no lock is needed; the snapshot below copies both
// slots, and a torn other-core record is acceptable in a post-mortem.
static diag_last_blend_t s_last_blend[2];
static diag_blend_t s_last_reject[2];
static uint32_t s_blend_stats[2][4];

// Copy the live blend-guard state into RTC RAM. IRAM and pointer loops only:
// it is called from the trap entry, where the flash cache may be off.
static IRAM_ATTR void blendSnapSave(void)
{
    if (s_rtc.magic != DIAG_MAGIC)
        return;

    diag_blend_snap_t *d = &s_rtc.blend_snap;
    const uint32_t *src;
    uint32_t *dst;

    src = (const uint32_t *)s_last_blend;
    dst = (uint32_t *)d->last;
    for (uint32_t i = 0; i < sizeof(d->last) / 4; ++i)
        dst[i] = src[i];

    src = (const uint32_t *)s_last_reject;
    dst = (uint32_t *)d->last_rej;
    for (uint32_t i = 0; i < sizeof(d->last_rej) / 4; ++i)
        dst[i] = src[i];

    src = (const uint32_t *)s_blend_stats;
    dst = (uint32_t *)d->stats;
    for (uint32_t i = 0; i < sizeof(d->stats) / 4; ++i)
        dst[i] = src[i];

    d->valid = 1;
}

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

#if defined(__riscv) && !defined(CONFIG_SECURE_ENABLE_TEE)
// Trap cause/value of the current machine-mode trap. The trap entry saves
// these CSRs but does not modify them, so a probe inside the trap handler
// reads the values of the trap that is being handled.
static inline uint32_t __attribute__((always_inline)) read_trap_cause(void)
{
    uint32_t cause = 0;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    return cause;
}

static inline uint32_t __attribute__((always_inline)) read_trap_val(void)
{
    uint32_t val = 0;
    __asm__ volatile("csrr %0, mtval" : "=r"(val));
    return val;
}
#else
static inline uint32_t __attribute__((always_inline)) read_trap_cause(void)
{
    return 0;
}

static inline uint32_t __attribute__((always_inline)) read_trap_val(void)
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

        // Heap headroom once a second, plus the minima since boot in RTC RAM.
        // Small LVGL structs spill to the TCM/LP heaps (which still count as
        // MALLOC_CAP_INTERNAL) and then to PSRAM as this shrinks; a crash at a
        // low internal free/largest value points at allocation pressure.
        const uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_rtc.heap_int_free = int_free;
        s_rtc.heap_int_largest = int_largest;
        s_rtc.heap_psram_free = psram_free;
        // The heap's own watermarks for free bytes; the largest-block minimum is
        // sampled (no such counter exists), with 0 meaning "not sampled yet".
        s_rtc.heap_int_free_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        s_rtc.heap_psram_free_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        if (s_rtc.heap_int_largest_min == 0 || int_largest < s_rtc.heap_int_largest_min)
            s_rtc.heap_int_largest_min = int_largest;

        // ESP_EARLY_LOG goes through esp_rom_printf instead of the logging
        // mutex, so the surviving core still reports if the other one stopped
        // while holding that mutex. The ROM printf has no long-long support,
        // hence the plain int/unsigned casts for every argument.
        ESP_EARLY_LOGW("HB", "core%d dt=%dms tick0=+%u tick1=+%u (total %u/%u) int=%u/%u psram=%u",
                       core, (int)((now_us - prev_us) / 1000),
                       (unsigned)(tick0 - prev_tick[0]),
                       (unsigned)(tick1 - prev_tick[1]),
                       (unsigned)tick0, (unsigned)tick1,
                       (unsigned)int_free, (unsigned)int_largest,
                       (unsigned)psram_free);

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

    // Independent timebase: the LP timer counts in the always-on domain, so it
    // still advances if the systimer (and with it esp_timer and the FreeRTOS
    // tick) freezes. Its value is retained across the reset, letting the next
    // boot time the gap between this callback and the boot.
    const uint64_t rtc_now = esp_clk_rtc_time();
    if (rtc_now != 0)
    {
        s_rtc.rtc_last_us = (int64_t)rtc_now;
        s_rtc.rtc_valid = 1;
    }

    // Keep the blend-guard records in RTC RAM too, so they survive a plain
    // hang (no fault frame to capture them). Deliberately no heap queries
    // here: they take the heap lock, and this callback must keep running even
    // if a task is stuck inside the allocator.
    blendSnapSave();

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

// ── esp_cache_msync instrumentation ──────────────────────────────────────────

// The wrapper is installed with -Wl,--wrap=esp_cache_msync (main/CMakeLists.txt),
// so every call in the firmware, including the DPI driver's full-framebuffer
// writeback, passes through here first. It only records timestamps; the real
// call is untouched.
static void IRAM_ATTR msyncEnter(size_t size, int flags)
{
    if (s_rtc.magic != DIAG_MAGIC)
        return;

    const int core = esp_cpu_get_core_id() & 1;
    diag_msync_t *m = &s_rtc.msync[core];
    m->in_flight = 1;
    m->size = (uint32_t)size;
    m->flags = (uint32_t)flags;
    m->enter_us = esp_timer_get_time();
}

static void IRAM_ATTR msyncExit(void)
{
    if (s_rtc.magic != DIAG_MAGIC)
        return;

    const int core = esp_cpu_get_core_id() & 1;
    diag_msync_t *m = &s_rtc.msync[core];
    const int64_t now = esp_timer_get_time();
    const uint32_t dur_us = (uint32_t)(now - m->enter_us);

    m->in_flight = 0;
    m->exit_us = now;
    m->last_us = dur_us;
    m->count = m->count + 1;
    if (dur_us > m->max_us)
    {
        m->max_us = dur_us;
        m->max_size = m->size;
        m->max_flags = m->flags;
    }
}

extern "C" esp_err_t __real_esp_cache_msync(void *addr, size_t size, int flags);
extern "C" esp_err_t __wrap_esp_cache_msync(void *addr, size_t size, int flags)
{
    msyncEnter(size, flags);
    const esp_err_t err = __real_esp_cache_msync(addr, size, flags);
    msyncExit();
    return err;
}

// ── esp_restart caller instrumentation ───────────────────────────────────────

static const char *task_name(uint32_t id);

// Records who asked for a software restart. Only the first caller is kept:
// esp_restart() calls esp_restart_noos() internally, and the outer call is the
// one that identifies the responsible component.
static void noteRestart(uint32_t kind, uint32_t caller_pc)
{
    if (s_rtc.magic != DIAG_MAGIC || s_rtc.restart.valid != 0)
        return;

    const int core = esp_cpu_get_core_id() & 1;
    s_rtc.restart.core = (uint32_t)core;
    s_rtc.restart.caller_pc = caller_pc;
    s_rtc.restart.caller_task = current_task_id((BaseType_t)core);
    s_rtc.restart.at_us = esp_timer_get_time();
    s_rtc.restart.valid = kind;

    // Log immediately as well, so the serial capture alone shows the restart
    // even if the next boot's dump is lost. The format string and task names
    // live in flash, so only print while the cache is on - always true for the
    // flash-resident esp_restart paths, and false only for the very earliest
    // boot failures.
    if (spi_flash_cache_enabled())
    {
        ESP_EARLY_LOGW(TAG, "software restart: %s called from pc=0x%x on core%d (task=%s)",
                       kind == 3 ? "esp_restart_noos" : "esp_restart",
                       (unsigned)caller_pc, core, task_name(s_rtc.restart.caller_task));
    }
}

extern "C" void __real_esp_restart(void) __attribute__((noreturn));
extern "C" void __wrap_esp_restart(void) __attribute__((noreturn));
extern "C" void __wrap_esp_restart(void)
{
    noteRestart(2, (uint32_t)(uintptr_t)__builtin_return_address(0));
    __real_esp_restart();
}

extern "C" void __real_esp_restart_noos(void) __attribute__((noreturn));
extern "C" void __wrap_esp_restart_noos(void) __attribute__((noreturn));
extern "C" void __wrap_esp_restart_noos(void)
{
    noteRestart(3, (uint32_t)(uintptr_t)__builtin_return_address(0));
    __real_esp_restart_noos();
}

// ── Panic entry instrumentation ──────────────────────────────────────────────

// Called by the linker-wrapped esp_panic_handler_increment_entry_count(),
// which the port-level panic handler invokes before doing anything else. The
// first entry on a core is the root fault. Kept IRAM-safe: everything here is
// either IRAM/ROM code or RTC RAM, and the log is only printed with the flash
// cache on, because the panic entry may run with the cache still disabled.
static void IRAM_ATTR notePanicEntry(void)
{
    if (s_rtc.magic != DIAG_MAGIC)
        return;

    const int core = esp_cpu_get_core_id() & 1;
    const uint32_t cache_on = spi_flash_cache_enabled() ? 1u : 0u;
    const uint32_t n = s_rtc.panic_entry_count[core];
    if (n < DIAG_PANIC_SLOTS)
    {
        diag_panic_entry_t *e = &s_rtc.panic[core][n];
        e->seq = n + 1;
        e->mepc = read_trap_pc();
        e->mcause = read_trap_cause();
        e->mtval = read_trap_val();
        e->cache_on = cache_on;
        // Only the CSR reads above are safe with the cache off; everything
        // else may be flash code, so skip it in that case rather than fault
        // inside the probe and add entries of our own.
        e->task_id = cache_on ? current_task_id((BaseType_t)core) : DIAG_TASK_UNKNOWN;
        e->at_us = cache_on ? esp_timer_get_time() : 0;
    }
    s_rtc.panic_entry_count[core] = n + 1;

#if DIAG_LIVE_PANIC_LOG
    // Log live as well: if the panic handler wedges after this entry instead
    // of resetting, the serial capture still shows the root fault's PC/cause.
    if (cache_on)
    {
        const diag_panic_entry_t *e = n < DIAG_PANIC_SLOTS ? &s_rtc.panic[core][n] : nullptr;
        ESP_EARLY_LOGW(TAG,
                       "panic entry %u on core%d: mepc=0x%x mcause=0x%x mtval=0x%x cache=%u task=%s",
                       (unsigned)(n + 1), core,
                       e ? (unsigned)e->mepc : 0u, e ? (unsigned)e->mcause : 0u,
                       e ? (unsigned)e->mtval : 0u, e ? (unsigned)e->cache_on : 0u,
                       e ? task_name(e->task_id) : "?");
    }
#endif
}

extern "C" void __real_esp_panic_handler_increment_entry_count(void);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler_increment_entry_count(void);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler_increment_entry_count(void)
{
    notePanicEntry();
    __real_esp_panic_handler_increment_entry_count();
}

// ── Synchronous fault frame capture ──────────────────────────────────────────

// Called by the linker-wrapped xt_unhandled_exception(), the very first code
// that runs for a synchronous fault (instruction/load/store access fault,
// illegal instruction, ...). It runs on the faulting stack, before
// panic_enable_cache() and before the panic handler saves or prints anything,
// so it must stay register/RTC-RAM only while the cache is off. Unlike the
// v6 probe, it deliberately prints nothing: the previous run wedged inside the
// panic-entry log call, which is what stopped the panic handler from ever
// arming the RTC WDT or printing the Guru backtrace.
static void IRAM_ATTR noteException(void *frame)
{
    if (s_rtc.magic != DIAG_MAGIC || frame == nullptr)
        return;

    const int core = esp_cpu_get_core_id() & 1;
    diag_exception_t *e = &s_rtc.exc[core];

    // Keep the first fault on each core; count any later ones (the panic path
    // faulting again) without touching the root frame.
    if (e->valid != 0)
    {
        s_rtc.exc_extra[core] = s_rtc.exc_extra[core] + 1;
        return;
    }

    e->valid = 1; // first, so a fault inside this probe cannot be recorded again
    e->core = (uint32_t)core;
    e->cache_on = spi_flash_cache_enabled() ? 1u : 0u;
    e->frame_ptr = (uint32_t)(uintptr_t)frame;
    e->name[0] = '\0';

    // The vector has already stored the frame at this address, so reading it
    // back cannot fault. Layout: mepc, ra, sp, ... (index table above).
    const uint32_t *w = static_cast<const uint32_t *>(frame);
    for (uint32_t i = 0; i < DIAG_EXC_FRAME_WORDS; ++i)
        e->frame[i] = w[i];

    // What the blend guard was doing on both cores when this fault hit. DRAM
    // is readable with the cache off and the copy is pointer arithmetic only.
    blendSnapSave();

    // Stack-guard bounds the FreeRTOS context switch programmed for the task
    // running on this core. Register reads in a HAL inline, safe with the
    // cache off.
    uint32_t sp_min = 0;
    uint32_t sp_max = 0;
#if SOC_ASSIST_DEBUG_SUPPORTED
    assist_debug_hal_get_sp_bounds((uint32_t)core, &sp_min, &sp_max);
#endif
    e->sp_min = sp_min;
    e->sp_max = sp_max;

    const uint32_t sp = e->frame[2]; // pre-trap sp
    e->stack_sp = sp;
    e->stack_words = 0;
    e->stack_clamped = 0;

    // Only copy inside the task's own stack, and only while the cache is on:
    // a stack allocated in PSRAM would not be readable without it, and a fault
    // inside this probe would only add noise. A sp outside the bounds means the
    // stack pointer itself is suspect; the bounds and sp above are then the
    // useful part, and reading around a bad sp could fault again.
    const bool bounds_ok = sp_min != 0 && sp_max > sp_min && (sp_max - sp_min) <= 0x200000u;
    if (e->cache_on != 0 && bounds_ok && sp >= sp_min && sp < sp_max)
    {
        uint32_t end = sp + DIAG_EXC_STACK_WORDS * 4u;
        if (end > sp_max)
            end = sp_max;
        uint32_t n = (end - sp) / 4u;
        if (n > DIAG_EXC_STACK_WORDS)
            n = DIAG_EXC_STACK_WORDS;
        e->stack_words = n;
        const uint32_t *s = reinterpret_cast<const uint32_t *>(sp);
        for (uint32_t i = 0; i < n; ++i)
            e->stack[i] = s[i];
    }
    else
    {
        e->stack_clamped = e->cache_on != 0 ? 1u : 2u;
    }

    // Naming the faulting task needs the cache (the kernel and the name helper
    // live in flash); the raw frame above is captured either way.
    if (e->cache_on != 0)
    {
        const TaskHandle_t tcb = xTaskGetCurrentTaskHandleForCore((BaseType_t)core);
        e->tcb = (uint32_t)(uintptr_t)tcb;
        e->task_id = current_task_id((BaseType_t)core);
        const char *nm = tcb != nullptr ? pcTaskGetName(tcb) : nullptr;
        if (nm != nullptr)
        {
            uint32_t i = 0;
            for (; i + 1 < DIAG_EXC_NAME_LEN && nm[i] != '\0'; ++i)
                e->name[i] = nm[i];
            for (; i < DIAG_EXC_NAME_LEN; ++i)
                e->name[i] = '\0';
        }
        e->at_us = esp_timer_get_time();
    }
}

extern "C" void __real_xt_unhandled_exception(void *frame);
extern "C" void IRAM_ATTR __wrap_xt_unhandled_exception(void *frame);
extern "C" void IRAM_ATTR __wrap_xt_unhandled_exception(void *frame)
{
    noteException(frame);
    __real_xt_unhandled_exception(frame);
}

// ── Panic path progress ──────────────────────────────────────────────────────

// The port panic handler arms the RTC WDT, then disables the TIMG WDTs, then
// stalls the other core and calls the system panic handler. In v6 the TIMG
// WDTs were never disabled (the reset was HP_SYS_HP_WDT_RESET, the 4 s + 8 s
// stage-1 reset), which places the wedge after the panic entry and before the
// RTC WDT setup. These wrappers leave a trail in RTC RAM so the next dump
// says exactly how far that path got, even when the chip dies before printing.
static void IRAM_ATTR notePanicStage(uint32_t bit)
{
    if (s_rtc.magic == DIAG_MAGIC)
    {
        const int core = esp_cpu_get_core_id() & 1;
        s_rtc.panic_stage[core] = s_rtc.panic_stage[core] | bit;
    }
}

extern "C" void __real_esp_panic_handler_enable_rtc_wdt(uint32_t timeout_ms);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler_enable_rtc_wdt(uint32_t timeout_ms);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler_enable_rtc_wdt(uint32_t timeout_ms)
{
    notePanicStage(1u);
    __real_esp_panic_handler_enable_rtc_wdt(timeout_ms);
}

extern "C" void __real_esp_panic_handler_disable_timg_wdts(void);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler_disable_timg_wdts(void);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler_disable_timg_wdts(void)
{
    notePanicStage(2u);
    __real_esp_panic_handler_disable_timg_wdts();
}

extern "C" void __real_esp_panic_handler(void *info);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(void *info);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(void *info)
{
    notePanicStage(4u);
    __real_esp_panic_handler(info);
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

void diag_heap_log(const char *what)
{
    // heap_caps_get_* take the heap lock; call this from task context only.
    const uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const uint32_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_rtc.magic == DIAG_MAGIC)
    {
        s_rtc.heap_int_free = int_free;
        s_rtc.heap_int_largest = int_largest;
        s_rtc.heap_psram_free = psram_free;
        s_rtc.heap_int_free_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        s_rtc.heap_psram_free_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        if (s_rtc.heap_int_largest_min == 0 || int_largest < s_rtc.heap_int_largest_min)
            s_rtc.heap_int_largest_min = int_largest;
    }

    ESP_LOGI(TAG, "heap[%s]: internal free %u largest %u, psram free %u largest %u",
             what != nullptr ? what : "?", (unsigned)int_free, (unsigned)int_largest,
             (unsigned)psram_free, (unsigned)psram_largest);
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

// Encodings from the LP_CLKRST_HPCORE0_RESET_CAUSE field documentation.
static const char *hw_reset_cause_name(uint32_t cause)
{
    switch (cause)
    {
    case 0x0:
        return "none/cleared";
    case 0x1:
        return "por";
    case 0x3:
        return "sw system";
    case 0x5:
        return "pmu hp sys powerdown";
    case 0x7:
        return "HP sys from HP WDT";
    case 0x9:
        return "HP sys from LP WDT";
    case 0xb:
        return "HP core from HP WDT";
    case 0xc:
        return "HP core sw";
    case 0xd:
        return "HP core from LP WDT";
    case 0xf:
        return "brownout";
    case 0x10:
        return "LP WDT chip";
    case 0x12:
        return "super WDT";
    case 0x13:
        return "glitch";
    case 0x14:
        return "efuse crc";
    case 0x16:
        return "usb jtag chip";
    case 0x17:
        return "usb uart chip";
    case 0x18:
        return "jtag";
    case 0x1a:
        return "HP core lockup";
    default:
        return "?";
    }
}

// Raw ROM reset reasons (esp32p4/rom/rtc.h). esp_reset_reason() maps six of
// them onto the single ESP_RST_WDT value, losing exactly the distinction that
// matters here: which watchdog reset the chip.
static const char *rom_reset_name(uint32_t reason)
{
    switch (reason)
    {
    case 0:
        return "NO_MEAN";
    case 1:
        return "POWERON";
    case 3:
        return "SW_SYS";
    case 5:
        return "PMU_SYS_PWR_DOWN";
    case 7:
        return "HP_SYS_HP_WDT";
    case 9:
        return "HP_SYS_LP_WDT";
    case 11:
        return "HP_CORE_HP_WDT";
    case 12:
        return "SW_CPU";
    case 13:
        return "HP_CORE_LP_WDT";
    case 15:
        return "BROWN_OUT";
    case 16:
        return "CHIP_LP_WDT";
    case 18:
        return "SUPER_WDT";
    case 19:
        return "GLITCH_RTC";
    case 20:
        return "EFUSE_CRC_ERR";
    case 22:
        return "CHIP_USB_JTAG";
    case 23:
        return "CHIP_USB_UART";
    case 24:
        return "JTAG";
    case 26:
        return "CPU_LOCKUP";
    default:
        return "?";
    }
}

static void dumpPreviousRun(int reset_reason, uint32_t rom_reason,
                            uint32_t hw_cause, uint32_t hw_ctl,
                            int32_t rtc_gap_ms)
{
    // The ROM printf behind ESP_EARLY_LOG has no long-long support, so every
    // value here is printed as a 32-bit int/unsigned (all are milliseconds).
    ESP_EARLY_LOGW(TAG, "===== previous run #%u ended: reset_reason=%d, rom_reset=%u (%s) =====",
                   (unsigned)s_rtc.boot_seq, reset_reason,
                   (unsigned)rom_reason, rom_reset_name(rom_reason));

#if CONFIG_IDF_TARGET_ESP32P4
    // The reset_reason above is ESP_RST_WDT for every watchdog flavour. This
    // register keeps the raw cause per core, which tells HP WDT (panic never
    // disabled it) from LP WDT (panic started, then wedged for 10 s).
    const uint32_t lp_cause = hw_cause & 0x3f;
    const uint32_t hp0_cause = (hw_cause >> 7) & 0x3f;
    const uint32_t hp1_cause = (hw_cause >> 14) & 0x3f;
    ESP_EARLY_LOGW(TAG,
                   "hw reset cause: lp=0x%x hp0=0x%x (%s) hp1=0x%x (%s), flags %u/%u/%u, raw=0x%x",
                   (unsigned)lp_cause,
                   (unsigned)hp0_cause, hw_reset_cause_name(hp0_cause),
                   (unsigned)hp1_cause, hw_reset_cause_name(hp1_cause),
                   (unsigned)((hw_cause >> 6) & 1u),
                   (unsigned)((hw_cause >> 13) & 1u),
                   (unsigned)((hw_cause >> 20) & 1u),
                   (unsigned)hw_cause);
    ESP_EARLY_LOGW(TAG, "hpcpu reset ctrl0=0x%x (lockup reset en %u/%u, lp wdt core reset en %u/%u)",
                   (unsigned)hw_ctl,
                   (unsigned)(hw_ctl & 1u), (unsigned)((hw_ctl >> 16) & 1u),
                   (unsigned)((hw_ctl >> 4) & 1u), (unsigned)((hw_ctl >> 20) & 1u));
#else
    (void)hw_cause;
    (void)hw_ctl;
#endif

    // Recorded by the esp_restart()/esp_restart_noos() wrappers just before the
    // chip went down. Paired with ESP_RST_SW above, caller_pc is the code that
    // requested the restart; symbolize it with addr2line against that ELF.
    if (s_rtc.restart.valid != 0)
    {
        ESP_EARLY_LOGW(TAG,
                       "software restart: %s called from pc=0x%x on core%u (task=%s) at %u ms",
                       s_rtc.restart.valid == 3 ? "esp_restart_noos" : "esp_restart",
                       (unsigned)s_rtc.restart.caller_pc,
                       (unsigned)s_rtc.restart.core,
                       task_name(s_rtc.restart.caller_task),
                       (unsigned)(s_rtc.restart.at_us / 1000));
    }

    // Every entry the panic handler took on the run that just ended. Entry 1
    // is the root fault; entries 2+ are the handler itself faulting again.
    for (int c = 0; c < 2; ++c)
    {
        const uint32_t n = s_rtc.panic_entry_count[c];
        for (uint32_t i = 0; i < n && i < DIAG_PANIC_SLOTS; ++i)
        {
            const diag_panic_entry_t *p = &s_rtc.panic[c][i];
            ESP_EARLY_LOGW(TAG,
                           "panic entry %u on core%d: mepc=0x%x mcause=0x%x mtval=0x%x cache=%u task=%s at %u ms",
                           (unsigned)p->seq, c, (unsigned)p->mepc, (unsigned)p->mcause,
                           (unsigned)p->mtval, (unsigned)p->cache_on,
                           task_name(p->task_id), (unsigned)(p->at_us / 1000));
        }
        if (n > DIAG_PANIC_SLOTS)
        {
            ESP_EARLY_LOGW(TAG, "core%d: %u further panic entries not recorded", c,
                           (unsigned)(n - DIAG_PANIC_SLOTS));
        }
    }

    // The synchronous fault frame, captured before the panic handler ran. The
    // words are the register state at the faulting instruction: mepc is where
    // it died, ra the return address it was about to use, sp the pre-trap stack
    // pointer, and the stack window above sp holds the saved return addresses
    // of the current call chain. Symbolize them with addr2line against the ELF
    // of that build.
    for (int c = 0; c < 2; ++c)
    {
        const diag_exception_t *x = &s_rtc.exc[c];
        if (x->valid == 0)
            continue;

        ESP_EARLY_LOGW(TAG,
                       "fault frame core%d: mepc=0x%x ra=0x%x sp=0x%x gp=0x%x tp=0x%x t0=0x%x t1=0x%x t2=0x%x",
                       c, (unsigned)x->frame[0], (unsigned)x->frame[1], (unsigned)x->frame[2],
                       (unsigned)x->frame[3], (unsigned)x->frame[4], (unsigned)x->frame[5],
                       (unsigned)x->frame[6], (unsigned)x->frame[7]);
        ESP_EARLY_LOGW(TAG,
                       "  s0=0x%x s1=0x%x a0=0x%x a1=0x%x a2=0x%x a3=0x%x a4=0x%x a5=0x%x a6=0x%x a7=0x%x",
                       (unsigned)x->frame[8], (unsigned)x->frame[9], (unsigned)x->frame[10],
                       (unsigned)x->frame[11], (unsigned)x->frame[12], (unsigned)x->frame[13],
                       (unsigned)x->frame[14], (unsigned)x->frame[15], (unsigned)x->frame[16],
                       (unsigned)x->frame[17]);
        ESP_EARLY_LOGW(TAG,
                       "  s2=0x%x s3=0x%x s4=0x%x s5=0x%x s6=0x%x s7=0x%x s8=0x%x s9=0x%x s10=0x%x s11=0x%x",
                       (unsigned)x->frame[18], (unsigned)x->frame[19], (unsigned)x->frame[20],
                       (unsigned)x->frame[21], (unsigned)x->frame[22], (unsigned)x->frame[23],
                       (unsigned)x->frame[24], (unsigned)x->frame[25], (unsigned)x->frame[26],
                       (unsigned)x->frame[27]);
        ESP_EARLY_LOGW(TAG,
                       "  t3=0x%x t4=0x%x t5=0x%x t6=0x%x mstatus=0x%x mtvec=0x%x mcause=0x%x mtval=0x%x mhartid=0x%x",
                       (unsigned)x->frame[28], (unsigned)x->frame[29], (unsigned)x->frame[30],
                       (unsigned)x->frame[31], (unsigned)x->frame[32], (unsigned)x->frame[33],
                       (unsigned)x->frame[34], (unsigned)x->frame[35], (unsigned)x->frame[36]);
        ESP_EARLY_LOGW(TAG,
                       "  cache=%u tcb=0x%x name=%s task=%s frame=0x%x at %u ms",
                       (unsigned)x->cache_on, (unsigned)x->tcb, x->name,
                       task_name(x->task_id), (unsigned)x->frame_ptr,
                       (unsigned)(x->at_us / 1000));
        ESP_EARLY_LOGW(TAG,
                       "  stack bounds=0x%x..0x%x sp=0x%x copied=%u words clamped=%u, "
                       "%u further exception(s) on this core",
                       (unsigned)x->sp_min, (unsigned)x->sp_max, (unsigned)x->stack_sp,
                       (unsigned)x->stack_words, (unsigned)x->stack_clamped,
                       (unsigned)s_rtc.exc_extra[c]);

        // Hex dump of the captured stack window, 8 words per line.
        for (uint32_t i = 0; i < x->stack_words; i += 8)
        {
            char line[112];
            int off = snprintf(line, sizeof(line), "  stk+%03u:", (unsigned)(i * 4));
            for (uint32_t j = i; j < i + 8 && j < x->stack_words && off > 0 && (size_t)off < sizeof(line); ++j)
            {
                off += snprintf(line + off, sizeof(line) - (size_t)off, " %08x",
                                (unsigned)x->stack[j]);
            }
            ESP_EARLY_LOGW(TAG, "%s", line);
        }
    }

    // First software blend the guard rejected. The record is written before
    // the store runs, so it is present even when that store was what used to
    // corrupt lv_global.draw_info.sync or the swdraw stack.
    if (s_rtc.blend.count != 0)
    {
        blendLog(&s_rtc.blend);
    }

    // Blend guard state at the last fault or monitor sample: for each core the
    // blend that passed validation and was allowed to run, the last one that
    // was rejected, and the totals. A destination fault lands *after* the
    // accepted record is written, so "last accepted" names the allocation the
    // store was aimed at even though the trap frame cannot.
    if (s_rtc.blend_snap.valid != 0)
    {
        const diag_blend_snap_t *bs = &s_rtc.blend_snap;
        for (int c = 0; c < 2; ++c)
        {
            const diag_last_blend_t *l = &bs->last[c];
            ESP_EARLY_LOGW(TAG,
                           "blend guard core%d: accepted %u, rejected %u (src %u, desc %u), last accepted #%u",
                           c,
                           (unsigned)bs->stats[c][DIAG_BLEND_STAT_ACC],
                           (unsigned)bs->stats[c][DIAG_BLEND_STAT_REJ],
                           (unsigned)bs->stats[c][DIAG_BLEND_STAT_SRC],
                           (unsigned)bs->stats[c][DIAG_BLEND_STAT_DESC],
                           (unsigned)l->seq);
            if (l->seq != 0)
            {
                ESP_EARLY_LOGW(TAG,
                               "  core%d accepted @%u ms task=%s ra=0x%x t=0x%x %s layer=0x%x %s",
                               c, (unsigned)l->ms, task_name(l->task_id), (unsigned)l->ra,
                               (unsigned)l->t, guard_region(l->t),
                               (unsigned)l->layer, guard_region(l->layer));
                ESP_EARLY_LOGW(TAG,
                               "    draw buf 0x%x %s data 0x%x %s size %u stride %u dx %u dy %u w %u h %u",
                               (unsigned)l->draw_buf, guard_region(l->draw_buf),
                               (unsigned)l->data, guard_region(l->data),
                               (unsigned)l->size, (unsigned)l->stride,
                               (unsigned)l->dx, (unsigned)l->dy, (unsigned)l->w, (unsigned)l->h);
                ESP_EARLY_LOGW(TAG,
                               "    start 0x%x%08x end 0x%x%08x dest 0x%x %s "
                               "src 0x%x %s mask 0x%x %s opa %u color 0x%x",
                               (unsigned)l->start_hi, (unsigned)l->start_lo,
                               (unsigned)l->end_hi, (unsigned)l->end_lo,
                               (unsigned)l->dest, guard_region(l->dest),
                               (unsigned)l->src_buf, guard_region(l->src_buf),
                               (unsigned)l->mask_buf, guard_region(l->mask_buf),
                               (unsigned)l->opa, (unsigned)l->color);
            }
            if (bs->last_rej[c].count != 0)
                blendLog(&bs->last_rej[c]);
        }
    }
    else
    {
        ESP_EARLY_LOGW(TAG, "blend guard: no snapshot recorded (no fault and no monitor sample yet)");
    }

    // Draw-buffer free quarantine. A hit means a store wrote into a buffer
    // after LVGL destroyed it; the parked record and the blends accepted at
    // destroy time name the buffer and the writer.
    if (s_rtc.quar.valid != 0)
    {
        ESP_EARLY_LOGW(TAG,
                       "quarantine: %u parked, %u evicted, %u hits, %u skipped, "
                       "%u still parked (%u bytes)",
                       (unsigned)s_rtc.quar.parked, (unsigned)s_rtc.quar.evicted,
                       (unsigned)s_rtc.quar.hits, (unsigned)s_rtc.quar.skipped,
                       (unsigned)s_rtc.quar.active, (unsigned)s_rtc.quar.bytes);
        if (s_rtc.quar.hits != 0)
        {
            ESP_EARLY_LOGW(TAG,
                           "  last hit: buf 0x%x data 0x%x size %u stride %u %ux%u cf %u, "
                           "found at %u ms core%u task=%s; %u words differ from the fill "
                           "(first at +0x%x value 0x%x); page-bg words %u (first at +0x%x)",
                           (unsigned)s_rtc.quar.last_buf, (unsigned)s_rtc.quar.last_data,
                           (unsigned)s_rtc.quar.last_size, (unsigned)s_rtc.quar.last_stride,
                           (unsigned)s_rtc.quar.last_w, (unsigned)s_rtc.quar.last_h,
                           (unsigned)s_rtc.quar.last_cf, (unsigned)s_rtc.quar.last_ms,
                           (unsigned)s_rtc.quar.last_core, task_name(s_rtc.quar.last_task),
                           (unsigned)s_rtc.quar.last_dirty_count,
                           (unsigned)s_rtc.quar.last_dirty_off,
                           (unsigned)s_rtc.quar.last_dirty_val,
                           (unsigned)s_rtc.quar.last_hits,
                           (unsigned)s_rtc.quar.last_off);
            for (int c = 0; c < 2; ++c)
            {
                const diag_last_blend_t *l = &s_rtc.quar.last_blend[c];
                if (l->seq == 0)
                    continue;
                ESP_EARLY_LOGW(TAG,
                               "  core%d blend accepted when it was parked: #%u @%u ms "
                               "task=%s ra=0x%x draw buf 0x%x data 0x%x size %u dx %u dy %u "
                               "dest 0x%x color 0x%x",
                               c, (unsigned)l->seq, (unsigned)l->ms, task_name(l->task_id),
                               (unsigned)l->ra, (unsigned)l->draw_buf, (unsigned)l->data,
                               (unsigned)l->size, (unsigned)l->dx, (unsigned)l->dy,
                               (unsigned)l->dest, (unsigned)l->color);
            }
        }
    }
    else
    {
        ESP_EARLY_LOGW(TAG, "quarantine: no draw buffer was destroyed during the run");
    }

    // Heap headroom at the last sample (monitor, or an explicit diag_heap_log
    // call). Internal free close to zero is the signature of LVGL structs being
    // pushed into PSRAM.
    ESP_EARLY_LOGW(TAG,
                   "heap at last sample: internal free %u min %u largest %u min %u, "
                   "psram free %u min %u",
                   (unsigned)s_rtc.heap_int_free, (unsigned)s_rtc.heap_int_free_min,
                   (unsigned)s_rtc.heap_int_largest, (unsigned)s_rtc.heap_int_largest_min,
                   (unsigned)s_rtc.heap_psram_free, (unsigned)s_rtc.heap_psram_free_min);

    // How far each core got down the panic path. 0 means it never reached the
    // RTC WDT setup, which is what core0 showed in v6: the panic entry ran,
    // then the handler wedged and the TIMG stage-1 reset fired 12 s later.
    ESP_EARLY_LOGW(TAG, "panic path stage: core0=0x%x core1=0x%x "
                        "(1=RTC WDT enable, 2=TIMG WDTs disabled, 4=panic handler)",
                   (unsigned)s_rtc.panic_stage[0], (unsigned)s_rtc.panic_stage[1]);

    ESP_EARLY_LOGW(TAG,
                   "monitor: %u callbacks, first %u ms, last %u ms, gap last/max %u/%u ms",
                   (unsigned)s_rtc.mon_count,
                   (unsigned)(s_rtc.mon_first_us / 1000),
                   (unsigned)(s_rtc.mon_last_us / 1000),
                   (unsigned)s_rtc.mon_last_period_ms,
                   (unsigned)s_rtc.mon_max_period_ms);

    // Wall time from the last monitor callback to this boot, on a clock that
    // does not depend on the systimer and survives the reset. This is the
    // freeze -> watchdog -> reset -> boot delay: ~5 s means the INT WDT panic
    // fired and reset quickly, ~9 s the HP WDT stage reset before the panic
    // path ran, ~15 s the panic reached the RTC WDT and then wedged.
    if (rtc_gap_ms >= 0)
    {
        ESP_EARLY_LOGW(TAG,
                       "RTC clock: %d ms from last monitor callback to boot (watchdog delay + boot time)",
                       (int)rtc_gap_ms);
    }
    else if (s_rtc.rtc_valid)
    {
        ESP_EARLY_LOGW(TAG, "RTC clock: gap unavailable (retained RTC time went backwards)");
    }

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

        // If this says IN FLIGHT, the freeze happened inside esp_cache_msync,
        // i.e. inside its interrupts-off writeback.
        const diag_msync_t *m = &s_rtc.msync[c];
        if (m->in_flight)
        {
            ESP_EARLY_LOGW(TAG,
                           "core%d: esp_cache_msync IN FLIGHT since @%u ms, size=%u B flags=0x%x (never returned)",
                           c, (unsigned)(m->enter_us / 1000), (unsigned)m->size,
                           (unsigned)m->flags);
        }
        else if (m->count != 0)
        {
            ESP_EARLY_LOGW(TAG,
                           "core%d: esp_cache_msync %u calls, last %u B flags=0x%x took %u ms, max %u ms (%u B)",
                           c, (unsigned)m->count, (unsigned)m->size, (unsigned)m->flags,
                           (unsigned)(m->last_us / 1000),
                           (unsigned)(m->max_us / 1000), (unsigned)m->max_size);
        }
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

// ── Blend destination guard (temporary) ──────────────────────────────────────
//
// The v7 post-mortem showed a software fill writing UI_COLOR_PAGE_BG
// (0xF8F6FC -> 0xFFBF in RGB565, duplicated to 0xFFBFFFBF by the unrolled
// fill loop) over lv_global.draw_info.sync and over the top of the swdraw
// stack. Both addresses are outside every buffer LVGL may draw into, and with
// a valid PSRAM framebuffer base no blend coordinate can reach them: the
// destination arithmetic stays inside the framebuffer. The store can only get
// there if the blend's layer or draw buffer had already been corrupted, or if
// the blend area no longer maps inside the buffer
// (lv_draw_layer_go_to_xy() returns NULL then, and the fill writes through it -
// silently, because LV_USE_LOG is off).
//
// So: record every draw buffer LVGL creates, and before each software blend
// check that the target is still the buffer LVGL created (same data pointer,
// size, stride, geometry and color format) and that the blend area, after the
// same clip the real function applies, maps inside that buffer. The first
// blend that fails is stored in RTC RAM with its full descriptor, logged
// live, and skipped so that no store can land outside a draw buffer.
//
// The registry lookup runs for every blend: a spinlock plus a linear scan of
// DIAG_BUF_SLOTS entries on the two swdraw threads. That is a deliberate
// timing cost in this diagnostic build.
#ifndef SOC_DRAM_LOW
#define SOC_DRAM_LOW 0x4FF00000u
#define SOC_DRAM_HIGH 0x4FFC0000u
#endif
#ifndef SOC_EXTRAM_LOW
#define SOC_EXTRAM_LOW 0x48000000u
#define SOC_EXTRAM_HIGH 0x4C000000u
#endif
#ifndef SOC_TCM_LOW
#define SOC_TCM_LOW 0x30100000u
#define SOC_TCM_HIGH 0x30102000u
#endif
#ifndef SOC_RTC_DRAM_LOW
#define SOC_RTC_DRAM_LOW 0x50108000u
#define SOC_RTC_DRAM_HIGH 0x50110000u
#endif
#ifndef SOC_DROM_LOW
#define SOC_DROM_LOW 0x40000000u
#define SOC_DROM_HIGH 0x44000000u
#endif

// Every region the heap can hand out. Besides main internal DRAM and PSRAM,
// the P4 registers the 7 KiB TCM heap (boot log: "At 301003BC ... TCM") and
// the 28 KiB LP/RTC heap ("At 50108F28 ... RTCRAM") with MALLOC_CAP_INTERNAL,
// so once internal DRAM fragments, LVGL's small structs (draw tasks, the tile
// layer array from lv_refr_area) land there. v9 rejected both as "outside".
static inline bool guard_in_ram(uint32_t addr)
{
    return (addr >= SOC_DRAM_LOW && addr < SOC_DRAM_HIGH) ||
           (addr >= SOC_EXTRAM_LOW && addr < SOC_EXTRAM_HIGH) ||
           (addr >= SOC_TCM_LOW && addr < SOC_TCM_HIGH) ||
           (addr >= SOC_RTC_DRAM_LOW && addr < SOC_RTC_DRAM_HIGH);
}

// Readable regions also include the flash mapping: font glyph bitmaps and
// constant image data live there, and the image blender reads them through the
// same src_buf path as decoded pixels.
static inline bool guard_readable(uint32_t addr)
{
    return guard_in_ram(addr) || (addr >= SOC_DROM_LOW && addr < SOC_DROM_HIGH);
}

// True when [start, start + len) lies wholly inside one mapped region. A source
// window that spans two regions cannot be real data, so it is rejected too.
static inline bool guard_range_mapped(uint64_t start, uint64_t len)
{
    if (len == 0)
        return true;
    const uint64_t end = start + len;
    if (end < start)
        return false;

    struct guard_region_range
    {
        uint32_t lo;
        uint32_t hi;
    };
    static const guard_region_range regions[] = {
        {SOC_DRAM_LOW, SOC_DRAM_HIGH},
        {SOC_EXTRAM_LOW, SOC_EXTRAM_HIGH},
        {SOC_TCM_LOW, SOC_TCM_HIGH},
        {SOC_RTC_DRAM_LOW, SOC_RTC_DRAM_HIGH},
        {SOC_DROM_LOW, SOC_DROM_HIGH},
    };
    for (const guard_region_range &r : regions)
    {
        if (start >= r.lo && end <= r.hi)
            return true;
    }
    return false;
}

// The layer and draw buffer descriptors are ordinary heap allocations
// (CONFIG_LV_USE_CLIB_MALLOC), so they are not guaranteed to be in internal
// DRAM: malloc() only *prefers* internal memory for sizes up to
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL and falls back to PSRAM when that
// allocation fails, which this build does once the internal heap fills up
// (the first v8 run's draw task was a PSRAM address). Requiring internal DRAM
// would therefore reject legitimate blends; anything outside every mapped RAM
// region is still treated as a bad pointer.

// Live draw buffers are few (the two display buffers plus a shallow stack of
// layer buffers), but LVGL's image cache may also create draw buffers for
// decoded images. Size the table well above the working set; the record's
// registry_overflows value says if it ever filled.
#define DIAG_BUF_SLOTS 128

typedef struct
{
    uint32_t db; // lv_draw_buf_t*
    uint32_t data;
    uint32_t size;
    uint32_t stride;
    uint32_t w;
    uint32_t h;
    uint32_t cf;
    uint32_t flags; // header.flags at registration; 0x10 = LV_IMAGE_FLAGS_ALLOCATED
    uint32_t used;
} diag_buf_rec_t;

static diag_buf_rec_t s_buf_rec[DIAG_BUF_SLOTS];
static uint32_t s_buf_next;    // next slot to reuse once all are taken
static uint32_t s_buf_over;    // slots evicted while still in use
static portMUX_TYPE s_buf_mux = portMUX_INITIALIZER_UNLOCKED;

static void bufRecAdd(const lv_draw_buf_t *db)
{
    if (db == nullptr)
        return;
    const uint32_t key = (uint32_t)(uintptr_t)db;
    if (!guard_in_ram(key))
        return;

    // The caller has not published the buffer yet, so its fields can be read
    // before taking the lock.
    const uint32_t data = (uint32_t)(uintptr_t)db->data;
    const uint32_t size = db->data_size;
    const uint32_t stride = (uint32_t)db->header.stride;
    const uint32_t w = (uint32_t)db->header.w;
    const uint32_t h = (uint32_t)db->header.h;
    const uint32_t cf = (uint32_t)db->header.cf;
    const uint32_t flags = (uint32_t)db->header.flags;

    portENTER_CRITICAL(&s_buf_mux);
    uint32_t slot = DIAG_BUF_SLOTS;
    for (uint32_t i = 0; i < DIAG_BUF_SLOTS; ++i)
    {
        if (s_buf_rec[i].used && s_buf_rec[i].db == key)
        {
            slot = i;
            break;
        }
    }
    if (slot == DIAG_BUF_SLOTS)
    {
        // Prefer a free slot. The display's two static buffers are registered
        // once at boot and stay in use for the whole run; a blind round-robin
        // would evict them as soon as the transient layer buffers churn past
        // the table size, after which every screen blend would look like
        // NO_ENTRY and be skipped.
        for (uint32_t i = 0; i < DIAG_BUF_SLOTS; ++i)
        {
            if (!s_buf_rec[i].used)
            {
                slot = i;
                break;
            }
        }
    }
    if (slot == DIAG_BUF_SLOTS)
    {
        // All slots live: reuse the oldest, and remember that identity checks
        // may now see a spurious NO_ENTRY (the record carries this counter).
        slot = s_buf_next;
        s_buf_next = (s_buf_next + 1u) % DIAG_BUF_SLOTS;
        s_buf_over++;
    }
    s_buf_rec[slot].db = key;
    s_buf_rec[slot].data = data;
    s_buf_rec[slot].size = size;
    s_buf_rec[slot].stride = stride;
    s_buf_rec[slot].w = w;
    s_buf_rec[slot].h = h;
    s_buf_rec[slot].cf = cf;
    s_buf_rec[slot].flags = flags;
    s_buf_rec[slot].used = 1;
    portEXIT_CRITICAL(&s_buf_mux);
}

static void bufRecDel(const lv_draw_buf_t *db)
{
    if (db == nullptr)
        return;
    const uint32_t key = (uint32_t)(uintptr_t)db;

    portENTER_CRITICAL(&s_buf_mux);
    for (uint32_t i = 0; i < DIAG_BUF_SLOTS; ++i)
    {
        if (s_buf_rec[i].used && s_buf_rec[i].db == key)
        {
            s_buf_rec[i].used = 0;
            break;
        }
    }
    portEXIT_CRITICAL(&s_buf_mux);
}

static bool bufRecGet(uint32_t key, diag_buf_rec_t *out)
{
    bool found = false;
    portENTER_CRITICAL(&s_buf_mux);
    for (uint32_t i = 0; i < DIAG_BUF_SLOTS; ++i)
    {
        if (s_buf_rec[i].used && s_buf_rec[i].db == key)
        {
            *out = s_buf_rec[i];
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_buf_mux);
    return found;
}

// ── Draw-buffer free quarantine ─────────────────────────────────────────────
//
// Hypothesis under test: a blend destination outlives the draw buffer that
// owned it. The v10 crash read 0xFFBFFFBF (two RGB565 page-background pixels:
// UI_COLOR_PAGE_BG 0xF8F6FC -> 0xFFBF) out of an object style pointer, which
// means the object's styles array held draw-buffer fill data. If a blend was
// still holding a pointer to a buffer that had already been destroyed, its
// stores landed in memory LVGL had freed and reused.
//
// So do not destroy immediately: keep the data out of the allocator, fill it
// with 0xA5, and park the buffer in a small FIFO. When an entry is retired,
// scan it: any word that no longer holds 0xA5A5A5A5 proves a store arrived
// after the destroy, and the page-background value the v10 crash read out of
// object memory is counted separately. The parked record and the blends
// accepted at destroy time name the buffer and the code that was drawing.
//
// The fill is what makes this conclusive: before the destroy the pixels
// legitimately contain the page background, so scanning without erasing them
// would be meaningless. The fill stays inside the allocation: buf_malloc()
// requests data_size + LV_DRAW_BUF_ALIGN - 1 bytes and `data` is aligned up by
// at most that much.
#define QUAR_SLOTS 16
#define QUAR_BUDGET (4u * 1024u * 1024u)
#define QUAR_FILL_BYTE 0xA5u
#define QUAR_POISON 0xFFBFFFBFu

typedef struct
{
    lv_draw_buf_t *buf;
    uint32_t data;
    uint32_t size;
    uint32_t stride;
    uint32_t w;
    uint32_t h;
    uint32_t cf;
    uint32_t ms;
    uint32_t core;
    uint32_t task_id;
    diag_last_blend_t last_blend[2];
} quar_rec_t;

static quar_rec_t s_quar[QUAR_SLOTS];
static uint32_t s_quar_head; // oldest entry
static uint32_t s_quar_count;
static uint32_t s_quar_bytes;
static uint32_t s_quar_parked;
static uint32_t s_quar_evicted;
static uint32_t s_quar_hits;
static uint32_t s_quar_skipped;
static portMUX_TYPE s_quar_mux = portMUX_INITIALIZER_UNLOCKED;

extern "C" void __real_lv_draw_buf_destroy(lv_draw_buf_t *draw_buf);

#define QUAR_FILL_WORD 0xA5A5A5A5u

typedef struct
{
    uint32_t bg_hits;      // words equal to the page background
    uint32_t bg_first_off; // byte offset of the first of those
    uint32_t dirty_count;  // words that no longer hold the fill
    uint32_t dirty_first_off;
    uint32_t dirty_first_val;
} quar_scan_t;

// After the fill, every word must read 0xA5A5A5A5. Anything else proves a
// store landed after the destroy; the page background is called out separately
// because that is the value the v10 crash read out of object memory. LVGL's
// allocated buffers are 4-byte aligned (LV_DRAW_BUF_ALIGN=4); the byte-wise
// fallback covers a buffer handed in through lv_draw_buf_init() with other
// alignment.
static void quar_scan(uint32_t data, uint32_t size, quar_scan_t *out)
{
    memset(out, 0, sizeof(*out));
    if (data == 0 || size < 4)
        return;

    uint32_t bg = 0;
    uint32_t dirty = 0;

    if ((data & 3u) == 0)
    {
        const volatile uint32_t *p = (const volatile uint32_t *)(uintptr_t)data;
        const uint32_t n = size >> 2;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t v = p[i];
            if (v == QUAR_POISON)
            {
                if (bg == 0)
                    out->bg_first_off = i << 2;
                bg++;
            }
            if (v != QUAR_FILL_WORD)
            {
                if (dirty == 0)
                {
                    out->dirty_first_off = i << 2;
                    out->dirty_first_val = v;
                }
                dirty++;
            }
        }
    }
    else
    {
        const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)data;
        for (uint32_t i = 0; i + 3 < size; ++i)
        {
            const uint32_t v = (uint32_t)p[i] | ((uint32_t)p[i + 1] << 8) |
                               ((uint32_t)p[i + 2] << 16) | ((uint32_t)p[i + 3] << 24);
            if (v == QUAR_POISON)
            {
                if (bg == 0)
                    out->bg_first_off = i;
                bg++;
            }
            if (v != QUAR_FILL_WORD)
            {
                if (dirty == 0)
                {
                    out->dirty_first_off = i;
                    out->dirty_first_val = v;
                }
                dirty++;
            }
        }
    }

    out->bg_hits = bg;
    out->dirty_count = dirty;
}

// Retire the oldest parked buffer: scan it, record a hit, then really free it.
static void quar_retire_one(void)
{
    quar_rec_t rec;
    memset(&rec, 0, sizeof(rec));

    portENTER_CRITICAL(&s_quar_mux);
    if (s_quar_count != 0)
    {
        rec = s_quar[s_quar_head];
        memset(&s_quar[s_quar_head], 0, sizeof(s_quar[s_quar_head]));
        s_quar_head = (s_quar_head + 1u) % QUAR_SLOTS;
        s_quar_count--;
        s_quar_bytes -= rec.size;
        s_quar_evicted++;
    }
    portEXIT_CRITICAL(&s_quar_mux);

    if (rec.buf == nullptr)
        return;

    quar_scan_t scan;
    quar_scan(rec.data, rec.size, &scan);

    s_rtc.quar.valid = 1;
    s_rtc.quar.parked = s_quar_parked;
    s_rtc.quar.evicted = s_quar_evicted;
    s_rtc.quar.hits = s_quar_hits;
    s_rtc.quar.skipped = s_quar_skipped;
    s_rtc.quar.active = s_quar_count;
    s_rtc.quar.bytes = s_quar_bytes;

    // Any word that no longer holds the fill is a store that arrived after the
    // destroy. The page-background count says whether that store was the v10
    // fill; the dirty count catches a different colour painted into the same
    // memory.
    if (scan.dirty_count != 0)
    {
        s_quar_hits++;
        s_rtc.quar.hits = s_quar_hits;
        s_rtc.quar.last_buf = (uint32_t)(uintptr_t)rec.buf;
        s_rtc.quar.last_data = rec.data;
        s_rtc.quar.last_size = rec.size;
        s_rtc.quar.last_stride = rec.stride;
        s_rtc.quar.last_w = rec.w;
        s_rtc.quar.last_h = rec.h;
        s_rtc.quar.last_cf = rec.cf;
        s_rtc.quar.last_off = scan.bg_first_off;
        s_rtc.quar.last_hits = scan.bg_hits;
        s_rtc.quar.last_dirty_off = scan.dirty_first_off;
        s_rtc.quar.last_dirty_val = scan.dirty_first_val;
        s_rtc.quar.last_dirty_count = scan.dirty_count;
        s_rtc.quar.last_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_rtc.quar.last_core = (uint32_t)esp_cpu_get_core_id() & 1u;
        s_rtc.quar.last_task = current_task_id((BaseType_t)(esp_cpu_get_core_id() & 1));
        s_rtc.quar.last_blend[0] = rec.last_blend[0];
        s_rtc.quar.last_blend[1] = rec.last_blend[1];

        ESP_EARLY_LOGW(TAG,
                       "QUARANTINE HIT: buffer 0x%x data 0x%x size %u stride %u %ux%u cf %u "
                       "was written after destroy; %u words differ from the 0xA5 fill, first "
                       "at +0x%x value 0x%x; page-bg words %u, first at +0x%x",
                       (unsigned)(uintptr_t)rec.buf, (unsigned)rec.data, (unsigned)rec.size,
                       (unsigned)rec.stride, (unsigned)rec.w, (unsigned)rec.h,
                       (unsigned)rec.cf, (unsigned)scan.dirty_count,
                       (unsigned)scan.dirty_first_off, (unsigned)scan.dirty_first_val,
                       (unsigned)scan.bg_hits, (unsigned)scan.bg_first_off);
        ESP_EARLY_LOGW(TAG,
                       "  parked at %u ms on core%u (task=%s); %u parked, %u evicted, %u skipped, "
                       "%u active %u bytes",
                       (unsigned)rec.ms, (unsigned)rec.core, task_name(rec.task_id),
                       (unsigned)s_quar_parked, (unsigned)s_quar_evicted,
                       (unsigned)s_quar_skipped, (unsigned)s_quar_count,
                       (unsigned)s_quar_bytes);
        for (int c = 0; c < 2; ++c)
        {
            const diag_last_blend_t *l = &rec.last_blend[c];
            if (l->seq == 0)
                continue;
            ESP_EARLY_LOGW(TAG,
                           "  blend accepted at destroy core%d: #%u @%u ms task=%s ra=0x%x "
                           "draw buf 0x%x data 0x%x size %u stride %u dx %u dy %u w %u h %u "
                           "dest 0x%x color 0x%x",
                           c, (unsigned)l->seq, (unsigned)l->ms, task_name(l->task_id),
                           (unsigned)l->ra, (unsigned)l->draw_buf, (unsigned)l->data,
                           (unsigned)l->size, (unsigned)l->stride, (unsigned)l->dx,
                           (unsigned)l->dy, (unsigned)l->w, (unsigned)l->h,
                           (unsigned)l->dest, (unsigned)l->color);
        }

        // A blend that was still in flight when this buffer was parked has
        // completed by now, so it is the newest accepted record on its core.
        // If it names this buffer, that record is the writer; keep it too.
        for (int c = 0; c < 2; ++c)
        {
            const diag_last_blend_t *l = &s_last_blend[c];
            if (l->seq == 0 || l->draw_buf != (uint32_t)(uintptr_t)rec.buf)
                continue;
            ESP_EARLY_LOGW(TAG,
                           "  LAST ACCEPTED BLEND TARGETED THIS BUFFER core%d: #%u @%u ms "
                           "task=%s ra=0x%x draw buf 0x%x data 0x%x size %u dx %u dy %u "
                           "w %u h %u dest 0x%x color 0x%x",
                           c, (unsigned)l->seq, (unsigned)l->ms, task_name(l->task_id),
                           (unsigned)l->ra, (unsigned)l->draw_buf, (unsigned)l->data,
                           (unsigned)l->size, (unsigned)l->dx, (unsigned)l->dy,
                           (unsigned)l->w, (unsigned)l->h, (unsigned)l->dest,
                           (unsigned)l->color);
            s_rtc.quar.last_blend[c] = *l;
        }
    }

    // The registry entry was kept alive while parked so a late blend would
    // pass the guard; drop it now that the memory really goes back.
    bufRecDel(rec.buf);
    __real_lv_draw_buf_destroy(rec.buf);
}

static void quar_park(lv_draw_buf_t *draw_buf)
{
    const uint32_t data = (uint32_t)(uintptr_t)draw_buf->data;
    const uint32_t size = draw_buf->data_size;

    if (data == 0 || size == 0 || size > QUAR_BUDGET || (data & 3u) != 0 ||
        !guard_in_ram(data))
    {
        s_quar_skipped++;
        bufRecDel(draw_buf);
        __real_lv_draw_buf_destroy(draw_buf);
        return;
    }

    // Make room by retiring the oldest entry. The scan and the real free run
    // outside the lock; only the ring bookkeeping is inside.
    for (;;)
    {
        uint32_t count;
        uint32_t bytes;
        portENTER_CRITICAL(&s_quar_mux);
        count = s_quar_count;
        bytes = s_quar_bytes;
        portEXIT_CRITICAL(&s_quar_mux);

        if (count < QUAR_SLOTS && bytes + size <= QUAR_BUDGET)
            break;
        if (count == 0)
            break;
        quar_retire_one();
    }

    // Erase the legitimate page-background content before parking. A blend
    // that still holds the old pointer will put it back after the destroy;
    // nothing else writes to a buffer that LVGL just destroyed.
    memset((void *)(uintptr_t)data, QUAR_FILL_BYTE, size);

    quar_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.buf = draw_buf;
    rec.data = data;
    rec.size = size;
    rec.stride = (uint32_t)draw_buf->header.stride;
    rec.w = (uint32_t)draw_buf->header.w;
    rec.h = (uint32_t)draw_buf->header.h;
    rec.cf = (uint32_t)draw_buf->header.cf;
    rec.ms = (uint32_t)(esp_timer_get_time() / 1000);
    rec.core = (uint32_t)esp_cpu_get_core_id() & 1u;
    rec.task_id = current_task_id((BaseType_t)rec.core);
    rec.last_blend[0] = s_last_blend[0];
    rec.last_blend[1] = s_last_blend[1];

    bool parked = false;
    portENTER_CRITICAL(&s_quar_mux);
    if (s_quar_count < QUAR_SLOTS && s_quar_bytes + size <= QUAR_BUDGET)
    {
        const uint32_t slot = (s_quar_head + s_quar_count) % QUAR_SLOTS;
        s_quar[slot] = rec;
        s_quar_count++;
        s_quar_bytes += size;
        s_quar_parked++;
        parked = true;
    }
    portEXIT_CRITICAL(&s_quar_mux);

    s_rtc.quar.valid = 1;
    s_rtc.quar.parked = s_quar_parked;
    s_rtc.quar.evicted = s_quar_evicted;
    s_rtc.quar.hits = s_quar_hits;
    s_rtc.quar.skipped = s_quar_skipped;
    s_rtc.quar.active = s_quar_count;
    s_rtc.quar.bytes = s_quar_bytes;

    if (!parked)
    {
        // Raced with another destroyer that took the last slot.
        s_quar_skipped++;
        bufRecDel(draw_buf);
        __real_lv_draw_buf_destroy(draw_buf);
    }
}

// ── Boot layout report ───────────────────────────────────────────────────────
//
// The v10 crash put the victim object at 0x4841711c and its style arrays at
// 0x4840535c.., just past the 800x1280 framebuffer at 0x481f4e00 (end
// 0x483e8e00). This report shows where the two panel buffers, the LVGL root
// object and the heap LVGL allocates from actually sit, so the next capture
// can be compared with that layout.
//
// Store watchpoints on those v10 victim words were tried and removed: the word
// at fb_end+0x1C55C is ordinary heap memory, and the allocator's own
// comprehensive-poisoning fill (0xCECECECE on malloc, 0xFEFEFEFE on free)
// writes it during a plain SDIO RX allocation (sdio_process_rx_task), which
// tripped the watchpoint as a false positive. Any heap address receives
// legitimate stores, so a hardware store watchpoint cannot tell the corruption
// writer apart; the quarantine below is the probe that can.
void diag_report_layout(const void *lvgl_root)
{
    // The panel's two buffers are registered through lv_draw_buf_init() by
    // lv_display_set_buffers(), so they are in the same table as the transient
    // layer and image buffers. Snapshot it before logging (the table can
    // change while a line is being printed). Static: 128 records would be
    // 4.5 KiB of the 10 KiB main-task stack.
    static diag_buf_rec_t recs[DIAG_BUF_SLOTS];
    uint32_t n = 0;
    portENTER_CRITICAL(&s_buf_mux);
    for (uint32_t i = 0; i < DIAG_BUF_SLOTS; ++i)
    {
        if (s_buf_rec[i].used)
            recs[n++] = s_buf_rec[i];
    }
    portEXIT_CRITICAL(&s_buf_mux);

    multi_heap_info_t hi;
    multi_heap_info_t hp;
    heap_caps_get_info(&hi, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(&hp, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // LVGL is built with CONFIG_LV_USE_CLIB_MALLOC: lv_malloc() is libc
    // malloc(), so there is no LVGL pool. Everything below is ESP heap.
    // multi_heap_info_t has no total field: allocated + free is the pool size.
    ESP_LOGI(TAG, "layout: %u draw buffers; LVGL uses libc malloc (no LVGL pool); "
                  "heap internal total %u free %u largest %u min %u, "
                  "psram total %u free %u largest %u min %u",
             (unsigned)n,
             (unsigned)(hi.total_allocated_bytes + hi.total_free_bytes),
             (unsigned)hi.total_free_bytes,
             (unsigned)hi.largest_free_block, (unsigned)hi.minimum_free_bytes,
             (unsigned)(hp.total_allocated_bytes + hp.total_free_bytes),
             (unsigned)hp.total_free_bytes,
             (unsigned)hp.largest_free_block, (unsigned)hp.minimum_free_bytes);

    uint32_t fb_data = 0;
    uint32_t fb_size = 0;
    uint32_t fb_end = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const diag_buf_rec_t *r = &recs[i];
        const uint32_t end = r->data + r->size;
        // The panel framebuffers are the only large buffers LVGL did not
        // allocate itself (flags without ALLOCATED=0x10). Restricting the
        // "top framebuffer" search to them keeps a large layer buffer - or a
        // quarantined one, which stays registered - from being mistaken for
        // the framebuffer the v10 offsets are relative to.
        const bool panel = (r->flags & 0x10u) == 0 && r->size >= 256u * 1024u;
        if (r->size >= 256u * 1024u)
        {
            ESP_LOGI(TAG, "layout: buf 0x%x data 0x%x..0x%x size %u stride %u %ux%u cf %u "
                          "flags 0x%x%s",
                     (unsigned)r->db, (unsigned)r->data, (unsigned)end,
                     (unsigned)r->size, (unsigned)r->stride, (unsigned)r->w,
                     (unsigned)r->h, (unsigned)r->cf, (unsigned)r->flags,
                     panel ? " (panel)" : "");
        }
        if (panel && end > fb_end)
        {
            fb_end = end;
            fb_data = r->data;
            fb_size = r->size;
        }
    }

    // The root object marks where the object heap starts, i.e. how far the
    // objects that the v10 blend overwrote sit from the top framebuffer end.
    if (lvgl_root != nullptr)
    {
        const uint32_t r = (uint32_t)(uintptr_t)lvgl_root;
        ESP_LOGI(TAG, "layout: LVGL root 0x%x (%s top framebuffer end by %d bytes)",
                 (unsigned)r, r >= fb_end ? "after" : "before", (int)r - (int)fb_end);
    }

    // Where a fresh allocation of each kind lands relative to the top
    // framebuffer: this is the adjacency the v10 addresses suggest.
    void *pi = heap_caps_malloc(64, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *pp = heap_caps_malloc(64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (fb_end != 0)
    {
        ESP_LOGI(TAG, "layout: top framebuffer data 0x%x..0x%x size %u; "
                      "fresh internal alloc 0x%x, psram alloc 0x%x",
                 (unsigned)fb_data, (unsigned)fb_end, (unsigned)fb_size,
                 (unsigned)(uintptr_t)pi, (unsigned)(uintptr_t)pp);
    }
    else
    {
        ESP_LOGW(TAG, "layout: no large draw buffer registered yet");
    }
    if (pi != nullptr)
        heap_caps_free(pi);
    if (pp != nullptr)
        heap_caps_free(pp);
}

// Same intersection lv_area_intersect() does, reimplemented here so the guard
// does not depend on another LVGL private header.
static inline bool guard_intersect(lv_area_t *res, const lv_area_t *a1, const lv_area_t *a2)
{
    res->x1 = a1->x1 > a2->x1 ? a1->x1 : a2->x1;
    res->y1 = a1->y1 > a2->y1 ? a1->y1 : a2->y1;
    res->x2 = a1->x2 < a2->x2 ? a1->x2 : a2->x2;
    res->y2 = a1->y2 < a2->y2 ? a1->y2 : a2->y2;
    return res->x1 <= res->x2 && res->y1 <= res->y2;
}

// Says which mapped region a recorded pointer came from, or "outside" when the
// guard refused to dereference it.
static const char *guard_region(uint32_t addr)
{
    if (addr == 0)
        return "null";
    if (addr >= SOC_DRAM_LOW && addr < SOC_DRAM_HIGH)
        return "dram";
    if (addr >= SOC_EXTRAM_LOW && addr < SOC_EXTRAM_HIGH)
        return "psram";
    if (addr >= SOC_TCM_LOW && addr < SOC_TCM_HIGH)
        return "tcm";
    if (addr >= SOC_RTC_DRAM_LOW && addr < SOC_RTC_DRAM_HIGH)
        return "rtc";
    if (addr >= SOC_DROM_LOW && addr < SOC_DROM_HIGH)
        return "flash";
    return "outside";
}

static void blendLog(const diag_blend_t *b)
{
    ESP_EARLY_LOGW(TAG,
                   "blend guard: rejected blend #%u at %u ms on core%u (task=%s) reason=0x%x "
                   "ra=0x%x registry_overflows=%u",
                   (unsigned)b->count, (unsigned)b->ms, (unsigned)b->core,
                   task_name(b->task_id), (unsigned)b->reason, (unsigned)b->ra,
                   (unsigned)b->reg_over);
    ESP_EARLY_LOGW(TAG,
                   "  draw task 0x%x dsc 0x%x type %u area (%d,%d)-(%d,%d) clip (%d,%d)-(%d,%d)",
                   (unsigned)b->t, (unsigned)b->dsc, (unsigned)b->t_type,
                   (int)b->t_area[0], (int)b->t_area[1], (int)b->t_area[2], (int)b->t_area[3],
                   (int)b->t_clip[0], (int)b->t_clip[1], (int)b->t_clip[2], (int)b->t_clip[3]);
    ESP_EARLY_LOGW(TAG,
                   "  layer 0x%x %s parent 0x%x buf_area (%d,%d)-(%d,%d) cf %u",
                   (unsigned)b->layer, guard_region(b->layer), (unsigned)b->layer_parent,
                   (int)b->layer_buf_area[0], (int)b->layer_buf_area[1],
                   (int)b->layer_buf_area[2], (int)b->layer_buf_area[3], (unsigned)b->layer_cf);
    ESP_EARLY_LOGW(TAG,
                   "  draw buf 0x%x %s now: magic 0x%x data 0x%x size %u stride %u w %u h %u cf %u",
                   (unsigned)b->draw_buf, guard_region(b->draw_buf), (unsigned)b->db_magic, (unsigned)b->db_data,
                   (unsigned)b->db_size, (unsigned)b->db_stride, (unsigned)b->db_w,
                   (unsigned)b->db_h, (unsigned)b->db_cf);
    ESP_EARLY_LOGW(TAG,
                   "  draw buf created as (found=%u): data 0x%x size %u stride %u w %u h %u cf %u",
                   (unsigned)b->found, (unsigned)b->reg_data, (unsigned)b->reg_size,
                   (unsigned)b->reg_stride, (unsigned)b->reg_w, (unsigned)b->reg_h,
                   (unsigned)b->reg_cf);
    ESP_EARLY_LOGW(TAG,
                   "  blend area (%d,%d)-(%d,%d) opa %u color 0x%x src 0x%x mask 0x%x",
                   (int)b->blend_area[0], (int)b->blend_area[1],
                   (int)b->blend_area[2], (int)b->blend_area[3],
                   (unsigned)b->opa, (unsigned)b->color, (unsigned)b->src_buf,
                   (unsigned)b->mask_buf);
    ESP_EARLY_LOGW(TAG,
                   "  dx %d dy %d w %d h %d start %d (hi 0x%x) end %d (hi 0x%x) dest 0x%x",
                   (int)b->dx, (int)b->dy, (int)b->dest_w, (int)b->dest_h,
                   (int)b->start_lo, (unsigned)b->start_hi,
                   (int)b->end_lo, (unsigned)b->end_hi, (unsigned)b->dest);
}

static void blendAlert(diag_blend_t *b)
{
    b->valid = 1;
    bool first = false;

    portENTER_CRITICAL(&s_buf_mux);
    if (s_rtc.blend.valid == 0)
    {
        s_rtc.blend = *b;
        first = true;
    }
    s_rtc.blend.count++;
    portEXIT_CRITICAL(&s_buf_mux);

    if (first)
        blendLog(&s_rtc.blend);
}

// Returns true when the blend must be skipped.
static bool blendGuard(lv_draw_task_t *t, const lv_draw_sw_blend_dsc_t *dsc, uint32_t ra)
{
    const uint32_t t_addr = (uint32_t)(uintptr_t)t;
    const uint32_t dsc_addr = (uint32_t)(uintptr_t)dsc;

    // The real function dereferences both immediately. A null or out-of-RAM
    // descriptor cannot be inspected without a second fault, and running the
    // blend would read and store through it; v9 silently ran such a blend
    // unvalidated. Record it and skip instead.
    if (t == nullptr || dsc == nullptr ||
        !guard_in_ram(t_addr) || !guard_in_ram(dsc_addr))
    {
        diag_blend_t b;
        memset(&b, 0, sizeof(b));
        b.ms = (uint32_t)(esp_timer_get_time() / 1000);
        b.core = (uint32_t)esp_cpu_get_core_id() & 1u;
        b.task_id = current_task_id((BaseType_t)b.core);
        b.reason = DIAG_BLEND_R_DESC;
        b.ra = ra;
        b.t = t_addr;
        b.dsc = dsc_addr;
        s_last_reject[b.core] = b;
        s_blend_stats[b.core][DIAG_BLEND_STAT_REJ]++;
        s_blend_stats[b.core][DIAG_BLEND_STAT_DESC]++;
        blendAlert(&b);
        return true;
    }

    // Mirror the early-outs at the top of lv_draw_sw_blend() so the guard only
    // looks at blends that will actually store pixels.
    if (dsc->opa <= LV_OPA_MIN)
        return false;
    if (dsc->mask_buf != nullptr && dsc->mask_res == LV_DRAW_SW_MASK_RES_TRANSP)
        return false;

    lv_area_t area;
    memset(&area, 0, sizeof(area));
    const lv_area_t *ba = dsc->blend_area;
    lv_layer_t *layer = nullptr;
    lv_draw_buf_t *db = nullptr;
    bool layer_ok = false;
    bool db_ok = false;
    diag_buf_rec_t rec;
    bool found = false;
    bool nothing_to_read = false;
    uint32_t reason = 0;
    int32_t dx = 0;
    int32_t dy = 0;
    int64_t start = 0;
    int64_t end = 0;

    if (ba == nullptr || !guard_in_ram((uint32_t)(uintptr_t)ba))
    {
        // The real function dereferences the area unconditionally.
        reason |= DIAG_BLEND_R_NULL;
    }
    else if (!guard_intersect(&area, ba, &t->clip_area))
    {
        return false; // nothing to draw, exactly as the real function
    }
    else
    {
        // Keep the raw pointers even when they fail the check so the record
        // shows exactly what the blend would have dereferenced.
        layer = t->target_layer;
        if (layer == nullptr || !guard_in_ram((uint32_t)(uintptr_t)layer))
        {
            reason |= DIAG_BLEND_R_NULL;
        }
        else
        {
            layer_ok = true;
            db = layer->draw_buf;
            if (db == nullptr || !guard_in_ram((uint32_t)(uintptr_t)db))
            {
                reason |= DIAG_BLEND_R_NULL;
            }
            else
            {
                db_ok = true;
            }
        }
    }

    if (reason == 0)
    {
        found = bufRecGet((uint32_t)(uintptr_t)db, &rec);

        if (db->header.magic != LV_IMAGE_HEADER_MAGIC)
            reason |= DIAG_BLEND_R_BAD_MAGIC;

        if (!found)
            reason |= DIAG_BLEND_R_NO_ENTRY;
        else if (rec.data != (uint32_t)(uintptr_t)db->data ||
                 rec.size != db->data_size ||
                 rec.stride != (uint32_t)db->header.stride ||
                 rec.w != (uint32_t)db->header.w ||
                 rec.h != (uint32_t)db->header.h ||
                 rec.cf != (uint32_t)db->header.cf)
            reason |= DIAG_BLEND_R_MUTATED;

        // The blender is selected by layer->color_format while the destination
        // geometry comes from the draw buffer; the two must agree.
        if ((uint32_t)layer->color_format != (uint32_t)db->header.cf)
            reason |= DIAG_BLEND_R_MUTATED;

        const uint32_t data_addr = (uint32_t)(uintptr_t)db->data;
        const uint32_t unaligned = (uint32_t)(uintptr_t)db->unaligned_data;
        uint32_t data_off = 0;
        if (db->data == nullptr || !guard_in_ram(data_addr))
            reason |= DIAG_BLEND_R_NULL;
        else if (data_addr < unaligned)
            reason |= DIAG_BLEND_R_NULL; // data before its own allocation
        else
            data_off = data_addr - unaligned;

        // The geometry the real blend uses: lv_draw_sw_blend() clips to
        // t->clip_area, then lv_draw_layer_go_to_xy(layer, area.x1 -
        // layer->buf_area.x1, area.y1 - layer->buf_area.y1) computes the
        // destination inside the draw buffer. goto_xy() returns NULL (and the
        // fill then writes through it) when x/y fall outside header.w/h.
        dx = area.x1 - layer->buf_area.x1;
        dy = area.y1 - layer->buf_area.y1;
        const int64_t w64 = (int64_t)area.x2 - area.x1 + 1;
        const int64_t h64 = (int64_t)area.y2 - area.y1 + 1;
        const uint32_t bpp = lv_color_format_get_bpp((lv_color_format_t)db->header.cf);
        const uint32_t line = (uint32_t)((w64 * (int64_t)bpp + 7) >> 3);
        const uint32_t pal = (uint32_t)LV_COLOR_INDEXED_PALETTE_SIZE(db->header.cf) * 4u;
        // lv_draw_buf_create() allocates data_size + LV_DRAW_BUF_ALIGN - 1
        // bytes and aligns `data` up inside that allocation, so when data is
        // offset from unaligned_data the usable tail is data_size + ALIGN - 1
        // - data_off. lv_draw_buf_init() never offsets, and there data_size is
        // the caller's exact buffer size.
        int64_t valid = (int64_t)(found ? rec.size : db->data_size) - (int64_t)data_off;
        if (data_off != 0)
            valid += LV_DRAW_BUF_ALIGN - 1;
        start = (int64_t)pal + (int64_t)dy * db->header.stride + (int64_t)dx * (int64_t)bpp / 8;
        end = start + (h64 - 1) * (int64_t)db->header.stride + (int64_t)line;

        if (dx < 0 || dy < 0)
            reason |= DIAG_BLEND_R_NEG;
        else if ((uint32_t)dx >= (uint32_t)db->header.w ||
                 (uint32_t)dy >= (uint32_t)db->header.h)
            reason |= DIAG_BLEND_R_COORD;

        if (start < 0 || end < start || end > valid)
            reason |= DIAG_BLEND_R_RANGE;

        // The read side. A wild source or mask pointer faults on the load side,
        // which the destination checks above can never see. Replay the clipper
        // in lv_draw_sw_blend() exactly so a legitimate read is not flagged:
        // the image path intersects the blend area with src_area and then, when
        // masking, with mask_area (lines 120-128); a fill with a mask does not
        // clip at all. An empty intersection returns before reading anything.
        lv_area_t read_area = area;
        if (dsc->src_buf != nullptr)
        {
            if (dsc->src_area == nullptr)
            {
                reason |= DIAG_BLEND_R_SRC;
            }
            else if (!guard_intersect(&read_area, &read_area, dsc->src_area))
            {
                nothing_to_read = true;
            }
        }
        if (dsc->mask_buf != nullptr && dsc->mask_res != LV_DRAW_SW_MASK_RES_FULL_COVER)
        {
            if (dsc->mask_area == nullptr)
            {
                reason |= DIAG_BLEND_R_SRC;
            }
            else if (dsc->src_buf != nullptr &&
                     !guard_intersect(&read_area, &read_area, dsc->mask_area))
            {
                nothing_to_read = true;
            }
        }

        if (!nothing_to_read)
        {
            const int64_t rw = (int64_t)read_area.x2 - read_area.x1 + 1;
            const int64_t rh = (int64_t)read_area.y2 - read_area.y1 + 1;

            if (dsc->src_buf != nullptr && dsc->src_area != nullptr)
            {
                const int64_t sx = (int64_t)read_area.x1 - dsc->src_area->x1;
                const int64_t sy = (int64_t)read_area.y1 - dsc->src_area->y1;
                const uint32_t sbpp = lv_color_format_get_bpp((lv_color_format_t)dsc->src_color_format);
                const int64_t sstride = (int64_t)dsc->src_stride;
                const int64_t sstart = (int64_t)(uint32_t)(uintptr_t)dsc->src_buf +
                                       sy * sstride + ((sx * (int64_t)sbpp) >> 3);
                const int64_t send = sstart + (rh - 1) * sstride + ((rw * (int64_t)sbpp + 7) >> 3);
                if (sstart < 0 || send <= sstart ||
                    !guard_range_mapped((uint64_t)sstart, (uint64_t)(send - sstart)))
                    reason |= DIAG_BLEND_R_SRC;
            }
            if (dsc->mask_buf != nullptr && dsc->mask_res != LV_DRAW_SW_MASK_RES_FULL_COVER &&
                dsc->mask_area != nullptr)
            {
                const int64_t mx = (int64_t)read_area.x1 - dsc->mask_area->x1;
                const int64_t my = (int64_t)read_area.y1 - dsc->mask_area->y1;
                const int64_t mstride = (int64_t)(dsc->mask_stride
                                                      ? dsc->mask_stride
                                                      : (uint32_t)lv_area_get_width(dsc->mask_area));
                const int64_t mstart = (int64_t)(uint32_t)(uintptr_t)dsc->mask_buf + my * mstride + mx;
                const int64_t mend = mstart + (rh - 1) * mstride + rw;
                if (mstart < 0 || mend <= mstart ||
                    !guard_range_mapped((uint64_t)mstart, (uint64_t)(mend - mstart)))
                    reason |= DIAG_BLEND_R_SRC;
            }
        }
    }

    // An empty source/mask intersection makes the real function return before
    // it touches the destination, so running it is safe once everything else
    // validated. If a destination check already failed, fall through and reject.
    if (reason == 0 && nothing_to_read)
        return false;

    const uint32_t core = (uint32_t)esp_cpu_get_core_id() & 1u;

    if (reason == 0)
    {
        // Record what the guard just approved. If the fill faults, the trap
        // frame shows the destination address but not which buffer it belongs
        // to; this record supplies data/size/stride plus the blend geometry
        // from the same validation that allowed the store.
        s_blend_stats[core][DIAG_BLEND_STAT_ACC]++;
        diag_last_blend_t *lb = &s_last_blend[core];
        lb->seq = s_blend_stats[core][DIAG_BLEND_STAT_ACC];
        lb->ms = (uint32_t)(esp_timer_get_time() / 1000);
        lb->core = core;
        lb->task_id = current_task_id((BaseType_t)core);
        lb->ra = ra;
        lb->t = t_addr;
        lb->layer = (uint32_t)(uintptr_t)layer;
        lb->draw_buf = (uint32_t)(uintptr_t)db;
        lb->data = (uint32_t)(uintptr_t)db->data;
        lb->size = found ? rec.size : db->data_size;
        lb->stride = (uint32_t)db->header.stride;
        lb->dx = (uint32_t)dx;
        lb->dy = (uint32_t)dy;
        lb->w = (uint32_t)(int32_t)((int64_t)area.x2 - area.x1 + 1);
        lb->h = (uint32_t)(int32_t)((int64_t)area.y2 - area.y1 + 1);
        lb->start_hi = (uint32_t)((uint64_t)start >> 32);
        lb->start_lo = (uint32_t)start;
        lb->end_hi = (uint32_t)((uint64_t)end >> 32);
        lb->end_lo = (uint32_t)end;
        lb->dest = (uint32_t)(uintptr_t)db->data + (uint32_t)start;
        lb->src_buf = (uint32_t)(uintptr_t)dsc->src_buf;
        lb->mask_buf = (uint32_t)(uintptr_t)dsc->mask_buf;
        lb->opa = dsc->opa;
        lb->color = lv_color_to_u32(dsc->color);
        return false;
    }

    // Only the rejection path builds the full record, so the common case does
    // not pay for it.
    diag_blend_t b;
    memset(&b, 0, sizeof(b));
    b.ms = (uint32_t)(esp_timer_get_time() / 1000);
    b.core = core;
    b.task_id = current_task_id((BaseType_t)core);
    b.reason = reason;
    b.ra = ra;
    b.t = t_addr;
    b.dsc = dsc_addr;
    b.t_type = (uint32_t)t->type;
    b.t_area[0] = (uint32_t)t->area.x1;
    b.t_area[1] = (uint32_t)t->area.y1;
    b.t_area[2] = (uint32_t)t->area.x2;
    b.t_area[3] = (uint32_t)t->area.y2;
    b.t_clip[0] = (uint32_t)t->clip_area.x1;
    b.t_clip[1] = (uint32_t)t->clip_area.y1;
    b.t_clip[2] = (uint32_t)t->clip_area.x2;
    b.t_clip[3] = (uint32_t)t->clip_area.y2;
    // Identity always, contents only when it was safe to dereference.
    b.layer = (uint32_t)(uintptr_t)layer;
    b.draw_buf = (uint32_t)(uintptr_t)db;
    if (layer_ok)
    {
        b.layer_parent = (uint32_t)(uintptr_t)layer->parent;
        b.layer_buf_area[0] = (uint32_t)layer->buf_area.x1;
        b.layer_buf_area[1] = (uint32_t)layer->buf_area.y1;
        b.layer_buf_area[2] = (uint32_t)layer->buf_area.x2;
        b.layer_buf_area[3] = (uint32_t)layer->buf_area.y2;
        b.layer_cf = (uint32_t)layer->color_format;
    }
    if (db_ok)
    {
        b.db_magic = (uint32_t)db->header.magic;
        b.db_data = (uint32_t)(uintptr_t)db->data;
        b.db_size = db->data_size;
        b.db_stride = (uint32_t)db->header.stride;
        b.db_w = (uint32_t)db->header.w;
        b.db_h = (uint32_t)db->header.h;
        b.db_cf = (uint32_t)db->header.cf;
    }
    b.found = found ? 1u : 0u;
    b.reg_over = s_buf_over;
    if (found)
    {
        b.reg_data = rec.data;
        b.reg_size = rec.size;
        b.reg_stride = rec.stride;
        b.reg_w = rec.w;
        b.reg_h = rec.h;
        b.reg_cf = rec.cf;
    }
    b.blend_area[0] = (uint32_t)area.x1;
    b.blend_area[1] = (uint32_t)area.y1;
    b.blend_area[2] = (uint32_t)area.x2;
    b.blend_area[3] = (uint32_t)area.y2;
    b.opa = dsc->opa;
    b.color = lv_color_to_u32(dsc->color);
    b.src_buf = (uint32_t)(uintptr_t)dsc->src_buf;
    b.mask_buf = (uint32_t)(uintptr_t)dsc->mask_buf;
    b.dx = (uint32_t)dx;
    b.dy = (uint32_t)dy;
    b.dest_w = (uint32_t)(int32_t)((int64_t)area.x2 - area.x1 + 1);
    b.dest_h = (uint32_t)(int32_t)((int64_t)area.y2 - area.y1 + 1);
    b.start_hi = (uint32_t)((uint64_t)start >> 32);
    b.start_lo = (uint32_t)start;
    b.end_hi = (uint32_t)((uint64_t)end >> 32);
    b.end_lo = (uint32_t)end;
    if (start >= 0 && db_ok)
        b.dest = (uint32_t)(uintptr_t)db->data + (uint32_t)start;

    s_last_reject[core] = b;
    s_blend_stats[core][DIAG_BLEND_STAT_REJ]++;
    if (reason & DIAG_BLEND_R_SRC)
        s_blend_stats[core][DIAG_BLEND_STAT_SRC]++;
    if (reason & DIAG_BLEND_R_DESC)
        s_blend_stats[core][DIAG_BLEND_STAT_DESC]++;

    blendAlert(&b);
    return true;
}

extern "C" void __real_lv_draw_sw_blend(lv_draw_task_t *t, const lv_draw_sw_blend_dsc_t *dsc);
extern "C" void __wrap_lv_draw_sw_blend(lv_draw_task_t *t, const lv_draw_sw_blend_dsc_t *dsc)
{
    if (blendGuard(t, dsc, (uint32_t)(uintptr_t)__builtin_return_address(0)))
        return;
    __real_lv_draw_sw_blend(t, dsc);
}

extern "C" lv_result_t __real_lv_draw_buf_init(lv_draw_buf_t *draw_buf, uint32_t w, uint32_t h,
                                               lv_color_format_t cf, uint32_t stride, void *data,
                                               uint32_t data_size);
extern "C" lv_result_t __wrap_lv_draw_buf_init(lv_draw_buf_t *draw_buf, uint32_t w, uint32_t h,
                                               lv_color_format_t cf, uint32_t stride, void *data,
                                               uint32_t data_size)
{
    const lv_result_t res = __real_lv_draw_buf_init(draw_buf, w, h, cf, stride, data, data_size);
    if (res == LV_RESULT_OK)
        bufRecAdd(draw_buf);
    return res;
}

extern "C" lv_draw_buf_t *__real_lv_draw_buf_create(uint32_t w, uint32_t h, lv_color_format_t cf,
                                                    uint32_t stride);
extern "C" lv_draw_buf_t *__wrap_lv_draw_buf_create(uint32_t w, uint32_t h, lv_color_format_t cf,
                                                    uint32_t stride)
{
    lv_draw_buf_t *db = __real_lv_draw_buf_create(w, h, cf, stride);
    if (db != nullptr)
        bufRecAdd(db);
    return db;
}

extern "C" lv_draw_buf_t *__real_lv_draw_buf_create_ex(const lv_draw_buf_handlers_t *handlers,
                                                       uint32_t w, uint32_t h, lv_color_format_t cf,
                                                       uint32_t stride);
extern "C" lv_draw_buf_t *__wrap_lv_draw_buf_create_ex(const lv_draw_buf_handlers_t *handlers,
                                                       uint32_t w, uint32_t h, lv_color_format_t cf,
                                                       uint32_t stride)
{
    lv_draw_buf_t *db = __real_lv_draw_buf_create_ex(handlers, w, h, cf, stride);
    if (db != nullptr)
        bufRecAdd(db);
    return db;
}

extern "C" void __wrap_lv_draw_buf_destroy(lv_draw_buf_t *draw_buf)
{
    // Pass through anything LVGL did not allocate itself. The important case
    // is the display's panel framebuffers: they have no ALLOCATED flag, so the
    // real destroy is a logged no-op, but erasing one would paint a band over
    // the live screen.
    if (draw_buf == nullptr || !lv_draw_buf_has_flag(draw_buf, LV_IMAGE_FLAGS_ALLOCATED))
    {
        bufRecDel(draw_buf);
        __real_lv_draw_buf_destroy(draw_buf);
        return;
    }

    // Hypothesis: a blend can still target this buffer after its destroy.
    // Keep its data out of the allocator and watch for the page background
    // instead of freeing it now; see quar_park(). The registry entry stays
    // live until the quarantine retires the buffer, so a late blend still
    // passes the guard and its stores become visible instead of being
    // rejected as a destroyed buffer.
    quar_park(draw_buf);
}

// ── Init ─────────────────────────────────────────────────────────────────────

void diag_init(void)
{
    // Read the raw per-core reset cause before anything else can touch the
    // LP domain. These registers are sticky until software clears them, and
    // nothing in IDF does.
    uint32_t hw_cause = 0;
    uint32_t hw_ctl = 0;
#if CONFIG_IDF_TARGET_ESP32P4
    hw_cause = REG_READ(LP_CLKRST_RESET_CAUSE_REG);
    hw_ctl = REG_READ(LP_CLKRST_HPCPU_RESET_CTRL0_REG);
#endif

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const int reset_reason = (int)esp_reset_reason();
    // The unmapped ROM value behind esp_reset_reason(); see rom_reset_name().
    const uint32_t rom_reason = (uint32_t)esp_rom_get_reset_reason(0);

    // Same LP-timer wall clock the monitor sampled last run; retained through
    // the reset, and independent of the systimer the tick and esp_timer use.
    const int64_t rtc_now = (int64_t)esp_clk_rtc_time();

    const bool have_prev = (s_rtc.magic == DIAG_MAGIC) &&
                           (s_rtc.version == DIAG_VER) &&
                           (s_rtc.boot_seq != 0);

    if (have_prev)
    {
        int32_t rtc_gap_ms = -1;
        if (s_rtc.rtc_valid && s_rtc.rtc_last_us != 0 && rtc_now > s_rtc.rtc_last_us)
        {
            const int64_t gap_ms = (rtc_now - s_rtc.rtc_last_us) / 1000;
            rtc_gap_ms = gap_ms > INT32_MAX ? INT32_MAX : (int32_t)gap_ms;
        }
        dumpPreviousRun(reset_reason, rom_reason, hw_cause, hw_ctl, rtc_gap_ms);
        s_rtc.boot_seq = s_rtc.boot_seq + 1;
    }
    else
    {
        memset(&s_rtc, 0, sizeof(s_rtc));
        s_rtc.magic = DIAG_MAGIC;
        s_rtc.version = DIAG_VER;
        s_rtc.boot_seq = 1;
        ESP_EARLY_LOGW(TAG, "RTC diag log initialised (reset_reason=%d, rom_reset=%u (%s), hw cause raw=0x%x)",
                       reset_reason, (unsigned)rom_reason, rom_reset_name(rom_reason),
                       (unsigned)hw_cause);
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
    s_rtc.rtc_last_us = 0;
    s_rtc.rtc_valid = 0;
    s_rtc.tick_stall_mask = 0;
    s_rtc.lvgl_last_us = 0;
    s_rtc.lvgl_count = 0;
    s_rtc.lvgl_stall_logged = 0;
    memset(s_rtc.msync, 0, sizeof(s_rtc.msync));
    memset(&s_rtc.blend, 0, sizeof(s_rtc.blend));
    memset(&s_rtc.blend_snap, 0, sizeof(s_rtc.blend_snap));
    memset(&s_rtc.quar, 0, sizeof(s_rtc.quar));
    s_rtc.heap_int_free = 0;
    s_rtc.heap_int_free_min = 0;
    s_rtc.heap_int_largest = 0;
    s_rtc.heap_int_largest_min = 0;
    s_rtc.heap_psram_free = 0;
    s_rtc.heap_psram_free_min = 0;
    s_rtc.restart.valid = 0;
    memset(s_rtc.panic, 0, sizeof(s_rtc.panic));
    s_rtc.panic_entry_count[0] = 0;
    s_rtc.panic_entry_count[1] = 0;
    memset(s_rtc.exc, 0, sizeof(s_rtc.exc));
    s_rtc.exc_extra[0] = 0;
    s_rtc.exc_extra[1] = 0;
    s_rtc.panic_stage[0] = 0;
    s_rtc.panic_stage[1] = 0;
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
    memset(s_last_blend, 0, sizeof(s_last_blend));
    memset(s_last_reject, 0, sizeof(s_last_reject));
    memset(s_blend_stats, 0, sizeof(s_blend_stats));

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
