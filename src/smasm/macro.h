#ifndef MACRO_H
#define MACRO_H

#include <smasm/tok.h>

typedef struct {
    SmView         name;
    SmPos          pos;
    SmMacroTokView view;
} Macro;

typedef struct {
    Macro *entries;
    UInt   len;
    UInt   cap;
} MacroTab;

void   macroTabFini();
Macro *macroFind(SmView name);
void   macroAdd(SmView name, SmPos pos, SmMacroTokView view);

void macroInvoke(Macro macro);

#endif // MACRO_H
