#include "mne.h"

#include <stdlib.h>

static struct {
    SmView name;
    U8     mne;
} MNEMONICS[] = {
    {SM_VIEW("ADC"), MNE_ADC},   {SM_VIEW("ADD"), MNE_ADD},
    {SM_VIEW("AND"), MNE_AND},   {SM_VIEW("BIT"), MNE_BIT},
    {SM_VIEW("CALL"), MNE_CALL}, {SM_VIEW("CCF"), MNE_CCF},
    {SM_VIEW("CP"), MNE_CP},     {SM_VIEW("CPL"), MNE_CPL},
    {SM_VIEW("DAA"), MNE_DAA},   {SM_VIEW("DEC"), MNE_DEC},
    {SM_VIEW("DI"), MNE_DI},     {SM_VIEW("EI"), MNE_EI},
    {SM_VIEW("HALT"), MNE_HALT}, {SM_VIEW("INC"), MNE_INC},
    {SM_VIEW("JP"), MNE_JP},     {SM_VIEW("JR"), MNE_JR},
    {SM_VIEW("LD"), MNE_LD},     {SM_VIEW("LDD"), MNE_LDD},
    {SM_VIEW("LDH"), MNE_LDH},   {SM_VIEW("LDI"), MNE_LDI},
    {SM_VIEW("NOP"), MNE_NOP},   {SM_VIEW("OR"), MNE_OR},
    {SM_VIEW("POP"), MNE_POP},   {SM_VIEW("PUSH"), MNE_PUSH},
    {SM_VIEW("RES"), MNE_RES},   {SM_VIEW("RET"), MNE_RET},
    {SM_VIEW("RETI"), MNE_RETI}, {SM_VIEW("RL"), MNE_RL},
    {SM_VIEW("RLA"), MNE_RLA},   {SM_VIEW("RLC"), MNE_RLC},
    {SM_VIEW("RLCA"), MNE_RLCA}, {SM_VIEW("RR"), MNE_RR},
    {SM_VIEW("RRA"), MNE_RRA},   {SM_VIEW("RRC"), MNE_RRC},
    {SM_VIEW("RRCA"), MNE_RRCA}, {SM_VIEW("RST"), MNE_RST},
    {SM_VIEW("SBC"), MNE_SBC},   {SM_VIEW("SCF"), MNE_SCF},
    {SM_VIEW("SET"), MNE_SET},   {SM_VIEW("SLA"), MNE_SLA},
    {SM_VIEW("SRA"), MNE_SRA},   {SM_VIEW("SRL"), MNE_SRL},
    {SM_VIEW("STOP"), MNE_STOP}, {SM_VIEW("SUB"), MNE_SUB},
    {SM_VIEW("SWAP"), MNE_SWAP}, {SM_VIEW("XOR"), MNE_XOR},
};

U8 const *mneFind(SmView name) {
    for (UInt i = 0; i < (sizeof(MNEMONICS) / sizeof(MNEMONICS[0])); ++i) {
        if (smViewEqualIgnoreAsciiCase(name, MNEMONICS[i].name)) {
            return &MNEMONICS[i].mne;
        }
    }
    return NULL;
}
