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

// One more indirection level, shared by every indirect indexed submode: the
// computed effective address holds a 16-bit pointer (big-endian) to the
// real target, rather than being the target itself.
static uint16_t cpu_read_indirect(uint16_t addr) {
    uint8_t hi = mem_read8(addr);
    uint8_t lo = mem_read8(addr + 1);
    return ((uint16_t)hi << 8) | lo;
}

// Decodes one 6809 indexed-addressing postbyte and resolves it to an
// effective address, per the MC6809 datasheet's postbyte table (Table 2).
// Full table, RR selecting the register (00=X, 01=Y, 10=U, 11=S):
//   0 RR nnnnn        -- 5-bit signed constant offset (no indirect form)
//   1 RR 00000 ,R+       1 RR 10001 [,R++]  (,R+ has no indirect form)
//   1 RR 00001 ,R++
//   1 RR 00010 ,-R       1 RR 10011 [,--R]  (,-R has no indirect form)
//   1 RR 00011 ,--R
//   1 RR 00100 ,R        1 RR 10100 [,R]
//   1 RR 00101 B,R       1 RR 10101 [B,R]
//   1 RR 00110 A,R       1 RR 10110 [A,R]
//   1 RR 01000 n8,R      1 RR 11000 [n8,R]
//   1 RR 01001 n16,R     1 RR 11001 [n16,R]
//   1 RR 01011 D,R       1 RR 11011 [D,R]
//   1 RR 01100 n8,PCR    1 RR 11100 [n8,PCR]
//   1 RR 01101 n16,PCR   1 RR 11101 [n16,PCR]
//   1 xx 01111 [n16]  -- extended indirect; RR bits are unused/don't-care
// Remaining low-5-bit values (0x07, 0x0A, 0x0E, 0x10, 0x12, 0x17, 0x1A,
// 0x1E, 0x1F) are reserved/illegal per the spec -- not just unimplemented,
// genuinely undefined -- so they fail loudly like an unimplemented opcode
// rather than silently resolving to the wrong address.
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
        case 0x05: // B,R -- accumulator offset is sign-extended before adding
            *out_addr = (uint16_t)(*reg + (int8_t)cpu->B);
            return 1;
        case 0x06: // A,R
            *out_addr = (uint16_t)(*reg + (int8_t)cpu->A);
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
        case 0x0B: // D,R -- full 16-bit signed accumulator offset
            *out_addr = (uint16_t)(*reg + (int16_t)cpu->D);
            return 1;
        case 0x0C: { // n8,PCR -- relative to PC as left after this offset byte
            int8_t offset = (int8_t)mem_read8(cpu->PC);
            cpu->PC++;
            *out_addr = (uint16_t)(cpu->PC + offset);
            return 1;
        }
        case 0x0D: { // n16,PCR
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            int16_t offset = (int16_t)(((uint16_t)hi << 8) | lo);
            *out_addr = (uint16_t)(cpu->PC + offset);
            return 1;
        }
        case 0x0F: { // [n16] extended indirect -- RR field unused
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            uint16_t addr = ((uint16_t)hi << 8) | lo;
            *out_addr = cpu_read_indirect(addr);
            return 1;
        }

        // Indirect variants: compute the same effective address as the
        // matching non-indirect form above, then dereference it once more.
        case 0x11: { // [,R++]
            uint16_t addr = *reg;
            *reg = (uint16_t)(*reg + 2);
            *out_addr = cpu_read_indirect(addr);
            return 1;
        }
        case 0x13: // [,--R]
            *reg = (uint16_t)(*reg - 2);
            *out_addr = cpu_read_indirect(*reg);
            return 1;
        case 0x14: // [,R]
            *out_addr = cpu_read_indirect(*reg);
            return 1;
        case 0x15: // [B,R]
            *out_addr = cpu_read_indirect((uint16_t)(*reg + (int8_t)cpu->B));
            return 1;
        case 0x16: // [A,R]
            *out_addr = cpu_read_indirect((uint16_t)(*reg + (int8_t)cpu->A));
            return 1;
        case 0x18: { // [n8,R]
            int8_t offset = (int8_t)mem_read8(cpu->PC);
            cpu->PC++;
            *out_addr = cpu_read_indirect((uint16_t)(*reg + offset));
            return 1;
        }
        case 0x19: { // [n16,R]
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            int16_t offset = (int16_t)(((uint16_t)hi << 8) | lo);
            *out_addr = cpu_read_indirect((uint16_t)(*reg + offset));
            return 1;
        }
        case 0x1B: // [D,R]
            *out_addr = cpu_read_indirect((uint16_t)(*reg + (int16_t)cpu->D));
            return 1;
        case 0x1C: { // [n8,PCR]
            int8_t offset = (int8_t)mem_read8(cpu->PC);
            cpu->PC++;
            *out_addr = cpu_read_indirect((uint16_t)(cpu->PC + offset));
            return 1;
        }
        case 0x1D: { // [n16,PCR]
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            int16_t offset = (int16_t)(((uint16_t)hi << 8) | lo);
            *out_addr = cpu_read_indirect((uint16_t)(cpu->PC + offset));
            return 1;
        }

        default:
            printf("unimplemented indexed postbyte 0x%02X at PC=0x%04X\n", postbyte, (uint16_t)(cpu->PC - 1));
            return 0;
    }
}

// Resolves DIRECT/EXTENDED/INDEXED effective addresses, consuming operand
// byte(s) and advancing PC. Returns 0 only for unimplemented indexed submodes.
// IMMEDIATE is excluded: the fetched bytes are the value directly (no address
// to compute), and the fetch width depends on the destination register, not
// the mode -- so each instruction handles it inline.
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

// Shared by AND/OR/EOR (store_result != 0) and BIT (store_result == 0,
// flags-only, like cpu_sub8's CMP mode). All three logical ops set N/Z from
// the result, always clear V, and leave C untouched.
static void cpu_and8(Cpu *cpu, uint8_t *reg, uint8_t operand, int store_result) {
    uint8_t result = (uint8_t)(*reg & operand);
    if (store_result) *reg = result;
    set_nz8(cpu, result);
    cpu->CC &= ~CC_V;
}

static void cpu_or8(Cpu *cpu, uint8_t *reg, uint8_t operand) {
    uint8_t result = (uint8_t)(*reg | operand);
    *reg = result;
    set_nz8(cpu, result);
    cpu->CC &= ~CC_V;
}

