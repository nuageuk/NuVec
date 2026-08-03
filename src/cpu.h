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
#define IRQ_VECTOR   0xFFF8

#define OP_LDA_IMM 0x86
#define OP_LDA_DIR 0x96
#define OP_LDA_EXT 0xB6
#define OP_LDA_IDX 0xA6

#define OP_LDB_IMM 0xC6
#define OP_LDB_DIR 0xD6
#define OP_LDB_EXT 0xF6
#define OP_LDB_IDX 0xE6

#define OP_LDX_IMM 0x8E
#define OP_LDX_DIR 0x9E
#define OP_LDX_EXT 0xBE
#define OP_LDX_IDX 0xAE

#define OP_LDU_IMM 0xCE
#define OP_LDU_DIR 0xDE
#define OP_LDU_EXT 0xFE
#define OP_LDU_IDX 0xEE

#define OP_LDD_IMM 0xCC
#define OP_LDD_DIR 0xDC
#define OP_LDD_EXT 0xFC
#define OP_LDD_IDX 0xEC

#define OP_STA_DIR 0x97
#define OP_STA_EXT 0xB7
#define OP_STA_IDX 0xA7

#define OP_STB_DIR 0xD7
#define OP_STB_EXT 0xF7
#define OP_STB_IDX 0xE7

#define OP_STX_DIR 0x9F
#define OP_STX_EXT 0xBF
#define OP_STX_IDX 0xAF

#define OP_STU_DIR 0xDF
#define OP_STU_EXT 0xFF
#define OP_STU_IDX 0xEF

#define OP_STD_DIR 0xDD
#define OP_STD_EXT 0xFD
#define OP_STD_IDX 0xED

#define OP_ADDA_IMM 0x8B
#define OP_ADDA_DIR 0x9B
#define OP_ADDA_EXT 0xBB
#define OP_ADDA_IDX 0xAB

#define OP_ADCA_IMM 0x89

#define OP_ADDB_IMM 0xCB
#define OP_ADDB_DIR 0xDB
#define OP_ADDB_EXT 0xFB
#define OP_ADDB_IDX 0xEB

#define OP_SUBA_IMM 0x80
#define OP_SUBA_DIR 0x90
#define OP_SUBA_EXT 0xB0
#define OP_SUBA_IDX 0xA0

#define OP_SUBB_IMM 0xC0
#define OP_SUBB_DIR 0xD0
#define OP_SUBB_EXT 0xF0
#define OP_SUBB_IDX 0xE0

#define OP_CMPA_IMM 0x81
#define OP_CMPA_DIR 0x91
#define OP_CMPA_EXT 0xB1
#define OP_CMPA_IDX 0xA1

#define OP_CMPB_IMM 0xC1
#define OP_CMPB_DIR 0xD1
#define OP_CMPB_EXT 0xF1
#define OP_CMPB_IDX 0xE1

#define OP_ANDA_IMM 0x84
#define OP_ANDA_DIR 0x94
#define OP_ANDA_EXT 0xB4
#define OP_ANDA_IDX 0xA4

#define OP_ANDB_IMM 0xC4
#define OP_ANDB_DIR 0xD4
#define OP_ANDB_EXT 0xF4
#define OP_ANDB_IDX 0xE4

#define OP_ORA_IMM 0x8A
#define OP_ORA_DIR 0x9A
#define OP_ORA_EXT 0xBA
#define OP_ORA_IDX 0xAA

#define OP_ORB_IMM 0xCA
#define OP_ORB_DIR 0xDA
#define OP_ORB_EXT 0xFA
#define OP_ORB_IDX 0xEA

#define OP_EORA_IMM 0x88
#define OP_EORA_DIR 0x98
#define OP_EORA_EXT 0xB8
#define OP_EORA_IDX 0xA8

#define OP_EORB_IMM 0xC8
#define OP_EORB_DIR 0xD8
#define OP_EORB_EXT 0xF8
#define OP_EORB_IDX 0xE8

#define OP_BITA_IMM 0x85
#define OP_BITA_DIR 0x95
#define OP_BITA_EXT 0xB5
#define OP_BITA_IDX 0xA5

#define OP_BITB_IMM 0xC5
#define OP_BITB_DIR 0xD5
#define OP_BITB_EXT 0xF5
#define OP_BITB_IDX 0xE5

