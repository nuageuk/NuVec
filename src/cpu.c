#include "cpu.h"
#include "memory.h"
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

// Resolves DIRECT/EXTENDED effective addresses, consuming the operand
// byte(s) at cpu->PC and advancing PC past them.
//
// IMMEDIATE is deliberately not a case here. DIRECT/EXTENDED both follow the
// same shape: fetch operand bytes of a fixed size (1 or 2, regardless of the
// destination register), combine them into an effective address, then the
// caller dereferences that address separately. IMMEDIATE has no dereference
// step at all -- the fetched bytes ARE the value -- and the fetch width
// depends on the destination register (1 byte for A/B, 2 bytes for X/U), not
// on the addressing mode. Forcing it through this function would mean either
// returning PC as a fake "address" (misleading, since nothing is dereferenced
// through the normal memory-routing path conceptually) or adding a width
// parameter that only IMMEDIATE would use. Simpler to keep it inline per
// instruction, as before.
static uint16_t cpu_resolve_address(Cpu *cpu, AddrMode mode) {
    switch (mode) {
        case ADDR_DIRECT: {
            uint8_t operand = mem_read8(cpu->PC);
            cpu->PC++;
            return ((uint16_t)cpu->DP << 8) | operand;
        }
        case ADDR_EXTENDED: {
            uint8_t hi = mem_read8(cpu->PC);
            uint8_t lo = mem_read8(cpu->PC + 1);
            cpu->PC += 2;
            return ((uint16_t)hi << 8) | lo;
        }
    }
    return 0; // unreachable
}

int cpu_step(Cpu *cpu) {
    uint16_t opcode_pc = cpu->PC;
    uint8_t opcode = mem_read8(cpu->PC);
    cpu->PC++;

    switch (opcode) {
        case OP_LDA_IMM: {
            uint8_t value = mem_read8(cpu->PC);
            cpu->PC++;

            cpu->A = value;
            set_nz8(cpu, value);
            return 1;
        }
        case OP_LDA_DIR:
        case OP_LDA_EXT: {
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_LDA_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
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
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_LDB_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
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
        case OP_LDX_EXT: {
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_LDX_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
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
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_LDU_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
            uint8_t hi = mem_read8(addr);
            uint8_t lo = mem_read8(addr + 1);
            uint16_t value = ((uint16_t)hi << 8) | lo;

            cpu->U = value;
            set_nz16(cpu, value);
            return 1;
        }

        case OP_STA_DIR:
        case OP_STA_EXT: {
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_STA_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
            mem_write8(addr, cpu->A);
            return 1;
        }

        case OP_STB_DIR:
        case OP_STB_EXT: {
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_STB_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
            mem_write8(addr, cpu->B);
            return 1;
        }

        case OP_STX_DIR:
        case OP_STX_EXT: {
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_STX_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
            mem_write8(addr, (uint8_t)(cpu->X >> 8));
            mem_write8(addr + 1, (uint8_t)(cpu->X & 0xFF));
            return 1;
        }

        case OP_STU_DIR:
        case OP_STU_EXT: {
            uint16_t addr = cpu_resolve_address(cpu, opcode == OP_STU_DIR ? ADDR_DIRECT : ADDR_EXTENDED);
            mem_write8(addr, (uint8_t)(cpu->U >> 8));
            mem_write8(addr + 1, (uint8_t)(cpu->U & 0xFF));
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
}
