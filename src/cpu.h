#ifndef CPU_H
#define CPU_H

#include <stdint.h>

// Condition Code Register bits (Motorola 6809)
#define CC_C 0x01 // Carry
#define CC_V 0x02 // Overflow
#define CC_Z 0x04 // Zero
#define CC_N 0x08 // Negative
#define CC_I 0x10 // IRQ mask
#define CC_H 0x20 // Half carry
#define CC_F 0x40 // FIRQ mask
#define CC_E 0x80 // Entire state (stacked on interrupt)

#define RESET_VECTOR 0xFFFE

// Opcodes
#define OP_LDA_IMM 0x86
#define OP_LDA_DIR 0x96
#define OP_LDA_EXT 0xB6
#define OP_LDA_IDX 0xA6

#define OP_LDB_IMM 0xC6
#define OP_LDB_DIR 0xD6
#define OP_LDB_EXT 0xF6

#define OP_LDX_IMM 0x8E
#define OP_LDX_DIR 0x9E
#define OP_LDX_EXT 0xBE
#define OP_LDX_IDX 0xAE

#define OP_LDU_IMM 0xCE
#define OP_LDU_DIR 0xDE
#define OP_LDU_EXT 0xFE

#define OP_STA_DIR 0x97
#define OP_STA_EXT 0xB7
#define OP_STA_IDX 0xA7

#define OP_STB_DIR 0xD7
#define OP_STB_EXT 0xF7

#define OP_STX_DIR 0x9F
#define OP_STX_EXT 0xBF
#define OP_STX_IDX 0xAF

#define OP_STU_DIR 0xDF
#define OP_STU_EXT 0xFF

#define OP_ADDA_IMM 0x8B
#define OP_ADDA_DIR 0x9B
#define OP_ADDA_EXT 0xBB

#define OP_ADDB_IMM 0xCB
#define OP_ADDB_DIR 0xDB
#define OP_ADDB_EXT 0xFB

#define OP_SUBA_IMM 0x80
#define OP_SUBA_DIR 0x90
#define OP_SUBA_EXT 0xB0

#define OP_SUBB_IMM 0xC0
#define OP_SUBB_DIR 0xD0
#define OP_SUBB_EXT 0xF0

#define OP_CMPA_IMM 0x81
#define OP_CMPA_DIR 0x91
#define OP_CMPA_EXT 0xB1

#define OP_CMPB_IMM 0xC1
#define OP_CMPB_DIR 0xD1
#define OP_CMPB_EXT 0xF1

#define OP_INCA 0x4C
#define OP_INCB 0x5C
#define OP_DECA 0x4A
#define OP_DECB 0x5A

#define OP_BRA 0x20
#define OP_BCC 0x24
#define OP_BCS 0x25
#define OP_BNE 0x26
#define OP_BEQ 0x27
#define OP_BVC 0x28
#define OP_BVS 0x29
#define OP_BPL 0x2A
#define OP_BMI 0x2B

#define OP_JSR_DIR 0x9D
#define OP_JSR_EXT 0xBD
#define OP_RTS 0x39

// HLE (high-level emulation) BIOS routine addresses. When PC lands on one of
// these, cpu_step() calls a native handler instead of executing whatever
// 6809 bytes are actually sitting there, then simulates the RTS.
#define HLE_DRAW_VL     0xF3DD
// Confirmed against the real loaded BIOS: $F192 opens LDX/LEAX/STX (a
// memory-counter increment idiom) then BSR / LDA #imm / CMPA direct / BEQ -4
// -- a textbook poll-until-flag-matches loop, i.e. genuinely a wait routine.
// $F2EB, by contrast, is just ordinary sequential load/store/call code with
// no wait-loop shape -- see dump_bios_bytes() output in main.c.
#define HLE_WAIT_RECAL  0xF192

typedef enum {
    ADDR_DIRECT,
    ADDR_EXTENDED,
    ADDR_INDEXED
} AddrMode;

typedef struct {
    // Struct/uint16_t aliasing here assumes a little-endian target (x86/x86_64).
    union {
        struct {
            uint8_t B; // low byte
            uint8_t A; // high byte
        };
        uint16_t D;
    };

    uint16_t X;
    uint16_t Y;
    uint16_t U;
    uint16_t S;
    uint16_t PC;

    uint8_t DP; // Direct page register
    uint8_t CC; // Condition code register (packed flags)

    uint64_t cycles; // total 6809 clock cycles executed since reset
} Cpu;

void cpu_reset(Cpu *cpu);
void cpu_print_state(const Cpu *cpu);
int cpu_step(Cpu *cpu);

// Debug-only: temporarily disable/enable HLE BIOS interception so real ROM
// bytes execute even at a normally-hooked address (see main.c's BIOS tracer).
void cpu_set_hle_enabled(int enabled);

#endif // CPU_H
