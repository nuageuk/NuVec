#include "via.h"
#include "memory.h"
#include "analog.h"
#include <string.h>

// 6522 VIA register file, timers, and shift register, per
// docs/hardware-spec.md sections 1-4. Drives the analog beam integrator in
// analog.c (docs/hardware-spec.md sections 5-6): every register write that
// can affect the beam calls into analog.c directly, and via_tick() steps
// both the VIA and the integrator forward one cycle at a time.
typedef struct {
    uint8_t ora, orb, ddra, ddrb;

    uint16_t t1_latch;
    uint16_t t1_counter;
    int t1_running;
    int t1_irq_armed;
    int t1_pb7_high; // current level of Timer 1's PB7 view: 1 = high/idle, 0 = low

    uint8_t t2_latch_lo; // T2's only persistent latch byte (high byte loads straight into the counter)
    uint16_t t2_counter;
    int t2_running;
    int t2_irq_armed;

    uint8_t sr;
    int sr_bits_done;    // how many bits of the current transfer have shifted (8 = idle/complete)
    int sr_clock_ticks;  // countdown to the next shift-clock divider edge
    int sr_clock_high;   // current phase of the divided shift clock

    uint8_t acr, pcr;
    uint8_t ifr, ier;

    int ca2_level;      // ~ZERO line: 1 = high/idle, 0 = driven low
    int cb2_level;       // ~BLANK line as directly driven (handshake/pulse/manual)
    int cb2_shift_bit;   // ~BLANK as driven by the shift register, used when ACR bit 4 selects it
} Via;

static Via via;

void via_reset(void) {
    memset(&via, 0, sizeof(via));

    via.t1_pb7_high = 1;
    via.sr_bits_done = 8; // no transfer in progress
    via.ca2_level = 1;
    via.cb2_level = 1;

    analog_reset();
}

int via_irq_pending(void) {
    return (via.ifr & via.ier & 0x7F) != 0;
}

// Timer 1: docs/hardware-spec.md section 2, ACR bits 7-6.
static void step_timer1(void) {
    if (!via.t1_running) {
        return;
    }
    via.t1_counter--;
    if (via.t1_counter != 0xFFFF) {
        return; // no underflow this cycle
    }

    if (via.acr & 0x40) {
        // Free-running: reload, flag an interrupt, and keep toggling PB7
        // forever -- this square wave is also what drives ~RAMP when ACR
        // bit 7 gives Timer 1 control of it.
        via.ifr |= VIA_IFR_T1;
        via.t1_pb7_high = !via.t1_pb7_high;
        via.t1_counter = via.t1_latch;
    } else if (via.t1_irq_armed) {
        // One-shot: fire once, drop PB7 low once, then go idle until
        // software re-arms it with a new T1C-H write.
        via.ifr |= VIA_IFR_T1;
        via.t1_pb7_high = 1;
        via.t1_irq_armed = 0;
    }
}

// Timer 2: docs/hardware-spec.md section 2, ACR bit 5. Pulse-counting mode
// (ACR bit 5 set) would count negative pulses on PB6; no PB6 input source
// is modeled, so Timer 2 simply never advances while that mode is selected.
static void step_timer2(void) {
    if (!via.t2_running || (via.acr & 0x20)) {
        return;
    }
    via.t2_counter--;
    if (via.t2_counter != 0xFFFF) {
        return;
    }
    if (via.t2_irq_armed) {
        via.ifr |= VIA_IFR_T2;
        via.t2_irq_armed = 0;
    }
}

typedef enum { SR_CLOCK_NONE, SR_CLOCK_T2, SR_CLOCK_SYSTEM, SR_CLOCK_EXTERNAL, SR_CLOCK_FREE_T2 } SrClock;
typedef enum { SR_IN, SR_OUT } SrDirection;

typedef struct {
    SrDirection dir;
    SrClock clock;
    int counts_bits; // does this mode track the 8-bit count / raise the SR interrupt?
} SrMode;

// The 8 shift register modes, ACR bits 4-2 (docs/hardware-spec.md section 3).
static const SrMode SR_MODES[8] = {
    { SR_IN,  SR_CLOCK_NONE,     0 }, // 000: disabled
    { SR_IN,  SR_CLOCK_T2,       1 }, // 001
    { SR_IN,  SR_CLOCK_SYSTEM,   1 }, // 010
    { SR_IN,  SR_CLOCK_EXTERNAL, 0 }, // 011: CB1-clocked, unmodeled
    { SR_OUT, SR_CLOCK_FREE_T2,  0 }, // 100: free-running
    { SR_OUT, SR_CLOCK_T2,       1 }, // 101
    { SR_OUT, SR_CLOCK_SYSTEM,   1 }, // 110
    { SR_OUT, SR_CLOCK_EXTERNAL, 0 }, // 111: CB1-clocked, unmodeled
};

