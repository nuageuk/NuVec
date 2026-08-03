#include "memory.h"
#include "cpu.h"
#include "mem_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Headless trace harness (not part of the CMake/SDL build -- compiled and
// run standalone). Loads the BIOS, optionally a cartridge from argv[1], then
// free-runs cpu_step() for a fixed budget with HLE active, reporting whether
// execution ever diverges from the BIOS-only idle path into cartridge space
// and whether/what it draws via Draw_VL ($F3DD).

int main(int argc, char *argv[]) {
    if (!mem_load_rom("roms/Vectrex BIOS (1982).vec", bios_rom, sizeof(bios_rom))) {
        fprintf(stderr, "Failed to load BIOS ROM\n");
        return 1;
    }

    int has_cart = 0;
    if (argc > 1 && strcmp(argv[1], "-") != 0) {
        if (!mem_load_rom(argv[1], cart_rom, sizeof(cart_rom))) {
            fprintf(stderr, "Failed to load cartridge ROM from '%s'\n", argv[1]);
            return 1;
        }
        has_cart = 1;
        printf("Cartridge ROM loaded from '%s'\n", argv[1]);
    } else {
        printf("No cartridge ROM provided; booting BIOS only\n");
    }

    Cpu cpu;
    cpu_reset(&cpu);

    static int visited[65536];
    memset(visited, 0, sizeof(visited));

    const int STEP_BUDGET = (argc > 2) ? atoi(argv[2]) : 2000000;
    long cart_region_hits = 0;
    int first_cart_step = -1;
    uint16_t first_cart_pc = 0;
    long draw_vl_hits = 0;
    int first_draw_vl_step = -1;
    uint16_t first_draw_vl_x = 0;

#define RING 64
    uint16_t ring[RING];
    memset(ring, 0, sizeof(ring));

    // Optional VIA/AY access-logging window: argv[3]=start step, argv[4]=how
    // many steps to log. Off by default (num_steps==0).
    int trace_start = (argc > 3) ? atoi(argv[3]) : 0;
    int trace_count = (argc > 4) ? atoi(argv[4]) : 0;

    int step;
    int halted = 0;
    uint16_t halt_pc = 0;
    uint8_t halt_opcode = 0;

    for (step = 0; step < STEP_BUDGET; step++) {
        uint16_t pc = cpu.PC;
        visited[pc]++;
        ring[step % RING] = pc;

        mem_trace_pc = pc;
        mem_trace_enabled = (trace_count > 0 && step >= trace_start && step < trace_start + trace_count);

        if (has_cart && pc <= CART_ROM_END) {
            cart_region_hits++;
            if (first_cart_step < 0) {
                first_cart_step = step;
                first_cart_pc = pc;
            }
        }

        if (pc == HLE_DRAW_VL) {
            draw_vl_hits++;
            if (first_draw_vl_step < 0) {
                first_draw_vl_step = step;
                first_draw_vl_x = cpu.X;
            }
        }

        if (!cpu_step(&cpu)) {
            halted = 1;
            halt_pc = pc;
            halt_opcode = mem_read8(pc);
            break;
        }
    }

    printf("\n=== Trace summary (%s) ===\n", has_cart ? argv[1] : "no cartridge");
    printf("Steps executed: %d / %d\n", step, STEP_BUDGET);
    if (halted) {
        printf("HALTED: unimplemented opcode 0x%02X at PC=$%04X\n", halt_opcode, halt_pc);
    }
    printf("Vec_Loop_Count ($C825/$C826) final value: 0x%04X\n",
           ((uint16_t)mem_read8(0xC825) << 8) | mem_read8(0xC826));

    int distinct_pc = 0;
    for (int i = 0; i < 65536; i++) if (visited[i]) distinct_pc++;
    printf("Distinct PC values visited: %d\n", distinct_pc);

    if (getenv("DUMP_VISITED")) {
        printf("All visited PCs (addr:count):\n");
        for (int i = 0; i < 65536; i++) {
            if (visited[i]) printf("$%04X:%d ", i, visited[i]);
        }
        printf("\n");
    }

    if (has_cart) {
        printf("Cartridge-space ($0000-$7FFF) instructions executed: %ld\n", cart_region_hits);
        if (first_cart_step >= 0) {
            printf("First entered cartridge space at step %d, PC=$%04X\n", first_cart_step, first_cart_pc);
        } else {
            printf("Never entered cartridge space -- execution stayed entirely in BIOS/RAM/VIA space\n");
        }
    }

    printf("Draw_VL ($F3DD) hit count: %ld\n", draw_vl_hits);
    if (first_draw_vl_step >= 0) {
        printf("First Draw_VL at step %d, X (vector list ptr)=$%04X\n", first_draw_vl_step, first_draw_vl_x);

        uint16_t addr = first_draw_vl_x;
        uint8_t count = mem_read8(addr);
        int total_bytes = (count + 1) * 2 + 1;
        printf("Vector list dump at $%04X: count-1=%d (%d vectors)\n", addr, count, count + 1);
        printf("  raw bytes:");
        for (int i = 0; i < total_bytes; i++) {
            printf(" %02X", mem_read8((uint16_t)(addr + i)));
        }
        printf("\n");
        printf("  as (dy,dx) pairs:");
        uint16_t p = (uint16_t)(addr + 1);
        for (int i = 0; i <= count; i++) {
            int8_t dy = (int8_t)mem_read8(p);
            int8_t dx = (int8_t)mem_read8((uint16_t)(p + 1));
            printf(" (%d,%d)", dy, dx);
            p = (uint16_t)(p + 2);
        }
        printf("\n");
    } else {
        printf("Draw_VL was never reached.\n");
    }

    printf("Last %d PCs visited (tail, chronological): ", RING);
    int n = (step >= RING) ? RING : step;
    int start = step - n;
    for (int i = 0; i < n; i++) {
        printf("$%04X ", ring[(start + i) % RING]);
    }
    printf("\n");

    return 0;
}
