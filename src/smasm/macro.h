#ifndef MACRO_H
#define MACRO_H

#include <smasm/tok.h>

typedef struct {
    SmView        name;
    SmPos         pos;
    SmMacroTokBuf buf;
} Macro;

typedef struct {
    Macro *entries;
    UInt   len;
    UInt   size;
} MacroTab;

void   macroTabFini();
Macro *macroFind(SmView name);
void   macroAdd(SmView name, SmPos pos, SmMacroTokBuf buf);

void macroInvoke(Macro macro);

#endif // MACRO_H
