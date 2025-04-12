#include "macro.h"
#include "fmt.h"
#include "state.h"

#include <smasm/fatal.h>
#include <smasm/tab.h>

SM_TAB_WHENCE_IMPL(MacroTab, Macro);

static MacroTab MACS = {0};

Macro *macroFind(SmBuf name) {
    MacroTab *tab = &MACS;
    SM_TAB_FIND_IMPL(MacroTab, Macro);
}

static SmMacroTokIntern MTOKS = {0};

void macroInvoke(Macro macro) {
    SmPos pos = tokPos();
    eat();
    SmMacroArgQueue args        = {0};
    SmMacroTokGBuf  toks        = {0};
    UInt            brace_depth = 0;
    if (peek() == '{') {
        eat();
        ++brace_depth;
    }
    while (true) {
        switch (peek()) {
        case '\n':
        case SM_TOK_EOF:
            if (brace_depth == 0) {
                goto flush;
            }
            break;
        case SM_TOK_ID:
            smMacroTokGBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_ID,
                                                  .pos  = tokPos(),
                                                  .buf  = intern(tokBuf())});
            break;
        case SM_TOK_STR:
            smMacroTokGBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_STR,
                                                  .pos  = tokPos(),
                                                  .buf  = intern(tokBuf())});
            break;
        case SM_TOK_STRFMT:
            fmtInvoke(SM_TOK_STR);
            continue;
        case SM_TOK_IDFMT:
            fmtInvoke(SM_TOK_ID);
            continue;
        default:
            if (brace_depth > 0) {
                if (peek() == '{') {
                    ++brace_depth;
                } else if (peek() == '}') {
                    --brace_depth;
                    if (brace_depth == 0) {
                        goto flush;
                    }
                }
            }
            smMacroTokGBufAdd(&toks, (SmMacroTok){.kind = SM_MACRO_TOK_TOK,
                                                  .pos  = tokPos(),
                                                  .tok  = peek()});
            break;
        }
        eat();
        if (peek() == ',') {
            eat();
            smMacroArgEnqueue(&args, smMacroTokIntern(&MTOKS, toks.inner));
            toks.inner.len = 0;
        }
    }
flush:
    if (toks.inner.len > 0) {
        smMacroArgEnqueue(&args, smMacroTokIntern(&MTOKS, toks.inner));
    }
    smMacroTokGBufFini(&toks);
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    ++nonce;
    smTokStreamMacroInit(ts, macro.name, pos, macro.buf, args, nonce);
}
