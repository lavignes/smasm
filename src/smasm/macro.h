#ifndef MACRO_H
#define MACRO_H

#include <smasm/tok.h>

struct Macro {
    SmBuf         name;
    SmMacroTokBuf buf;
};
typedef struct Macro Macro;

struct MacroTab {
    Macro *entries;
    UInt   len;
    UInt   size;
};
typedef struct MacroTab MacroTab;

Macro *macroFind(SmBuf name);

void macroInvoke(Macro macro);

#endif // MACRO_H
