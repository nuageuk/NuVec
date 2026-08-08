/* NuVec application entry point, event loop, and host-time scheduling. */

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "analog.h"
#include "ay.h"
#include "cpu.h"
#include "display.h"
#include "input.h"
#include "memory.h"

int main(int argc, char *argv[]) {
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

    Cpu cpu;
    cpu_reset(&cpu);

    if (!display_init()) {
        fprintf(stderr, "Failed to initialize display\n");
        return 1;
    }
    if (!ay_init()) {
        fprintf(stderr, "Failed to initialize AY audio: %s\n", SDL_GetError());
        display_shutdown();
        return 1;
    }

    int display_hz = 144;
    SDL_DisplayMode display_mode;
    if (SDL_GetCurrentDisplayMode(0, &display_mode) == 0 &&
        display_mode.refresh_rate > 0) {
        display_hz = display_mode.refresh_rate;
    }

    input_reset();

    /*
     * Simulation follows wall-clock time at the 1.5 MHz CPU rate while
     * rendering is paced independently by vsync or a 50 Hz fallback.
     */
    const double cpu_cycles_per_second = 1500000.0;
    const uint64_t cycle_batch_size = 100;
    const double fallback_frame_seconds = 1.0 / 50.0;
    const double vsync_frame_seconds = 1.0 / (double)display_hz;
    const double spin_threshold_seconds = 0.002;

    const uint64_t performance_frequency = SDL_GetPerformanceFrequency();
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
                    last_time = SDL_GetPerformanceCounter();
                    cycle_debt = 0.0;
                } else {
                    input_handle_key(event.key.keysym.sym,
                                     event.type == SDL_KEYDOWN);
                }
            }
        }

        uint64_t iteration_start = SDL_GetPerformanceCounter();
        double elapsed_seconds =
            (double)(iteration_start - last_time) /
            (double)performance_frequency;
        last_time = iteration_start;
        if (elapsed_seconds > fallback_frame_seconds) {
            elapsed_seconds = fallback_frame_seconds;
        }
        cycle_debt += elapsed_seconds * cpu_cycles_per_second;

        /*
         * Pay cycle debt in small batches. Instruction-boundary overshoot
         * remains as negative debt and is absorbed on the next iteration.
         */
        while (!halted && cycle_debt >= 1.0) {
            uint64_t batch_cycles = (uint64_t)cycle_debt;
            if (batch_cycles > cycle_batch_size) {
                batch_cycles = cycle_batch_size;
            }

            uint64_t batch_start = cpu.cycles;
            uint64_t batch_target = batch_start + batch_cycles;
            while (cpu.cycles < batch_target) {
                if (!cpu_step(&cpu)) {
                    printf("CPU halted: unimplemented opcode at PC=0x%04X\n",
                           cpu.PC);
                    halted = 1;
                    break;
                }
            }
            uint64_t executed_cycles = cpu.cycles - batch_start;
            ay_update(executed_cycles);
            cycle_debt -= (double)executed_cycles;
        }

        /*
         * Re-render the current and last completed analog checkpoints even
         * when the simulation has not produced a new checkpoint. This keeps
         * the last real vector frame visible at higher host refresh rates.
         */
        display_clear();
        analog_render();
        display_present();

        uint64_t iteration_end = SDL_GetPerformanceCounter();
        double work_seconds =
            (double)(iteration_end - iteration_start) /
            (double)performance_frequency;
        if (vsync_mode == VSYNC_ON) {
            double remaining_seconds = vsync_frame_seconds - work_seconds;
            if (remaining_seconds > spin_threshold_seconds) {
                Uint32 delay_ms = (Uint32)(remaining_seconds * 1000.0);
                if (delay_ms > 1) {
                    SDL_Delay(delay_ms - 1);
                }
            }

            uint64_t frame_target = iteration_start +
                (uint64_t)(vsync_frame_seconds * (double)performance_frequency);
            while (SDL_GetPerformanceCounter() < frame_target) {
            }
        } else {
            double remaining_seconds = fallback_frame_seconds - work_seconds;
            Uint32 delay_ms = remaining_seconds > 0.0
                                ? (Uint32)(remaining_seconds * 1000.0 + 0.999)
                                : 1;
            SDL_Delay(delay_ms);
        }
    }

    ay_shutdown();
    display_shutdown();

    return 0;
}
