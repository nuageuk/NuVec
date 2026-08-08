#ifndef AY_H
#define AY_H

#include <stdint.h>

int ay_init(void);
void ay_shutdown(void);
void ay_update(uint64_t cpu_cycles);
void ay_write_addr(uint8_t address);
void ay_write_data(uint8_t value);
uint8_t ay_read_data(void);
void ay_via_orb(uint8_t orb);
void ay_via_ora(uint8_t ora);

#endif // AY_H
