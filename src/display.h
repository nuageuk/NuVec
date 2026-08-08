#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DECAY_FRAMES 12

typedef enum {
    DECAY_OFF,
    DECAY_ON
} DisplayDecayMode;

// Returns 1 on success, 0 on failure.
int display_init(void);

void display_clear(void);

// brightness is the vector's Z-channel intensity, 0 (invisible) - 255
// (full white), already scaled up from the analog integrator's 0-127
// Z sample-and-hold range by the caller.
void display_draw_line(int x1, int y1, int x2, int y2, uint8_t brightness);

void display_present(void);
void display_toggle_decay(void);
void display_shutdown(void);

// Vectrex screen resolution and the analog->pixel mapping derived from it
// (see analog.c's analog_render(), which uses these to convert ALG_MAX_X/Y
// integrator-space coordinates down to actual window pixels by fitting the
// integrator range into the window and centering it).
#define DISPLAY_WIDTH  600
#define DISPLAY_HEIGHT 400

#endif // DISPLAY_H
