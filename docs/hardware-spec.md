# Hardware behavior spec: 6522 VIA + Vectrex analog vector generator

This document specifies the observable behavior `via.c` and `analog.c` must
implement, derived from the 6522 VIA's public register-level datasheet and
from public Vectrex hardware/analog-board documentation (playvectrex.com and
the Vectrex hardware reference material it's built from). It intentionally
describes *what the hardware does*, not any particular emulator's internal
data structures — the implementation is free to represent state however is
clearest, as long as it produces this behavior.

## 1. The 6522 VIA register file

16 registers, address bits A3-A0 select which one (A15-A4 are wired so the
whole VIA_START-VIA_END range mirrors the same 16 registers repeatedly).

| # | Name | Read | Write |
|---|------|------|-------|
| 0x0 | ORB/IRB | Output bits read back the ORB latch; bits DDRB marks as inputs read the pin state (no input source modeled here, so they read idle) | Latches the byte; triggers CB2 handshake pulse if PCR selects it |
| 0x1 | ORA/IRA (handshake) | Same latch/pin split as ORB, using DDRA; also triggers the CA2 handshake/pulse side effect if PCR selects it | Same, plus CA2 handshake/pulse side effect |
| 0x2 | DDRB | Direction register (1=output) | — |
| 0x3 | DDRA | Direction register (1=output) | — |
| 0x4 | T1C-L | Returns the counter's low byte; **clears the T1 interrupt flag and stops T1 from generating further interrupts until re-armed** | Writes only the low-order **latch** (not the counter) |
| 0x5 | T1C-H | Returns the counter's high byte, no side effect | Writes the high-order latch, **loads both latch bytes into the live counter, clears the T1 interrupt flag, arms T1, and forces PB7's timer-controlled view low** — this is the actual trigger that starts a timed run |
| 0x6 | T1L-L | Returns the low-order latch | Writes the low-order latch only (does not touch the counter or PB7) |
| 0x7 | T1L-H | Returns the high-order latch | Writes the high-order latch only (does not touch the counter or PB7) |
| 0x8 | T2C-L | Returns the counter's low byte; **clears the T2 interrupt flag** | Writes the low-order **latch** only |
| 0x9 | T2C-H | Returns the counter's high byte, no side effect | Combines the write byte (high) with the existing low-order latch, **loads the live counter, clears the T2 interrupt flag, and arms T2** |
| 0xA | SR | Returns the shift register byte; **resets the bit counter and shift-clock phase** | Writes the shift register byte; **resets the bit counter and shift-clock phase**, arming a new transfer |
| 0xB | ACR | Auxiliary Control Register | as read |
| 0xC | PCR | Peripheral Control Register | as read |
| 0xD | IFR | Bits 0-6 are individually-settable flags; bit 7 is a **computed** "any enabled flag set" indicator, not independently stored | Write **1** to a bit to **clear** that flag (bits 0-6 only; bit 7 is never written directly) |
| 0xE | IER | Bit 7 always reads as 1 (documented quirk); bits 0-6 are the enable mask | Bit 7 of the written byte selects the operation: 1 = **OR** the low 7 bits into the enable mask (enable), 0 = **AND them out** (disable) |
| 0xF | ORA/IRA (no handshake) | Same underlying latch as 0x1 | Same underlying latch as 0x1, **without** the CA2 handshake/pulse side effect |

Interrupt flags this emulation cares about (the rest correspond to CA1/CB1
edge inputs that aren't modeled since no controller/peripheral edge source
exists here): **T1** (bit 6), **T2** (bit 5), **SR** (bit 2).

## 2. Auxiliary Control Register (ACR)

| Bits | Meaning |
|---|---|
| 7 | PB7 control: 1 = Timer 1's internal toggle/one-shot output is presented on PB7; 0 = PB7 behaves as an ordinary ORB/DDRB-controlled bit |
| 6 | Timer 1 mode: 1 = **free-running** — on every underflow, reload from the latch, flag an interrupt, and (if bit 7 is set) toggle PB7's level, indefinitely; 0 = **one-shot** — on the *next* underflow after being armed, flag one interrupt and (if bit 7 is set) drop PB7 low once, then stop generating further interrupts until re-armed by a new T1C-H write |
| 5 | Timer 2 mode: 0 = **timed one-shot**, counts down once per system cycle and flags one interrupt on underflow; 1 = **pulse counting**, counts external pulses on PB6 instead of free-running off the system clock (no PB6 source is modeled, so in this mode T2 simply never advances) |
| 4-2 | Shift register mode, 8 combinations (see §3) |
| 1-0 | Port A/B input-latching enables — not relevant to beam control, no observable effect needed here beyond storing the bits |

## 3. Shift register

Selected by ACR bits 4-2 as a 3-bit mode:

| Mode | Direction | Clock source | Counts to 8 / sets SR interrupt flag? |
|---|---|---|---|
| 000 | disabled | — | never |
| 001 | in | Timer 2 | yes |
| 010 | in | system clock (every cycle) | yes |
| 011 | in | external CB1 edge (unmodeled — no shifting occurs) | no |
| 100 | out, free-running | Timer 2 | no (repeats indefinitely, bit counter not tracked) |
| 101 | out | Timer 2 | yes |
| 110 | out | system clock (every cycle) | yes |
| 111 | out | external CB1 edge (unmodeled — no shifting occurs) | no |

The Timer-2-derived shift clock is a divided-down version of the system
clock, with the division rate set by Timer 2's low-order **latch** value
(independent of Timer 2's own counter/mode) — every reload of that divider
produces one shift-clock edge. "Out" modes drive CB2 with the bit being
shifted out; when ACR bit 4 is set, CB2's *level* used for beam blanking
(§6) is this shift-register-driven value instead of the directly-controlled
CB2 level — this is what the Vectrex BIOS's dashed/patterned-line routine
uses to blank/unblank the beam rapidly while it moves.

## 4. Peripheral Control Register (PCR) — CA2/CB2

Both CA2 (bits 3-1) and CB2 (bits 7-5) use the same 3-bit code:

| Code | Behavior |
|---|---|
| 0xx (0,1) | Input, negative edge |
| 10x (2,3) | Input, positive edge |
| 100 (4) | Handshake output: driven low by an ORA/ORB access, restored high by the paired CA1/CB1 edge (no CA1/CB1 edge source modeled — treat as staying low until PCR changes, since nothing here restores it) |
| 101 (5) | Pulse output: driven low for exactly one cycle in response to the associated ORA/ORB access, then automatically restored high the following cycle regardless of anything else |
| 110 (6) | Manual output, held low until PCR is written again |
| 111 (7) | Manual output, held high |

For the Vectrex's use of these lines specifically: CA2 is the beam's
**~ZERO** control (manual-low mode forces the integrators toward center —
see §6) and CB2 is the beam's **~BLANK** control (any of handshake/pulse/
manual modes, or the shift register's own output when ACR bit 4 selects
that).

## 5. Vectrex analog signal path

**DAC.** Port A (all 8 bits configured as outputs) feeds an 8-bit DAC.
The DAC's conversion is bipolar around its midpoint: the byte's sign bit is
inverted before conversion, so a raw ORA byte `B` produces an effective
sample-and-hold value of `B XOR 0x80` in an unsigned 0-255 space, where 128
is the electrical zero/center point.

**Multiplexer.** The DAC's held output is routed to one of four
destinations by a 2-bit select formed from ORB bits 2 and 1 (bit 2 = high
order, bit 1 = low order), and routing only happens while ORB bit 0 (the
demultiplexer enable) is clear:

| Select (ORB bits 2:1) | Destination |
|---|---|
| 00 | Y-axis integrator sample-and-hold |
| 01 | Zero-reference (R) sample-and-hold |
| 10 | Z-axis (brightness) sample-and-hold — **only the upper half** of the centered range is visible: values at or below center map to zero brightness, values above map to `value - center` on a 0-127 scale |
| 11 | Sound chip data line — not a beam axis at all |

**Zero-reference.** The X sample-and-hold is always fed directly and
immediately by the DAC's current value (no mux gating) — it's what feeds
the multiplexer input itself, so "writing X" and "loading the mux input"
are the same DAC write. Y and R are only updated when the mux selects them
per the table above.

**Beam deltas.** The beam's per-cycle X displacement is the difference
between the X and R (zero-reference) sample-and-holds; its per-cycle Y
displacement is the difference between R and Y. Using a shared R term
(rather than independent X/Y offsets) is a real property of the analog
integrator circuit — the R sample-and-hold's voltage is applied as a common
bias into both axes' integrator op-amps, so a single zero-reference write
shifts both axes' effective origin at once. These deltas are recomputed
whenever any of X/R/Y changes.

**~ZERO (CA2).** While driven low (manual-output-low PCR mode), the
integrators are forced toward the screen's electrical center regardless of
the held X/R/Y deltas — used by BIOS/game code to recenter the beam
(e.g. at the start of a frame) before drawing.

**~RAMP.** Whether the beam moves at all this cycle is gated by a signal
that is active (moving) when low: it comes from Timer 1's PB7 view when ACR
bit 7 gives Timer 1 control of it, or directly from ORB bit 7 otherwise
(software manually toggling the gate). While active, the beam's position
advances by the current X/Y deltas every cycle; while inactive, the beam
holds still.

**~BLANK (CB2).** Whether the (possibly moving, possibly still) beam is
actually visible this cycle is CB2's current level as described in §3/§4 —
handshake/pulse/manual-driven, or shift-register-driven when ACR bit 4
selects that.

**Z (brightness).** The Z sample-and-hold's current 0-127 value is the
intensity of whatever is currently being drawn; it can change mid-line.

## 6. What the emulator needs to produce from this

The beam is, at any moment, either not tracing a visible segment or in the
middle of one. A new segment begins the instant the beam becomes visible
(~BLANK active) at a valid on-screen position. It continues extending every
cycle the beam keeps moving, unblanked, with an unchanged delta and Z value.
It ends — and should be handed off for display — the moment any of these
happen: ~BLANK goes inactive, the delta or Z value changes while still
unblanked (in which case a new segment starts immediately from the same
point), or the beam's position leaves the drawable coordinate range.

There is no hardware concept of a "frame" — real games simply redraw their
entire display list roughly 50 times a second because that's what looks
stable on a CRT with phosphor persistence, not because any register enforces
it. The emulator needs its own periodic checkpoint (paced by an approximate
CPU-cycles-per-redraw figure derived from the 6809's clock rate and a chosen
target refresh rate) at which it hands the segments accumulated since the
last checkpoint to the display. So the screen doesn't flash to black while
a game is mid-way through redrawing its next set of lines, the previous
checkpoint's segments remain part of the submitted display list until the
next checkpoint replaces them. Phosphor persistence is then applied by the
display layer to complete submitted host frames.
