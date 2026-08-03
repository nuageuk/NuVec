#include "via.h"
#include "memory.h"
#include "display.h"
#include <string.h>

// 6522 VIA + Vectrex beam integrator. Real hardware draws vectors by
// time-multiplexing a single DAC (VIA Port A) onto two analog integrators
// (X and Y) through a mux (VIA Port B bits 2-1), then running Timer 1 in
// one-shot mode to gate ~RAMP for a fixed duration -- the DAC's held value
// on each integrator is added every cycle for as long as ~RAMP is active,
// producing a linear beam displacement proportional to (DAC value) *
// (T1 duration). The shift register (mode 4, driving ~CB2/~BLANK) decides
// whether that move is drawn or blanked. See:
// https://www.playvectrex.com/designit/chrissalo/vectordisplay.htm
//
// This replaces the old HLE_DRAW_VL approach (a fake "render this whole
// vector list in one call" shortcut) with the real mechanism: every ORA/
// ORB/shift-register/T1C-H write updates integrator state incrementally,
// exactly as it happens on real hardware, so any BIOS or cartridge code
// that draws via real VIA writes (Draw_Pat_VL and friends, not just the
// one HLE'd Draw_VL entry point) now produces visible output automatically.
typedef struct {
    uint16_t t1_counter;
    uint16_t t1_latch;
    uint16_t t2_counter;
    uint16_t t2_latch;
    int t2_expired; // one-shot: true once T2 has fired since its last reload

    uint8_t ifr;
    uint8_t ier;

    uint8_t ora, orb, ddra, ddrb, sr, acr, pcr;

    // Per-axis integrator state. rate_x/rate_y hold the last DAC value
    // (raw ORA byte) sampled while that axis was mux-selected -- real
    // hardware's per-axis sample-and-hold, which is why Draw_Pat_VL can
    // write Y then X then arm Timer 1 just once and still get a diagonal:
    // both integrators keep receiving their own held rate simultaneously
    // once ~RAMP goes active, not just whichever axis was selected last.
    uint8_t rate_x, rate_y;

    // Accumulated beam position, relative to center (0,0). Reset to
    // origin on via_reset() and whenever CA2 (~ZERO) is manually driven
    // low via PCR, matching real hardware grounding the beam to origin.
    double beam_x, beam_y;
} Via;

static Via via;

// DAC center value: real hardware's bipolar DAC produces zero net
// integrator velocity around mid-scale. 0x80 matches the convention the
// old HLE Draw_VL placeholder also assumed for signed deltas.
#define DAC_CENTER 0x80

// No official datasheet numbers exist for the analog integrator's
// volts-per-cycle gain (see investigation notes) -- this is an empirical
// placeholder scaling (DAC_value - center) * T1_duration down to a
// screen-sized displacement, tuned so a typical BIOS-driven vector lands
// within the 600x400 window. Replace with a measured constant if real
// hardware captures become available.
#define BEAM_SCALE (1.0 / 256.0)

#define SCREEN_ORIGIN_X 300
#define SCREEN_ORIGIN_Y 200

void via_reset(void) {
    memset(&via, 0, sizeof(via));
    via.rate_x = DAC_CENTER;
    via.rate_y = DAC_CENTER;
}

// T1 is modeled as always free-running (auto-reload from latch on every
// underflow) rather than switchable via ACR -- that's the only mode real
// Vectrex BIOS code actually relies on for its polling-delay idiom.
static void via_advance_t1(uint32_t cycles) {
    uint32_t remaining = via.t1_counter;
    if (cycles <= remaining) {
        via.t1_counter = (uint16_t)(remaining - cycles);
        return;
    }

    via.ifr |= VIA_IFR_T1;
    cycles -= remaining + 1;

    uint32_t period = (uint32_t)via.t1_latch + 1;
    via.t1_counter = (uint16_t)(via.t1_latch - (cycles % period));
}

