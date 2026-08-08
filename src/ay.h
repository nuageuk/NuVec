/* Public interface for AY-3-8912 register, timing, and audio emulation. */

#ifndef AY_H
#define AY_H

#include <stdint.h>

/* Initializes AY state and opens the SDL audio device. */
int ay_init(void);

/* Closes the SDL audio device and audio subsystem. */
void ay_shutdown(void);

/* Advances AY timing by the supplied number of 6809 CPU cycles. */
void ay_update(uint64_t cpu_cycles);

/* Selects the AY register addressed by the next data operation. */
void ay_write_addr(uint8_t address);

/* Writes a value to the currently selected AY register. */
void ay_write_data(uint8_t value);

/* Reads the value of the currently selected AY register. */
uint8_t ay_read_data(void);

/* Decodes an AY bus-control transition from VIA port B. */
void ay_via_orb(uint8_t orb);

/* Captures the AY data bus value driven through VIA port A. */
void ay_via_ora(uint8_t ora);

#endif /* AY_H */
