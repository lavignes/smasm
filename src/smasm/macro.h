#ifndef MACRO_H
#define MACRO_H

#include <smasm/tok.h>

struct Macro {
    SmBuf         name;
    SmPos         pos;
    SmMacroTokBuf buf;
};
typedef struct Macro Macro;

struct MacroTab {
    Macro *entries;
    UInt   len;
    UInt   size;
};
typedef struct MacroTab MacroTab;

void   macroTabFini();
Macro *macroFind(SmBuf name);
void   macroAdd(SmBuf name, SmPos pos, SmMacroTokBuf buf);

void macroInvoke(Macro macro);

#endif // MACRO_H
