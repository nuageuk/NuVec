```                                                
▄▄▄    ▄▄▄       ▄▄▄▄  ▄▄▄▄  ▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄   
████▄  ███       ▀███  ███▀ ███▀▀▀▀▀ ███▀▀▀▀▀   
███▀██▄███ ██ ██  ███  ███  ███▄▄    ███        
███  ▀████ ██ ██  ███▄▄███  ███      ███        
███    ███ ▀██▀█   ▀████▀   ▀███████ ▀███████   
```                                     
                                                
A from-scratch Vectrex emulator — 6809 CPU core + real vector display via a cycle-accurate VIA/analog integrator model, written for the fun of getting close to the metal.

> **This project is under active, early development.** Core emulation — CPU, VIA, and vector/text rendering — is working and has run real commercial cartridges to completion. Missing pieces are sound, controls, and a proper loading UI (see [Status](#status)). Expect bugs and breaking changes on every commit.

## What is this?

NuVec is a standalone Vectrex emulator: point it at a BIOS image and a ROM, and it boots and renders the real game. Long-term, the plan is to also support building original Vectrex homebrew games on top of it.

## Status

**Working:**
- Full memory map: RAM + BIOS ROM, cartridge ROM loading from file
- 6809 CPU: complete opcode coverage as exercised by real BIOS + cartridge execution (fetch-decode-execute, all addressing modes including per-submode indexed-addressing cycle timing, arithmetic with condition codes, branches, `JSR`/`RTS`, interrupt delivery via `CWAI`/IRQ)
- Real 6522 VIA emulation
- Real per-cycle analog beam/integrator model (DAC, sample-and-holds, mux, ramp/blank signals) — not HLE; this is what makes both vector graphics and text render correctly, timed the same way real hardware does it
- Frame timing throttled to match real Vectrex speed (~1.5MHz CPU, ~50Hz refresh)
- Confirmed working end-to-end: the real Vectrex BIOS boot splash renders correctly ("VECTREX / GCE / ENTERTAINING NEW IDEAS", cleanly aligned text)
- Two commercial cartridges tested and running to completion: **Space Wars (1982)** and **Star Hawk (1982)** — real gameplay visuals, moving ships, starfields, 3D-perspective wireframe scenes

**Not yet implemented:**
- AY-3-8912 sound emulation
- Controller/input handling
- BIOS/ROM file picker or drag-and-drop loading UI
- Packaging as a standalone executable

## Usage

Build with CMake + MinGW-w64 + SDL2, then run with a cartridge path as an argument:

\`\`\`
NuVec.exe path\to\game.vec
\`\`\`

No file picker yet — command-line argument only for now.

## Why Vectrex?

Picked over other obscure targets (like the Pokémon Mini) for being close-to-metal but with a smaller scope than a full OS or a more mainstream console emulator.

## Roadmap

- [x] 6809 opcode coverage
- [x] Real analog integrator → vector rendering (replaced the old hardcoded shape)
- [x] Cycle-accurate timing (fixes both speed and text rendering)
- [ ] Controller input
- [ ] Sound (AY-3-8912)
- [ ] ROM/BIOS loading UI
- [ ] Standalone executable with splash screen
- [ ] Original homebrew game, once the emulator is solid

## License

MIT

## Contributing

Not really set up for contributions yet given how early this is, but issues and discussion are welcome!