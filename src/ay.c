#include "ay.h"

#define AY_REGISTER_COUNT 16

static uint8_t registers[AY_REGISTER_COUNT];
static uint8_t selected_register;

void ay_write_addr(uint8_t address) {
    selected_register = address & 0x0F;
}

void ay_write_data(uint8_t value) {
    registers[selected_register] = value;
}

uint8_t ay_read_data(void) {
    return registers[selected_register];
}
