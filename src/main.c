#include <stdio.h>
#include "memory.h"
#include "cpu.h"
#include "display.h"
#include "analog.h"
#include "input.h"
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

    input_reset();

    // Fresh reset before the render loop: run_cpu_tests() above reused this
    // Cpu instance as scratch space for unrelated register-level tests, so
    // PC/registers are left in whatever state those tests wanted, not the
    // real reset vector. The render loop below free-runs actual BIOS code,
    // so it needs to start exactly where real hardware would.
    cpu_reset(&cpu);

    // Simulation follows wall-clock time at the 1.5 MHz CPU rate while
    // rendering is paced independently by vsync (or a 50 Hz fallback).
    const double CPU_CYCLES_PER_SEC = 1500000.0;
    const uint64_t CYCLE_BATCH_SIZE = 100;
    const double FALLBACK_FRAME_SEC = 1.0 / 50.0;
    const double MAX_CATCHUP_CYCLES = CPU_CYCLES_PER_SEC * FALLBACK_FRAME_SEC * 4.0;

    const uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t last_time = SDL_GetPerformanceCounter();
    double cycle_debt = 0.0;
    DisplayVsyncMode vsync_mode = VSYNC_ON;

    int halted = 0;

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_WINDOWEVENT &&
                       event.window.event == SDL_WINDOWEVENT_RESIZED) {
                display_resize(event.window.data1, event.window.data2);
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                    event.key.keysym.sym == SDLK_F1) {
                    display_toggle_decay();
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                           event.key.keysym.sym == SDLK_F2) {
                    display_toggle_bloom();
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                           event.key.keysym.sym == SDLK_F3) {
                    vsync_mode = display_toggle_vsync();
                } else {
                    input_handle_key(event.key.keysym.sym, event.type == SDL_KEYDOWN);
                }
            }
        }

        uint64_t iter_start = SDL_GetPerformanceCounter();
        double elapsed_sec = (double)(iter_start - last_time) / (double)perf_freq;
        last_time = iter_start;
        cycle_debt += elapsed_sec * CPU_CYCLES_PER_SEC;
        if (cycle_debt > MAX_CATCHUP_CYCLES) {
            cycle_debt = MAX_CATCHUP_CYCLES;
        }

        // Pay the cycle debt in small batches. Instruction-boundary
        // overshoot remains as negative debt and is absorbed next time.
        while (!halted && cycle_debt >= 1.0) {
            uint64_t batch_cycles = (uint64_t)cycle_debt;
            if (batch_cycles > CYCLE_BATCH_SIZE) {
                batch_cycles = CYCLE_BATCH_SIZE;
            }

            uint64_t batch_start = cpu.cycles;
            uint64_t batch_target = batch_start + batch_cycles;
            while (cpu.cycles < batch_target) {
                if (!cpu_step(&cpu)) {
                    printf("CPU halted: unimplemented opcode at PC=0x%04X\n", cpu.PC);
                    halted = 1;
                    break;
                }
            }
            cycle_debt -= (double)(cpu.cycles - batch_start);
        }

        // Render the current-frame vector list plus the completed previous
        // checkpoint (analog_render(), see analog.c) every host frame,
        // regardless of whether analog.c's own ~1/30s-of-cycles frame timer
        // has swapped since the last SDL frame -- this is what keeps the
        // screen showing the last real frame instead of going black
        // whenever the CPU loop hasn't produced fresh vectors since then.
        display_clear();
        analog_render();
        display_present();

        uint64_t iter_end = SDL_GetPerformanceCounter();
        double iter_work_sec = (double)(iter_end - iter_start) / (double)perf_freq;
        if (vsync_mode == VSYNC_OFF) {
            double remaining_sec = FALLBACK_FRAME_SEC - iter_work_sec;
            Uint32 delay_ms = remaining_sec > 0.0
                            ? (Uint32)(remaining_sec * 1000.0 + 0.999)
                            : 1;
            SDL_Delay(delay_ms);
        }
    }

    display_shutdown();

    return 0;
}
