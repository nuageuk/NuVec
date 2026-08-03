#include "memory.h"
#include "via.h"
#include "mem_trace.h"
#include <stdio.h>

// Instrumented drop-in replacement for memory.c, used only by the standalone
// trace harness (trace_main.c) to log VIA ($D000-$D7FF) and AY-3-8912
// ($C800/$C900) access on demand (see mem_trace_enabled). Not part of the
// CMake/SDL build -- behavior is otherwise identical to memory.c.

uint8_t ram[RAM_END - RAM_START + 1];
uint8_t bios_rom[BIOS_ROM_END - BIOS_ROM_START + 1];
uint8_t cart_rom[CART_ROM_SIZE];

int mem_trace_enabled = 0;
uint16_t mem_trace_pc = 0;
long mem_trace_step = 0;

static void log_access(const char *op, uint16_t address, uint8_t value) {
    if (!mem_trace_enabled) {
        return;
    }
    // Narrowed to a small fixed address set (rather than every VIA/AY/cart
    // access) so a multi-million-step trace doesn't drown in noise. Adjust
    // this list to whatever's currently under investigation.
    if (address == 0xC839 || address == 0xC83A || address == 0xC856 ||
        (address >= CART_ROM_START && address <= CART_ROM_END)) {
        printf("step=%ld  PC=$%04X  %s $%04X = $%02X\n", mem_trace_step, mem_trace_pc, op, address, value);
    }
}

uint8_t mem_read8(uint16_t address) {
    uint8_t value;
    if (address >= CART_ROM_START && address <= CART_ROM_END) {
        value = cart_rom[address - CART_ROM_START];
    } else if (address >= RAM_START && address <= RAM_END) {
        value = ram[address - RAM_START];
    } else if (address >= VIA_START && address <= VIA_END) {
        value = via_read8(address);
    } else if (address >= BIOS_ROM_START && address <= BIOS_ROM_END) {
        value = bios_rom[address - BIOS_ROM_START];
    } else {
        value = 0;
    }
    log_access("READ ", address, value);
    return value;
}

void mem_write8(uint16_t address, uint8_t value) {
    log_access("WRITE", address, value);
    if (address >= RAM_START && address <= RAM_END) {
        ram[address - RAM_START] = value;
        return;
    }
    if (address >= VIA_START && address <= VIA_END) {
        via_write8(address, value);
        return;
    }
    // Writes to ROM or unmapped regions are silently ignored.
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
