#include <stdio.h>
#include "memory.h"

int main(void) {
    printf("NuVec starting\n");
    printf("BIOS ROM starts at 0x%04X\n", BIOS_ROM_START);

    mem_write8(RAM_START, 0x42);
    uint8_t value = mem_read8(RAM_START);
    printf("Wrote 0x42 to RAM_START, read back 0x%02X\n", value);

    return 0;
}