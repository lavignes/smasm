#include <smasm/fatal.h>
#include <smasm/fmt.h>
#include <smasm/mne.h>
#include <smasm/path.h>
#include <smasm/sect.h>
#include <smasm/tab.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void help() {
    fprintf(stderr,
            "Usage: smasm [OPTIONS] <SOURCE>\n"
            "\n"
            "Arguments:\n"
            "  <SOURCE>  Assembly source file\n"
            "\n"
            "Options:\n"
            "  -o, --output <OUTPUT>        Output file (default: stdout)\n"
            "  -D, --define <KEY1=val>      Pre-defined symbols (repeatable)\n"
            "  -I, --include <INCLUDE>      Search directories for included "
            "files (repeatable)\n"
            "  -MD                          Output Makefile dependencies\n"
            "  -MF <DEPFILE>                Make dependencies file (default: "
            "<SOURCE>.d)\n"
            "  -h, --help                   Print help\n");
}

static FILE *infile        = NULL;
static char *infile_name   = NULL;
static FILE *outfile       = NULL;
static char *outfile_name  = NULL;
static char *depfile_name  = NULL;
static Bool  makedepend    = false;

static SmBufIntern  STRS   = {0};
static SmSymTab     SYMS   = {0};
static SmExprIntern EXPRS  = {0};
static SmPathSet    IPATHS = {0};
// static SmPathSet    INCS   = {0}; // files included in the translation unit

static SmBuf DEFINES_SECTION;
static SmBuf CODE_SECTION;
static SmBuf STATIC_UNIT;
static SmBuf EXPORT_UNIT;

static SmBuf     intern(SmBuf buf);
static SmLbl     globalLbl(SmBuf name);
static SmExprBuf constExprBuf(I32 num);
static FILE     *openFileCstr(char const *name, char const *modes);
static void      closeFile(FILE *hnd);
static void      pushFile(SmBuf path);
static void      pass();
static void      rewindPass();
static void      pullStream();
static void      writeDepend();
static void      serialize();

int main(int argc, char **argv) {
    outfile = stdout;
    if (argc == 1) {
        help();
        return EXIT_SUCCESS;
    }
    DEFINES_SECTION = intern(SM_BUF("@DEFINES"));
    CODE_SECTION    = intern(SM_BUF("CODE"));
    STATIC_UNIT     = intern(SM_BUF("@STATIC"));
    EXPORT_UNIT     = intern(SM_BUF("@EXPORT"));
    for (int argi = 1; argi < argc; ++argi) {
        if ((strcmp(argv[argi], "-h") == 0) ||
            (strcmp(argv[argi], "--help") == 0)) {
            help();
            return EXIT_SUCCESS;
        }
        if ((strcmp(argv[argi], "-o") == 0) ||
            (strcmp(argv[argi], "--output") == 0)) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            outfile_name = argv[argi];
            continue;
        }
        if (!strcmp(argv[argi], "-D") || !strcmp(argv[argi], "--define")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected symbol definition\n");
            }
            char const *offset = strchr(argv[argi], '=');
            if (!offset) {
                smFatal("expected `=` in %s\n", argv[argi]);
            }
            UInt  name_len = offset - argv[argi];
            SmBuf name =
                smBufIntern(&STRS, (SmBuf){(U8 *)argv[argi], name_len});
            UInt value_len = strlen(argv[argi]) - name_len - 1;
            I32  num =
                smParse((SmBuf){(U8 *)(argv[argi] + name_len + 1), value_len});
            smSymTabAdd(&SYMS, (SmSym){
                                   .lbl     = globalLbl(name),
                                   .value   = constExprBuf(num),
                                   .unit    = STATIC_UNIT,
                                   .section = DEFINES_SECTION,
                                   .pos     = {DEFINES_SECTION, 1, 1},
                                   .flags   = SM_SYM_EQU,
                               });
            continue;
        }
        if (!strcmp(argv[argi], "-I") || !strcmp(argv[argi], "--include")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            smPathSetAdd(&IPATHS,
                         (SmBuf){(U8 *)argv[argi], strlen(argv[argi])});
            continue;
        }
        if (!strcmp(argv[argi], "-MD")) {
            makedepend = true;
            continue;
        }
        if (!strcmp(argv[argi], "-MF")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            depfile_name = argv[argi];
            continue;
        }
        infile      = openFileCstr(argv[argi], "rb");
        infile_name = argv[argi];
        ++argi;
        if (argi != argc) {
            smFatal("unexpected option: %s\n", argv[argi]);
        }
    }

    pushFile(
        smPathIntern(&STRS, (SmBuf){(U8 *)infile_name, strlen(infile_name)}));
    pass();
    rewindPass();
    pass();
    pullStream();

    if (outfile_name) {
        outfile = openFileCstr(outfile_name, "wb+");
    } else {
        outfile_name = "stdout";
    }

    if (makedepend) {
        writeDepend();
    }

    serialize();
    closeFile(outfile);
    return 0;
}

