#ifndef VIA_H
#define VIA_H

#include <stdint.h>

// Interrupt Flag / Enable Register bits this emulation actually models.
// The other IFR/IER bits (CA1, CA2, CB1, CB2, shift register) correspond to
// peripheral pins (joystick/button input, AY-3-8912 handshake) that aren't
// emulated yet -- those bits just never set, which is harmless: software
// polling them for now will simply never see them fire.
#define VIA_IFR_T2 0x20
#define VIA_IFR_T1 0x40

void via_reset(void);

// Routed from mem_read8/mem_write8 for the whole VIA_START-VIA_END range.
// Only the low 4 address bits are decoded (real Vectrex hardware wiring),
// so the 16-register block mirrors throughout that entire window.
uint8_t via_read8(uint16_t address);
void via_write8(uint16_t address, uint8_t value);

// Advances both timers by the given number of CPU cycles, setting IFR bits
// on underflow. Called once per instruction from cpu_step() with that
// instruction's cycle cost.
void via_tick(uint32_t cycles);

// True whenever an enabled interrupt condition is pending (IFR & IER on any
// of the low 7 bits) -- the real 6522's IRQ output line. cpu_step() polls
// this once per instruction to decide whether to service a 6809 IRQ.
int via_irq_pending(void);

#endif // VIA_H
