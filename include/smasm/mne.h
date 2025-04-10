#ifndef SMASM_MNE_H
#define SMASM_MNE_H

#include <smasm/buf.h>

enum SmMne {
    SM_MNE_ADC,
    SM_MNE_ADD,
    SM_MNE_AND,
    SM_MNE_BIT,
    SM_MNE_CALL,
    SM_MNE_CCF,
    SM_MNE_CP,
    SM_MNE_CPL,
    SM_MNE_DAA,
    SM_MNE_DEC,
    SM_MNE_DI,
    SM_MNE_EI,
    SM_MNE_INC,
    SM_MNE_JP,
    SM_MNE_JR,
    SM_MNE_LD,
    SM_MNE_LDH,
    SM_MNE_NOP,
    SM_MNE_OR,
    SM_MNE_POP,
    SM_MNE_PUSH,
    SM_MNE_RES,
    SM_MNE_RET,
    SM_MNE_RETI,
    SM_MNE_RL,
    SM_MNE_RLA,
    SM_MNE_RLC,
    SM_MNE_RLCA,
    SM_MNE_RR,
    SM_MNE_RRA,
    SM_MNE_RRC,
    SM_MNE_RRCA,
    SM_MNE_RST,
    SM_MNE_SBC,
    SM_MNE_SCF,
    SM_MNE_SET,
    SM_MNE_SLA,
    SM_MNE_SRA,
    SM_MNE_STOP,
    SM_MNE_SUB,
    SM_MNE_SWAP,
    SM_MNE_XOR,
};

U8 const *smMneFind(SmBuf buf);

#endif // SMASM_MNE_H