#define OP_ADDD_IMM 0xC3
#define OP_ADDD_DIR 0xD3
#define OP_ADDD_EXT 0xF3
#define OP_ADDD_IDX 0xE3

#define OP_SUBD_IMM 0x83
#define OP_SUBD_DIR 0x93
#define OP_SUBD_EXT 0xB3
#define OP_SUBD_IDX 0xA3

#define OP_CMPX_IMM 0x8C
#define OP_CMPX_DIR 0x9C
#define OP_CMPX_EXT 0xBC
#define OP_CMPX_IDX 0xAC

#define OP_INCA 0x4C
#define OP_INCB 0x5C
#define OP_DECA 0x4A
#define OP_DECB 0x5A

#define OP_NEGA 0x40
#define OP_NEGB 0x50
#define OP_COMA 0x43
#define OP_COMB 0x53
#define OP_LSRA 0x44
#define OP_LSRB 0x54
#define OP_RORA 0x46
#define OP_RORB 0x56
#define OP_ASRA 0x47
#define OP_ASRB 0x57
#define OP_ASLA 0x48
#define OP_ASLB 0x58
#define OP_ROLA 0x49
#define OP_ROLB 0x59
#define OP_TSTA 0x4D
#define OP_TSTB 0x5D

#define OP_CLRA 0x4F
#define OP_CLRB 0x5F
#define OP_CLR_DIR 0x0F
#define OP_CLR_EXT 0x7F
#define OP_CLR_IDX 0x6F

// Memory-addressed single-operand family, same direct/extended/indexed
// opcode-numbering pattern as CLR/JMP above (direct=0x0_, indexed=0x6_,
// extended=0x7_ sharing the same low nibble).
#define OP_NEG_DIR 0x00
#define OP_NEG_EXT 0x70
#define OP_NEG_IDX 0x60

#define OP_COM_DIR 0x03
#define OP_COM_EXT 0x73
#define OP_COM_IDX 0x63

#define OP_LSR_DIR 0x04
#define OP_LSR_EXT 0x74
#define OP_LSR_IDX 0x64

#define OP_ROR_DIR 0x06
#define OP_ROR_EXT 0x76
#define OP_ROR_IDX 0x66

#define OP_ASR_DIR 0x07
#define OP_ASR_EXT 0x77
#define OP_ASR_IDX 0x67

#define OP_ASL_DIR 0x08
#define OP_ASL_EXT 0x78
#define OP_ASL_IDX 0x68

#define OP_ROL_DIR 0x09
#define OP_ROL_EXT 0x79
#define OP_ROL_IDX 0x69

#define OP_DEC_DIR 0x0A
#define OP_DEC_EXT 0x7A
#define OP_DEC_IDX 0x6A

#define OP_INC_DIR 0x0C
#define OP_INC_EXT 0x7C
#define OP_INC_IDX 0x6C

#define OP_TST_DIR 0x0D
#define OP_TST_EXT 0x7D
#define OP_TST_IDX 0x6D

#define OP_BRA 0x20
#define OP_BRN 0x21
#define OP_BHI 0x22
#define OP_BLS 0x23
#define OP_BCC 0x24
#define OP_BCS 0x25
#define OP_BNE 0x26
#define OP_BEQ 0x27
#define OP_BVC 0x28
#define OP_BVS 0x29
#define OP_BPL 0x2A
#define OP_BMI 0x2B
#define OP_BGE 0x2C
#define OP_BLT 0x2D
#define OP_BGT 0x2E
#define OP_BLE 0x2F

#define OP_JSR_DIR 0x9D
#define OP_JSR_EXT 0xBD
#define OP_RTS 0x39
#define OP_RTI 0x3B
#define OP_CWAI 0x3C
#define OP_NOP 0x12

#define OP_ANDCC 0x1C
#define OP_ORCC 0x1A
#define OP_ABX 0x3A
#define OP_MUL 0x3D

#define OP_BSR 0x8D
#define OP_LBRA 0x16
#define OP_LBSR 0x17

#define OP_JMP_DIR 0x0E
#define OP_JMP_EXT 0x7E
#define OP_JMP_IDX 0x6E

// LEAX/LEAY/LEAS/LEAU: indexed-only (no direct/extended/immediate form) --
// compute the indexed effective address and load it into the register
// without dereferencing memory at that address.
#define OP_LEAX 0x30
#define OP_LEAY 0x31
#define OP_LEAS 0x32
#define OP_LEAU 0x33

