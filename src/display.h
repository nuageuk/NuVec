#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// Returns 1 on success, 0 on failure (SDL2 init/window/renderer creation).
int display_init(void);

void display_clear(void);
void display_draw_line(int x1, int y1, int x2, int y2);
void display_present(void);
void display_shutdown(void);

// Reads a display list out of memory starting at list_addr (see
// DISPLAY_LIST_START in memory.h for the format) and draws every segment.
void display_render_from_memory(uint16_t list_addr);

#endif // DISPLAY_H
