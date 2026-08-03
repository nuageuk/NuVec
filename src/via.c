#include "via.h"
#include "memory.h"
#include <string.h>

// Minimal 6522 VIA: enough real register/timer behavior to unblock BIOS code
// that polls IFR for T1/T2 timeout, without modeling the peripheral side
// (joystick/button input via ORA/ORB, AY-3-8912 handshake via CA2/CB2, shift
// register). Those registers are still readable/writable -- just as inert
// storage that round-trips whatever was last written, rather than driving
// any real behavior.
typedef struct {
    uint16_t t1_counter;
    uint16_t t1_latch;
    uint16_t t2_counter;
    uint16_t t2_latch;
    int t2_expired; // one-shot: true once T2 has fired since its last reload

    uint8_t ifr;
    uint8_t ier;

    uint8_t ora, orb, ddra, ddrb, sr, acr, pcr;
} Via;

static Via via;

void via_reset(void) {
    memset(&via, 0, sizeof(via));
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
        case 0x0: via.orb = value; return;
        case 0x1: via.ora = value; return;
        case 0x2: via.ddrb = value; return;
        case 0x3: via.ddra = value; return;

        case 0x4: // T1C-L write only touches the low-order LATCH, not the counter
            via.t1_latch = (uint16_t)((via.t1_latch & 0xFF00) | value);
            return;
        case 0x5:
            // T1C-H write loads both latch bytes into the counter, clears
            // the T1 flag, and (re)starts the count -- the real trigger for
            // T1's "arm and go" behavior.
            via.t1_latch = (uint16_t)((via.t1_latch & 0x00FF) | ((uint16_t)value << 8));
            via.t1_counter = via.t1_latch;
            via.ifr &= (uint8_t)~VIA_IFR_T1;
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
        case 0xC: via.pcr = value; return;

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