#define OP_EXG 0x1E
#define OP_TFR 0x1F
#define OP_PSHS 0x34
#define OP_PULS 0x35

// Page-2/3 prefix bytes: the next fetched byte selects an opcode from a
// separate extended table, distinct from (and overlapping in numeric value
// with) the page-1 table above.
#define OP_PAGE2_PREFIX 0x10
#define OP_PAGE3_PREFIX 0x11

// Page-2 (0x10-prefixed) opcodes.
#define OP2_LDS_IMM 0xCE
#define OP2_LDS_DIR 0xDE
#define OP2_LDS_EXT 0xFE
#define OP2_STS_DIR 0xDF
#define OP2_STS_EXT 0xFF

// LDY/STY reuse LDX/STX's own page-1 secondary bytes (X and Y are
// symmetric index registers) -- unlike LDS/STS above, which reuse LDU/STU's.
#define OP2_LDY_IMM 0x8E
#define OP2_LDY_DIR 0x9E
#define OP2_LDY_EXT 0xBE
#define OP2_LDY_IDX 0xAE

#define OP2_STY_DIR 0x9F
#define OP2_STY_EXT 0xBF
#define OP2_STY_IDX 0xAF

// Long conditional branches: same secondary byte as the matching page-1
// short branch above (e.g. OP2_LBCC == OP_BCC), just reached via the 0x10
// prefix and followed by a 16-bit rather than 8-bit relative offset.
#define OP2_LBCC 0x24
#define OP2_LBCS 0x25
#define OP2_LBNE 0x26
#define OP2_LBEQ 0x27
#define OP2_LBVC 0x28
#define OP2_LBVS 0x29
#define OP2_LBPL 0x2A
#define OP2_LBMI 0x2B

// CMPD reuses SUBD's page-1 secondary byte (0x83 family); CMPY reuses
// CMPX's page-1 secondary byte (0x8C family) -- same pattern as LDS/STS
// reusing LDU/STU's bytes above.
#define OP2_CMPD_IMM 0x83
#define OP2_CMPD_DIR 0x93
#define OP2_CMPD_EXT 0xB3
#define OP2_CMPD_IDX 0xA3

#define OP2_CMPY_IMM 0x8C
#define OP2_CMPY_DIR 0x9C
#define OP2_CMPY_EXT 0xBC
#define OP2_CMPY_IDX 0xAC

// Page-3 (0x11-prefixed) opcodes. CMPU reuses the 0x83 family, CMPS reuses
// the 0x8C family -- same secondary-byte reuse pattern as page-2 above.
#define OP3_CMPU_IMM 0x83
#define OP3_CMPU_DIR 0x93
#define OP3_CMPU_EXT 0xB3
#define OP3_CMPU_IDX 0xA3

#define OP3_CMPS_IMM 0x8C
#define OP3_CMPS_DIR 0x9C
#define OP3_CMPS_EXT 0xBC
#define OP3_CMPS_IDX 0xAC

// HLE (high-level emulation) BIOS routine addresses. When PC lands on one of
// these, cpu_step() calls a native handler instead of executing whatever
// 6809 bytes are actually sitting there, then simulates the RTS.
#define HLE_DRAW_VL     0xF3DD
// Confirmed against the real loaded BIOS: $F192 opens LDX/LEAX/STX (a
// memory-counter increment idiom) then BSR / LDA #imm / CMPA direct / BEQ -4
// -- a textbook poll-until-flag-matches loop, i.e. genuinely a wait routine.
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

    // Set by CWAI, cleared by cpu_step() once an unmasked interrupt is
    // actually pending (see cpu_wake_from_cwai()). While set, dispatch is
    // suspended -- cpu_step() just idles (still ticking the VIA) instead
    // of fetching/executing instructions.
    int waiting_for_irq;
} Cpu;

void cpu_reset(Cpu *cpu);
void cpu_print_state(const Cpu *cpu);
int cpu_step(Cpu *cpu);

// Debug-only: temporarily disable/enable HLE BIOS interception so real ROM
// bytes execute even at a normally-hooked address (see main.c's BIOS tracer).
void cpu_set_hle_enabled(int enabled);

#endif // CPU_H
