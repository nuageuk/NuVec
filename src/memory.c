#include "memory.h"

uint8_t ram[RAM_END - RAM_START + 1];
uint8_t bios_rom[BIOS_ROM_END - BIOS_ROM_START + 1];

uint8_t mem_read8(uint16_t address) {
    if (address >= RAM_START && address <= RAM_END) {
        return ram[address - RAM_START];
    }
    if (address >= BIOS_ROM_START && address <= BIOS_ROM_END) {
        return bios_rom[address - BIOS_ROM_START];
    }
    // Unmapped region — real hardware behavior varies, return 0 for now
    return 0;
}

void mem_write8(uint16_t address, uint8_t value) {
    if (address >= RAM_START && address <= RAM_END) {
        ram[address - RAM_START] = value;
        return;
    }
    // Writes to ROM or unmapped regions are ignored for now
}