static SmBuf const NULL_BUF = {0};
static SmBuf       scope    = NULL_BUF;
static UInt        if_level = 0;
static UInt        nonce    = 0;
static Bool        emit     = false;

static SmLbl localLbl(SmBuf name) { return (SmLbl){scope, name}; }
static SmLbl globalLbl(SmBuf name) { return (SmLbl){{0}, name}; }
static SmLbl absLbl(SmBuf scope, SmBuf name) { return (SmLbl){scope, name}; }

#define STACK_SIZE 16
static SmTokStream  STACK[STACK_SIZE] = {0};
static SmTokStream *ts                = STACK - 1;

static _Noreturn void fatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalV(ts, fmt, args);
    va_end(args);
}

static _Noreturn void fatalPos(SmPos pos, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(ts, pos, fmt, args);
    va_end(args);
}

static U32 peek() {
    U32 tok = smTokStreamPeek(ts);
    // pop if we reached EOF
    if ((tok == SM_TOK_EOF) && (ts > STACK)) {
        pullStream();
        return peek(); // yuck
    }
    return tok;
}

static void eat() { smTokStreamEat(ts); }

static SmBuf tokBuf() { return smTokStreamBuf(ts); }
static I32   tokNum() { return smTokStreamNum(ts); }
static SmPos tokPos() { return smTokStreamPos(ts); }

static SmLbl tokLbl() {
    SmBuf buf    = tokBuf();
    U8   *offset = memchr(buf.bytes, '.', buf.len);
    if (!offset) {
        return globalLbl(intern(buf));
    }
    UInt scope_len = offset - buf.bytes;
    UInt name_len  = buf.len - scope_len - 1;
    if (name_len == 0) {
        fatal("label is malformed: %.*s\n", buf.len, buf.bytes);
    }
    SmBuf name = {.bytes = buf.bytes + scope_len + 1, .len = name_len};
    if (scope_len > 0) {
        return absLbl(intern((SmBuf){buf.bytes, scope_len}), intern(name));
    }
    return localLbl(intern(name));
}

static void pullStream() {
    assert(ts >= STACK);
    smTokStreamFini(ts);
    --ts;
}

static SmSectGBuf SECTS = {0};
static UInt       sect;

static UInt findSection(SmBuf name) {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        if (smBufEqual(SECTS.inner.items->name, name)) {
            return i;
        }
    }
    return UINT_MAX;
}

static SmSect *getSection() { return SECTS.inner.items + sect; }

static void setSection(SmBuf name) {
    sect = findSection(name);
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

static void rewindSections() {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        sect->pc     = 0;
    }
}

static void setPC(U16 num) { SECTS.inner.items[sect].pc = num; }
static U16  getPC() { return SECTS.inner.items[sect].pc; }

static void rewindPass() {
    smTokStreamRewind(ts);
    rewindSections();
    scope    = NULL_BUF;
    if_level = 0;
    nonce    = 0;
    emit     = true;
}

