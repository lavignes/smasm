#include "mne.h"

#include <stdlib.h>

static struct {
    SmBuf name;
    U8    mne;
} MNEMONICS[] = {
    {SM_BUF("ADC"), MNE_ADC},   {SM_BUF("ADD"), MNE_ADD},
    {SM_BUF("AND"), MNE_AND},   {SM_BUF("BIT"), MNE_BIT},
    {SM_BUF("CALL"), MNE_CALL}, {SM_BUF("CCF"), MNE_CCF},
    {SM_BUF("CP"), MNE_CP},     {SM_BUF("CPL"), MNE_CPL},
    {SM_BUF("DAA"), MNE_DAA},   {SM_BUF("DEC"), MNE_DEC},
    {SM_BUF("DI"), MNE_DI},     {SM_BUF("EI"), MNE_EI},
    {SM_BUF("HALT"), MNE_HALT}, {SM_BUF("INC"), MNE_INC},
    {SM_BUF("JP"), MNE_JP},     {SM_BUF("JR"), MNE_JR},
    {SM_BUF("LD"), MNE_LD},     {SM_BUF("LDD"), MNE_LDD},
    {SM_BUF("LDH"), MNE_LDH},   {SM_BUF("LDI"), MNE_LDI},
    {SM_BUF("NOP"), MNE_NOP},   {SM_BUF("OR"), MNE_OR},
    {SM_BUF("POP"), MNE_POP},   {SM_BUF("PUSH"), MNE_PUSH},
    {SM_BUF("RES"), MNE_RES},   {SM_BUF("RET"), MNE_RET},
    {SM_BUF("RETI"), MNE_RETI}, {SM_BUF("RL"), MNE_RL},
    {SM_BUF("RLA"), MNE_RLA},   {SM_BUF("RLC"), MNE_RLC},
    {SM_BUF("RLCA"), MNE_RLCA}, {SM_BUF("RR"), MNE_RR},
    {SM_BUF("RRA"), MNE_RRA},   {SM_BUF("RRC"), MNE_RRC},
    {SM_BUF("RRCA"), MNE_RRCA}, {SM_BUF("RST"), MNE_RST},
    {SM_BUF("SBC"), MNE_SBC},   {SM_BUF("SCF"), MNE_SCF},
    {SM_BUF("SET"), MNE_SET},   {SM_BUF("SLA"), MNE_SLA},
    {SM_BUF("SRA"), MNE_SRA},   {SM_BUF("SRL"), MNE_SRL},
    {SM_BUF("STOP"), MNE_STOP}, {SM_BUF("SUB"), MNE_SUB},
    {SM_BUF("SWAP"), MNE_SWAP}, {SM_BUF("XOR"), MNE_XOR},
};

U8 const *mneFind(SmBuf buf) {
    for (UInt i = 0; i < (sizeof(MNEMONICS) / sizeof(MNEMONICS[0])); ++i) {
        if (smBufEqualIgnoreAsciiCase(buf, MNEMONICS[i].name)) {
            return &MNEMONICS[i].mne;
        }
    }
    return NULL;
}
