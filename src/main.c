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

    // Display list: have the CPU itself assemble the triangle into RAM at
    // DISPLAY_LIST_START via a real LDA/STA subroutine called through JSR/RTS
    // -- not poked in directly from C.
    //
    // Hand-typing the ~125 program bytes this takes (each of the 25 display
    // list bytes needs its own LDA #imm + STA extended pair) would be
    // tedious and error-prone, so this small loop assembles them the same
    // way the earlier phases did by hand, just generated instead of
    // transcribed.
    {
        typedef struct { int16_t x1, y1, x2, y2; } Segment;
        Segment triangle[] = {
            { 300, 120, 200, 300 },
            { 200, 300, 400, 300 },
            { 400, 300, 300, 120 },
        };
        const uint8_t segment_count = (uint8_t)(sizeof(triangle) / sizeof(triangle[0]));

        const uint16_t call_addr = RAM_START + 0x30; // JSR site
        const uint16_t sub_addr  = RAM_START + 0x50; // subroutine body

        // JSR extended -> sub_addr
        mem_write8(call_addr, 0xBD);
        mem_write8(call_addr + 1, (uint8_t)(sub_addr >> 8));
        mem_write8(call_addr + 2, (uint8_t)(sub_addr & 0xFF));
        const uint16_t after_call = call_addr + 3;

        // Subroutine: for every byte of the display list, LDA #value; STA extended dest.
        uint16_t wptr = sub_addr;
        uint16_t dest = DISPLAY_LIST_START;

        mem_write8(wptr++, 0x86); mem_write8(wptr++, segment_count);            // LDA #N
        mem_write8(wptr++, 0xB7);                                               // STA extended
        mem_write8(wptr++, (uint8_t)(dest >> 8));
        mem_write8(wptr++, (uint8_t)(dest & 0xFF));
        dest++;

        for (int i = 0; i < segment_count; i++) {
            int16_t coords[4] = { triangle[i].x1, triangle[i].y1, triangle[i].x2, triangle[i].y2 };
            for (int c = 0; c < 4; c++) {
                uint16_t v = (uint16_t)coords[c];
                uint8_t bytes[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };

                for (int b = 0; b < 2; b++) {
                    mem_write8(wptr++, 0x86); mem_write8(wptr++, bytes[b]);     // LDA #byte
                    mem_write8(wptr++, 0xB7);                                   // STA extended
                    mem_write8(wptr++, (uint8_t)(dest >> 8));
                    mem_write8(wptr++, (uint8_t)(dest & 0xFF));
                    dest++;
                }
            }
        }
        mem_write8(wptr++, 0x39); // RTS
        const uint16_t sub_end = wptr;

        cpu->PC = call_addr;
        int dl_steps = 0;
        const int dl_max_steps = 500; // 1 JSR + (1 LDA + 1 STA) per byte + 1 RTS, comfortably under this
        while (cpu->PC != after_call && dl_steps < dl_max_steps) {
            if (!cpu_step(cpu)) break;
            dl_steps++;
        }

        int ok = (cpu->PC == after_call) && (mem_read8(DISPLAY_LIST_START) == segment_count);
        for (uint16_t addr = DISPLAY_LIST_START + 1, i = 0; ok && i < segment_count; i++) {
            int16_t expect[4] = { triangle[i].x1, triangle[i].y1, triangle[i].x2, triangle[i].y2 };
            for (int c = 0; c < 4 && ok; c++) {
                int16_t got = (int16_t)(((uint16_t)mem_read8(addr) << 8) | mem_read8(addr + 1));
                if (got != expect[c]) ok = 0;
                addr += 2;
            }
        }

        if (ok) {
            printf("display list build PASSED (%d cpu_step() calls, %u bytes written at $%04X-$%04X by code at $%04X-$%04X)\n",
                   dl_steps, (unsigned)(1 + segment_count * 8), DISPLAY_LIST_START,
                   DISPLAY_LIST_START + segment_count * 8, sub_addr, sub_end - 1);
        } else {
            printf("display list build FAILED (%d cpu_step() calls, PC=%04X)\n", dl_steps, cpu->PC);
        }
        cpu_print_state(cpu);
    }

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
    (void)argc;
    (void)argv;

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

    run_cpu_tests(&cpu);

    if (!display_init()) {
        fprintf(stderr, "Failed to initialize display\n");
        return 1;
    }

    // HLE Draw_VL: JSR $F3DD with X pointing at a small vector list, then
    // verify PC resumes exactly where a real RTS would land and S stays
    // balanced -- same verification shape as the earlier hand-rolled JSR/RTS
    // test, just landing on an HLE'd BIOS routine instead of 6809 code.
    // vlist_addr/caller_addr stay in scope for the render loop below, which
    // re-invokes this JSR every frame the way a real Vectrex game's own loop
    // would call Draw_VL once per frame.
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

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        display_clear();
        display_render_from_memory(DISPLAY_LIST_START);

        // Redraw the HLE'd shape every frame too, the way a real Vectrex
        // game calls Draw_VL once per frame from its own game loop.
        cpu.X = vlist_addr;
        cpu.PC = caller_addr;
        cpu_step(&cpu); // JSR extended -> $F3DD
        cpu_step(&cpu); // HLE Draw_VL + simulated RTS

        display_present();
    }

    display_shutdown();

    return 0;
}