static void expect(U32 tok) {
    if (peek() != tok) {
        if (isprint(tok)) {
            fatal("expected `%c`\n", tok);
        } else {
            SmBuf name = smTokName(tok);
            fatal("expected %.*s\n", name.len, name.bytes);
        }
    }
}

static void expectEOL() {
    switch (peek()) {
    case SM_TOK_EOF:
    case '\n':
        return;
    default:
        fatal("expected end of line\n");
    }
}

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

SM_TAB_WHENCE_IMPL(MacroTab, Macro);

static Macro *macroFind(MacroTab *tab, SmBuf name) {
    SM_TAB_FIND_IMPL(MacroTab, Macro);
}

static MacroTab         MACS  = {0};
static SmMacroTokIntern MTOKS = {0};

static void invokeFmt(U32 tok);

static void invoke(Macro macro) {
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
            invokeFmt(SM_TOK_STR);
            continue;
        case SM_TOK_IDFMT:
            invokeFmt(SM_TOK_ID);
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

static SmExprGBuf expr_stack = {0};
static SmOpGBuf   op_stack   = {0};

static SmExpr constExpr(I32 num) {
    return (SmExpr){.kind = SM_EXPR_CONST, .num = num};
}

static SmExpr addrExpr(SmBuf section, U16 offset) {
    return (SmExpr){.kind = SM_EXPR_ADDR, .addr = {section, offset}};
}

static void pushExpr(SmExpr expr) { smExprGBufAdd(&expr_stack, expr); }

static Bool solve(SmExprBuf buf, I32 *num) {
    SmI32GBuf stack = {0};
    for (UInt i = 0; i < buf.len; ++i) {
        SmExpr *expr = buf.items + i;
        switch (expr->kind) {
        case SM_EXPR_CONST:
            smI32GBufAdd(&stack, expr->num);
            break;
        case SM_EXPR_LABEL: {
            SmSym *sym = smSymTabFind(&SYMS, expr->lbl);
            if (!sym) {
                goto fail;
            }
            I32 num;
            // yuck
            if (!solve(sym->value, &num)) {
                goto fail;
            }
            smI32GBufAdd(&stack, num);
            break;
        }
        case SM_EXPR_TAG:
            goto fail; // can only solve during link
        case SM_EXPR_OP:
            while (stack.inner.len > 0) {
                --stack.inner.len;
                I32 rhs = stack.inner.items[stack.inner.len];
                if (expr->op.unary) {
                    switch (expr->op.tok) {
                    case '+':
                        smI32GBufAdd(&stack, rhs);
                        break;
                    case '-':
                        smI32GBufAdd(&stack, -rhs);
                        break;
                    case '~':
                        smI32GBufAdd(&stack, ~rhs);
                        break;
                    case '!':
                        smI32GBufAdd(&stack, !rhs);
                        break;
                    case '<':
                        smI32GBufAdd(&stack, ((U32)rhs) & 0xFF);
                        break;
                    case '>':
                        smI32GBufAdd(&stack, ((U32)rhs & 0xFF00) >> 8);
                        break;
                    case '^':
                        smI32GBufAdd(&stack, ((U32)rhs & 0xFF0000) >> 16);
                        break;
                    default:
                        smUnreachable();
                    }
                } else {
                    --stack.inner.len;
                    I32 lhs = stack.inner.items[stack.inner.len];
                    switch (expr->op.tok) {
                    case '+':
                        smI32GBufAdd(&stack, lhs + rhs);
                        break;
                    case '-':
                        smI32GBufAdd(&stack, lhs - rhs);
                        break;
                    case '*':
                        smI32GBufAdd(&stack, lhs * rhs);
                        break;
                    case '/':
                        smI32GBufAdd(&stack, lhs / rhs);
                        break;
                    case '%':
                        smI32GBufAdd(&stack, lhs % rhs);
                        break;
                    case SM_TOK_ASL:
                        smI32GBufAdd(&stack, lhs << rhs);
                        break;
                    case SM_TOK_ASR:
                        smI32GBufAdd(&stack, lhs >> rhs);
                        break;
                    case SM_TOK_LSR:
                        smI32GBufAdd(&stack, ((U32)lhs) >> ((U32)rhs));
                        break;
                    case '<':
                        smI32GBufAdd(&stack, lhs < rhs);
                        break;
                    case SM_TOK_LTE:
                        smI32GBufAdd(&stack, lhs <= rhs);
                        break;
                    case '>':
                        smI32GBufAdd(&stack, lhs > rhs);
                        break;
                    case SM_TOK_GTE:
                        smI32GBufAdd(&stack, lhs >= rhs);
                        break;
                    case SM_TOK_DEQ:
                        smI32GBufAdd(&stack, lhs == rhs);
                        break;
                    case SM_TOK_NEQ:
                        smI32GBufAdd(&stack, lhs != rhs);
                        break;
                    case '&':
                        smI32GBufAdd(&stack, lhs & rhs);
                        break;
                    case '|':
                        smI32GBufAdd(&stack, lhs | rhs);
                        break;
                    case '^':
                        smI32GBufAdd(&stack, lhs ^ rhs);
                        break;
                    case SM_TOK_AND:
                        smI32GBufAdd(&stack, lhs && rhs);
                        break;
                    case SM_TOK_OR:
                        smI32GBufAdd(&stack, lhs || rhs);
                        break;
                    default:
                        smUnreachable();
                    }
                }
            }
            break;
        case SM_EXPR_ADDR:
            if (!smBufEqual(expr->addr.sect, getSection()->name)) {
                goto fail;
            }
            // relative offset within same section?
            smI32GBufAdd(&stack, expr->addr.pc);
            break;
        default:
            smUnreachable();
        }
    }
    assert(stack.inner.len == 1);
    *num = *stack.inner.items;
    smI32GBufFini(&stack);
    return true;
fail:
    smI32GBufFini(&stack);
    return false;
}

static U8 precedence(SmOp op) {
    if (op.unary) {
        // lowest precedence
        if (op.tok == '(') {
            return 0;
        }
        return U8_MAX;
    }
    switch (op.tok) {
    case '/':
    case '%':
    case '*':
        return 1;
    case '+':
    case '-':
        return 2;
    case SM_TOK_ASL:
    case SM_TOK_ASR:
    case SM_TOK_LSR:
        return 3;
    case '<':
    case '>':
    case SM_TOK_LTE:
    case SM_TOK_GTE:
        return 4;
    case SM_TOK_DEQ:
    case SM_TOK_NEQ:
        return 5;
    case '&':
        return 6;
    case '^':
        return 7;
    case '|':
        return 8;
    case SM_TOK_AND:
        return 9;
    case SM_TOK_OR:
        return 10;
    default:
        smUnreachable();
    }
}

static void pushApply(SmOp op) {
    // pratt parser magic
    while (op_stack.inner.len > 0) {
        --op_stack.inner.len;
        SmOp top = op_stack.inner.items[op_stack.inner.len];
        if (precedence(top) >= precedence(op)) {
            break;
        }
        pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
    }
    smOpGBufAdd(&op_stack, op);
}

static void pushApplyBinary(U32 tok) { pushApply((SmOp){tok, false}); }

static void pushApplyUnary(U32 tok) { pushApply((SmOp){tok, true}); }

static SmExprBuf eatExpr() {
    expr_stack.inner.len = 0;
    op_stack.inner.len   = 0;
    Bool seen_value      = false;
    UInt paren_depth     = 0;
    while (true) {
        switch (peek()) {
        case '*':
            eat();
            // * must be the relative PC
            if (!seen_value) {
                pushExpr(constExpr(getPC()));
                seen_value = true;
                continue;
            }
            pushApplyBinary('*');
            seen_value = false;
            continue;
        case SM_TOK_DSTAR:
            // ** the absolute PC
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            pushExpr(addrExpr(getSection()->name, getPC()));
            seen_value = true;
            continue;
        case '+':
        case '-':
        case '^':
        case '<':
        case '>':
            // sometimes unary
            if (seen_value) {
                pushApplyBinary(peek());
            } else {
                pushApplyUnary(peek());
            }
            eat();
            seen_value = false;
            continue;
        case '!':
        case '~':
            // always unary
            pushApplyUnary(peek());
            eat();
            seen_value = false;
            continue;
        case '&':
        case SM_TOK_AND:
        case SM_TOK_OR:
        case '/':
        case '%':
        case '|':
        case SM_TOK_ASL:
        case SM_TOK_ASR:
        case SM_TOK_LSR:
        case SM_TOK_LTE:
        case SM_TOK_GTE:
        case SM_TOK_DEQ:
        case SM_TOK_NEQ:
            // binary
            if (!seen_value) {
                fatal("expected a value\n");
            }
            pushApplyBinary(peek());
            eat();
            seen_value = false;
            continue;
        case SM_TOK_NUM:
            if (seen_value) {
                fatal("expected an operator\n");
            }
            pushExpr(constExpr(tokNum()));
            eat();
            seen_value = true;
            continue;
        case '(':
            if (seen_value) {
                fatal("expected an operator\n");
            }
            ++paren_depth;
            pushApplyUnary('(');
            eat();
            seen_value = false;
            continue;
        case ')':
            if (!seen_value) {
                fatal("expected a value\n");
            }
            --paren_depth;
            while (op_stack.inner.len > 0) {
                --op_stack.inner.len;
                SmOp op = op_stack.inner.items[op_stack.inner.len];
                if (op.tok == '(') {
                    eat();
                    goto matched;
                }
                pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
            }
            fatal("unmatched parentheses\n");
        matched:
            continue;
        case SM_TOK_ID: {
            // is this a macro?
            Macro *macro = macroFind(&MACS, tokBuf());
            if (macro) {
                invoke(*macro);
                continue;
            }
            if (seen_value) {
                fatal("expected an operator\n");
            }
            pushExpr((SmExpr){.kind = SM_EXPR_LABEL, .lbl = tokLbl()});
            eat();
            seen_value = true;
            continue;
        }
        case SM_TOK_TAG: {
            eat();
            expect(SM_TOK_ID);
            SmLbl lbl = tokLbl();
            eat();
            expect(',');
            eat();
            expect(SM_TOK_STR);
            pushExpr(
                (SmExpr){.kind = SM_EXPR_TAG, .tag = {lbl, intern(tokBuf())}});
            seen_value = true;
            eat();
            continue;
        }
        case SM_TOK_STRLEN:
            eat();
            expect(SM_TOK_STR);
            pushExpr(constExpr(tokBuf().len));
            eat();
            seen_value = true;
            continue;
        default:
            if (!seen_value) {
                fatal("expected a value\n");
            }
            if (paren_depth > 0) {
                fatal("unmatched parentheses\n");
            }
            goto complete;
        }
    }
complete:
    while (op_stack.inner.len > 0) {
        // TODO: what ops would remain here?
        --op_stack.inner.len;
        SmOp op = op_stack.inner.items[op_stack.inner.len];
        pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
    }
    return smExprIntern(&EXPRS, expr_stack.inner);
}

static Bool canReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }
static Bool canReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }

