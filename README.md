```                                                
▄▄▄    ▄▄▄       ▄▄▄▄  ▄▄▄▄  ▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄   
████▄  ███       ▀███  ███▀ ███▀▀▀▀▀ ███▀▀▀▀▀   
███▀██▄███ ██ ██  ███  ███  ███▄▄    ███        
███  ▀████ ██ ██  ███▄▄███  ███      ███        
███    ███ ▀██▀█   ▀████▀   ▀███████ ▀███████   
```                                     
                                                
A from-scratch Vectrex emulator — 6809 CPU core + vector display, written for the fun of getting close to the metal.

> **This project is unfinished and under active, early development.** Large parts of the system are missing or incomplete (see [Status](#status) below). It is not yet playable end-to-end. Expect bugs, missing opcodes, crashes, and breaking changes on every commit. Use it to poke around or follow along, not to actually play games yet.

## What is this?

NuVec aims to be a standalone Vectrex emulator: point it at a BIOS image and a ROM, and it boots into the game with mapped controls. Long-term, the plan is to also support building original Vectrex homebrew games on top of it.

## Status

**Working:**
- Memory map: RAM + BIOS ROM buffers, cartridge ROM loading from file
- `mem_read8` / `mem_write8` memory routing
- 6809 CPU: register struct, reset vector loading, fetch-decode-execute loop
- Load/store instruction family
- Arithmetic instructions with condition code flags
- Conditional and unconditional branches
- `JSR` / `RTS` with hardware stack
- Core indexed addressing modes (shared resolution helper)
- Per-instruction cycle counting
- Minimal SDL2 window rendering (currently a hardcoded shape, not real game output)
- CPU execution bridged to display via a RAM-based display list
- `Wait_Recal` wired to `$F192`, based on BIOS byte-pattern investigation

**Not yet implemented:**
- Full 6809 instruction set (opcode coverage still growing)
- Real vector display rendering driven by actual game ROM output
- AY-3-8912 sound emulation
- Controller/input handling
- BIOS/ROM file picker or drag-and-drop loading UI
- Packaging as a standalone executable

## Usage

There isn't a usable build yet. This section will be filled in once boot-to-game works end to end. For now, the code is here to read, run, and follow the CPU/display logic as it's built.

## Why Vectrex?

Picked over other obscure targets (like the Pokémon Mini) for being close-to-metal but with a smaller scope than a full OS or a more mainstream console emulator.

## Roadmap

- [ ] Finish 6809 opcode coverage
- [ ] Real display list → vector rendering (replace hardcoded shape)
- [ ] Controller input
- [ ] Sound (AY-3-8912)
- [ ] ROM/BIOS loading UI
- [ ] Standalone executable with splash screen
- [ ] Original homebrew game, once the emulator is solid

## License

MIT

## Contributing

Not really set up for contributions yet given how early this is, but issues and discussion are welcome!
