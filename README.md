A from-scratch Vectrex emulator written in C, featuring a Motorola 6809 CPU core, 6522 VIA emulation, a cycle-driven analogue vector display model, AY-3-8912 sound chip emulation, and keyboard controller input.

![Space Wars gameplay](screenshots/nuvecboot040826.gif)

## Overview

NuVec emulates the core hardware of the Vectrex console rather than translating game drawing routines into conventional graphics calls.

The vector display is generated from the emulated VIA, DAC, sample-and-hold circuits, multiplexer and analogue integrators. Sound is produced by a from-scratch AY-3-8912 implementation driven directly from the emulated VIA register writes, with logarithmic volume scaling and polyBLEP antialiasing. This allows graphics, text and audio to emerge from the same signal path used by the original hardware.

The project currently runs:

* **Space Wars (1982)** — tested from BIOS startup through live gameplay with sound

## Current status

### Working

* Cartridge ROM loading from file
* Vectrex memory map, including RAM, BIOS ROM and cartridge ROM
* Motorola 6809 fetch-decode-execute core
* Documented addressing modes, including all indexed-addressing submodes
* Arithmetic, branches, stack operations and condition-code handling
* Subroutine flow through `JSR` and `RTS`
* Interrupt handling, including `CWAI` and IRQ delivery
* Register-level 6522 VIA emulation
* Per-cycle DAC, multiplexer, sample-and-hold, ramp and blanking behaviour
* Analogue integrator model for vector and text rendering
* AY-3-8912 sound chip emulation — tone channels, noise channel, envelope generator, logarithmic volume table, polyBLEP antialiasing
* Keyboard controller input with configurable mappings
* Phosphor decay rendering with ring buffer history (toggleable)
* Bloom/glow effect (toggleable)
* Resizable window with correct 3:4 portrait aspect ratio and letterboxing
* Vsync with native monitor refresh rate detection (toggleable)
* FPS counter and OSD toggle notifications
* CPU and display timing approximating the original 1.5 MHz / 50 Hz hardware
* SDL2 rendering and audio output

### Not yet implemented

* BIOS and ROM selection interface
* Drag-and-drop ROM loading
* Packaged standalone releases
* Automated CPU and hardware tests

## Building

NuVec builds with:

* CMake
* MinGW-w64 (via a portable toolchain or MSYS2's ucrt64 environment)
* SDL2

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

The generator depends on your toolchain — `Ninja` above assumes MSYS2's ucrt64 (`ninja.exe` included). If you're using a plain MinGW-w64 install without Ninja, use `-G "MinGW Makefiles"` instead.

## Usage

Run NuVec with a cartridge ROM path:

```bash
NuVec.exe path\to\game.vec
```

The BIOS path is currently hardcoded in `main.c` (set the `BIOS_PATH` constant to point at your own Vectrex BIOS dump before building). A proper BIOS and ROM loading interface is planned.

## Controls

| Action          | Key         |
|-----------------|-------------|
| Button 1        | Z           |
| Button 2        | X           |
| Button 3        | C           |
| Button 4        | V           |
| Joystick Up     | Up arrow    |
| Joystick Down   | Down arrow  |
| Joystick Left   | Left arrow  |
| Joystick Right  | Right arrow |

| Toggle          | Key |
|-----------------|-----|
| Phosphor decay  | F1  |
| Bloom           | F2  |
| Vsync           | F3  |

## Architecture

```text
Cartridge / BIOS
       │
       ▼
 Motorola 6809
       │
       ▼
   6522 VIA
      │ │
      │ └──────────────────┐
      ▼                    ▼
DAC / MUX /          AY-3-8912
sample-and-hold      sound chip
      │                    │
      ▼                    ▼
Analogue integrator  SDL2 audio
model                output
      │
      ▼
SDL2 vector renderer
```

Unlike a high-level-emulation approach, NuVec does not replace BIOS drawing routines with hardcoded lines or text. The displayed vectors and audio are produced from the state of the emulated hardware.

## Roadmap

* [x] Vectrex memory map
* [x] Motorola 6809 execution core
* [x] 6522 VIA emulation
* [x] Analogue vector display model
* [x] BIOS startup and text rendering
* [x] Commercial cartridges running through gameplay
* [x] AY-3-8912 sound emulation
* [x] Keyboard controller input
* [x] Phosphor decay and bloom rendering
* [x] Resizable window and vsync
* [ ] Automated CPU and hardware tests
* [ ] BIOS and ROM loading interface
* [ ] Standalone release builds
* [ ] Original homebrew game and development tooling
* [ ] Browser port via Emscripten

## Project goals

NuVec is primarily an exploration of emulator development, embedded-style hardware behaviour and low-level systems programming.

Longer term, the project may also become a platform for developing and testing original Vectrex homebrew software.

## License

Licensed under the MIT License.

Vectrex, game ROMs and BIOS images are not included in this repository.

## Contributing

The project is still changing rapidly, so it is not yet organised around external contributions. Bug reports, technical discussion and issue submissions are welcome.

## References

* MOS Technology 6522 Versatile Interface Adapter datasheet
* General Instrument AY-3-8910/8912 Programmable Sound Generator datasheet
* Vectrex Programmer's Manual
* Vectrex hardware and vector-display documentation at playvectrex.com