static SmExprBuf eatExprPos(SmPos *pos) {
    // advance to get the location of the expr
    peek();
    *pos = tokPos();
    return eatExpr();
}

static I32 eatSolvedPos(SmPos *pos) {
    I32 num;
    if (!solve(eatExprPos(pos), &num)) {
        fatalPos(*pos, "expression must be constant\n");
    }
    return num;
}

static U8 eatSolvedU8() {
    SmPos pos;
    I32   num = eatSolvedPos(&pos);
    if (!canReprU8(num)) {
        fatalPos(pos, "expression does not fit in a byte\n");
    }
    return (U8)num;
}

static U16 eatSolvedU16() {
    SmPos pos;
    I32   num = eatSolvedPos(&pos);
    if (!canReprU16(num)) {
        fatalPos(pos, "expression does not fit in a word\n");
    }
    return (U16)num;
}

static void eatMne() { smUnimplemented(); }

static void eatDirective() { smUnimplemented(); }

static void pass() {
    setSection(CODE_SECTION);
    while (peek() != SM_TOK_EOF) {
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case '*':
            // setting the _relative_ PC: * = $1234
            eat();
            expect('=');
            eat();
            setPC(eatSolvedU16());
            expectEOL();
            eat();
            continue;
        case SM_TOK_ID: {
            U8 const *mne = smMneFind(tokBuf());
            if (mne) {
                eatMne();
                expectEOL();
                eat();
                continue;
            }
        }
        default:
            eatDirective();
        }
    }
}