// T2 is one-shot only (real 6522 behavior): it fires once, then this
// simplified model parks the counter at $FFFF until software reloads it via
// a T2C-H write. Real hardware keeps free-running past the underflow
// instead of parking, but nothing reads T2's raw counter value in the
// BIOS's use of it as a polled delay, so this is a deliberate simplification.
static void via_advance_t2(uint32_t cycles) {
    if (via.t2_expired) {
        return;
    }

    uint32_t remaining = via.t2_counter;
    if (cycles <= remaining) {
        via.t2_counter = (uint16_t)(remaining - cycles);
        return;
    }

    via.ifr |= VIA_IFR_T2;
    via.t2_expired = 1;
    via.t2_counter = 0xFFFF;
}

void via_tick(uint32_t cycles) {
    via_advance_t1(cycles);
    via_advance_t2(cycles);
}

// Mux select is Port B bits 2-1: 00=Y integrator, 01=X integrator,
// 10=Z (brightness) channel, 11=sound (AY-3-8912 handshake, not a beam axis).
typedef enum { MUX_Y = 0, MUX_X = 1, MUX_Z = 2, MUX_SOUND = 3 } MuxAxis;

static MuxAxis via_mux_axis(void) {
    return (MuxAxis)((via.orb >> 1) & 0x03);
}

// Samples the current DAC value (ORA) onto whichever integrator is
// currently mux-selected -- the real hardware's sample-and-hold per axis.
// This has to run on BOTH an ORA write (DAC value changes while an axis is
// already connected) AND an ORB write (a *new* axis gets connected to
// whatever's already sitting on the DAC) -- Draw_Pat_VL writes ORA=dy,
// *then* switches the mux to Y, then switches to X, *then* writes ORA=dx.
// Sampling only on ORA writes would miss dy entirely, since the mux still
// points at X (left over from the previous vector) at the moment dy is
// written -- Y only actually gets connected afterward, so it must sample
// whatever's already on the bus at that point.
static void via_sample_dac(void) {
    switch (via_mux_axis()) {
        case MUX_Y: via.rate_y = via.ora; break;
        case MUX_X: via.rate_x = via.ora; break;
        default: break; // Z/sound aren't beam axes
    }
}

// Runs one full ~RAMP-gated integration: both axes' held rates are applied
// simultaneously for the just-armed Timer 1 duration, then the resulting
// move is drawn (or not, if the shift register pattern is all zero, i.e.
// a blanked repositioning move) from the beam's prior position.
static void via_integrate_and_draw(void) {
    double duration = (double)via.t1_latch + 1.0;

    double vx = (double)((int)via.rate_x - DAC_CENTER);
    double vy = (double)((int)via.rate_y - DAC_CENTER);

    double old_x = via.beam_x;
    double old_y = via.beam_y;

    via.beam_x += vx * duration * BEAM_SCALE;
    // Vectrex Y increases upward; screen/SDL Y increases downward.
    via.beam_y -= vy * duration * BEAM_SCALE;

    // Shift register drives ~BLANK: all-zero means the beam stays
    // unblanked-off for this move (a repositioning move, not a draw).
    // Dash/partial-intensity patterns ($AA etc.) aren't modeled as actual
    // intermittent segments here -- any nonzero pattern draws a solid
    // line, a simplification of the real per-bit blank/unblank shifting.
    if (via.sr != 0) {
        display_draw_line((int)(SCREEN_ORIGIN_X + old_x), (int)(SCREEN_ORIGIN_Y - old_y),
                           (int)(SCREEN_ORIGIN_X + via.beam_x), (int)(SCREEN_ORIGIN_Y - via.beam_y));
    }
}

// CA2 (~ZERO) manually driven low (PCR bits 3-1 == 110) grounds the beam
// back to origin on real hardware -- the BIOS's Reset0Ref/Reset_Pen use
// this (PCR=$CC: CA2 and CB2 both manual-low) before repositioning.
static void via_check_zero_ref(uint8_t pcr_value) {
    uint8_t ca2_control = (pcr_value >> 1) & 0x07;
    if (ca2_control == 0x06) {
        via.beam_x = 0.0;
        via.beam_y = 0.0;
    }
}

