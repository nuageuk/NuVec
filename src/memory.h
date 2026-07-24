#ifndef MEMORY_H
#define MEMORY_H

// Cartridge ROM
#define CART_ROM_START   0x0000
#define CART_ROM_END     0x7FFF

// RAM
#define RAM_START        0xC000
#define RAM_END          0xC7FF

// AY-3-8912 sound chip
#define AY_ADDR_LATCH    0xC800
#define AY_DATA          0xC900

// 6522 VIA (I/O)
#define VIA_START        0xD000
#define VIA_END          0xD7FF

// BIOS ROM
#define BIOS_ROM_START   0xE000
#define BIOS_ROM_END     0xFFFF

#endif // MEMORY_H