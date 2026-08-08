#include "display.h"
#include <stdio.h>
#include <stdlib.h>

// No-op display backend for the headless trace harness (trace_main.c): lets
// analog.c's beam integrator call display_draw_line() without pulling in SDL
// or opening a window. Not part of the CMake/SDL build. Tracks line-draw
// stats so the trace summary can report whether anything is actually being
// rendered, even headlessly.

static long line_count = 0;
static int have_bounds = 0;
static int min_x, max_x, min_y, max_y;
static DisplayVsyncMode vsync_mode = VSYNC_ON;

int display_init(void) { return 1; }
void display_clear(void) {}

void display_draw_line(int x1, int y1, int x2, int y2, uint8_t brightness) {
    (void)brightness;
    line_count++;
    if (!have_bounds) {
        min_x = max_x = x1;
        min_y = max_y = y1;
        have_bounds = 1;
    }
    int xs[2] = { x1, x2 };
    int ys[2] = { y1, y2 };
    for (int i = 0; i < 2; i++) {
        if (xs[i] < min_x) min_x = xs[i];
        if (xs[i] > max_x) max_x = xs[i];
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
}

void display_present(void) {}
void display_toggle_decay(void) {}
void display_toggle_bloom(void) {}
DisplayVsyncMode display_toggle_vsync(void) {
    vsync_mode = vsync_mode == VSYNC_ON ? VSYNC_OFF : VSYNC_ON;
    return vsync_mode;
}
void display_resize(int width, int height) { (void)width; (void)height; }
void display_shutdown(void) {}

long display_stub_line_count(void) { return line_count; }
int display_stub_get_bounds(int *out_min_x, int *out_max_x, int *out_min_y, int *out_max_y) {
    if (!have_bounds) return 0;
    *out_min_x = min_x; *out_max_x = max_x; *out_min_y = min_y; *out_max_y = max_y;
    return 1;
}
