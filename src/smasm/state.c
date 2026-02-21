#include "state.h"
#include "fmt.h"
#include "if.h"
#include "macro.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

SmViewIntern STRS   = {};
SmSymTab     SYMS   = {};
SmExprIntern EXPRS  = {};
SmPathSet    IPATHS = {};
SmPathSet    INCS   = {};

SmView intern(SmView view) { return smViewIntern(&STRS, view); }

SmView DEFINES_SECTION;
SmView CODE_SECTION;
SmView STATIC_UNIT;
SmView EXPORT_UNIT;

SmView scope     = {};
UInt   nonce     = 0;
Bool   emit      = false;
Bool   streamdef = false;

SmLbl lblLocal(SmView name) { return (SmLbl){scope, name}; }
SmLbl lblGlobal(SmView name) { return (SmLbl){{}, name}; }
SmLbl lblAbs(SmView scope, SmView name) { return (SmLbl){scope, name}; }

SmTokStream  STACK[STACK_SIZE] = {};
SmTokStream *ts                = STACK - 1;

_Noreturn void fatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalV(ts, fmt, args);
}

_Noreturn void fatalPos(SmPos pos, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(ts, pos, fmt, args);
}

void popStream() {
    assert(ts >= STACK);
    smTokStreamFini(ts);
    --ts;
}

U32 peek() {
    U32 tok = smTokStreamPeek(ts);
    // pop if we reached EOF
    if ((tok == SM_TOK_EOF) && (ts > STACK)) {
        popStream();
        return peek(); // yuck
    }
    // if we're in a macro/if definition, don't evaluate other meta-constructs
    if (streamdef) {
        return tok;
    }
    switch (tok) {
    case SM_TOK_ID: {
        Macro *macro = macroFind(tokView());
        if (macro) {
            macroInvoke(*macro);
            return peek(); // yuck
        }
        return tok;
    }
    case SM_TOK_IF:
        ifInvoke();
        return peek(); // yuck
    case SM_TOK_STRFMT:
        fmtInvoke(SM_TOK_STR);
        return peek(); // yuck
    case SM_TOK_IDFMT:
        fmtInvoke(SM_TOK_ID);
        return peek(); // yuck
    default:
        return tok;
    }
}

void eat() { smTokStreamEat(ts); }

void expect(U32 tok) {
    U32 peeked = peek();
    if (peeked != tok) {
        SmView expected = smTokName(tok);
        SmView found    = smTokName(peeked);
        fatal("expected %" SM_VIEW_FMT ", got %" SM_VIEW_FMT "\n",
              SM_VIEW_FMT_ARG(expected), SM_VIEW_FMT_ARG(found));
    }
}

SmView tokView() { return smTokStreamView(ts); }
I32    tokNum() { return smTokStreamNum(ts); }
SmPos  tokPos() { return smTokStreamPos(ts); }

SmLbl tokLbl() {
    SmView view   = tokView();
    U8    *offset = memchr(view.bytes, '.', view.len);
    if (!offset) {
        return lblGlobal(intern(view));
    }
    UInt scope_len = offset - view.bytes;
    UInt name_len  = view.len - scope_len - 1;
    if (name_len == 0) {
        fatal("label is malformed: %" SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
    }
    SmView name = {view.bytes + scope_len + 1, name_len};
    if (scope_len > 0) {
        return lblAbs(intern((SmView){view.bytes, scope_len}), intern(name));
    }
    return lblLocal(intern(name));
}

SmSectBuf SECTS                  = {};
UInt      SECT_STACK[STACK_SIZE] = {};
UInt     *sect                   = SECT_STACK - 1;

static UInt sectFind(SmView name) {
    for (UInt i = 0; i < SECTS.view.len; ++i) {
        if (smViewEqual(SECTS.view.items[i].name, name)) {
            return i;
        }
    }
    return UINT_MAX;
}

SmSect *sectGet() { return SECTS.view.items + *sect; }

void sectSet(SmView name) {
    UInt idx = sectFind(name);
    if (idx == UINT_MAX) {
        smSectBufAdd(&SECTS, (SmSect){
                                 .name   = name,
                                 .pc     = 0,
                                 .data   = {},
                                 .relocs = {},
                             });
        idx = SECTS.view.len - 1;
    }
    *sect = idx;
}

void sectPush(SmView name) {
    ++sect;
    if (sect >= (SECT_STACK + STACK_SIZE)) {
        fatal("@SECTION stack overflow\n");
    }
    sectSet(name);
}

void sectPop() {
    if (sect <= SECT_STACK) {
        fatal("@SECTION stack underflow\n");
    }
    --sect;
}

void sectRewind() {
    for (UInt i = 0; i < SECTS.view.len; ++i) {
        SmSect *sect = SECTS.view.items + i;
        sect->pc     = 0;
    }
    sect = SECT_STACK - 1;
}

void setPC(U16 num) { SECTS.view.items[*sect].pc = num; }
U16  getPC() { return SECTS.view.items[*sect].pc; }

void addPC(U16 offset) {
    I32 cur = (U32)getPC();
    I32 new = cur + offset;
    if (new > ((I32)(U32)U16_MAX)) {
        fatal("pc overflow: $%08X\n", new);
    }
    setPC(new);
}