static void writeDepend() { smUnimplemented(); }

static void serialize() { smUnimplemented(); }

static FILE *openFileCstr(char const *name, char const *modes) {
    FILE *hnd = fopen(name, modes);
    if (!hnd) {
        smFatal("could not open file: %s: %s\n", name, strerror(errno));
    }
    return hnd;
}

static void closeFile(FILE *hnd) {
    if (fclose(hnd) == EOF) {
        smFatal("failed to close file: %s\n", strerror(errno));
    }
}

static FILE *openFile(SmBuf path, char const *modes) {
    static SmGBuf buf = {0};
    buf.inner.len     = 0;
    smGBufCat(&buf, path);
    smGBufCat(&buf, (SmBuf){(U8 *)"\0", 1});
    return openFileCstr((char const *)buf.inner.bytes, modes);
}

static void pushFile(SmBuf path) {
    FILE *hnd = openFile(path, "rb");
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    smTokStreamFileInit(ts, path, hnd);
}

static SmBuf intern(SmBuf buf) { return smBufIntern(&STRS, buf); }

static SmExprBuf constExprBuf(I32 num) {
    SmExpr expr = constExpr(num);
    return smExprIntern(&EXPRS, (SmExprBuf){&expr, 1});
}

enum FmtState {
    FMT_STATE_INIT,
    FMT_STATE_FLAG_OPT,
    FMT_STATE_WIDTH_OPT,
    FMT_STATE_PREC_DOT_OPT,
    FMT_STATE_PREC_OPT,
    FMT_STATE_SPEC,
};