uint8_t via_read8(uint16_t address) {
    uint8_t reg = (uint8_t)((address - VIA_START) & 0x0F);

    switch (reg) {
        case 0x0: return via.orb;
        case 0x1: return via.ora;
        case 0x2: return via.ddrb;
        case 0x3: return via.ddra;

        case 0x4: // T1C-L: reading the counter's low byte clears the T1 flag
            via.ifr &= (uint8_t)~VIA_IFR_T1;
            return (uint8_t)(via.t1_counter & 0xFF);
        case 0x5:
            return (uint8_t)(via.t1_counter >> 8);
        case 0x6:
            return (uint8_t)(via.t1_latch & 0xFF);
        case 0x7:
            return (uint8_t)(via.t1_latch >> 8);

        case 0x8: // T2C-L: reading the counter's low byte clears the T2 flag
            via.ifr &= (uint8_t)~VIA_IFR_T2;
            return (uint8_t)(via.t2_counter & 0xFF);
        case 0x9:
            return (uint8_t)(via.t2_counter >> 8);

        case 0xA: return via.sr;
        case 0xB: return via.acr;
        case 0xC: return via.pcr;

        case 0xD: {
            // Bit 7 is a computed composite: set whenever any enabled flag
            // (IFR & IER) is set, not an independently stored bit.
            uint8_t composite = (via.ifr & via.ier & 0x7F) != 0 ? 0x80 : 0x00;
            return (uint8_t)((via.ifr & 0x7F) | composite);
        }
        case 0xE:
            // IER always reads back with bit 7 set -- a documented 6522 quirk.
            return (uint8_t)(via.ier | 0x80);

        default: // 0xF: ORA/IRA without handshake -- same store as ORA
            return via.ora;
    }
}

void via_write8(uint16_t address, uint8_t value) {
    uint8_t reg = (uint8_t)((address - VIA_START) & 0x0F);

    switch (reg) {
        case 0x0:
            via.orb = value;
            via_sample_dac();
            return;
        case 0x1:
            via.ora = value;
            via_sample_dac();
            return;
        case 0x2: via.ddrb = value; return;
        case 0x3: via.ddra = value; return;

        case 0x4: // T1C-L write only touches the low-order LATCH, not the counter
            via.t1_latch = (uint16_t)((via.t1_latch & 0xFF00) | value);
            return;
        case 0x5:
            // T1C-H write loads both latch bytes into the counter, clears
            // the T1 flag, and (re)starts the count -- the real trigger for
            // T1's "arm and go" behavior. This is also the real trigger for
            // ~RAMP going active, so it's where beam integration happens.
            via.t1_latch = (uint16_t)((via.t1_latch & 0x00FF) | ((uint16_t)value << 8));
            via.t1_counter = via.t1_latch;
            via.ifr &= (uint8_t)~VIA_IFR_T1;
            via_integrate_and_draw();
            return;
        case 0x6:
            via.t1_latch = (uint16_t)((via.t1_latch & 0xFF00) | value);
            return;
        case 0x7:
            via.t1_latch = (uint16_t)((via.t1_latch & 0x00FF) | ((uint16_t)value << 8));
            return;

        case 0x8: // T2C-L: low-order latch only, same as T1
            via.t2_latch = (uint16_t)((via.t2_latch & 0xFF00) | value);
            return;
        case 0x9:
            // T2C-H write loads the counter, clears the T2 flag, and re-arms
            // the one-shot.
            via.t2_latch = (uint16_t)((via.t2_latch & 0x00FF) | ((uint16_t)value << 8));
            via.t2_counter = via.t2_latch;
            via.t2_expired = 0;
            via.ifr &= (uint8_t)~VIA_IFR_T2;
            return;

        case 0xA: via.sr = value; return;
        case 0xB: via.acr = value; return;
        case 0xC:
            via.pcr = value;
            via_check_zero_ref(value);
            return;

        case 0xD: // IFR: write-1-to-clear
            via.ifr &= (uint8_t)~(value & 0x7F);
            return;
        case 0xE:
            // IER: bit 7 set means OR the rest of the bits in (enable);
            // bit 7 clear means AND them out (disable).
            if (value & 0x80) {
                via.ier |= (uint8_t)(value & 0x7F);
            } else {
                via.ier &= (uint8_t)~(value & 0x7F);
            }
            return;

        default: // 0xF
            via.ora = value;
            return;
    }
}
