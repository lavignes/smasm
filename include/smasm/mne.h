#ifndef SMASM_MNE_H
#define SMASM_MNE_H

#include <smasm/buf.h>

enum SmMne {
    SM_MNE_NOP,
    SM_MNE_LD,
    SM_MNE_INC,
    SM_MNE_DEC,
    SM_MNE_RCLA,
    SM_MNE_ADD,
    SM_MNE_RRCA,
    SM_MNE_STOP,
    SM_MNE_RLA,
    SM_MNE_JR,
    SM_MNE_RRA,
    SM_MNE_DAA,
    SM_MNE_CPL,
    SM_MNE_SCF,
    SM_MNE_CCF,
    SM_MNE_ADC,
    SM_MNE_SUB,
    SM_MNE_SBC,
    SM_MNE_AND,
    SM_MNE_XOR,
    SM_MNE_OR,
    SM_MNE_CP,
    SM_MNE_RET,
    SM_MNE_POP,
    SM_MNE_JP,
    SM_MNE_CALL,
    SM_MNE_PUSH,
    SM_MNE_RST,
    SM_MNE_RETI,
    SM_MNE_LDH,
    SM_MNE_DI,
    SM_MNE_EI,
    SM_MNE_RLC,
    SM_MNE_RRC,
    SM_MNE_RL,
    SM_MNE_RR,
    SM_MNE_SLA,
    SM_MNE_SRA,
    SM_MNE_SWAP,
    SM_MNE_BIT,
    SM_MNE_RES,
    SM_MNE_SET,
};

U8 const *smMneFind(SmBuf buf);

enum SmAddr {
    SM_ADDR_IMP,         // implied
    SM_ADDR_BC_IMM,      // BC,word
    SM_ADDR_BC_IND,      // [BC],A
    SM_ADDR_BC,          // BC
    SM_ADDR_B,           // B
    SM_ADDR_B_IMM,       // B,byte
    SM_ADDR_WORD_IND_SP, // [word],SP
    SM_ADDR_HL_BC,       // HL,BC
    SM_ADDR_IND_BC,      // A,[BC]
    SM_ADDR_C,           // C
    SM_ADDR_C_IMM,       // C,byte
    SM_ADDR_DE_IMM,      // DE,word
    SM_ADDR_DE_IND,      // [DE],A
    SM_ADDR_DE,          // DE
    SM_ADDR_D,           // D
    SM_ADDR_D_IMM,       // D,byte
    SM_ADDR_REL,         // ±$00
    SM_ADDR_HL_DE,       // HL,DE
    SM_ADDR_IND_DE,      // A,[DE]
};

SmBuf smOpcodeFind(U8 addr, U8 mne);

#endif // SMASM_MNE_H
