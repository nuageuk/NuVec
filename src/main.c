#include <stdio.h>
#include "memory.h"
#include "cpu.h"
#include "display.h"
#include <SDL.h>

static void run_cpu_tests(Cpu *cpu) {
    mem_write8(RAM_START, 0x86);
    mem_write8(RAM_START + 1, 0x05);
    cpu->PC = RAM_START;
    cpu_step(cpu);
    cpu_print_state(cpu);

    // STA direct / LDA direct round-trip (DP set so the direct page lands in RAM)
    cpu->DP = 0xC0;
    mem_write8(RAM_START + 2, 0x97); // STA direct $10 -> effective addr 0xC010
    mem_write8(RAM_START + 3, 0x10);
    mem_write8(RAM_START + 4, 0x86); // LDA immediate #$00 (clear A before reload)
    mem_write8(RAM_START + 5, 0x00);
    mem_write8(RAM_START + 6, 0x96); // LDA direct $10 -> should reload 0x05
    mem_write8(RAM_START + 7, 0x10);

    cpu->PC = RAM_START + 2;
    cpu_step(cpu);
    cpu_step(cpu);
    cpu_step(cpu);
    cpu_print_state(cpu);

    mem_write8(0xC700, 0x12);
    mem_write8(0xC701, 0x34);
    mem_write8(RAM_START + 8,  0xBE); // LDX extended $C700
    mem_write8(RAM_START + 9,  0xC7);
    mem_write8(RAM_START + 10, 0x00);

    cpu->PC = RAM_START + 8;
    cpu_step(cpu);
    cpu_print_state(cpu);

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

    cpu->PC = RAM_START + 11;

    int steps = 0;
    const int max_steps = 100; // safety cap: a wrong-direction/infinite branch can't hang the test
    while (cpu->PC != RAM_START + 19 && steps < max_steps) {
        if (!cpu_step(cpu)) break;
        steps++;
    }

    if (steps == 11 && cpu->A == 0x03 && cpu->B == 0x00 && cpu->PC == RAM_START + 19) {
        printf("loop test PASSED (%d cpu_step() calls, A=%02X iterations, B=%02X)\n", steps, cpu->A, cpu->B);
    } else {
        printf("loop test FAILED (%d cpu_step() calls, A=%02X, B=%02X, PC=%04X)\n", steps, cpu->A, cpu->B, cpu->PC);
    }
    cpu_print_state(cpu);

    // JSR/RTS: call a subroutine that increments A, then return.
    // S isn't set by cpu_reset() (real hardware relies on BIOS for that), so
    // the test has to give it a sensible RAM address itself.
    cpu->S = 0xC780;
    uint16_t initial_s = cpu->S;

    mem_write8(RAM_START + 19, 0x86); // LDA #$00           (reset A before the call)
    mem_write8(RAM_START + 20, 0x00);
    mem_write8(RAM_START + 21, 0xBD); // JSR extended $C028 -> return addr is RAM_START+24
    mem_write8(RAM_START + 22, 0xC0);
    mem_write8(RAM_START + 23, 0x28);
    // RAM_START + 24: instruction resumed here after RTS (not executed by this test)

    mem_write8(RAM_START + 40, 0x4C); // subroutine @ $C028: INCA
    mem_write8(RAM_START + 41, 0x39); //                     RTS

    cpu->PC = RAM_START + 19;
    cpu_step(cpu); // LDA #$00
    cpu_step(cpu); // JSR extended
    cpu_step(cpu); // INCA (inside subroutine)
    cpu_step(cpu); // RTS

    if (cpu->PC == RAM_START + 24 && cpu->A == 0x01 && cpu->S == initial_s) {
        printf("JSR/RTS test PASSED (PC=%04X resumed correctly, A=%02X ran once, S=%04X balanced)\n",
               cpu->PC, cpu->A, cpu->S);
    } else {
        printf("JSR/RTS test FAILED (PC=%04X, A=%02X, S=%04X, expected PC=%04X, A=01, S=%04X)\n",
               cpu->PC, cpu->A, cpu->S, RAM_START + 24, initial_s);
    }
    cpu_print_state(cpu);

    // Indexed addressing (,X): zero-offset load, zero-offset store, ,X+
    // post-increment (checking X actually advances), and an 8-bit offset
    // load. Threaded as one sequential program so each step's expectations
    // depend on the previous step's side effects actually having happened.
    {
        mem_write8(0xC200, 0xAB); // fixture for the zero-offset load
        mem_write8(0xC201, 0x33); // fixture proving X really moved after ,X+
        mem_write8(0xC206, 0x99); // fixture for the 8-bit-offset load (X=0xC201 + 5)

        cpu->X = 0xC200;
        const uint16_t prog = RAM_START + 0x300; // 0xC300, clear of every earlier phase

        mem_write8(prog + 0,  0xA6); mem_write8(prog + 1,  0x84);                              // LDA ,X
        mem_write8(prog + 2,  0x86); mem_write8(prog + 3,  0x7F);                              // LDA #$7F
        mem_write8(prog + 4,  0xA7); mem_write8(prog + 5,  0x84);                              // STA ,X
        mem_write8(prog + 6,  0xA6); mem_write8(prog + 7,  0x80);                              // LDA ,X+
        mem_write8(prog + 8,  0xA6); mem_write8(prog + 9,  0x84);                              // LDA ,X
        mem_write8(prog + 10, 0xA6); mem_write8(prog + 11, 0x88); mem_write8(prog + 12, 0x05); // LDA 5,X

        cpu->PC = prog;
        cpu_step(cpu); // LDA ,X   -> A should be the 0xC200 fixture
        uint8_t zero_offset_load = cpu->A;

        cpu_step(cpu); // LDA #$7F
        cpu_step(cpu); // STA ,X   -> writes 0x7F to 0xC200 (X still 0xC200)
        uint8_t stored_back = mem_read8(0xC200);

        cpu_step(cpu); // LDA ,X+  -> reads 0xC200 (the just-stored 0x7F), then X becomes 0xC201
        uint8_t postinc_load = cpu->A;
        uint16_t x_after_postinc = cpu->X;

        cpu_step(cpu); // LDA ,X   -> reads 0xC201's distinct fixture, proving X really advanced
        uint8_t advance_check = cpu->A;

        cpu_step(cpu); // LDA 5,X  -> reads 0xC201+5 = 0xC206
        uint8_t offset_load = cpu->A;

        int ok = zero_offset_load == 0xAB
               && stored_back == 0x7F
               && postinc_load == 0x7F
               && x_after_postinc == 0xC201
               && advance_check == 0x33
               && offset_load == 0x99
               && cpu->X == 0xC201; // 8-bit offset must not itself modify X

        if (ok) {
            printf("indexed addressing test PASSED (,X=%02X, store round-trip=%02X, ,X+=%02X then X=%04X, ,X after=%02X, 5,X=%02X)\n",
                   zero_offset_load, stored_back, postinc_load, x_after_postinc, advance_check, offset_load);
        } else {
            printf("indexed addressing test FAILED (,X=%02X, store=%02X, ,X+=%02X X=%04X, ,X=%02X, 5,X=%02X, final X=%04X)\n",
                   zero_offset_load, stored_back, postinc_load, x_after_postinc, advance_check, offset_load, cpu->X);
        }
        cpu_print_state(cpu);
    }
}

