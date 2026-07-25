#include "cpu.h"
#include "display.h"
#include "memory.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void cpu_reset(Cpu *cpu) {
    memset(cpu, 0, sizeof(*cpu));

    cpu->DP = 0x00;
    cpu->CC = CC_I | CC_F; // IRQ/FIRQ masked on reset

    uint8_t hi = mem_read8(RESET_VECTOR);
    uint8_t lo = mem_read8(RESET_VECTOR + 1);
    cpu->PC = (uint16_t)(hi << 8) | lo;
}

static void set_nz8(Cpu *cpu, uint8_t value) {
    if (value & 0x80) cpu->CC |= CC_N; else cpu->CC &= ~CC_N;
    if (value == 0)   cpu->CC |= CC_Z; else cpu->CC &= ~CC_Z;
}

static void set_nz16(Cpu *cpu, uint16_t value) {
    if (value & 0x8000) cpu->CC |= CC_N; else cpu->CC &= ~CC_N;
    if (value == 0)     cpu->CC |= CC_Z; else cpu->CC &= ~CC_Z;
}

static uint16_t *cpu_index_register(Cpu *cpu, uint8_t rr) {
    switch (rr) {
        case 0: return &cpu->X;
        case 1: return &cpu->Y;
        case 2: return &cpu->U;
        default: return &cpu->S; // 3
    }
}

// Decodes one 6809 indexed-addressing postbyte and resolves it to an
// effective address, per the MC6809 datasheet's postbyte table. Implements
// the common non-indirect subset only:
//   0 RR nnnnn         -- 5-bit signed constant offset
//   1 RR 0 00000 (,R+)    -- post-increment by 1
//   1 RR 0 00001 (,R++)   -- post-increment by 2
//   1 RR 0 00010 (,-R)    -- pre-decrement by 1
//   1 RR 0 00011 (,--R)   -- pre-decrement by 2
//   1 RR 0 00100 (,R)     -- zero offset
//   1 RR 0 01000 (n8,R)   -- 8-bit signed offset follows
//   1 RR 0 01001 (n16,R)  -- 16-bit signed offset follows (big-endian)
// where RR selects the register: 00=X, 01=Y, 10=U, 11=S.
//
// NOT implemented: indirect modes (bit 4 of the postbyte set, i.e. an extra
// memory dereference on top of the computed address) and accumulator-offset
// modes (A,R / B,R / D,R) or PC-relative. Those fail loudly -- print a
// diagnostic and return 0 -- rather than silently resolving to the wrong
// address, same as an unimplemented opcode.
static int cpu_resolve_indexed(Cpu *cpu, uint16_t *out_addr) {
    uint8_t postbyte = mem_read8(cpu->PC);
    cpu->PC++;

    uint8_t rr = (postbyte >> 5) & 0x03;
    uint16_t *reg = cpu_index_register(cpu, rr);

    if ((postbyte & 0x80) == 0) {
        // 0 RR nnnnn: sign-extend the low 5 bits (bit 4 is the sign bit).
        int8_t offset = (int8_t)((postbyte & 0x1F) << 3) >> 3;
        *out_addr = (uint16_t)(*reg + offset);
        return 1;
    }

    switch (postbyte & 0x1F) {
        case 0x00: // ,R+
            *out_addr = *reg;
            *reg = (uint16_t)(*reg + 1);
            return 1;
        case 0x01: // ,R++
            *out_addr = *reg;
            *reg = (uint16_t)(*reg + 2);
            return 1;
        case 0x02: // ,-R
            *reg = (uint16_t)(*reg - 1);
            *out_addr = *reg;
            return 1;
        case 0x03: // ,--R
            *reg = (uint16_t)(*reg - 2);
            *out_addr = *reg;
            return 1;
        case 0x04: // ,R
            *out_addr = *reg;
            return 1;
        case 0x08: { // n8,R
            int8_t offset = (int8_t)mem_read8(cpu->PC);
            cpu->PC++;
            *out_addr = (uint16_t)(*reg + offset);
            return 1;
        }
        case 0x09: { // n16,R
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            int16_t offset = (int16_t)(((uint16_t)hi << 8) | lo);
            *out_addr = (uint16_t)(*reg + offset);
            return 1;
        }
        default:
            printf("unimplemented indexed postbyte 0x%02X at PC=0x%04X\n", postbyte, (uint16_t)(cpu->PC - 1));
            return 0;
    }
}

