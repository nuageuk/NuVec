#ifndef DISPLAY_H
#define DISPLAY_H

// Returns 1 on success, 0 on failure (SDL2 init/window/renderer creation).
int display_init(void);

void display_clear(void);
void display_draw_line(int x1, int y1, int x2, int y2);
void display_present(void);
void display_shutdown(void);

#endif // DISPLAY_H
