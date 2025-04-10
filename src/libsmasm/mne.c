#include <smasm/mne.h>

#include <stdlib.h>

static struct {
    SmBuf name;
    U8    mne;
} MNEMONICS[] = {
    {SM_BUF("ADC"), SM_MNE_ADC},   {SM_BUF("ADD"), SM_MNE_ADD},
    {SM_BUF("AND"), SM_MNE_AND},   {SM_BUF("BIT"), SM_MNE_BIT},
    {SM_BUF("CALL"), SM_MNE_CALL}, {SM_BUF("CCF"), SM_MNE_CCF},
    {SM_BUF("CP"), SM_MNE_CP},     {SM_BUF("CPL"), SM_MNE_CPL},
    {SM_BUF("DAA"), SM_MNE_DAA},   {SM_BUF("DEC"), SM_MNE_DEC},
    {SM_BUF("DI"), SM_MNE_DI},     {SM_BUF("EI"), SM_MNE_EI},
    {SM_BUF("INC"), SM_MNE_INC},   {SM_BUF("JP"), SM_MNE_JP},
    {SM_BUF("JR"), SM_MNE_JR},     {SM_BUF("LD"), SM_MNE_LD},
    {SM_BUF("LDH"), SM_MNE_LDH},   {SM_BUF("NOP"), SM_MNE_NOP},
    {SM_BUF("OR"), SM_MNE_OR},     {SM_BUF("POP"), SM_MNE_POP},
    {SM_BUF("PUSH"), SM_MNE_PUSH}, {SM_BUF("RES"), SM_MNE_RES},
    {SM_BUF("RET"), SM_MNE_RET},   {SM_BUF("RETI"), SM_MNE_RETI},
    {SM_BUF("RL"), SM_MNE_RL},     {SM_BUF("RLA"), SM_MNE_RLA},
    {SM_BUF("RLC"), SM_MNE_RLC},   {SM_BUF("RLCA"), SM_MNE_RLCA},
    {SM_BUF("RR"), SM_MNE_RR},     {SM_BUF("RRA"), SM_MNE_RRA},
    {SM_BUF("RRC"), SM_MNE_RRC},   {SM_BUF("RRCA"), SM_MNE_RRCA},
    {SM_BUF("RST"), SM_MNE_RST},   {SM_BUF("SBC"), SM_MNE_SBC},
    {SM_BUF("SCF"), SM_MNE_SCF},   {SM_BUF("SET"), SM_MNE_SET},
    {SM_BUF("SLA"), SM_MNE_SLA},   {SM_BUF("SRA"), SM_MNE_SRA},
    {SM_BUF("STOP"), SM_MNE_STOP}, {SM_BUF("SUB"), SM_MNE_SUB},
    {SM_BUF("SWAP"), SM_MNE_SWAP}, {SM_BUF("XOR"), SM_MNE_XOR},
};

U8 const *smMneFind(SmBuf buf) {
    for (UInt i = 0; i < (sizeof(MNEMONICS) / sizeof(MNEMONICS[0])); ++i) {
        if (smBufEqualIgnoreAsciiCase(buf, MNEMONICS[i].name)) {
            return &MNEMONICS[i].mne;
        }
    }
    return NULL;
}
