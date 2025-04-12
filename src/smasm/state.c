#include "state.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

SmBufIntern  STRS   = {0};
SmSymTab     SYMS   = {0};
SmExprIntern EXPRS  = {0};
SmPathSet    IPATHS = {0};
SmPathSet    INCS   = {0};

SmBuf intern(SmBuf buf) { return smBufIntern(&STRS, buf); }

SmBuf DEFINES_SECTION;
SmBuf CODE_SECTION;
SmBuf STATIC_UNIT;
SmBuf EXPORT_UNIT;

SmBuf scope    = SM_BUF_NULL;
UInt  if_level = 0;
UInt  nonce    = 0;
Bool  emit     = false;

SmLbl lblLocal(SmBuf name) { return (SmLbl){scope, name}; }
SmLbl lblGlobal(SmBuf name) { return (SmLbl){{0}, name}; }
SmLbl lblAbs(SmBuf scope, SmBuf name) { return (SmLbl){scope, name}; }

SmTokStream  STACK[STACK_SIZE] = {0};
SmTokStream *ts                = STACK - 1;

_Noreturn void fatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalV(ts, fmt, args);
    va_end(args);
}

_Noreturn void fatalPos(SmPos pos, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(ts, pos, fmt, args);
    va_end(args);
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
    return tok;
}

void eat() { smTokStreamEat(ts); }

void expect(U32 tok) {
    if (peek() != tok) {
        // TODO: utf8
        if (isprint(tok)) {
            fatal("expected `%c`\n", tok);
        } else {
            SmBuf name = smTokName(tok);
            fatal("expected %.*s\n", name.len, name.bytes);
        }
    }
}

SmBuf tokBuf() { return smTokStreamBuf(ts); }
I32   tokNum() { return smTokStreamNum(ts); }
SmPos tokPos() { return smTokStreamPos(ts); }

SmLbl tokLbl() {
    SmBuf buf    = tokBuf();
    U8   *offset = memchr(buf.bytes, '.', buf.len);
    if (!offset) {
        return lblGlobal(intern(buf));
    }
    UInt scope_len = offset - buf.bytes;
    UInt name_len  = buf.len - scope_len - 1;
    if (name_len == 0) {
        fatal("label is malformed: %.*s\n", buf.len, buf.bytes);
    }
    SmBuf name = {.bytes = buf.bytes + scope_len + 1, .len = name_len};
    if (scope_len > 0) {
        return lblAbs(intern((SmBuf){buf.bytes, scope_len}), intern(name));
    }
    return lblLocal(intern(name));
}

SmSectGBuf  SECTS = {0};
static UInt sect;

static UInt sectFind(SmBuf name) {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        if (smBufEqual(SECTS.inner.items->name, name)) {
            return i;
        }
    }
    return UINT_MAX;
}

SmSect *sectGet() { return SECTS.inner.items + sect; }

void sectSet(SmBuf name) {
    sect = sectFind(name);
    if (sect == UINT_MAX) {
        smSectGBufAdd(&SECTS, (SmSect){
                                  .name   = name,
                                  .pc     = 0,
                                  .data   = {{0}, 0}, // GCC doesnt like {0}
                                  .relocs = {{0}, 0},
                              });
        sect = SECTS.inner.len - 1;
    }
}

void sectRewind() {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        sect->pc     = 0;
    }
}

void setPC(U16 num) { SECTS.inner.items[sect].pc = num; }
U16  getPC() { return SECTS.inner.items[sect].pc; }

void addPC(U16 offset) {
    I32 cur = (U32)getPC();
    I32 new = cur + offset;
    if (new > ((I32)(U32)U16_MAX)) {
        fatal("pc overflow: $%08X\n", new);
    }
    setPC(new);
}
