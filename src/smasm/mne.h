#ifndef MNE_H
#define MNE_H

#include <smasm/buf.h>

enum Mne {
    MNE_ADC,
    MNE_ADD,
    MNE_AND,
    MNE_BIT,
    MNE_CALL,
    MNE_CCF,
    MNE_CP,
    MNE_CPL,
    MNE_DAA,
    MNE_DEC,
    MNE_DI,
    MNE_EI,
    MNE_HALT,
    MNE_INC,
    MNE_JP,
    MNE_JR,
    MNE_LD,
    MNE_LDD,
    MNE_LDH,
    MNE_LDI,
    MNE_NOP,
    MNE_OR,
    MNE_POP,
    MNE_PUSH,
    MNE_RES,
    MNE_RET,
    MNE_RETI,
    MNE_RL,
    MNE_RLA,
    MNE_RLC,
    MNE_RLCA,
    MNE_RR,
    MNE_RRA,
    MNE_RRC,
    MNE_RRCA,
    MNE_RST,
    MNE_SBC,
    MNE_SCF,
    MNE_SET,
    MNE_SLA,
    MNE_SRA,
    MNE_SRL,
    MNE_STOP,
    MNE_SUB,
    MNE_SWAP,
    MNE_XOR,
};

U8 const *mneFind(SmBuf buf);

#endif // MNE_H
