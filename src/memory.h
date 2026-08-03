#ifndef MEMORY_H
#define MEMORY_H

#define CART_ROM_START   0x0000
#define CART_ROM_END     0x7FFF

#define RAM_START        0xC000
#define RAM_END          0xCBFF

// AY-3-8912 sound chip
#define AY_ADDR_LATCH    0xC800
#define AY_DATA          0xC900

// 6522 VIA (I/O)
#define VIA_START        0xD000
#define VIA_END          0xD7FF

#define BIOS_ROM_START  0xF000
#define BIOS_ROM_END    0xFFFF

#include <stdint.h>

extern uint8_t ram[RAM_END - RAM_START + 1];
extern uint8_t bios_rom[BIOS_ROM_END - BIOS_ROM_START + 1];

uint8_t mem_read8(uint16_t address);
void mem_write8(uint16_t address, uint8_t value);

#define CART_ROM_SIZE (CART_ROM_END - CART_ROM_START + 1)

extern uint8_t cart_rom[CART_ROM_SIZE];

int mem_load_rom(const char* filepath, uint8_t* dest, size_t max_size);

#endif // MEMORY_H