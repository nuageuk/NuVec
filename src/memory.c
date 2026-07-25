#include "memory.h"
#include <stdio.h>

uint8_t ram[RAM_END - RAM_START + 1];
uint8_t bios_rom[BIOS_ROM_END - BIOS_ROM_START + 1];
uint8_t cart_rom[CART_ROM_SIZE];

uint8_t mem_read8(uint16_t address) {
    if (address >= CART_ROM_START && address <= CART_ROM_END) {
        return cart_rom[address - CART_ROM_START];
    }
    if (address >= RAM_START && address <= RAM_END) {
        return ram[address - RAM_START];
    }
    if (address >= BIOS_ROM_START && address <= BIOS_ROM_END) {
        return bios_rom[address - BIOS_ROM_START];
    }
    return 0;
}

void mem_write8(uint16_t address, uint8_t value) {
    if (address >= RAM_START && address <= RAM_END) {
        ram[address - RAM_START] = value;
        return;
    }
    // Writes to ROM or unmapped regions are ignored for now
}

int mem_load_rom(const char* filepath, uint8_t* dest, size_t max_size) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        return 0;
    }

    size_t bytes_read = fread(dest, 1, max_size, f);
    fclose(f);

    return bytes_read > 0;
}