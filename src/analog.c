#include "analog.h"
#include "display.h"
#include <string.h>

// Implements docs/hardware-spec.md sections 5-6 (Vectrex analog signal
// path and the segment-tracing behavior the emulator needs to produce
// from it). See analog.h for the public interface this presents to via.c.

// The DAC/mux sample-and-holds (docs/hardware-spec.md section 5).
typedef struct {
    unsigned x;   // fed directly by every ORA write
    unsigned y;
    unsigned ref; // "R", the shared zero-reference bias for both axes
    unsigned z;   // 0-127 brightness
} SampleHolds;

// The beam's position and, when a line is currently being traced, the
// segment that's being extended.
typedef struct {
    int32_t x, y;
    int32_t delta_x, delta_y;

    int segment_open;
    int32_t seg_x0, seg_y0, seg_x1, seg_y1;
    int32_t seg_delta_x, seg_delta_y; // deltas in effect when this segment opened
    uint8_t seg_z;
} Beam;

typedef struct {
    int32_t x0, y0, x1, y1;
    uint8_t z;
} Segment;

enum {
    // Real hardware's ~1.5MHz 6809E clock. There's no register that defines
    // a "frame" -- this just approximates how often a game redraws its full
    // display list, which is what a CRT with phosphor persistence needs to
    // look stable (see docs/hardware-spec.md section 6).
    CPU_CYCLES_PER_SEC = 1500000,
    TARGET_REFRESH_HZ = 30,
    FRAME_CYCLES = CPU_CYCLES_PER_SEC / TARGET_REFRESH_HZ,

    // Upper bound on distinct segments one checkpoint interval could ever
    // produce: a segment can open at soonest every couple of cycles, so
    // this (generous) count can never actually be exceeded by a real
    // program -- the capacity check in push_segment() is just a defensive
    // backstop, not something expected to trigger.
    SEGMENT_CAPACITY = FRAME_CYCLES
};

typedef struct {
    Segment slot_a[SEGMENT_CAPACITY];
    Segment slot_b[SEGMENT_CAPACITY];
    Segment *current;
    Segment *previous;
    int current_count;
    int previous_count;
    int cycles_left;
} Checkpoint;

static SampleHolds hold;
static Beam beam;
static Checkpoint frame;

void analog_reset(void) {
    memset(&hold, 0, sizeof(hold));
    memset(&beam, 0, sizeof(beam));
    memset(&frame, 0, sizeof(frame));

    // Sample-and-holds power up centered/off, matching real hardware: Z is
    // deliberately 0 (invisible) until BIOS/game code writes a brightness.
    hold.x = 128;
    hold.y = 128;
    hold.ref = 128;
    hold.z = 0;

    beam.x = ALG_MAX_X / 2;
    beam.y = ALG_MAX_Y / 2;

    frame.current = frame.slot_a;
    frame.previous = frame.slot_b;
    frame.cycles_left = FRAME_CYCLES;
}

static void recompute_beam_deltas(void) {
    // R-relative rather than raw X-Y: a single zero-reference write shifts
    // both axes' effective origin at once, because R is a shared bias into
    // both integrators (docs/hardware-spec.md section 5).
    beam.delta_x = (int32_t)hold.x - (int32_t)hold.ref;
    beam.delta_y = (int32_t)hold.ref - (int32_t)hold.y;
}

void analog_dac_write(uint8_t value, uint8_t orb) {
    // Bipolar DAC coding: the sign bit is inverted before conversion, so
    // the effective sample-and-hold value sits in an unsigned 0-255 space
    // centered on 128 (docs/hardware-spec.md section 5).
    hold.x = value ^ 0x80;
    analog_mux_route(orb);
}

void analog_mux_route(uint8_t orb) {
    // Demultiplexor is only active while ORB bit 0 is clear; while
    // inactive, whichever hold *would* have been targeted just keeps its
    // last sampled value.
    if ((orb & 0x01) == 0) {
        switch ((orb >> 1) & 0x03) {
            case 0x00: // Y-axis integrator
                hold.y = hold.x;
                break;
            case 0x01: // zero-reference (R)
                hold.ref = hold.x;
                break;
            case 0x02: // Z (brightness) -- only the upper half of the
                       // range is visible; the lower half reads as off
                hold.z = (hold.x > 0x80) ? (hold.x - 0x80) : 0;
                break;
            case 0x03: // sound chip data line, not a beam axis
                break;
        }
    }

    recompute_beam_deltas();
}

static int beam_in_bounds(void) {
    return beam.x >= 0 && beam.x < ALG_MAX_X && beam.y >= 0 && beam.y < ALG_MAX_Y;
}

