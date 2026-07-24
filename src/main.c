#include <stdio.h>
#include "memory.h"

int main(void) {
    printf("NuVec starting\n");
    printf("BIOS ROM starts at 0x%04X\n", BIOS_ROM_START);
    return 0;
}
