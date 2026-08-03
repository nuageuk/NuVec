#include "display.h"

// No-op display backend for the headless trace harness (trace_main.c): lets
// cpu.c's HLE Draw_VL call display_draw_line() etc. without pulling in SDL
// or opening a window. Not part of the CMake/SDL build.

int display_init(void) { return 1; }
void display_clear(void) {}
void display_draw_line(int x1, int y1, int x2, int y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}
void display_present(void) {}
void display_shutdown(void) {}