static void cpu_eor8(Cpu *cpu, uint8_t *reg, uint8_t operand) {
    uint8_t result = (uint8_t)(*reg ^ operand);
    *reg = result;
    set_nz8(cpu, result);
    cpu->CC &= ~CC_V;
}

// 16-bit counterpart to cpu_add8 -- same logic, widened. Real 6809 ADDD
// doesn't touch CC_H (that's only defined for the 8-bit ADD/ADC forms, used
// by DAA), matching cpu_add8 already not touching it either.
static void cpu_add16(Cpu *cpu, uint16_t *reg, uint16_t operand) {
    uint16_t a = *reg;
    uint32_t wide = (uint32_t)a + (uint32_t)operand;
    uint16_t result = (uint16_t)wide;
    *reg = result;

    set_nz16(cpu, result);

    // V: operands share a sign and the result's sign differs from it.
    if ((~(a ^ operand) & (a ^ result)) & 0x8000) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;

    // C: unsigned carry out of bit 15.
    if (wide & 0x10000) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
}

// 16-bit counterpart to cpu_sub8 -- shared by SUBD (store_result != 0) and
// CMPD/CMPX/CMPY/CMPU/CMPS (store_result == 0, flags-only).
static void cpu_sub16(Cpu *cpu, uint16_t *reg, uint16_t operand, int store_result) {
    uint16_t a = *reg;
    uint16_t result = (uint16_t)(a - operand);
    if (store_result) *reg = result;

    set_nz16(cpu, result);

    // V: operands differ in sign and the result's sign differs from the minuend.
    if (((a ^ operand) & (a ^ result)) & 0x8000) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;

    // C: borrow -- unsigned subtrahend exceeded unsigned minuend.
    if (operand > a) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
}

// Value-based core shared by the register forms (INCA/INCB, via the
// pointer wrapper below) and the memory-addressed forms (INC direct/
// extended/indexed, which read/write through mem_read8/mem_write8 instead
// of a register pointer).
static uint8_t cpu_inc8_value(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)(value + 1);
    set_nz8(cpu, result);

    // V only fires on the one edge case: $7F -> $80. C is untouched by INC.
    if (value == 0x7F) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
    return result;
}

static uint8_t cpu_dec8_value(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)(value - 1);
    set_nz8(cpu, result);

    // V only fires on the one edge case: $80 -> $7F. C is untouched by DEC.
    if (value == 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
    return result;
}

static void cpu_inc8(Cpu *cpu, uint8_t *reg) {
    *reg = cpu_inc8_value(cpu, *reg);
}

static void cpu_dec8(Cpu *cpu, uint8_t *reg) {
    *reg = cpu_dec8_value(cpu, *reg);
}

// NEG: two's complement negate (0 - value). C is set whenever a borrow was
// needed to compute it, i.e. whenever value != 0; V fires on the one
// self-negating edge case, $80 -> $80.
static uint8_t cpu_neg8(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)(0 - value);
    set_nz8(cpu, result);
    if (value == 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
    if (value != 0) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
    return result;
}

// COM: one's complement. V is always cleared and C is always set -- a
// documented 6809 quirk, not a simplification.
static uint8_t cpu_com8(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)~value;
    set_nz8(cpu, result);
    cpu->CC &= ~CC_V;
    cpu->CC |= CC_C;
    return result;
}

// LSR: logical shift right, 0 fills bit 7, old bit 0 -> C. N is always 0
// since bit 7 is always 0 after the shift; V isn't defined for LSR so it's
// left untouched, matching how cpu_inc8/cpu_dec8 leave C untouched.
static uint8_t cpu_lsr8(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)(value >> 1);
    if (value & 0x01) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
    set_nz8(cpu, result);
    return result;
}

// ASR: arithmetic shift right, bit 7 is preserved (sign-extended), old bit
// 0 -> C. V isn't defined for ASR, left untouched.
static uint8_t cpu_asr8(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)((value >> 1) | (value & 0x80));
    if (value & 0x01) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
    set_nz8(cpu, result);
    return result;
}