enum FmtFlags {
    FMT_FLAG_JUSTIFY_LEFT = 1 << 0,
    FMT_FLAG_FORCE_SIGN   = 1 << 1,
    FMT_FLAG_PAD_SIGN     = 1 << 2,
    FMT_FLAG_NUM_MOD      = 1 << 3,
    FMT_FLAG_ZERO_JUSTIFY = 1 << 4,
    // technically not a format flag, but easier to repr this way
    FMT_FLAG_UPPERCASE    = 1 << 5,
};

static UInt uMax(UInt lhs, UInt rhs) {
    if (lhs > rhs) {
        return lhs;
    }
    return rhs;
}

static UInt uMin(UInt lhs, UInt rhs) {
    if (lhs < rhs) {
        return lhs;
    }
    return rhs;
}

static const U8 DIGITS[]       = "0123456789abcdef";
static const U8 DIGITS_UPPER[] = "0123456789ABCDEF";

void fmtUInt(SmGBuf *buf, I32 num, I32 radix, U8 flags, U16 width, U16 prec,
             Bool negative) {
    // write digits to a small buffer (at least big enough to hold 32 bits
    // of binary)
    U8        numbytes[32];
    U8       *end    = numbytes + 32;
    U8 const *digits = DIGITS;
    if (flags & FMT_FLAG_UPPERCASE) {
        digits = DIGITS_UPPER;
    }
    do {
        *(--end) = digits[num % radix];
        num /= radix;
    } while (num);
    SmBuf numbuf = {end, (numbytes + 32) - end};
    prec         = uMax(prec, numbuf.len);
    UInt len     = uMax(width, prec);
    if (negative || (flags & (FMT_FLAG_FORCE_SIGN | FMT_FLAG_PAD_SIGN))) {
        ++len;
    }
    UInt i   = 0;
    UInt pad = len - prec;
    // if we are not left-justifying, then we will write padding
    if (!(flags & FMT_FLAG_JUSTIFY_LEFT)) {
        U8 c = ' ';
        if (flags & FMT_FLAG_ZERO_JUSTIFY) {
            c = '0';
        }
        for (; i < pad; ++i) {
            smGBufCat(buf, (SmBuf){&c, 1});
        }
    }
    // write sign
    if (i < len) {
        if (negative) {
            smGBufCat(buf, SM_BUF("-"));
        } else if (flags & FMT_FLAG_PAD_SIGN) {
            smGBufCat(buf, SM_BUF(" "));
        } else if (flags & FMT_FLAG_FORCE_SIGN) {
            smGBufCat(buf, SM_BUF("+"));
        }
        ++i;
    }
    // add leading zeros to reach the desired precision
    for (pad = prec - numbuf.len; pad > 0; --pad) {
        smGBufCat(buf, SM_BUF("0"));
        ++i;
    }
    // write the actual number
    smGBufCat(buf, numbuf);
    i += numbuf.len;
    // write out any leftover padding
    for (; i < len; ++i) {
        smGBufCat(buf, SM_BUF(" "));
    }
}