// The shift clock is the system clock divided down by Timer 2's low-order
// latch (independent of Timer 2's own counter/mode), then divided by two
// again -- one full divider period high, one low -- so a shift edge fires
// once per two divider reloads.
static int shift_clock_edge(void) {
    if (via.sr_clock_ticks != 0) {
        via.sr_clock_ticks--;
        return 0;
    }
    via.sr_clock_ticks = via.t2_latch_lo;
    int firing = via.sr_clock_high;
    via.sr_clock_high = !via.sr_clock_high;
    return firing;
}

static void step_shift_register(void) {
    int edge = shift_clock_edge();

    if (via.sr_bits_done >= 8) {
        return; // idle: no transfer armed
    }

    const SrMode *mode = &SR_MODES[(via.acr >> 2) & 0x07];
    int clocked = (mode->clock == SR_CLOCK_SYSTEM) ||
                  ((mode->clock == SR_CLOCK_T2 || mode->clock == SR_CLOCK_FREE_T2) && edge);
    if (!clocked) {
        return;
    }

    if (mode->dir == SR_IN) {
        via.sr = (uint8_t)(via.sr << 1);
    } else {
        via.cb2_shift_bit = (via.sr >> 7) & 1;
        via.sr = (uint8_t)((via.sr << 1) | via.cb2_shift_bit);
    }

    if (mode->counts_bits) {
        via.sr_bits_done++;
        if (via.sr_bits_done == 8) {
            via.ifr |= VIA_IFR_SR;
        }
    }
}

// ~RAMP: docs/hardware-spec.md section 5. Active (beam moving) when low.
static int ramp_is_active(void) {
    if (via.acr & 0x80) {
        return !via.t1_pb7_high;
    }
    return (via.orb & 0x80) == 0;
}

// ~BLANK: docs/hardware-spec.md section 5.
static int blank_is_on(void) {
    return (via.acr & 0x10) ? via.cb2_shift_bit : via.cb2_level;
}

// PCR pulse mode (docs/hardware-spec.md section 4): CA2/CB2 self-restore to
// '1' one cycle after whatever drove them low.
static void restore_pulsed_lines(void) {
    if ((via.pcr & 0x0E) == 0x0A) {
        via.ca2_level = 1;
    }
    if ((via.pcr & 0xE0) == 0xA0) {
        via.cb2_level = 1;
    }
}

static void via_step_cycle(void) {
    step_timer1();
    step_timer2();
    step_shift_register();

    int zero_ref = (via.ca2_level == 0);
    analog_step(ramp_is_active(), blank_is_on(), zero_ref);

    restore_pulsed_lines();
}

void via_tick(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        via_step_cycle();
    }
}

// Port A read-back: output bits (DDRA=1) read the ORA latch; input bits
// read as idle/released (1, active-low convention) since no live
// controller is modeled.
static uint8_t read_port_a(void) {
    return (uint8_t)((via.ora & via.ddra) | (uint8_t)(~via.ddra));
}

