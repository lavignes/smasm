#include "macro.h"
#include "state.h"

#include <smasm/fatal.h>
#include <smasm/tab.h>

#include <stdlib.h>
#include <string.h>

SM_TAB_WHENCE_IMPL(MacroTab, Macro);
SM_TAB_TRYGROW_IMPL(MacroTab, Macro);

static MacroTab MACS = {0};

static void noop(Macro *entry) { (void)entry; }

void macroTabFini() {
    MacroTab *tab = &MACS;
    SM_TAB_FINI_IMPL(noop);
}

Macro *macroFind(SmView name) {
    MacroTab *tab = &MACS;
    SM_TAB_FIND_IMPL(MacroTab, Macro);
}

static SmMacroTokIntern MTOKS = {0};

static Macro *add(Macro entry) {
    MacroTab *tab = &MACS;
    SM_TAB_ADD_IMPL(MacroTab, Macro);
}

void macroAdd(SmView name, SmPos pos, SmMacroTokView view) {
    add((Macro){
        name,
        pos,
        smMacroTokIntern(&MTOKS, view),
    });
}

void macroInvoke(Macro macro) {
    SmPos pos = tokPos();
    eat();
    SmMacroArgQueue args  = {0};
    SmMacroTokBuf   toks  = {0};
    UInt            depth = 0;
    if (peek() == '{') {
        eat();
        ++depth;
    }
    while (true) {
        switch (peek()) {
        case '\n':
        case SM_TOK_EOF:
            if (depth == 0) {
                goto flush;
            }
            break;
        case SM_TOK_ID:
            smMacroTokBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_ID,
                                                 .pos  = tokPos(),
                                                 .view = intern(tokView())});
            break;
        case SM_TOK_NUM:
            smMacroTokBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_NUM,
                                                 .pos  = tokPos(),
                                                 .num  = tokNum()});
            break;
        case SM_TOK_STR:
            smMacroTokBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_STR,
                                                 .pos  = tokPos(),
                                                 .view = intern(tokView())});
            break;
        default:
            if (depth > 0) {
                if (peek() == '{') {
                    ++depth;
                } else if (peek() == '}') {
                    --depth;
                    if (depth == 0) {
                        eat();
                        goto flush;
                    }
                }
            }
            smMacroTokBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_TOK,
                                                 .pos  = tokPos(),
                                                 .tok  = peek()});
            break;
        }
        eat();
        if (peek() == ',') {
            eat();
            smMacroArgEnqueue(&args, smMacroTokIntern(&MTOKS, toks.view));
            toks.view.len = 0;
        }
    }
flush:
    if (toks.view.len > 0) {
        smMacroArgEnqueue(&args, smMacroTokIntern(&MTOKS, toks.view));
    }
    smMacroTokBufFini(&toks);
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    ++nonce;
    smTokStreamMacroInit(ts, macro.name, pos, macro.view, args, nonce);
}