static void fmtStr(SmGBuf *buf, SmBuf str, U8 flags, U16 width, U16 prec) {
    UInt len = 0;
    if (prec == 0) {
        prec = str.len;
    }
    len += uMax(width, prec);
    UInt i   = 0;
    UInt pad = len - prec;
    // if we are not left-justifying, then we will write padding
    if (!(flags & FMT_FLAG_JUSTIFY_LEFT)) {
        U8 c = ' ';
        if (flags & FMT_FLAG_ZERO_JUSTIFY) {
            c = '0';
        }
        for (; i < pad; ++i) {
            smGBufCat(buf, (SmBuf){&c, 1});
        }
    }
    // write the str
    smGBufCat(buf, (SmBuf){str.bytes, uMin(prec, str.len)});
    i += uMin(prec, str.len);
    // write out any leftover padding
    for (; i < len; ++i) {
        smGBufCat(buf, SM_BUF(" "));
    }
}

static void fmtInt(SmGBuf *buf, I32 num, I32 radix, U8 flags, U16 width,
                   U8 prec) {
    Bool negative = false;
    if (num < 0) {
        num      = -num;
        negative = true;
    }
    fmtUInt(buf, num, radix, flags, width, prec, negative);
}

static UInt scanDigits(SmBuf fmt, U16 *num) {
    UInt len;
    for (len = 0; len < fmt.len; ++len) {
        if (!isdigit(fmt.bytes[len])) {
            break;
        }
    }
    I32 bignum = smParse((SmBuf){fmt.bytes, len});
    if (!canReprU16(bignum)) {
        fatal("expression does not fit in word\n");
    }
    *num = (U16)bignum;
    return len;
}

