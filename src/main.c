#include <stdio.h>
#include "memory.h"
#include "cpu.h"

int main(void) {
    printf("NuVec starting\n");
    printf("BIOS ROM starts at 0x%04X\n", BIOS_ROM_START);

    if (!mem_load_rom("roms/Vectrex BIOS (1982).vec", bios_rom, sizeof(bios_rom))) {
        fprintf(stderr, "Failed to load BIOS ROM\n");
        return 1;
    }

    mem_write8(RAM_START, 0x42);
    uint8_t value = mem_read8(RAM_START);
    printf("Wrote 0x42 to RAM_START, read back 0x%02X\n", value);

    Cpu cpu;
    cpu_reset(&cpu);
    cpu_print_state(&cpu);

    // LDA immediate
    mem_write8(RAM_START, 0x86);
    mem_write8(RAM_START + 1, 0x05);
    cpu.PC = RAM_START;

    cpu_step(&cpu);
    cpu_print_state(&cpu);

    // STA direct / LDA direct round-trip (DP set so the direct page lands in RAM)
    cpu.DP = 0xC0;
    mem_write8(RAM_START + 2, 0x97); // STA direct $10 -> effective addr 0xC010
    mem_write8(RAM_START + 3, 0x10);
    mem_write8(RAM_START + 4, 0x86); // LDA immediate #$00 (clear A before reload)
    mem_write8(RAM_START + 5, 0x00);
    mem_write8(RAM_START + 6, 0x96); // LDA direct $10 -> should reload 0x05
    mem_write8(RAM_START + 7, 0x10);

    cpu.PC = RAM_START + 2;
    cpu_step(&cpu);
    cpu_step(&cpu);
    cpu_step(&cpu);
    cpu_print_state(&cpu);

    // LDX extended from a known 16-bit value
    mem_write8(0xC700, 0x12);
    mem_write8(0xC701, 0x34);
    mem_write8(RAM_START + 8, 0xBE); // LDX extended $C700
    mem_write8(RAM_START + 9, 0xC7);
    mem_write8(RAM_START + 10, 0x00);

    cpu.PC = RAM_START + 8;
    cpu_step(&cpu);
    cpu_print_state(&cpu);

    // DECB/BNE loop: count B down from 3 to 0, using INCA to prove the loop
    // body actually ran the expected number of times (not zero, not forever).
    mem_write8(RAM_START + 11, 0x86);        // LDA #$00      (iteration counter)
    mem_write8(RAM_START + 12, 0x00);
    mem_write8(RAM_START + 13, 0xC6);        // LDB #$03      (loop counter)
    mem_write8(RAM_START + 14, 0x03);
    mem_write8(RAM_START + 15, 0x4C);        // loop: INCA
    mem_write8(RAM_START + 16, 0x5A);        //       DECB
    mem_write8(RAM_START + 17, 0x26);        //       BNE loop
    mem_write8(RAM_START + 18, (uint8_t)-4); //       offset back to RAM_START+15

    cpu.PC = RAM_START + 11;

    int steps = 0;
    const int max_steps = 100; // safety cap: a wrong-direction/infinite branch can't hang the test
    while (cpu.PC != RAM_START + 19 && steps < max_steps) {
        if (!cpu_step(&cpu)) break;
        steps++;
    }

    if (steps == 11 && cpu.A == 0x03 && cpu.B == 0x00 && cpu.PC == RAM_START + 19) {
        printf("loop test PASSED (%d cpu_step() calls, A=%02X iterations, B=%02X)\n", steps, cpu.A, cpu.B);
    } else {
        printf("loop test FAILED (%d cpu_step() calls, A=%02X, B=%02X, PC=%04X)\n", steps, cpu.A, cpu.B, cpu.PC);
    }
    cpu_print_state(&cpu);

    return 0;
}