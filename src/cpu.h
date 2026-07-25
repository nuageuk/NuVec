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
} Cpu;

void cpu_reset(Cpu *cpu);
void cpu_print_state(const Cpu *cpu);
int cpu_step(Cpu *cpu);

#endif // CPU_H