static void invokeFmt(U32 tok) {
    eat();
    Bool braced = false;
    if (peek() == '{') {
        eat();
        braced = true;
    }
    SmPos pos = tokPos();
    expect(SM_TOK_STR);
    SmGBuf fmt = {0};
    smGBufCat(&fmt, tokBuf());
    eat();
    SmGBuf buf      = {0};
    U8     stack[6] = {FMT_STATE_INIT};
    U8     top      = 0;
    U8     flags    = 0;
    U16    width    = 0;
    U16    prec     = 0;
    for (UInt i = 0; i < fmt.inner.len; ++i) {
        U8 c = fmt.inner.bytes[i];
        switch (stack[top]) {
        case FMT_STATE_INIT:
            if (c == '%') {
                flags        = 0;
                width        = 0;
                prec         = 0;
                stack[++top] = FMT_STATE_SPEC;
                stack[++top] = FMT_STATE_PREC_DOT_OPT;
                stack[++top] = FMT_STATE_WIDTH_OPT;
                stack[++top] = FMT_STATE_FLAG_OPT;
                break;
            }
            smGBufCat(&buf, (SmBuf){&c, 1});
            break;
        case FMT_STATE_FLAG_OPT:
            switch (c) {
            case '%':
                smGBufCat(&buf, SM_BUF("%"));
                top = 0;
                break;
            case '-':
                flags |= FMT_FLAG_JUSTIFY_LEFT;
                break;
            case '+':
                flags |= FMT_FLAG_FORCE_SIGN;
                break;
            case ' ':
                flags |= FMT_FLAG_PAD_SIGN;
                break;
            case '#':
                flags |= FMT_FLAG_NUM_MOD;
                break;
            case '0':
                flags |= FMT_FLAG_ZERO_JUSTIFY;
                break;
            default:
                --top;
                --i;
            }
            break;
        case FMT_STATE_WIDTH_OPT:
            --top;
            if (c == '*') {
                expect(',');
                eat();
                width = eatSolvedU16();
            } else if (isdigit(c)) {
                i += scanDigits((SmBuf){fmt.inner.bytes + i, fmt.inner.len - i},
                                &width) -
                     1;
            } else {
                --i;
            }
            break;
        case FMT_STATE_PREC_DOT_OPT:
            --top;
            if (c == '.') {
                stack[++top] = FMT_STATE_PREC_OPT;
            } else {
                --i;
            }
            break;
        case FMT_STATE_PREC_OPT:
            --top;
            if (c == '*') {
                expect(',');
                eat();
                prec = eatSolvedU16();
            } else if (isdigit(c)) {
                i += scanDigits((SmBuf){fmt.inner.bytes + i, fmt.inner.len - i},
                                &width) -
                     1;
            } else {
                --i;
            }
            break;
        case FMT_STATE_SPEC: {
            --top;
            SmPos expr_pos;
            expect(',');
            eat();
            switch (c) {
            case 'c': {
                U8 c = eatSolvedU8();
                smGBufCat(&buf, (SmBuf){&c, 1});
                break;
            }
            case 'b':
                fmtUInt(&buf, eatSolvedPos(&expr_pos), 2, flags, width, prec,
                        false);
                break;
            case 'd':
            case 'i':
                fmtInt(&buf, eatSolvedPos(&expr_pos), 10, flags, width, prec);
                break;
            case 'u':
                fmtUInt(&buf, eatSolvedPos(&expr_pos), 10, flags, width, prec,
                        false);
                break;
            case 'X':
                flags |= FMT_FLAG_UPPERCASE;
                // fall through
            case 'x':
                fmtUInt(&buf, eatSolvedPos(&expr_pos), 16, flags, width, prec,
                        false);
                break;
            case 's':
                if ((peek() != SM_TOK_STR) && (peek() != SM_TOK_ID)) {
                    fatal("expected string or identifier\n");
                }
                fmtStr(&buf, tokBuf(), flags, width, prec);
                eat();
                break;
            default:
                fatalPos(pos, "unrecognized format conversion: %c\n", c);
            }
            break;
        }
        default:
            smUnreachable();
        }
    }
    if (braced) {
        expect('}');
        eat();
    }
    if (fmt.inner.bytes) {
        free(fmt.inner.bytes);
    }
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    switch (tok) {
    case SM_TOK_STR:
    case SM_TOK_ID:
        smTokStreamFmtInit(ts, intern(buf.inner), pos, tok);
        return;
    default:
        smUnreachable();
    }
}
