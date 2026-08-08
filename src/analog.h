/* Public interface for the Vectrex analog beam integrator. */

#ifndef ANALOG_H
#define ANALOG_H

#include <stdint.h>

// Vectrex analog beam integrator: the X/Y/zero-ref/Z sample-and-holds, the
// beam-delta computation, and the per-cycle position integration that turns
// VIA register writes into actual vectors. via.c owns the 6522 register
// file and timer/shift-register state, and calls into here on every
// register write that can affect the beam plus once per CPU cycle to
// advance the integrator itself. Behavior is specified in
// docs/hardware-spec.md (sections 5-6).
//
// ALG_MAX_X/Y are the DAC's full excursion range in integrator units (not
// screen pixels), tuned so accumulating a beam delta in DAC units every
// cycle for a real vector's Timer 1 duration produces a displacement of the
// right *proportion* of the screen -- no separate empirical scale constant
// needed. display.c maps this space down to actual window pixels at render
// time.
enum {
    ALG_MAX_X = 33000,
    ALG_MAX_Y = 41000,

    // Number of distinct brightness levels the Z-channel sample-and-hold
    // can express (7 usable bits of DAC range once the sign half is used
    // for the "invisible" case -- see docs/hardware-spec.md section 5).
    ANALOG_COLORS = 128
};

/* Resets the sample-and-holds, beam position, and vector checkpoints. */
void analog_reset(void);

// Called after every ORA write: value is the raw DAC byte (via.ora) and orb
// is the *current* ORB (via.orb, mux/demux control) at the moment of this
// write. Feeds the DAC's held value into the X sample-and-hold, then
// re-derives whichever axis the mux currently selects -- the value already
// sitting on the DAC has to be visible to the mux switch too, since real
// hardware routes the DAC's live output, not a separately latched copy.
void analog_dac_write(uint8_t value, uint8_t orb);

// Called after every ORB write with the new ORB value: re-derives Y/zero-ref
// (R)/Z from whatever is currently held in X, per the ORB bits 2-1 mux
// select (demultiplexor enabled only when ORB bit 0 is clear).
void analog_mux_route(uint8_t orb);

// One cycle of beam integration, called once per CPU clock cycle from
// via_tick(). ramp_active is true when the beam should actually move this
// cycle (real hardware's ~RAMP line asserted). blank_on is the unblank
// signal (beam currently visible). zero_ref is true while ~ZERO is being
// driven low (forces the beam back toward center for recentering).
void analog_step(int ramp_active, int blank_on, int zero_ref);

// Draws the current and previous checkpoint's accumulated line segments
// (see docs/hardware-spec.md section 6) via display_draw_line(). Call once
// per host/SDL frame, between display_clear() and display_present() --
// does NOT clear or present itself, so the caller controls that pacing.
void analog_render(void);

#endif /* ANALOG_H */
