/* Public interface and tuning constants for SDL vector rendering. */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DECAY_FRAMES 2
#define DECAY_MAX_SEGMENTS 2048
#define BLOOM_OFFSET 2
#define BLOOM_ALPHA 64

typedef enum {
    DECAY_OFF,
    DECAY_ON
} DisplayDecayMode;

typedef enum {
    BLOOM_OFF,
    BLOOM_ON
} DisplayBloomMode;

typedef enum {
    VSYNC_OFF,
    VSYNC_ON
} DisplayVsyncMode;

/* Initializes the SDL window and renderer; returns zero on failure. */
int display_init(void);

/* Begins capture or clears the renderer for the next host frame. */
void display_clear(void);

// brightness is the vector's Z-channel intensity, 0 (invisible) - 255
// (full white), already scaled up from the analog integrator's 0-127
// Z sample-and-hold range by the caller.
void display_draw_line(int x1, int y1, int x2, int y2, uint8_t brightness);

/* Composites the completed frame, draws the OSD, and presents it. */
void display_present(void);

/* Toggles phosphor decay and posts an OSD notification. */
void display_toggle_decay(void);

/* Toggles bloom and posts an OSD notification. */
void display_toggle_bloom(void);

/* Toggles host vsync and returns the new mode. */
DisplayVsyncMode display_toggle_vsync(void);

/* Fits rendering to a resized window while preserving the display aspect. */
void display_resize(int width, int height);

/* Releases all display history and SDL video resources. */
void display_shutdown(void);

// Vectrex screen resolution and the analog->pixel mapping derived from it
// (see analog.c's analog_render(), which uses these to convert ALG_MAX_X/Y
// integrator-space coordinates down to actual window pixels by fitting the
// integrator range into the window and centering it).
#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 640

#endif /* DISPLAY_H */