// Resolves DIRECT/EXTENDED/INDEXED effective addresses, consuming the
// operand byte(s) at cpu->PC and advancing PC past them. Returns 1 on
// success with *out_addr set, or 0 if an indexed postbyte encoded a submode
// this emulator doesn't implement (see cpu_resolve_indexed) -- DIRECT and
// EXTENDED can never fail.
//
// IMMEDIATE is deliberately not a case here. DIRECT/EXTENDED/INDEXED all
// follow the same shape: fetch operand byte(s), combine into an effective
// address, then the caller dereferences that address separately. IMMEDIATE
// has no dereference step at all -- the fetched bytes ARE the value -- and
// the fetch width depends on the destination register (1 byte for A/B, 2
// bytes for X/U), not on the addressing mode. Forcing it through this
// function would mean either returning PC as a fake "address" (misleading,
// since nothing is dereferenced through the normal memory-routing path
// conceptually) or adding a width parameter that only IMMEDIATE would use.
// Simpler to keep it inline per instruction, as before.
static int cpu_resolve_address(Cpu *cpu, AddrMode mode, uint16_t *out_addr) {
    switch (mode) {
        case ADDR_DIRECT: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            *out_addr = ((uint16_t)cpu->DP << 8) | operand;
            return 1;
        }
        case ADDR_EXTENDED: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            *out_addr = ((uint16_t)hi << 8) | lo;
            return 1;
        }
        case ADDR_INDEXED:
            return cpu_resolve_indexed(cpu, out_addr);
    }
    return 0; // unreachable
}

