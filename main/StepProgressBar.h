#pragma once
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <algorithm>

// spb = the lv_obj_t* for the spb_root panel (first component of the userWidget).
// Call spb_init() once after the widget is sized by EEZ/placed on screen.
// Call spb_set_step() any time to advance or rewind progress.

#define SPB_MAX_STEPS 12
#define SPB_CIRCLE_D 28 // diameter
#define SPB_RADIUS 14   // half of SPB_CIRCLE_D
#define SPB_TRACK_H 4
#define SPB_LABEL_W 120 // fixed label width; centred under circle
#define SPB_LABEL_GAP 6 // px gap between circle bottom and label top
#define SPB_MARGIN 20   // left/right margin for circle positioning

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

    // Disable layout engine on the container so manual positioning works
    lv_obj_clear_flag(spb, LV_OBJ_FLAG_LAYOUT_1);

    const int W = lv_obj_get_content_width(spb);
    ESP_LOGI("StepProgressBar", "spb_init: container width=%d, child count=%d", W, lv_obj_get_child_cnt(spb));
    const int trackW = W - SPB_CIRCLE_D; // track spans centre-to-centre
    const int trackX = SPB_RADIUS;
    const int trackY = SPB_RADIUS - SPB_TRACK_H / 2;

    // --- track background (always full centre-to-centre width) ---
    lv_obj_t *bg = spb_track_bg(spb);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_LAYOUT_1);
    ESP_LOGI("StepProgressBar", "track background pos: trackX=%d, trackY=%d, trackW=%d", trackX, trackY, trackW);
    lv_obj_set_pos(bg, trackX, trackY);
    lv_obj_set_size(bg, trackW, SPB_TRACK_H);

    // --- track fill (starts at 0, spb_set_step will resize) ---
    lv_obj_t *fill = spb_track_fill(spb);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_LAYOUT_1);
    ESP_LOGI("StepProgressBar", "track fill pos: trackX=%d, trackY=%d", trackX, trackY);
    lv_obj_set_pos(fill, trackX, trackY);
    lv_obj_set_size(fill, 0, SPB_TRACK_H);

    // --- per-step objects ---
    // Log all children for debugging (minimal to avoid crashes)
    int child_cnt = lv_obj_get_child_cnt(spb);
    ESP_LOGI("StepProgressBar", "Total children: %d", child_cnt);
    // Just log pointers for first few children to verify order
    for (int ci = 0; ci < child_cnt && ci < 20; ci++) // Limit to first 20
    {
        lv_obj_t *child = lv_obj_get_child(spb, ci);
        ESP_LOGI("StepProgressBar", "  child %d: %p", ci, child);
    }
    if (child_cnt > 20)
        ESP_LOGI("StepProgressBar", "  ... and %d more children", child_cnt - 20);

    for (int i = 0; i < SPB_MAX_STEPS; i++)
    {
        lv_obj_t *c = spb_circle(spb, i);
        lv_obj_t *n = spb_num(spb, i);
        lv_obj_t *l = spb_label(spb, i);
        ESP_LOGI("StepProgressBar", "  step %d: child indices %d,%d,%d -> ptrs %p,%p,%p", i,
                 2 + i * 3, 2 + i * 3 + 1, 2 + i * 3 + 2, c, n, l);

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
        // Clear any layout flags that might interfere with manual positioning
        lv_obj_set_style_align(c, LV_ALIGN_DEFAULT, 0);
        lv_obj_set_style_align(n, LV_ALIGN_DEFAULT, 0);
        lv_obj_set_style_align(l, LV_ALIGN_DEFAULT, 0);

        ESP_LOGI("StepProgressBar", "  step %d: label ptr=%p hidden flag cleared", i, l);
        ESP_LOGI("StepProgressBar", "  step %d: original positions - circle(%d,%d) label(%d,%d)", i,
                 lv_obj_get_x(c), lv_obj_get_y(c), lv_obj_get_x(l), lv_obj_get_y(l));

        // Position circles from center, with equal left and right margins
        // First circle center at SPB_RADIUS, last at (W - SPB_RADIUS)
        // Position circles from center, with equal left and right margins
        // First circle center at SPB_RADIUS, last at (W - SPB_RADIUS)
        int circleCenterX;
        if (numSteps == 1)
        {
            circleCenterX = W / 2;
        }
        else
        {
            // Ensure first circle starts at SPB_RADIUS and last circle ends at W - SPB_RADIUS
            // This gives us equal margins and prevents overflow
            int availableWidth = W - 2 * SPB_RADIUS; // space between first and last circle centers
            circleCenterX = SPB_RADIUS + (i * availableWidth) / (numSteps - 1);
        }

        // Convert center to left edge for positioning
        int cx = circleCenterX - SPB_RADIUS;
        ESP_LOGI("StepProgressBar", "  step %d: setting circle position cx=%d", i, cx);

        lv_obj_set_pos(c, cx, 0);
        lv_obj_set_size(c, SPB_CIRCLE_D, SPB_CIRCLE_D);
        // Verify position was set
        int actualX = lv_obj_get_x(c);
        if (actualX != cx)
        {
            ESP_LOGW("StepProgressBar", "  step %d: circle position mismatch! requested=%d, actual=%d", i, cx, actualX);
        }

        // Number is centered on the circle
        ESP_LOGI("StepProgressBar", "  step %d: setting number position cx=%d", i, cx);
        ESP_LOGI("StepProgressBar", "  step %d: number current size w=%d h=%d, parent=%p", i, lv_obj_get_width(n), lv_obj_get_height(n), lv_obj_get_parent(n));
        lv_obj_set_pos(n, cx + 10, 5);
        lv_obj_set_size(n, SPB_CIRCLE_D, SPB_CIRCLE_D);
        // Verify position was set
        int actualNX = lv_obj_get_x(n);
        if (actualNX != cx)
        {
            ESP_LOGW("StepProgressBar", "  step %d: number position mismatch! requested=%d, actual=%d", i, cx, actualNX);
        }

        // Label centred under circle center, clamped to right edge only (left can be negative)
        int labelX = std::max(circleCenterX - (SPB_LABEL_W / 2), 0);
        ESP_LOGI("StepProgressBar", "  step %d: raw labelX=%d (circleCenterX=%d)", i, labelX, circleCenterX);
        if (labelX + SPB_LABEL_W > W)
            labelX = W - SPB_LABEL_W;
        ESP_LOGI("StepProgressBar", "  step %d: labelX=%d (circleCenterX=%d, SPB_LABEL_W=%d, W=%d)", i, labelX, circleCenterX, SPB_LABEL_W, W);

        ESP_LOGI("StepProgressBar", "  step %d: setting label position labelX=%d, y=%d", i, labelX, SPB_CIRCLE_D + SPB_LABEL_GAP);
        ESP_LOGI("StepProgressBar", "  step %d: label current size w=%d h=%d, parent=%p", i, lv_obj_get_width(l), lv_obj_get_height(l), lv_obj_get_parent(l));
        lv_obj_set_pos(l, labelX, SPB_CIRCLE_D + SPB_LABEL_GAP);
        lv_obj_set_size(l, SPB_LABEL_W, 50);
        ESP_LOGI("StepProgressBar", "  step %d: new positions - circle(%d,%d) label(%d,%d)", i,
                 lv_obj_get_x(c), lv_obj_get_y(c), lv_obj_get_x(l), lv_obj_get_y(l));

        // number text
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i + 1);
        lv_label_set_text(n, buf);

        // step label text
        if (labels && labels[i])
        {
            ESP_LOGI("StepProgressBar", "  step %d: setting label text='%s' (len=%d)", i, labels[i], (int)strlen(labels[i]));
            lv_label_set_text(l, labels[i]);
            // verify
            const char *setText = lv_label_get_text(l);
            ESP_LOGI("StepProgressBar", "  step %d: after set, label text='%s'", i, setText ? setText : "(null)");
        }
        else
        {
            ESP_LOGW("StepProgressBar", "  step %d: label text is null", i);
        }
    }
    lv_obj_update_layout(spb);
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
    static const lv_color_t COL_PENDING = lv_color_hex(0x555555);

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
            lv_obj_set_style_translate_x(n, -5, LV_PART_MAIN);
        }
        else
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", i + 1);
            lv_label_set_text(n, buf);
            lv_obj_set_style_translate_x(n, 0, LV_PART_MAIN);
        }
    }

    // fill track: proportion of steps completed before the active one
    const int W = lv_obj_get_width(spb);
    const int edgeMargin = SPB_MARGIN > SPB_RADIUS ? SPB_MARGIN : SPB_RADIUS; // consistent with spb_init
    const int centerMargin = edgeMargin + SPB_RADIUS;                         // distance from container edge to circle center
    const int trackW = W - 2 * centerMargin;                                  // track spans centre-to-centre (first to last circle center)
    const int fillW = (currentStep > 1) ? ((currentStep - 1) * trackW / (numSteps - 1)) : 0;
    lv_obj_set_width(spb_track_fill(spb), fillW);
}