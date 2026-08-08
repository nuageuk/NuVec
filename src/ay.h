#ifndef AY_H
#define AY_H

#include <stdint.h>

void ay_write_addr(uint8_t address);
void ay_write_data(uint8_t value);
uint8_t ay_read_data(void);

#endif // AY_H
