#include "if.h"

#include "expr.h"
#include "state.h"

void ifInvoke() {
    SmPos pos = tokPos();
    eat();
    streamdef          = true;
    Bool        ignore = (exprEatSolvedPos(&pos) == 0);
    UInt        depth  = 0;
    SmPosTokBuf buf    = {};
    while (true) {
        switch (peek()) {
        case SM_TOK_IF:
        case SM_TOK_MACRO:
        case SM_TOK_REPEAT:
        case SM_TOK_STRUCT:
        case SM_TOK_UNION:
            ++depth;
            break;
        case SM_TOK_END:
            if (depth == 0) {
                eat();
                goto ifdone;
            }
            --depth;
            break;
        case SM_TOK_ELSE:
            if (depth == 0) {
                eat();
                ignore = !ignore;
            }
            break;
        default:
            break;
        }
        switch (peek()) {
        case SM_TOK_EOF:
            fatal("unexpected end of file\n");
        case SM_TOK_ID:
        case SM_TOK_STR:
            if (!ignore) {
                smPosTokBufAdd(&buf, (SmPosTok){.tok  = peek(),
                                                .pos  = tokPos(),
                                                .view = intern(tokView())});
            }
            break;
        case SM_TOK_NUM:
        case SM_TOK_ARG:
            if (!ignore) {
                smPosTokBufAdd(&buf, (SmPosTok){.tok = peek(),
                                                .pos = tokPos(),
                                                .num = tokNum()});
            }
            break;
        default:
            if (!ignore) {
                smPosTokBufAdd(&buf,
                               (SmPosTok){.tok = peek(), .pos = tokPos()});
            }
            break;
        }
        eat();
    }
ifdone:
    streamdef = false;
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    smTokStreamIfElseInit(ts, pos, buf);
}