uint8_t via_read8(uint16_t address) {
    uint8_t reg = (uint8_t)((address - VIA_START) & 0x0F);

    switch (reg) {
        case 0x0: // ORB -- bit 7 is a computed composite when Timer 1 has
                  // control of it (ACR bit 7); bit 5 is the joystick
                  // comparator, unmodeled, masked to 0.
            if (via.acr & 0x80) {
                return (uint8_t)((via.orb & 0x5F) | (via.t1_pb7_high ? 0x80 : 0x00));
            }
            return (uint8_t)(via.orb & 0xDF);
        case 0x1: return read_port_a();
        case 0x2: return via.ddrb;
        case 0x3: return via.ddra;

        case 0x4: // T1C-L: reading the low byte clears the T1 flag and
                  // stops the timer -- a mid-ramp read aborts the vector.
            via.ifr &= (uint8_t)~VIA_IFR_T1;
            via.t1_running = 0;
            via.t1_irq_armed = 0;
            via.t1_pb7_high = 1;
            return (uint8_t)(via.t1_counter & 0xFF);
        case 0x5:
            return (uint8_t)(via.t1_counter >> 8);
        case 0x6:
            return (uint8_t)(via.t1_latch & 0xFF);
        case 0x7:
            return (uint8_t)(via.t1_latch >> 8);

        case 0x8: // T2C-L: reading the low byte clears the T2 flag.
            via.ifr &= (uint8_t)~VIA_IFR_T2;
            via.t2_running = 0;
            via.t2_irq_armed = 0;
            return (uint8_t)(via.t2_counter & 0xFF);
        case 0x9:
            return (uint8_t)(via.t2_counter >> 8);

        case 0xA: // SR: reading it resets the bit counter and clock phase,
                  // restarting whatever transfer is in progress.
            via.ifr &= (uint8_t)~VIA_IFR_SR;
            via.sr_bits_done = 0;
            via.sr_clock_high = 1;
            return via.sr;
        case 0xB: return via.acr;
        case 0xC: return via.pcr;

        case 0xD: {
            // Bit 7 is computed: set whenever any enabled flag (IFR & IER) is set.
            uint8_t any_enabled = (via.ifr & via.ier & 0x7F) != 0 ? 0x80 : 0x00;
            return (uint8_t)((via.ifr & 0x7F) | any_enabled);
        }
        case 0xE:
            return (uint8_t)(via.ier | 0x80); // bit 7 always reads set

        default: // 0xF: ORA/IRA without handshake -- same underlying latch
            return read_port_a();
    }
}

void via_write8(uint16_t address, uint8_t value) {
    uint8_t reg = (uint8_t)((address - VIA_START) & 0x0F);

    switch (reg) {
        case 0x0:
            via.orb = value;
            analog_mux_route(value);
            return;
        case 0x1:
        case 0xF: // ORA/IRA without handshake writes the same latch
            via.ora = value;
            analog_dac_write(value, via.orb);
            return;
        case 0x2: via.ddrb = value; return;
        case 0x3: via.ddra = value; return;

        case 0x4: // T1C-L / T1L-L: low-order latch only, counter untouched
        case 0x6:
            via.t1_latch = (uint16_t)((via.t1_latch & 0xFF00) | value);
            return;
        case 0x5:
            // T1C-H: loads both latch bytes into the live counter, clears
            // the T1 flag, arms it, and drops PB7 low -- the actual
            // trigger for a real vector's draw beginning.
            via.t1_latch = (uint16_t)((via.t1_latch & 0x00FF) | ((uint16_t)value << 8));
            via.t1_counter = via.t1_latch;
            via.ifr &= (uint8_t)~VIA_IFR_T1;
            via.t1_running = 1;
            via.t1_irq_armed = 1;
            via.t1_pb7_high = 0;
            return;
        case 0x7: // T1L-H: high-order latch only, no counter/PB7 effect
            via.t1_latch = (uint16_t)((via.t1_latch & 0x00FF) | ((uint16_t)value << 8));
            return;

        case 0x8: // T2C-L / T2L-L: low-order latch only
            via.t2_latch_lo = value;
            return;
        case 0x9:
            // T2C-H: combines with the low latch to load the counter,
            // clears the T2 flag, and arms it.
            via.t2_counter = (uint16_t)(((uint16_t)value << 8) | via.t2_latch_lo);
            via.ifr &= (uint8_t)~VIA_IFR_T2;
            via.t2_running = 1;
            via.t2_irq_armed = 1;
            return;

        case 0xA:
            via.sr = value;
            via.ifr &= (uint8_t)~VIA_IFR_SR;
            via.sr_bits_done = 0;
            via.sr_clock_high = 1;
            return;
        case 0xB: via.acr = value; return;
        case 0xC:
            via.pcr = value;
            // Manual-low is the only PCR mode Vectrex software actually
            // drives ~ZERO/~BLANK with directly (docs/hardware-spec.md
            // section 4); anything else reads as high here, with pulse
            // mode's low-then-restore handled per-cycle above.
            via.ca2_level = ((value & 0x0E) == 0x0C) ? 0 : 1;
            via.cb2_level = ((value & 0xE0) == 0xC0) ? 0 : 1;
            return;

        case 0xD: // IFR: write-1-to-clear
            via.ifr &= (uint8_t)~(value & 0x7F);
            return;
        case 0xE:
            // IER: bit 7 of the written byte selects OR (enable, bit7=1)
            // vs AND-out (disable, bit7=0) for the remaining bits.
            if (value & 0x80) {
                via.ier |= (uint8_t)(value & 0x7F);
            } else {
                via.ier &= (uint8_t)~(value & 0x7F);
            }
            return;

        default:
            return; // unreachable: all 16 registers handled above
    }
}