static void push_segment(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t z) {
    if (frame.current_count >= SEGMENT_CAPACITY) {
        return;
    }
    Segment *s = &frame.current[frame.current_count++];
    s->x0 = x0;
    s->y0 = y0;
    s->x1 = x1;
    s->y1 = y1;
    s->z = z;
}

static void close_open_segment(void) {
    if (!beam.segment_open) {
        return;
    }
    push_segment(beam.seg_x0, beam.seg_y0, beam.seg_x1, beam.seg_y1, beam.seg_z);
    beam.segment_open = 0;
}

static void open_segment(int32_t delta_x, int32_t delta_y) {
    if (!beam_in_bounds()) {
        return; // beam isn't anywhere a segment can start from right now
    }
    beam.segment_open = 1;
    beam.seg_x0 = beam.seg_x1 = beam.x;
    beam.seg_y0 = beam.seg_y1 = beam.y;
    beam.seg_delta_x = delta_x;
    beam.seg_delta_y = delta_y;
    beam.seg_z = (uint8_t)hold.z;
}

static void checkpoint_flush(void) {
    frame.previous_count = frame.current_count;
    frame.current_count = 0;

    Segment *swap = frame.previous;
    frame.previous = frame.current;
    frame.current = swap;
}

void analog_step(int ramp_active, int blank_on, int zero_ref) {
    int32_t delta_x, delta_y;

    if (zero_ref) {
        // ~ZERO held low: pull the beam back toward center regardless of
        // whatever the sample-and-holds are currently set to.
        delta_x = ALG_MAX_X / 2 - beam.x;
        delta_y = ALG_MAX_Y / 2 - beam.y;
    } else if (ramp_active) {
        delta_x = beam.delta_x;
        delta_y = beam.delta_y;
    } else {
        delta_x = 0;
        delta_y = 0;
    }

    if (blank_on) {
        if (!beam.segment_open) {
            open_segment(delta_x, delta_y);
        } else if (delta_x != beam.seg_delta_x || delta_y != beam.seg_delta_y ||
                   (uint8_t)hold.z != beam.seg_z) {
            // Still visible, but the rate or brightness changed mid-line:
            // this is a new segment as far as the display is concerned.
            close_open_segment();
            open_segment(delta_x, delta_y);
        }
    } else {
        close_open_segment();
    }

    // The beam physically moves every cycle regardless of visibility -- a
    // blanked move still repositions it, it just isn't drawn.
    beam.x += delta_x;
    beam.y += delta_y;

    if (beam.segment_open && beam_in_bounds()) {
        beam.seg_x1 = beam.x;
        beam.seg_y1 = beam.y;
    }

    if (--frame.cycles_left < 0) {
        frame.cycles_left += FRAME_CYCLES;
        checkpoint_flush();
    }
}

// Scales ALG_MAX_X/Y integrator-space coordinates down to actual window
// pixels, fitting the whole integrator range into the window without
// distortion and centering it (nuvec's SDL window isn't resizable, so this
// only ever needs doing once -- but it's cheap enough to just redo per
// segment).
static void analog_to_screen(int32_t ax, int32_t ay, int *sx, int *sy) {
    static const double scale_x = (double)ALG_MAX_X / DISPLAY_WIDTH;
    static const double scale_y = (double)ALG_MAX_Y / DISPLAY_HEIGHT;
    double scale = scale_x > scale_y ? scale_x : scale_y;
    double offset_x = (DISPLAY_WIDTH - ALG_MAX_X / scale) / 2.0;
    double offset_y = (DISPLAY_HEIGHT - ALG_MAX_Y / scale) / 2.0;

    *sx = (int)(offset_x + ax / scale);
    *sy = (int)(offset_y + ay / scale);
}

static void render_segments(const Segment *list, int count, int dim) {
    for (int i = 0; i < count; i++) {
        int x0, y0, x1, y1;
        analog_to_screen(list[i].x0, list[i].y0, &x0, &y0);
        analog_to_screen(list[i].x1, list[i].y1, &x1, &y1);

        // 0-127 Z-channel range -> 0-255 display brightness.
        int brightness = list[i].z * 256 / ANALOG_COLORS;
        if (dim) {
            // The prior checkpoint's segments render dimmer, both as a
            // crude phosphor-persistence approximation and, concretely, so
            // the screen never goes solid black just because the CPU
            // hasn't produced a new segment since the last checkpoint.
            brightness /= 2;
        }
        if (brightness > 255) {
            brightness = 255;
        }

        display_draw_line(x0, y0, x1, y1, (uint8_t)brightness);
    }
}

void analog_render(void) {
    render_segments(frame.previous, frame.previous_count, 1);
    render_segments(frame.current, frame.current_count, 0);
}