static void cpu_add8(Cpu *cpu, uint8_t *reg, uint8_t operand) {
    uint8_t a = *reg;
    uint16_t wide = (uint16_t)a + (uint16_t)operand;
    uint8_t result = (uint8_t)wide;
    *reg = result;

    set_nz8(cpu, result);

    // V: operands share a sign and the result's sign differs from it.
    if ((~(a ^ operand) & (a ^ result)) & 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;

    // C: unsigned carry out of bit 7.
    if (wide & 0x100) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
}

// Shared by SUB (store_result != 0) and CMP (store_result == 0, flags-only).
static void cpu_sub8(Cpu *cpu, uint8_t *reg, uint8_t operand, int store_result) {
    uint8_t a = *reg;
    uint8_t result = (uint8_t)(a - operand);
    if (store_result) *reg = result;

    set_nz8(cpu, result);

    // V: operands differ in sign and the result's sign differs from the minuend.
    if (((a ^ operand) & (a ^ result)) & 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;

    // C: borrow -- unsigned subtrahend exceeded unsigned minuend.
    if (operand > a) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
}

static void cpu_inc8(Cpu *cpu, uint8_t *reg) {
    uint8_t a = *reg;
    uint8_t result = (uint8_t)(a + 1);
    *reg = result;

    set_nz8(cpu, result);

    // V only fires on the one edge case: $7F -> $80. C is untouched by INC.
    if (a == 0x7F) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
}

static void cpu_dec8(Cpu *cpu, uint8_t *reg) {
    uint8_t a = *reg;
    uint8_t result = (uint8_t)(a - 1);
    *reg = result;

    set_nz8(cpu, result);

    // V only fires on the one edge case: $80 -> $7F. C is untouched by DEC.
    if (a == 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
}

// Fetches the signed 8-bit relative offset (always, whether or not the
// branch is taken -- the byte still has to be consumed either way) and,
// if take is nonzero, adds it to PC as already advanced past the full
// 2-byte branch instruction. Branches never touch CC themselves.
static void cpu_branch(Cpu *cpu, int take) {
    int8_t offset = (int8_t)mem_read8(cpu->PC);
    cpu->PC++;

    if (take) {
        cpu->PC = (uint16_t)(cpu->PC + offset);
    }
}

// The 6809 stack grows downward. Pushing a 16-bit value decrements S then
// writes, high byte first (at S-1) then low byte (at S-2) -- so after the
// push S is down by 2 and the low byte sits at the lower address. Popping
// reverses this: read low byte first (at S), then high byte (at S+1), then
// increment S by 2.
static void cpu_push16(Cpu *cpu, uint16_t value) {
    cpu->S--;
    mem_write8(cpu->S, (uint8_t)(value >> 8));
    cpu->S--;
    mem_write8(cpu->S, (uint8_t)(value & 0xFF));
}

static uint16_t cpu_pop16(Cpu *cpu) {
    uint8_t lo = mem_read8(cpu->S);
    cpu->S++;
    uint8_t hi = mem_read8(cpu->S);
    cpu->S++;
    return ((uint16_t)hi << 8) | lo;
}

// Draw_VL: X points at a vector list -- a count-minus-1 byte followed by
// that many (dy, dx) signed-byte pairs, each a delta from the current pen
// position (not an absolute coordinate). Walks the list and draws each
// segment via the existing display_draw_line(). Intensity/scale/mode-byte
// nuances aren't modeled yet, and the pen always starts at (0,0) -- real
// Vectrex coordinate centering/scaling is future work, same as the display
// list renderer from the previous step.
static void hle_draw_vl(Cpu *cpu) {
    uint16_t addr = cpu->X;
    uint8_t count = mem_read8(addr);
    addr++;

    int pen_x = 0, pen_y = 0;
    for (uint16_t i = 0; i <= count; i++) {
        int8_t dy = (int8_t)mem_read8(addr);
        int8_t dx = (int8_t)mem_read8(addr + 1);
        addr += 2;

        int new_x = pen_x + dx;
        int new_y = pen_y + dy;
        display_draw_line(pen_x, pen_y, new_x, new_y);
        pen_x = new_x;
        pen_y = new_y;
    }
}

// Wait_Recal: real hardware waits for the beam recalibration/frame timing
// window. Not modeled yet -- structured as its own handler so it's a single
// place to add real frame-timing behavior later, but for now it's a no-op.
static void hle_wait_recal(Cpu *cpu) {
    (void)cpu;
}

// Lets debug/diagnostic code (see main.c's BIOS tracer) temporarily disable
// HLE interception so real ROM bytes execute through the CPU core even at
// an address that's normally hooked -- e.g. to inspect what $F192 actually
// does before trusting it's really Wait_Recal. Defaults on.
static int hle_enabled = 1;

void cpu_set_hle_enabled(int enabled) {
    hle_enabled = enabled;
}

// Checked at the very top of cpu_step(), before any opcode fetch: if PC is
// sitting on a known BIOS routine's entry address, run the native handler
// instead of whatever 6809 bytes are actually there, then pop the return
// address off the stack straight into PC -- the same mechanic as the RTS
// opcode -- rather than letting the real (or, for BIOS regions we haven't
// loaded meaningfully, garbage) bytes at that address execute.
static int cpu_try_hle(Cpu *cpu) {
    if (!hle_enabled) {
        return 0;
    }

    switch (cpu->PC) {
        case HLE_DRAW_VL:
            hle_draw_vl(cpu);
            break;
        case HLE_WAIT_RECAL:
            hle_wait_recal(cpu);
            break;
        default:
            return 0;
    }

    cpu->PC = cpu_pop16(cpu);
    return 1;
}

// Per-opcode base cycle cost, straight from the MC6809 datasheet timing
// table. Indexed by opcode byte; entries left at 0 are opcodes cpu_step()
// doesn't implement yet (the default: case below handles those and halts,
// so their cost is moot -- 0 just avoids leaving a gap in the accounting).
static const uint8_t opcode_cycles[256] = {
    [OP_LDA_IMM] = 2, [OP_LDA_DIR] = 4, [OP_LDA_EXT] = 5, [OP_LDA_IDX] = 4,
    [OP_LDB_IMM] = 2, [OP_LDB_DIR] = 4, [OP_LDB_EXT] = 5,
    [OP_LDX_IMM] = 3, [OP_LDX_DIR] = 5, [OP_LDX_EXT] = 6, [OP_LDX_IDX] = 5,
    [OP_LDU_IMM] = 3, [OP_LDU_DIR] = 5, [OP_LDU_EXT] = 6,

    // Indexed entries are the datasheet's base ,R cost. Real indexed timing
    // varies with the postbyte submode (offset bytes add cycles, auto
    // inc/dec by 2 adds one more, etc.) -- not modeled yet, consistent with
    // cpu_resolve_indexed() itself only covering the common submodes.
    [OP_STA_DIR] = 4, [OP_STA_EXT] = 5, [OP_STA_IDX] = 4,
    [OP_STB_DIR] = 4, [OP_STB_EXT] = 5,
    [OP_STX_DIR] = 5, [OP_STX_EXT] = 6, [OP_STX_IDX] = 5,
    [OP_STU_DIR] = 5, [OP_STU_EXT] = 6,

    [OP_ADDA_IMM] = 2, [OP_ADDA_DIR] = 4, [OP_ADDA_EXT] = 5,
    [OP_ADDB_IMM] = 2, [OP_ADDB_DIR] = 4, [OP_ADDB_EXT] = 5,
    [OP_SUBA_IMM] = 2, [OP_SUBA_DIR] = 4, [OP_SUBA_EXT] = 5,
    [OP_SUBB_IMM] = 2, [OP_SUBB_DIR] = 4, [OP_SUBB_EXT] = 5,
    [OP_CMPA_IMM] = 2, [OP_CMPA_DIR] = 4, [OP_CMPA_EXT] = 5,
    [OP_CMPB_IMM] = 2, [OP_CMPB_DIR] = 4, [OP_CMPB_EXT] = 5,

    [OP_INCA] = 2, [OP_INCB] = 2, [OP_DECA] = 2, [OP_DECB] = 2,

    [OP_BRA] = 3, [OP_BCC] = 3, [OP_BCS] = 3, [OP_BNE] = 3, [OP_BEQ] = 3,
    [OP_BVC] = 3, [OP_BVS] = 3, [OP_BPL] = 3, [OP_BMI] = 3,

    [OP_JSR_DIR] = 7, [OP_JSR_EXT] = 8, [OP_RTS] = 5,
};

int cpu_step(Cpu *cpu) {
    // HLE interception happens before any opcode fetch/decode: if PC is
    // sitting on a known BIOS routine address, the native handler runs (and
    // simulates the RTS) instead of whatever's actually at that address in
    // the loaded ROM. No cycle cost is charged here -- these are native
    // substitutes for routines of unknown/variable real timing, not
    // something we can honestly cost against the datasheet table.
    if (cpu_try_hle(cpu)) {
        return 1;
    }

    uint16_t opcode_pc = cpu->PC;
    uint8_t opcode = mem_read8(cpu->PC);
    cpu->PC++;

    // Cycle cost is a fixed property of the opcode byte for every
    // instruction implemented so far (none of our branches vary cost by
    // taken/not-taken, unlike some other 8-bit CPUs), so it's safe to
    // account for it up front rather than threading it through every case.
    cpu->cycles += opcode_cycles[opcode];

    switch (opcode) {
        case OP_LDA_IMM: {
            uint8_t value = mem_read8(cpu->PC);
            cpu->PC++;

            cpu->A = value;
            set_nz8(cpu, value);
            return 1;
        }
        case OP_LDA_DIR:
        case OP_LDA_EXT:
        case OP_LDA_IDX: {
            AddrMode mode = opcode == OP_LDA_DIR ? ADDR_DIRECT : opcode == OP_LDA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t value = mem_read8(addr);

            cpu->A = value;
            set_nz8(cpu, value);
            return 1;
        }

        case OP_LDB_IMM: {
            uint8_t value = mem_read8(cpu->PC);
            cpu->PC++;

            cpu->B = value;
            set_nz8(cpu, value);
            return 1;
        }
        case OP_LDB_DIR:
        case OP_LDB_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_LDB_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            uint8_t value = mem_read8(addr);

            cpu->B = value;
            set_nz8(cpu, value);
            return 1;
        }

        case OP_LDX_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->X = value;
            set_nz16(cpu, value);
            return 1;
        }
        case OP_LDX_DIR:
        case OP_LDX_EXT:
        case OP_LDX_IDX: {
            AddrMode mode = opcode == OP_LDX_DIR ? ADDR_DIRECT : opcode == OP_LDX_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->X = value;
            set_nz16(cpu, value);
            return 1;
        }

        case OP_LDU_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->U = value;
            set_nz16(cpu, value);
            return 1;
        }
        case OP_LDU_DIR:
        case OP_LDU_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_LDU_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->U = value;
            set_nz16(cpu, value);
            return 1;
        }

        case OP_STA_DIR:
        case OP_STA_EXT:
        case OP_STA_IDX: {
            AddrMode mode = opcode == OP_STA_DIR ? ADDR_DIRECT : opcode == OP_STA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu->A);
            return 1;
        }

        case OP_STB_DIR:
        case OP_STB_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_STB_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            mem_write8(addr, cpu->B);
            return 1;
        }

        case OP_STX_DIR:
        case OP_STX_EXT:
        case OP_STX_IDX: {
            AddrMode mode = opcode == OP_STX_DIR ? ADDR_DIRECT : opcode == OP_STX_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, (uint8_t)(cpu->X >> 8));
            mem_write8(addr + 1, (uint8_t)(cpu->X & 0xFF));
            return 1;
        }

        case OP_STU_DIR:
        case OP_STU_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_STU_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            mem_write8(addr, (uint8_t)(cpu->U >> 8));
            mem_write8(addr + 1, (uint8_t)(cpu->U & 0xFF));
            return 1;
        }

        case OP_ADDA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_add8(cpu, &cpu->A, operand);
            return 1;
        }
        case OP_ADDA_DIR:
        case OP_ADDA_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_ADDA_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            cpu_add8(cpu, &cpu->A, mem_read8(addr));
            return 1;
        }

        case OP_ADDB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_add8(cpu, &cpu->B, operand);
            return 1;
        }
        case OP_ADDB_DIR:
        case OP_ADDB_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_ADDB_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            cpu_add8(cpu, &cpu->B, mem_read8(addr));
            return 1;
        }

        case OP_SUBA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_sub8(cpu, &cpu->A, operand, 1);
            return 1;
        }
        case OP_SUBA_DIR:
        case OP_SUBA_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_SUBA_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            cpu_sub8(cpu, &cpu->A, mem_read8(addr), 1);
            return 1;
        }

        case OP_SUBB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_sub8(cpu, &cpu->B, operand, 1);
            return 1;
        }
        case OP_SUBB_DIR:
        case OP_SUBB_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_SUBB_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            cpu_sub8(cpu, &cpu->B, mem_read8(addr), 1);
            return 1;
        }

        case OP_CMPA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_sub8(cpu, &cpu->A, operand, 0);
            return 1;
        }
        case OP_CMPA_DIR:
        case OP_CMPA_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_CMPA_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            cpu_sub8(cpu, &cpu->A, mem_read8(addr), 0);
            return 1;
        }

        case OP_CMPB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_sub8(cpu, &cpu->B, operand, 0);
            return 1;
        }
        case OP_CMPB_DIR:
        case OP_CMPB_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode == OP_CMPB_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            cpu_sub8(cpu, &cpu->B, mem_read8(addr), 0);
            return 1;
        }

        case OP_INCA:
            cpu_inc8(cpu, &cpu->A);
            return 1;
        case OP_INCB:
            cpu_inc8(cpu, &cpu->B);
            return 1;
        case OP_DECA:
            cpu_dec8(cpu, &cpu->A);
            return 1;
        case OP_DECB:
            cpu_dec8(cpu, &cpu->B);
            return 1;

        case OP_BRA:
            cpu_branch(cpu, 1);
            return 1;
        case OP_BEQ:
            cpu_branch(cpu, (cpu->CC & CC_Z) != 0);
            return 1;
        case OP_BNE:
            cpu_branch(cpu, (cpu->CC & CC_Z) == 0);
            return 1;
        case OP_BCC:
            cpu_branch(cpu, (cpu->CC & CC_C) == 0);
            return 1;
        case OP_BCS:
            cpu_branch(cpu, (cpu->CC & CC_C) != 0);
            return 1;
        case OP_BPL:
            cpu_branch(cpu, (cpu->CC & CC_N) == 0);
            return 1;
        case OP_BMI:
            cpu_branch(cpu, (cpu->CC & CC_N) != 0);
            return 1;
        case OP_BVC:
            cpu_branch(cpu, (cpu->CC & CC_V) == 0);
            return 1;
        case OP_BVS:
            cpu_branch(cpu, (cpu->CC & CC_V) != 0);
            return 1;

        case OP_JSR_DIR:
        case OP_JSR_EXT: {
            uint16_t target;
            if (!cpu_resolve_address(cpu, opcode == OP_JSR_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &target)) return 0;
            cpu_push16(cpu, cpu->PC); // return address: PC after the full JSR instruction
            cpu->PC = target;
            return 1;
        }

        case OP_RTS:
            cpu->PC = cpu_pop16(cpu);
            return 1;

        default:
            printf("unimplemented opcode 0x%02X at PC=0x%04X\n", opcode, opcode_pc);
            return 0;
    }
}

void cpu_print_state(const Cpu *cpu) {
    printf("A=%02X B=%02X D=%04X\n", cpu->A, cpu->B, cpu->D);
    printf("X=%04X Y=%04X U=%04X S=%04X PC=%04X\n",
           cpu->X, cpu->Y, cpu->U, cpu->S, cpu->PC);
    printf("DP=%02X CC=%02X [%c%c%c%c%c%c%c%c]\n",
           cpu->DP, cpu->CC,
           (cpu->CC & CC_E) ? 'E' : '-',
           (cpu->CC & CC_F) ? 'F' : '-',
           (cpu->CC & CC_H) ? 'H' : '-',
           (cpu->CC & CC_I) ? 'I' : '-',
           (cpu->CC & CC_N) ? 'N' : '-',
           (cpu->CC & CC_Z) ? 'Z' : '-',
           (cpu->CC & CC_V) ? 'V' : '-',
           (cpu->CC & CC_C) ? 'C' : '-');
    printf("Cycles=%" PRIu64 "\n", cpu->cycles);
}