int main(int argc, char *argv[]) {
    printf("NuVec starting\n");
    printf("BIOS ROM starts at 0x%04X\n", BIOS_ROM_START);

    if (!mem_load_rom("roms/Vectrex BIOS (1982).vec", bios_rom, sizeof(bios_rom))) {
        fprintf(stderr, "Failed to load BIOS ROM\n");
        return 1;
    }

    if (argc > 1) {
        if (!mem_load_rom(argv[1], cart_rom, sizeof(cart_rom))) {
            fprintf(stderr, "Failed to load cartridge ROM from '%s'\n", argv[1]);
            return 1;
        }
        printf("Cartridge ROM loaded from '%s'\n", argv[1]);
    } else {
        printf("No cartridge ROM provided; booting BIOS only\n");
    }

    mem_write8(RAM_START, 0x42);
    uint8_t value = mem_read8(RAM_START);
    printf("Wrote 0x42 to RAM_START, read back 0x%02X\n", value);

    Cpu cpu;
    cpu_reset(&cpu);
    cpu_print_state(&cpu);

    run_cpu_tests(&cpu);

    if (!display_init()) {
        fprintf(stderr, "Failed to initialize display\n");
        return 1;
    }

    // HLE Draw_VL: JSR $F3DD with X pointing at a small vector list, then
    // verify PC resumes exactly where a real RTS would land and S stays
    // balanced -- same verification shape as the earlier hand-rolled JSR/RTS
    // test, just landing on an HLE'd BIOS routine instead of 6809 code. This
    // is a one-time sanity check of the HLE mechanism itself, done here with
    // a synthetic forced JSR; the render loop below no longer drives Draw_VL
    // this way -- it free-runs real BIOS code instead, which triggers the
    // same HLE interception on its own whenever it executes a genuine
    // JSR $F3DD. The vector list this test writes to vlist_addr is left in
    // RAM afterward but nothing forces the CPU to visit it anymore.
    const uint16_t vlist_addr = 0xC400;
    const uint16_t caller_addr = RAM_START + 0x320; // 0xC320
    const uint16_t after_jsr = caller_addr + 3;
    {
        // count = N-1 (3 vectors), each pair is (dy, dx) as signed bytes,
        // deltas from a pen that starts at (0,0):
        // (0,0) -> (100,0) -> (50,90) -> (0,0), kept on-screen near the
        // origin since Draw_VL's pen isn't centered/scaled yet.
        mem_write8(vlist_addr + 0, 0x02); // count - 1 = 2
        mem_write8(vlist_addr + 1, 0x00); // dy=0
        mem_write8(vlist_addr + 2, 0x64); // dx=100
        mem_write8(vlist_addr + 3, 0x5A); // dy=90
        mem_write8(vlist_addr + 4, 0xCE); // dx=-50
        mem_write8(vlist_addr + 5, 0xA6); // dy=-90
        mem_write8(vlist_addr + 6, 0xCE); // dx=-50

        mem_write8(caller_addr + 0, 0xBD); // JSR extended
        mem_write8(caller_addr + 1, 0xF3);
        mem_write8(caller_addr + 2, 0xDD); // -> $F3DD (HLE_DRAW_VL)

        cpu.X = vlist_addr;
        cpu.PC = caller_addr;
        uint16_t s_before = cpu.S;

        cpu_step(&cpu); // JSR extended -> pushes return addr, PC = $F3DD
        cpu_step(&cpu); // HLE intercept: draws the shape, pops PC back (RTS-equivalent)

        if (cpu.PC == after_jsr && cpu.S == s_before) {
            printf("HLE Draw_VL test PASSED (PC=%04X resumed correctly after JSR $F3DD, S=%04X balanced)\n",
                   cpu.PC, cpu.S);
        } else {
            printf("HLE Draw_VL test FAILED (PC=%04X, S=%04X, expected PC=%04X, S=%04X)\n",
                   cpu.PC, cpu.S, after_jsr, s_before);
        }
        cpu_print_state(&cpu);
    }

    // Fresh reset before the render loop: run_cpu_tests() and the one-time
    // HLE sanity check above both reused this Cpu instance as scratch space
    // for unrelated register-level tests, so PC/registers are left in
    // whatever state those tests wanted, not the real reset vector. The
    // render loop below free-runs actual BIOS code, so it needs to start
    // exactly where real hardware would.
    cpu_reset(&cpu);

    // Placeholder instruction budget, not cycle-paced yet (real hardware is
    // ~1.5MHz over a ~50Hz frame, i.e. closer to 30000 *cycles*, not
    // instructions -- this just needs to be "many" for now per-frame).
    const int steps_per_frame = 10000;
    int halted = 0;

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        display_clear();

        // Free-run the real BIOS from wherever it currently is. Draw_VL (and
        // any other HLE'd routine) fires via cpu_try_hle()'s PC-match inside
        // cpu_step() whenever BIOS code itself executes a genuine JSR to it
        // -- nothing here forces that call anymore.
        if (!halted) {
            for (int i = 0; i < steps_per_frame; i++) {
                if (!cpu_step(&cpu)) {
                    printf("CPU halted: unimplemented opcode at PC=0x%04X\n", cpu.PC);
                    halted = 1;
                    break;
                }
            }
        }

        display_present();
    }

    display_shutdown();

    return 0;
}