// ASL/LSL: shift left, 0 fills bit 0, old bit 7 -> C. V is set if the sign
// bit changes as a result of the shift (old bit 7 XOR old bit 6).
static uint8_t cpu_asl8(Cpu *cpu, uint8_t value) {
    uint8_t result = (uint8_t)(value << 1);
    if (value & 0x80) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
    set_nz8(cpu, result);
    if ((value ^ (uint8_t)(value << 1)) & 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
    return result;
}

// ROR: rotate right through carry -- old C -> bit 7, old bit 0 -> C. V
// isn't defined for ROR, left untouched.
static uint8_t cpu_ror8(Cpu *cpu, uint8_t value) {
    int old_c = (cpu->CC & CC_C) != 0;
    uint8_t result = (uint8_t)((value >> 1) | (old_c ? 0x80 : 0x00));
    if (value & 0x01) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
    set_nz8(cpu, result);
    return result;
}

// ROL: rotate left through carry -- old bit 7 -> C, old C -> bit 0. Same V
// logic as ASL (sign bit change), since both are left rotations/shifts.
static uint8_t cpu_rol8(Cpu *cpu, uint8_t value) {
    int old_c = (cpu->CC & CC_C) != 0;
    uint8_t result = (uint8_t)((value << 1) | (old_c ? 0x01 : 0x00));
    if (value & 0x80) cpu->CC |= CC_C; else cpu->CC &= ~CC_C;
    set_nz8(cpu, result);
    if ((value ^ (uint8_t)(value << 1)) & 0x80) cpu->CC |= CC_V; else cpu->CC &= ~CC_V;
    return result;
}

// TST: flags-only compare-to-zero. N/Z per value, V cleared, C untouched.
static void cpu_tst8(Cpu *cpu, uint8_t value) {
    set_nz8(cpu, value);
    cpu->CC &= ~CC_V;
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

// Long-branch counterpart: 16-bit relative offset, always consumed
// regardless of whether the branch is taken. Unlike short branches, long
// conditional branches DO vary cycle cost by taken/not-taken on real 6809
// hardware, so (unlike cpu_branch) this doesn't charge cycles itself --
// callers add the right cost based on `take`.
static void cpu_long_branch(Cpu *cpu, int take) {
    uint8_t hi = mem_read8(cpu->PC);
    uint8_t lo = mem_read8(cpu->PC + 1);
    cpu->PC += 2;
    int16_t offset = (int16_t)(((uint16_t)hi << 8) | lo);

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

static void cpu_push8(Cpu *cpu, uint8_t value) {
    cpu->S--;
    mem_write8(cpu->S, value);
}

static uint8_t cpu_pop8(Cpu *cpu) {
    uint8_t value = mem_read8(cpu->S);
    cpu->S++;
    return value;
}

// TFR/EXG postbyte register field: high nibble is source, low nibble is
// dest (TFR) or the other side of the swap (EXG). Only same-size pairs are
// defined by the 6809 spec; mixed 8/16-bit pairs and the reserved nibbles
// (0x6, 0x7, 0xC-0xF) are left unimplemented rather than guessed at, same
// as cpu_resolve_indexed()'s unimplemented submodes.
typedef enum { REG_SIZE_8, REG_SIZE_16, REG_SIZE_INVALID } RegSize;

static RegSize cpu_tfr_reg_size(uint8_t nibble) {
    switch (nibble) {
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5:
            return REG_SIZE_16; // D, X, Y, U, S, PC
        case 0x8: case 0x9: case 0xA: case 0xB:
            return REG_SIZE_8; // A, B, CC, DP
        default:
            return REG_SIZE_INVALID;
    }
}

static uint16_t cpu_get_reg16(Cpu *cpu, uint8_t nibble) {
    switch (nibble) {
        case 0x0: return cpu->D;
        case 0x1: return cpu->X;
        case 0x2: return cpu->Y;
        case 0x3: return cpu->U;
        case 0x4: return cpu->S;
        default:  return cpu->PC; // 0x5
    }
}

static void cpu_set_reg16(Cpu *cpu, uint8_t nibble, uint16_t value) {
    switch (nibble) {
        case 0x0: cpu->D = value; break;
        case 0x1: cpu->X = value; break;
        case 0x2: cpu->Y = value; break;
        case 0x3: cpu->U = value; break;
        case 0x4: cpu->S = value; break;
        default:  cpu->PC = value; break; // 0x5
    }
}

static uint8_t cpu_get_reg8(Cpu *cpu, uint8_t nibble) {
    switch (nibble) {
        case 0x8: return cpu->A;
        case 0x9: return cpu->B;
        case 0xA: return cpu->CC;
        default:  return cpu->DP; // 0xB
    }
}

static void cpu_set_reg8(Cpu *cpu, uint8_t nibble, uint8_t value) {
    switch (nibble) {
        case 0x8: cpu->A = value; break;
        case 0x9: cpu->B = value; break;
        case 0xA: cpu->CC = value; break;
        default:  cpu->DP = value; break; // 0xB
    }
}

// Draw_VL: X points at a vector list -- a count-minus-1 byte followed by
// that many (dy, dx) signed-byte pairs, each a delta from the current pen
// position. Intensity and BIOS-scale-register modeling are future work.
static void hle_draw_vl(Cpu *cpu) {
    uint16_t addr = cpu->X;
    uint8_t count = mem_read8(addr);
    addr++;

    // TODO: replace with real BIOS scale register once game/BIOS code drives Draw_VL.
    // Component bytes are DAC values, not pixels; 2x is a visible placeholder.
    // Origin maps Vectrex (0,0) to window center (tied to 600x400 SDL window).
    // Y is negated: Vectrex Y increases upward, SDL Y increases downward.
    const int scale    = 2;
    const int origin_x = 300;
    const int origin_y = 200;

    // Pass 1: walk the vector list to find the bounding box, without drawing.
    uint16_t scan_addr = addr;
    int x = 0, y = 0;
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    for (uint16_t i = 0; i <= count; i++) {
        int8_t dy = (int8_t)mem_read8(scan_addr);
        int8_t dx = (int8_t)mem_read8(scan_addr + 1);
        scan_addr += 2;

        x += dx * scale;
        y -= dy * scale;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }

    int center_x = (min_x + max_x) / 2;
    int center_y = (min_y + max_y) / 2;
    int shift_x = origin_x - center_x;
    int shift_y = origin_y - center_y;

    // Pass 2: redraw, offsetting every point so the bounding-box center
    // lands on (origin_x, origin_y) instead of the path's start vertex.
    int pen_x = shift_x, pen_y = shift_y;
    for (uint16_t i = 0; i <= count; i++) {
        int8_t dy = (int8_t)mem_read8(addr);
        int8_t dx = (int8_t)mem_read8(addr + 1);
        addr += 2;

        int new_x = pen_x + dx * scale;
        int new_y = pen_y - dy * scale;
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

// Intercepts known BIOS entry addresses before opcode fetch. Runs the native
// handler, then pops the return address off S (equivalent to RTS).
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
    [OP_LDB_IMM] = 2, [OP_LDB_DIR] = 4, [OP_LDB_EXT] = 5, [OP_LDB_IDX] = 4,
    [OP_LDX_IMM] = 3, [OP_LDX_DIR] = 5, [OP_LDX_EXT] = 6, [OP_LDX_IDX] = 5,
    [OP_LDU_IMM] = 3, [OP_LDU_DIR] = 5, [OP_LDU_EXT] = 6, [OP_LDU_IDX] = 5,
    [OP_LDD_IMM] = 3, [OP_LDD_DIR] = 5, [OP_LDD_EXT] = 6, [OP_LDD_IDX] = 5,

    // Indexed entries are the datasheet's base ,R cost. Real indexed timing
    // varies with the postbyte submode (offset bytes add cycles, auto
    // inc/dec by 2 adds one more, etc.) -- not modeled yet, consistent with
    // cpu_resolve_indexed() itself only covering the common submodes.
    [OP_STA_DIR] = 4, [OP_STA_EXT] = 5, [OP_STA_IDX] = 4,
    [OP_STB_DIR] = 4, [OP_STB_EXT] = 5, [OP_STB_IDX] = 4,
    [OP_STX_DIR] = 5, [OP_STX_EXT] = 6, [OP_STX_IDX] = 5,
    [OP_STU_DIR] = 5, [OP_STU_EXT] = 6, [OP_STU_IDX] = 5,
    [OP_STD_DIR] = 5, [OP_STD_EXT] = 6, [OP_STD_IDX] = 5,

    [OP_ADDA_IMM] = 2, [OP_ADDA_DIR] = 4, [OP_ADDA_EXT] = 5, [OP_ADDA_IDX] = 4,
    [OP_ADDB_IMM] = 2, [OP_ADDB_DIR] = 4, [OP_ADDB_EXT] = 5, [OP_ADDB_IDX] = 4,
    [OP_SUBA_IMM] = 2, [OP_SUBA_DIR] = 4, [OP_SUBA_EXT] = 5, [OP_SUBA_IDX] = 4,
    [OP_SUBB_IMM] = 2, [OP_SUBB_DIR] = 4, [OP_SUBB_EXT] = 5, [OP_SUBB_IDX] = 4,
    [OP_CMPA_IMM] = 2, [OP_CMPA_DIR] = 4, [OP_CMPA_EXT] = 5, [OP_CMPA_IDX] = 4,
    [OP_CMPB_IMM] = 2, [OP_CMPB_DIR] = 4, [OP_CMPB_EXT] = 5, [OP_CMPB_IDX] = 4,

    [OP_ANDA_IMM] = 2, [OP_ANDA_DIR] = 4, [OP_ANDA_EXT] = 5, [OP_ANDA_IDX] = 4,
    [OP_ANDB_IMM] = 2, [OP_ANDB_DIR] = 4, [OP_ANDB_EXT] = 5, [OP_ANDB_IDX] = 4,
    [OP_ORA_IMM] = 2, [OP_ORA_DIR] = 4, [OP_ORA_EXT] = 5, [OP_ORA_IDX] = 4,
    [OP_ORB_IMM] = 2, [OP_ORB_DIR] = 4, [OP_ORB_EXT] = 5, [OP_ORB_IDX] = 4,
    [OP_EORA_IMM] = 2, [OP_EORA_DIR] = 4, [OP_EORA_EXT] = 5, [OP_EORA_IDX] = 4,
    [OP_EORB_IMM] = 2, [OP_EORB_DIR] = 4, [OP_EORB_EXT] = 5, [OP_EORB_IDX] = 4,
    [OP_BITA_IMM] = 2, [OP_BITA_DIR] = 4, [OP_BITA_EXT] = 5, [OP_BITA_IDX] = 4,
    [OP_BITB_IMM] = 2, [OP_BITB_DIR] = 4, [OP_BITB_EXT] = 5, [OP_BITB_IDX] = 4,

    [OP_ADDD_IMM] = 4, [OP_ADDD_DIR] = 6, [OP_ADDD_EXT] = 7, [OP_ADDD_IDX] = 6,
    [OP_SUBD_IMM] = 4, [OP_SUBD_DIR] = 6, [OP_SUBD_EXT] = 7, [OP_SUBD_IDX] = 6,
    [OP_CMPX_IMM] = 4, [OP_CMPX_DIR] = 6, [OP_CMPX_EXT] = 7, [OP_CMPX_IDX] = 6,

    [OP_INCA] = 2, [OP_INCB] = 2, [OP_DECA] = 2, [OP_DECB] = 2,
    [OP_NEGA] = 2, [OP_NEGB] = 2, [OP_COMA] = 2, [OP_COMB] = 2,
    [OP_LSRA] = 2, [OP_LSRB] = 2, [OP_ASRA] = 2, [OP_ASRB] = 2,
    [OP_ASLA] = 2, [OP_ASLB] = 2, [OP_RORA] = 2, [OP_RORB] = 2,
    [OP_ROLA] = 2, [OP_ROLB] = 2, [OP_TSTA] = 2, [OP_TSTB] = 2,
    [OP_CLRA] = 2, [OP_CLRB] = 2,
    [OP_CLR_DIR] = 6, [OP_CLR_EXT] = 7, [OP_CLR_IDX] = 6,

    // Memory-addressed single-operand family: all share CLR's read-modify-
    // write cost per addressing mode, TST included (it doesn't write back,
    // but real hardware costs it identically to the others).
    [OP_NEG_DIR] = 6, [OP_NEG_EXT] = 7, [OP_NEG_IDX] = 6,
    [OP_COM_DIR] = 6, [OP_COM_EXT] = 7, [OP_COM_IDX] = 6,
    [OP_LSR_DIR] = 6, [OP_LSR_EXT] = 7, [OP_LSR_IDX] = 6,
    [OP_ASR_DIR] = 6, [OP_ASR_EXT] = 7, [OP_ASR_IDX] = 6,
    [OP_ASL_DIR] = 6, [OP_ASL_EXT] = 7, [OP_ASL_IDX] = 6,
    [OP_ROR_DIR] = 6, [OP_ROR_EXT] = 7, [OP_ROR_IDX] = 6,
    [OP_ROL_DIR] = 6, [OP_ROL_EXT] = 7, [OP_ROL_IDX] = 6,
    [OP_DEC_DIR] = 6, [OP_DEC_EXT] = 7, [OP_DEC_IDX] = 6,
    [OP_INC_DIR] = 6, [OP_INC_EXT] = 7, [OP_INC_IDX] = 6,
    [OP_TST_DIR] = 6, [OP_TST_EXT] = 7, [OP_TST_IDX] = 6,

    [OP_BRA] = 3, [OP_BCC] = 3, [OP_BCS] = 3, [OP_BNE] = 3, [OP_BEQ] = 3,
    [OP_BVC] = 3, [OP_BVS] = 3, [OP_BPL] = 3, [OP_BMI] = 3,
    [OP_BRN] = 3, [OP_BHI] = 3, [OP_BLS] = 3,
    [OP_BGE] = 3, [OP_BLT] = 3, [OP_BGT] = 3, [OP_BLE] = 3,
    [OP_BSR] = 7,

    // LBRA/LBSR are always taken, so (unlike the page-2 conditional long
    // branches below) their cost doesn't vary and belongs in this flat table.
    [OP_LBRA] = 5, [OP_LBSR] = 9,

    [OP_JMP_DIR] = 3, [OP_JMP_EXT] = 4, [OP_JMP_IDX] = 3,

    [OP_JSR_DIR] = 7, [OP_JSR_EXT] = 8, [OP_RTS] = 5,

    // PSHS/PULS cost (5 + 1 per byte transferred) depends on the postbyte's
    // register mask, so it's charged inline in their cases instead of here.
    [OP_TFR] = 6, [OP_EXG] = 8,
};

// Page-2 (0x10-prefixed) opcodes. Same numeric values as some page-1
// opcodes (e.g. OP2_LDS_IMM == OP_LDU_IMM's byte) -- that's expected, since
// the prefix byte is what selects which table applies.
static const uint8_t page2_cycles[256] = {
    [OP2_LDS_IMM] = 4, [OP2_LDS_DIR] = 6, [OP2_LDS_EXT] = 7,
    [OP2_STS_DIR] = 6, [OP2_STS_EXT] = 7,

    [OP2_CMPD_IMM] = 5, [OP2_CMPD_DIR] = 7, [OP2_CMPD_EXT] = 8, [OP2_CMPD_IDX] = 7,
    [OP2_CMPY_IMM] = 5, [OP2_CMPY_DIR] = 7, [OP2_CMPY_EXT] = 8, [OP2_CMPY_IDX] = 7,

    // Long conditional branches are the one case where taken/not-taken cost
    // genuinely differs on real 6809 hardware (5 not taken, 6 taken), so
    // their cost is charged inline in their cases instead of here.
};

// Page-3 (0x11-prefixed) opcodes.
static const uint8_t page3_cycles[256] = {
    [OP3_CMPU_IMM] = 5, [OP3_CMPU_DIR] = 7, [OP3_CMPU_EXT] = 8, [OP3_CMPU_IDX] = 7,
    [OP3_CMPS_IMM] = 5, [OP3_CMPS_DIR] = 7, [OP3_CMPS_EXT] = 8, [OP3_CMPS_IDX] = 7,
};

static int cpu_step_page2(Cpu *cpu, uint16_t prefix_pc) {
    uint8_t opcode2 = mem_read8(cpu->PC);
    cpu->PC++;

    cpu->cycles += page2_cycles[opcode2];

    switch (opcode2) {
        case OP2_LDS_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->S = value;
            set_nz16(cpu, value);
            return 1;
        }
        case OP2_LDS_DIR:
        case OP2_LDS_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode2 == OP2_LDS_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->S = value;
            set_nz16(cpu, value);
            return 1;
        }

        case OP2_STS_DIR:
        case OP2_STS_EXT: {
            uint16_t addr;
            if (!cpu_resolve_address(cpu, opcode2 == OP2_STS_DIR ? ADDR_DIRECT : ADDR_EXTENDED, &addr)) return 0;
            mem_write8(addr, (uint8_t)(cpu->S >> 8));
            mem_write8(addr + 1, (uint8_t)(cpu->S & 0xFF));
            return 1;
        }

        case OP2_CMPD_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_sub16(cpu, &cpu->D, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }
        case OP2_CMPD_DIR:
        case OP2_CMPD_EXT:
        case OP2_CMPD_IDX: {
            AddrMode mode = opcode2 == OP2_CMPD_DIR ? ADDR_DIRECT : opcode2 == OP2_CMPD_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_sub16(cpu, &cpu->D, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }

        case OP2_CMPY_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_sub16(cpu, &cpu->Y, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }
        case OP2_CMPY_DIR:
        case OP2_CMPY_EXT:
        case OP2_CMPY_IDX: {
            AddrMode mode = opcode2 == OP2_CMPY_DIR ? ADDR_DIRECT : opcode2 == OP2_CMPY_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_sub16(cpu, &cpu->Y, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }

        // Long conditional branches: same condition checks as their page-1
        // short-branch counterparts, just a 16-bit offset and (genuinely,
        // per real 6809 timing) a taken/not-taken cost difference.
        case OP2_LBCC: {
            int take = (cpu->CC & CC_C) == 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBCS: {
            int take = (cpu->CC & CC_C) != 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBNE: {
            int take = (cpu->CC & CC_Z) == 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBEQ: {
            int take = (cpu->CC & CC_Z) != 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBVC: {
            int take = (cpu->CC & CC_V) == 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBVS: {
            int take = (cpu->CC & CC_V) != 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBPL: {
            int take = (cpu->CC & CC_N) == 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }
        case OP2_LBMI: {
            int take = (cpu->CC & CC_N) != 0;
            cpu->cycles += take ? 6 : 5;
            cpu_long_branch(cpu, take);
            return 1;
        }

        default:
            printf("unimplemented opcode 0x10 0x%02X at PC=0x%04X\n", opcode2, prefix_pc);
            return 0;
    }
}

static int cpu_step_page3(Cpu *cpu, uint16_t prefix_pc) {
    uint8_t opcode3 = mem_read8(cpu->PC);
    cpu->PC++;

    cpu->cycles += page3_cycles[opcode3];

    switch (opcode3) {
        case OP3_CMPU_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_sub16(cpu, &cpu->U, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }
        case OP3_CMPU_DIR:
        case OP3_CMPU_EXT:
        case OP3_CMPU_IDX: {
            AddrMode mode = opcode3 == OP3_CMPU_DIR ? ADDR_DIRECT : opcode3 == OP3_CMPU_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_sub16(cpu, &cpu->U, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }

        case OP3_CMPS_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_sub16(cpu, &cpu->S, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }
        case OP3_CMPS_DIR:
        case OP3_CMPS_EXT:
        case OP3_CMPS_IDX: {
            AddrMode mode = opcode3 == OP3_CMPS_DIR ? ADDR_DIRECT : opcode3 == OP3_CMPS_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_sub16(cpu, &cpu->S, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }

        default:
            printf("unimplemented opcode 0x11 0x%02X at PC=0x%04X\n", opcode3, prefix_pc);
            return 0;
    }
}

int cpu_step(Cpu *cpu) {
    // No cycle cost is charged for HLE -- these replace routines of variable
    // real timing, not something honestly chargeable from the datasheet.
    if (cpu_try_hle(cpu)) {
        return 1;
    }

    uint16_t opcode_pc = cpu->PC;
    uint8_t opcode = mem_read8(cpu->PC);
    cpu->PC++;

    if (opcode == OP_PAGE2_PREFIX) {
        return cpu_step_page2(cpu, opcode_pc);
    }
    if (opcode == OP_PAGE3_PREFIX) {
        return cpu_step_page3(cpu, opcode_pc);
    }

    // 6809 branches don't vary cost by taken/not-taken (unlike some other
    // 8-bit CPUs), so it's safe to charge cycles up front for all opcodes.
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
        case OP_LDB_EXT:
        case OP_LDB_IDX: {
            AddrMode mode = opcode == OP_LDB_DIR ? ADDR_DIRECT : opcode == OP_LDB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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

        case OP_LDD_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->D = value;
            set_nz16(cpu, value);
            return 1;
        }
        case OP_LDD_DIR:
        case OP_LDD_EXT:
        case OP_LDD_IDX: {
            AddrMode mode = opcode == OP_LDD_DIR ? ADDR_DIRECT : opcode == OP_LDD_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->D = value;
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
        case OP_LDU_EXT:
        case OP_LDU_IDX: {
            AddrMode mode = opcode == OP_LDU_DIR ? ADDR_DIRECT : opcode == OP_LDU_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_STB_EXT:
        case OP_STB_IDX: {
            AddrMode mode = opcode == OP_STB_DIR ? ADDR_DIRECT : opcode == OP_STB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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

        case OP_STD_DIR:
        case OP_STD_EXT:
        case OP_STD_IDX: {
            AddrMode mode = opcode == OP_STD_DIR ? ADDR_DIRECT : opcode == OP_STD_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, (uint8_t)(cpu->D >> 8));
            mem_write8(addr + 1, (uint8_t)(cpu->D & 0xFF));
            return 1;
        }

        case OP_STU_DIR:
        case OP_STU_EXT:
        case OP_STU_IDX: {
            AddrMode mode = opcode == OP_STU_DIR ? ADDR_DIRECT : opcode == OP_STU_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_ADDA_EXT:
        case OP_ADDA_IDX: {
            AddrMode mode = opcode == OP_ADDA_DIR ? ADDR_DIRECT : opcode == OP_ADDA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_ADDB_EXT:
        case OP_ADDB_IDX: {
            AddrMode mode = opcode == OP_ADDB_DIR ? ADDR_DIRECT : opcode == OP_ADDB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_SUBA_EXT:
        case OP_SUBA_IDX: {
            AddrMode mode = opcode == OP_SUBA_DIR ? ADDR_DIRECT : opcode == OP_SUBA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_SUBB_EXT:
        case OP_SUBB_IDX: {
            AddrMode mode = opcode == OP_SUBB_DIR ? ADDR_DIRECT : opcode == OP_SUBB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_CMPA_EXT:
        case OP_CMPA_IDX: {
            AddrMode mode = opcode == OP_CMPA_DIR ? ADDR_DIRECT : opcode == OP_CMPA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
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
        case OP_CMPB_EXT:
        case OP_CMPB_IDX: {
            AddrMode mode = opcode == OP_CMPB_DIR ? ADDR_DIRECT : opcode == OP_CMPB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_sub8(cpu, &cpu->B, mem_read8(addr), 0);
            return 1;
        }

        case OP_ANDA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_and8(cpu, &cpu->A, operand, 1);
            return 1;
        }
        case OP_ANDA_DIR:
        case OP_ANDA_EXT:
        case OP_ANDA_IDX: {
            AddrMode mode = opcode == OP_ANDA_DIR ? ADDR_DIRECT : opcode == OP_ANDA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_and8(cpu, &cpu->A, mem_read8(addr), 1);
            return 1;
        }

        case OP_ANDB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_and8(cpu, &cpu->B, operand, 1);
            return 1;
        }
        case OP_ANDB_DIR:
        case OP_ANDB_EXT:
        case OP_ANDB_IDX: {
            AddrMode mode = opcode == OP_ANDB_DIR ? ADDR_DIRECT : opcode == OP_ANDB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_and8(cpu, &cpu->B, mem_read8(addr), 1);
            return 1;
        }

        case OP_ORA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_or8(cpu, &cpu->A, operand);
            return 1;
        }
        case OP_ORA_DIR:
        case OP_ORA_EXT:
        case OP_ORA_IDX: {
            AddrMode mode = opcode == OP_ORA_DIR ? ADDR_DIRECT : opcode == OP_ORA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_or8(cpu, &cpu->A, mem_read8(addr));
            return 1;
        }

        case OP_ORB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_or8(cpu, &cpu->B, operand);
            return 1;
        }
        case OP_ORB_DIR:
        case OP_ORB_EXT:
        case OP_ORB_IDX: {
            AddrMode mode = opcode == OP_ORB_DIR ? ADDR_DIRECT : opcode == OP_ORB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_or8(cpu, &cpu->B, mem_read8(addr));
            return 1;
        }

        case OP_EORA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_eor8(cpu, &cpu->A, operand);
            return 1;
        }
        case OP_EORA_DIR:
        case OP_EORA_EXT:
        case OP_EORA_IDX: {
            AddrMode mode = opcode == OP_EORA_DIR ? ADDR_DIRECT : opcode == OP_EORA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_eor8(cpu, &cpu->A, mem_read8(addr));
            return 1;
        }

        case OP_EORB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_eor8(cpu, &cpu->B, operand);
            return 1;
        }
        case OP_EORB_DIR:
        case OP_EORB_EXT:
        case OP_EORB_IDX: {
            AddrMode mode = opcode == OP_EORB_DIR ? ADDR_DIRECT : opcode == OP_EORB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_eor8(cpu, &cpu->B, mem_read8(addr));
            return 1;
        }

        case OP_BITA_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_and8(cpu, &cpu->A, operand, 0);
            return 1;
        }
        case OP_BITA_DIR:
        case OP_BITA_EXT:
        case OP_BITA_IDX: {
            AddrMode mode = opcode == OP_BITA_DIR ? ADDR_DIRECT : opcode == OP_BITA_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_and8(cpu, &cpu->A, mem_read8(addr), 0);
            return 1;
        }

        case OP_BITB_IMM: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            cpu_and8(cpu, &cpu->B, operand, 0);
            return 1;
        }
        case OP_BITB_DIR:
        case OP_BITB_EXT:
        case OP_BITB_IDX: {
            AddrMode mode = opcode == OP_BITB_DIR ? ADDR_DIRECT : opcode == OP_BITB_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_and8(cpu, &cpu->B, mem_read8(addr), 0);
            return 1;
        }

        case OP_ADDD_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_add16(cpu, &cpu->D, ((uint16_t)hi << 8) | lo);
            return 1;
        }
        case OP_ADDD_DIR:
        case OP_ADDD_EXT:
        case OP_ADDD_IDX: {
            AddrMode mode = opcode == OP_ADDD_DIR ? ADDR_DIRECT : opcode == OP_ADDD_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_add16(cpu, &cpu->D, ((uint16_t)hi << 8) | lo);
            return 1;
        }

        case OP_SUBD_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_sub16(cpu, &cpu->D, ((uint16_t)hi << 8) | lo, 1);
            return 1;
        }
        case OP_SUBD_DIR:
        case OP_SUBD_EXT:
        case OP_SUBD_IDX: {
            AddrMode mode = opcode == OP_SUBD_DIR ? ADDR_DIRECT : opcode == OP_SUBD_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_sub16(cpu, &cpu->D, ((uint16_t)hi << 8) | lo, 1);
            return 1;
        }

        case OP_CMPX_IMM: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            cpu_sub16(cpu, &cpu->X, ((uint16_t)hi << 8) | lo, 0);
            return 1;
        }
        case OP_CMPX_DIR:
        case OP_CMPX_EXT:
        case OP_CMPX_IDX: {
            AddrMode mode = opcode == OP_CMPX_DIR ? ADDR_DIRECT : opcode == OP_CMPX_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            cpu_sub16(cpu, &cpu->X, ((uint16_t)hi << 8) | lo, 0);
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

        // Register forms of the memory-addressed single-operand family
        // above: same value-based helpers, just operating on A/B directly
        // instead of a resolved memory address.
        case OP_NEGA:
            cpu->A = cpu_neg8(cpu, cpu->A);
            return 1;
        case OP_NEGB:
            cpu->B = cpu_neg8(cpu, cpu->B);
            return 1;
        case OP_COMA:
            cpu->A = cpu_com8(cpu, cpu->A);
            return 1;
        case OP_COMB:
            cpu->B = cpu_com8(cpu, cpu->B);
            return 1;
        case OP_LSRA:
            cpu->A = cpu_lsr8(cpu, cpu->A);
            return 1;
        case OP_LSRB:
            cpu->B = cpu_lsr8(cpu, cpu->B);
            return 1;
        case OP_ASRA:
            cpu->A = cpu_asr8(cpu, cpu->A);
            return 1;
        case OP_ASRB:
            cpu->B = cpu_asr8(cpu, cpu->B);
            return 1;
        case OP_ASLA:
            cpu->A = cpu_asl8(cpu, cpu->A);
            return 1;
        case OP_ASLB:
            cpu->B = cpu_asl8(cpu, cpu->B);
            return 1;
        case OP_RORA:
            cpu->A = cpu_ror8(cpu, cpu->A);
            return 1;
        case OP_RORB:
            cpu->B = cpu_ror8(cpu, cpu->B);
            return 1;
        case OP_ROLA:
            cpu->A = cpu_rol8(cpu, cpu->A);
            return 1;
        case OP_ROLB:
            cpu->B = cpu_rol8(cpu, cpu->B);
            return 1;
        case OP_TSTA:
            cpu_tst8(cpu, cpu->A);
            return 1;
        case OP_TSTB:
            cpu_tst8(cpu, cpu->B);
            return 1;

        // CLR's result is always 0 regardless of the prior value, so flags
        // are fixed too: N=0, Z=1, V=0, C=0.
        case OP_CLRA:
            cpu->A = 0;
            set_nz8(cpu, 0);
            cpu->CC &= ~(CC_V | CC_C);
            return 1;
        case OP_CLRB:
            cpu->B = 0;
            set_nz8(cpu, 0);
            cpu->CC &= ~(CC_V | CC_C);
            return 1;
        case OP_CLR_DIR:
        case OP_CLR_EXT:
        case OP_CLR_IDX: {
            AddrMode mode = opcode == OP_CLR_DIR ? ADDR_DIRECT : opcode == OP_CLR_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, 0);
            set_nz8(cpu, 0);
            cpu->CC &= ~(CC_V | CC_C);
            return 1;
        }

        // Memory-addressed single-operand family: same addressing-mode
        // resolution shape as CLR above, just read-modify-write via one of
        // the value-based helpers instead of writing a fixed 0.
        case OP_NEG_DIR:
        case OP_NEG_EXT:
        case OP_NEG_IDX: {
            AddrMode mode = opcode == OP_NEG_DIR ? ADDR_DIRECT : opcode == OP_NEG_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_neg8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_COM_DIR:
        case OP_COM_EXT:
        case OP_COM_IDX: {
            AddrMode mode = opcode == OP_COM_DIR ? ADDR_DIRECT : opcode == OP_COM_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_com8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_LSR_DIR:
        case OP_LSR_EXT:
        case OP_LSR_IDX: {
            AddrMode mode = opcode == OP_LSR_DIR ? ADDR_DIRECT : opcode == OP_LSR_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_lsr8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_ASR_DIR:
        case OP_ASR_EXT:
        case OP_ASR_IDX: {
            AddrMode mode = opcode == OP_ASR_DIR ? ADDR_DIRECT : opcode == OP_ASR_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_asr8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_ASL_DIR:
        case OP_ASL_EXT:
        case OP_ASL_IDX: {
            AddrMode mode = opcode == OP_ASL_DIR ? ADDR_DIRECT : opcode == OP_ASL_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_asl8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_ROR_DIR:
        case OP_ROR_EXT:
        case OP_ROR_IDX: {
            AddrMode mode = opcode == OP_ROR_DIR ? ADDR_DIRECT : opcode == OP_ROR_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_ror8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_ROL_DIR:
        case OP_ROL_EXT:
        case OP_ROL_IDX: {
            AddrMode mode = opcode == OP_ROL_DIR ? ADDR_DIRECT : opcode == OP_ROL_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_rol8(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_DEC_DIR:
        case OP_DEC_EXT:
        case OP_DEC_IDX: {
            AddrMode mode = opcode == OP_DEC_DIR ? ADDR_DIRECT : opcode == OP_DEC_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_dec8_value(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_INC_DIR:
        case OP_INC_EXT:
        case OP_INC_IDX: {
            AddrMode mode = opcode == OP_INC_DIR ? ADDR_DIRECT : opcode == OP_INC_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            mem_write8(addr, cpu_inc8_value(cpu, mem_read8(addr)));
            return 1;
        }
        case OP_TST_DIR:
        case OP_TST_EXT:
        case OP_TST_IDX: {
            AddrMode mode = opcode == OP_TST_DIR ? ADDR_DIRECT : opcode == OP_TST_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu_tst8(cpu, mem_read8(addr));
            return 1;
        }

        case OP_BRA:
            cpu_branch(cpu, 1);
            return 1;
        case OP_BRN:
            cpu_branch(cpu, 0);
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

        // Unsigned ordering (C/Z only): BHI = !C && !Z, BLS = C || Z.
        case OP_BHI:
            cpu_branch(cpu, (cpu->CC & (CC_C | CC_Z)) == 0);
            return 1;
        case OP_BLS:
            cpu_branch(cpu, (cpu->CC & (CC_C | CC_Z)) != 0);
            return 1;

        // Signed ordering (N/V only): BGE = N==V, BLT = N!=V,
        // BGT = !Z && N==V, BLE = Z || N!=V.
        case OP_BGE:
        case OP_BLT: {
            int n_eq_v = ((cpu->CC & CC_N) != 0) == ((cpu->CC & CC_V) != 0);
            cpu_branch(cpu, opcode == OP_BGE ? n_eq_v : !n_eq_v);
            return 1;
        }
        case OP_BGT:
        case OP_BLE: {
            int n_eq_v = ((cpu->CC & CC_N) != 0) == ((cpu->CC & CC_V) != 0);
            int z = (cpu->CC & CC_Z) != 0;
            cpu_branch(cpu, opcode == OP_BGT ? (!z && n_eq_v) : (z || !n_eq_v));
            return 1;
        }

        case OP_LBRA:
            cpu_long_branch(cpu, 1);
            return 1;

        case OP_BSR: {
            int8_t offset = (int8_t)mem_read8(cpu->PC);
            cpu->PC++;
            cpu_push16(cpu, cpu->PC); // return address: PC after the full BSR instruction
            cpu->PC = (uint16_t)(cpu->PC + offset);
            return 1;
        }

        case OP_LBSR: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            int16_t offset = (int16_t)(((uint16_t)hi << 8) | lo);
            cpu_push16(cpu, cpu->PC); // return address: PC after the full LBSR instruction
            cpu->PC = (uint16_t)(cpu->PC + offset);
            return 1;
        }

        case OP_JMP_DIR:
        case OP_JMP_EXT:
        case OP_JMP_IDX: {
            AddrMode mode = opcode == OP_JMP_DIR ? ADDR_DIRECT : opcode == OP_JMP_EXT ? ADDR_EXTENDED : ADDR_INDEXED;
            uint16_t addr;
            if (!cpu_resolve_address(cpu, mode, &addr)) return 0;
            cpu->PC = addr;
            return 1;
        }

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

        case OP_TFR: {
            uint8_t postbyte = mem_read8(cpu->PC);
            cpu->PC++;
            uint8_t src = (postbyte >> 4) & 0x0F;
            uint8_t dst = postbyte & 0x0F;
            RegSize src_size = cpu_tfr_reg_size(src);
            RegSize dst_size = cpu_tfr_reg_size(dst);

            if (src_size == REG_SIZE_INVALID || dst_size == REG_SIZE_INVALID || src_size != dst_size) {
                printf("unimplemented/invalid TFR postbyte 0x%02X at PC=0x%04X\n", postbyte, (uint16_t)(cpu->PC - 1));
                return 0;
            }

            if (src_size == REG_SIZE_16) {
                cpu_set_reg16(cpu, dst, cpu_get_reg16(cpu, src));
            } else {
                cpu_set_reg8(cpu, dst, cpu_get_reg8(cpu, src));
            }
            return 1;
        }

        case OP_EXG: {
            uint8_t postbyte = mem_read8(cpu->PC);
            cpu->PC++;
            uint8_t r1 = (postbyte >> 4) & 0x0F;
            uint8_t r2 = postbyte & 0x0F;
            RegSize size1 = cpu_tfr_reg_size(r1);
            RegSize size2 = cpu_tfr_reg_size(r2);

            if (size1 == REG_SIZE_INVALID || size2 == REG_SIZE_INVALID || size1 != size2) {
                printf("unimplemented/invalid EXG postbyte 0x%02X at PC=0x%04X\n", postbyte, (uint16_t)(cpu->PC - 1));
                return 0;
            }

            if (size1 == REG_SIZE_16) {
                uint16_t tmp = cpu_get_reg16(cpu, r1);
                cpu_set_reg16(cpu, r1, cpu_get_reg16(cpu, r2));
                cpu_set_reg16(cpu, r2, tmp);
            } else {
                uint8_t tmp = cpu_get_reg8(cpu, r1);
                cpu_set_reg8(cpu, r1, cpu_get_reg8(cpu, r2));
                cpu_set_reg8(cpu, r2, tmp);
            }
            return 1;
        }

        // PSHS/PULS transfer order is fixed by the postbyte's bit position,
        // not the bit-scan order below: PSHS pushes PC,U,Y,X,DP,B,A,CC (high
        // bit first, so CC ends up on top of the stack); PULS pulls the
        // mirror image, CC,A,B,DP,X,Y,U,PC (low bit first). Cost is 5 base
        // cycles plus 1 per byte actually transferred.
        case OP_PSHS: {
            uint8_t mask = mem_read8(cpu->PC);
            cpu->PC++;
            int bytes = 0;

            if (mask & 0x80) { cpu_push16(cpu, cpu->PC); bytes += 2; }
            if (mask & 0x40) { cpu_push16(cpu, cpu->U);  bytes += 2; }
            if (mask & 0x20) { cpu_push16(cpu, cpu->Y);  bytes += 2; }
            if (mask & 0x10) { cpu_push16(cpu, cpu->X);  bytes += 2; }
            if (mask & 0x08) { cpu_push8(cpu, cpu->DP);  bytes += 1; }
            if (mask & 0x04) { cpu_push8(cpu, cpu->B);   bytes += 1; }
            if (mask & 0x02) { cpu_push8(cpu, cpu->A);   bytes += 1; }
            if (mask & 0x01) { cpu_push8(cpu, cpu->CC);  bytes += 1; }

            cpu->cycles += 5 + bytes;
            return 1;
        }

        case OP_PULS: {
            uint8_t mask = mem_read8(cpu->PC);
            cpu->PC++;
            int bytes = 0;

            if (mask & 0x01) { cpu->CC = cpu_pop8(cpu);  bytes += 1; }
            if (mask & 0x02) { cpu->A  = cpu_pop8(cpu);  bytes += 1; }
            if (mask & 0x04) { cpu->B  = cpu_pop8(cpu);  bytes += 1; }
            if (mask & 0x08) { cpu->DP = cpu_pop8(cpu);  bytes += 1; }
            if (mask & 0x10) { cpu->X  = cpu_pop16(cpu); bytes += 2; }
            if (mask & 0x20) { cpu->Y  = cpu_pop16(cpu); bytes += 2; }
            if (mask & 0x40) { cpu->U  = cpu_pop16(cpu); bytes += 2; }
            if (mask & 0x80) { cpu->PC = cpu_pop16(cpu); bytes += 2; }

            cpu->cycles += 5 + bytes;
            return 1;
        }

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
