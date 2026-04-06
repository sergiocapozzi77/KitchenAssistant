#pragma once
#include "lvgl.h"

// spb = the lv_obj_t* for the spb_root panel (first component of the userWidget).
// Call spb_init() once after the widget is sized by EEZ/placed on screen.
// Call spb_set_step() any time to advance or rewind progress.

#define SPB_MAX_STEPS 12
#define SPB_CIRCLE_D 28 // diameter
#define SPB_RADIUS 14   // half of SPB_CIRCLE_D
#define SPB_TRACK_H 4
#define SPB_LABEL_W 68  // fixed label width; centred under circle
#define SPB_LABEL_GAP 6 // px gap between circle bottom and label top

// Child index helpers — deterministic because JSON defines components in this order:
//   0: spb_track_bg
//   1: spb_track_fill
//   2 + i*3 + 0: spb_circle_(i+1)
//   2 + i*3 + 1: spb_num_(i+1)
//   2 + i*3 + 2: spb_label_(i+1)
static inline lv_obj_t *spb_track_bg(lv_obj_t *spb) { return lv_obj_get_child(spb, 0); }
static inline lv_obj_t *spb_track_fill(lv_obj_t *spb) { return lv_obj_get_child(spb, 1); }
static inline lv_obj_t *spb_circle(lv_obj_t *spb, int i) { return lv_obj_get_child(spb, 2 + i * 3); } // i: 0-based
static inline lv_obj_t *spb_num(lv_obj_t *spb, int i) { return lv_obj_get_child(spb, 2 + i * 3 + 1); }
static inline lv_obj_t *spb_label(lv_obj_t *spb, int i) { return lv_obj_get_child(spb, 2 + i * 3 + 2); }

// ─── spb_init ────────────────────────────────────────────────────────────────
// numSteps : 2..SPB_MAX_STEPS
// labels   : array of numSteps C-strings (may be nullptr to keep defaults)
//
// Must be called AFTER the widget is laid out so lv_obj_get_width() is valid.
// If called before layout is committed, pass the known pixel width explicitly
// by temporarily using lv_obj_set_width(spb, yourWidth) before calling.
static void spb_init(lv_obj_t *spb, int numSteps, const char *labels[])
{
    if (numSteps < 2)
        numSteps = 2;
    if (numSteps > SPB_MAX_STEPS)
        numSteps = SPB_MAX_STEPS;

    const int W = lv_obj_get_width(spb);
    const int trackW = W - SPB_CIRCLE_D; // track spans centre-to-centre
    const int trackX = SPB_RADIUS;
    const int trackY = SPB_RADIUS - SPB_TRACK_H / 2;

    // --- track background (always full centre-to-centre width) ---
    lv_obj_t *bg = spb_track_bg(spb);
    lv_obj_set_pos(bg, trackX, trackY);
    lv_obj_set_size(bg, trackW, SPB_TRACK_H);

    // --- track fill (starts at 0, spb_set_step will resize) ---
    lv_obj_t *fill = spb_track_fill(spb);
    lv_obj_set_pos(fill, trackX, trackY);
    lv_obj_set_size(fill, 0, SPB_TRACK_H);

    // --- per-step objects ---
    for (int i = 0; i < SPB_MAX_STEPS; i++)
    {
        lv_obj_t *c = spb_circle(spb, i);
        lv_obj_t *n = spb_num(spb, i);
        lv_obj_t *l = spb_label(spb, i);

        if (i >= numSteps)
        {
            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(n, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(n, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);

        // x of the circle's left edge so circle centres are evenly spread
        const int cx = (numSteps > 1) ? (i * trackW / (numSteps - 1)) : (W / 2 - SPB_RADIUS);

        lv_obj_set_pos(c, cx, 0);
        lv_obj_set_size(c, SPB_CIRCLE_D, SPB_CIRCLE_D);

        lv_obj_set_pos(n, cx, 0);
        lv_obj_set_size(n, SPB_CIRCLE_D, SPB_CIRCLE_D);

        // label centred under circle: shift left by (LABEL_W - CIRCLE_D)/2
        lv_obj_set_pos(l, cx - (SPB_LABEL_W - SPB_CIRCLE_D) / 2,
                       SPB_CIRCLE_D + SPB_LABEL_GAP);
        lv_obj_set_size(l, SPB_LABEL_W, 18);

        // number text
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i + 1);
        lv_label_set_text(n, buf);

        // step label text
        if (labels && labels[i])
            lv_label_set_text(l, labels[i]);
    }
}

// ─── spb_set_step ────────────────────────────────────────────────────────────
// currentStep : 1-based. 1 = first step active, numSteps = last step done.
static void spb_set_step(lv_obj_t *spb, int numSteps, int currentStep)
{
    if (numSteps < 2)
        numSteps = 2;
    if (numSteps > SPB_MAX_STEPS)
        numSteps = SPB_MAX_STEPS;
    if (currentStep < 1)
        currentStep = 1;
    if (currentStep > numSteps)
        currentStep = numSteps;

    static const lv_color_t COL_DONE = lv_color_hex(0x4CAF50);
    static const lv_color_t COL_ACTIVE = lv_color_hex(0x2196F3);
    static const lv_color_t COL_PENDING = lv_color_hex(0xBDBDBD);

    for (int i = 0; i < numSteps; i++)
    {
        lv_obj_t *c = spb_circle(spb, i);
        lv_obj_t *n = spb_num(spb, i);

        const bool done = (i + 1 < currentStep);
        const bool active = (i + 1 == currentStep);

        lv_color_t col = done ? COL_DONE : (active ? COL_ACTIVE : COL_PENDING);
        lv_obj_set_style_bg_color(c, col, LV_PART_MAIN);

        if (done)
        {
            lv_label_set_text(n, LV_SYMBOL_OK);
        }
        else
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", i + 1);
            lv_label_set_text(n, buf);
        }
    }

    // fill track: proportion of steps completed before the active one
    const int W = lv_obj_get_width(spb);
    const int trackW = W - SPB_CIRCLE_D;
    const int fillW = (currentStep > 1) ? ((currentStep - 1) * trackW / (numSteps - 1)) : 0;
    lv_obj_set_width(spb_track_fill(spb), fillW);
}