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
static Bool canReprI8(I32 num) { return (num >= I8_MIN) && (num <= I8_MAX); }

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
        fatalPos(pos, "expression does not fit in a byte: $%08X\n", num);
    }
    return (U8)num;
}

static U16 eatSolvedU16() {
    SmPos pos;
    I32   num = eatSolvedPos(&pos);
    if (!canReprU16(num)) {
        fatalPos(pos, "expression does not fit in a word: $%08X\n", num);
    }
    return (U16)num;
}

static void emitBuf(SmBuf buf) { smGBufCat(&getSection()->data, buf); }
static void emit8(U8 byte) { emitBuf((SmBuf){&byte, 1}); }
static void emit16(U16 word) {
    U8 bytes[2] = {word & 0x00FF, word >> 8};
    emitBuf((SmBuf){bytes, 2});
}

static void reloc(U16 offset, U8 width, SmExprBuf buf, SmPos pos, U8 flags) {
    smRelocGBufAdd(&getSection()->relocs, (SmReloc){
                                              .offset = getPC() + offset,
                                              .width  = width,
                                              .value  = buf,
                                              .unit   = STATIC_UNIT,
                                              .pos    = pos,
                                              .flags  = flags,
                                          });
}

static void addPC(U16 offset) {
    I32 cur = (U32)getPC();
    I32 new = cur + offset;
    if (new > ((I32)(U32)U16_MAX)) {
        fatal("pc overflow: $%08X\n", new);
    }
    setPC(new);
}

static Bool loadIndirect(U32 tok, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = 0x0A;
        return true;
    case SM_TOK_DE:
        *op = 0x1A;
        return true;
    case SM_TOK_HL:
        *op = 0x7E;
        return true;
    default:
        return false;
    }
}

static Bool storeIndirect(U32 tok, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = 0x02;
        return true;
    case SM_TOK_DE:
        *op = 0x12;
        return true;
    default:
        return false;
    }
}

static Bool reg8Offset(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case 'B':
        *op = base + 0;
        return true;
    case 'C':
        *op = base + 1;
        return true;
    case 'D':
        *op = base + 2;
        return true;
    case 'E':
        *op = base + 3;
        return true;
    case 'H':
        *op = base + 4;
        return true;
    case 'L':
        *op = base + 5;
        return true;
    case 'A':
        *op = base + 7;
        return true;
    default:
        return false;
    }
}

static Bool reg16OffsetSP(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = base + 0x00;
        return true;
    case SM_TOK_DE:
        *op = base + 0x10;
        return true;
    case SM_TOK_HL:
        *op = base + 0x20;
        return true;
    case SM_TOK_SP:
        *op = base + 0x30;
        return true;
    default:
        return false;
    }
}

static void loadStoreIncDec(U8 load, U8 store) {
    eat();
    switch (peek()) {
    case 'A':
        eat();
        expect(',');
        eat();
        expect('[');
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        if (emit) {
            emit8(load);
        }
        addPC(1);
        return;
    default:
        expect('[');
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        expect(',');
        eat();
        expect('A');
        eat();
        if (emit) {
            emit8(store);
        }
        addPC(1);
        return;
    }
}

static Bool reg16OffsetAF(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = base + 0x00;
        return true;
    case SM_TOK_DE:
        *op = base + 0x10;
        return true;
    case SM_TOK_HL:
        *op = base + 0x20;
        return true;
    case SM_TOK_AF:
        *op = base + 0x30;
        return true;
    default:
        return false;
    }
}

static void pushPop(U8 base) {
    U8 op;
    eat();
    if (!reg16OffsetAF(peek(), base, &op)) {
        fatal("illegal operand\n");
    }
    eat();
    if (emit) {
        emit8(op);
    }
    addPC(1);
}

static void aluReg8(U8 base, U8 imm) {
    U8        op;
    SmPos     expr_pos;
    SmExprBuf buf;
    I32       num;
    eat();
    expect(',');
    eat();
    if (reg8Offset(peek(), base, &op)) {
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(1);
        return;
    }
    if (peek() == '[') {
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        if (emit) {
            emit8(base + 6);
        }
        addPC(1);
        return;
    }
    buf = eatExprPos(&expr_pos);
    if (emit) {
        emit8(imm);
        if (solve(buf, &num)) {
            if (!canReprU8(num)) {
                fatalPos(expr_pos, "expression does not fit in byte: $%08X\n",
                         num);
            }
            emit8(num);
        } else {
            emit8(0xFD);
            reloc(1, 1, buf, expr_pos, 0);
        }
    }
}

static void doAluReg8Cb(U8 base) {
    U8 op;
    eat();
    if (emit) {
        emit8(0xCB);
    }
    if (reg8Offset(peek(), base, &op)) {
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(2);
        return;
    }
    expect('[');
    eat();
    expect(SM_TOK_HL);
    eat();
    expect(']');
    eat();
    if (emit) {
        emit8(base + 6);
    }
    addPC(2);
}

static void doBitCb(U8 base) {
    U8        op;
    SmPos     expr_pos;
    SmExprBuf buf;
    I32       num;
    eat();
    buf = eatExprPos(&expr_pos);
    expect(',');
    eat();
    if (reg8Offset(peek(), 0x00, &op)) {
        eat();
    } else {
        expect('[');
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        op = base + 6;
    }
    if (emit) {
        emit8(0xCB);
        if (!solve(buf, &num)) {
            fatalPos(expr_pos, "expression must be constant\n");
        }
        if ((num < 0) || (num > 7)) {
            fatalPos(expr_pos, "bit number must be between 0 and 7\n");
        }
        emit8(base + ((U8)num << 8) + op);
    }
    addPC(2);
}

static Bool flag(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case SM_TOK_NZ:
        *op = base + 0x00;
        return true;
    case 'Z':
        *op = base + 0x08;
        return true;
    case SM_TOK_NC:
        *op = base + 0x10;
        return true;
    case 'C':
        *op = base + 0x18;
        return true;
    default:
        return false;
    }
}

static void eatMne(U8 mne) {
    U8        op;
    SmPos     expr_pos;
    SmExprBuf buf;
    I32       num;
    switch (mne) {
    case SM_MNE_LD:
        eat();
        switch (peek()) {
        case 'A':
            eat();
            expect(',');
            eat();
            switch (peek()) {
            case '[':
                eat();
                if (loadIndirect(peek(), &op)) {
                    eat();
                    expect(']');
                    eat();
                    if (emit) {
                        emit8(op);
                    }
                    addPC(1);
                    return;
                }
                buf = eatExprPos(&expr_pos);
                expect(']');
                eat();
                if (emit) {
                    emit8(0xFA);
                    if (solve(buf, &num)) {
                        if (!canReprU16(num)) {
                            fatalPos(
                                expr_pos,
                                "expression does not fit in a word: $%08X\n",
                                num);
                        }
                        emit16(num);
                    } else {
                        emit16(0xFDFD);
                        reloc(1, 2, buf, expr_pos, 0);
                    }
                }
                addPC(3);
                return;
            default:
                if (reg8Offset(peek(), 0x78, &op)) {
                    eat();
                    if (emit) {
                        emit8(op);
                    }
                    addPC(1);
                    return;
                }
                buf = eatExprPos(&expr_pos);
                if (emit) {
                    emit8(0x3E);
                    if (solve(buf, &num)) {
                        if (!canReprU8(num)) {
                            fatalPos(expr_pos,
                                     "expression does not fit in byte: $%08X\n",
                                     num);
                        }
                        emit8(num);
                    } else {
                        emit8(0xFD);
                        reloc(1, 1, buf, expr_pos, 0);
                    }
                }
                addPC(2);
                return;
            }
        case 'B':
            aluReg8(0x40, 0x06);
            return;
        case 'C':
            aluReg8(0x48, 0x0E);
            return;
        case 'D':
            aluReg8(0x50, 0x16);
            return;
        case 'E':
            aluReg8(0x58, 0x1E);
            return;
        case 'H':
            aluReg8(0x60, 0x26);
            return;
        case 'L':
            aluReg8(0x68, 0x2E);
            return;
        case '[':
            eat();
            if (storeIndirect(peek(), &op)) {
                eat();
                expect(']');
                eat();
                expect(',');
                eat();
                expect('A');
                eat();
                if (emit) {
                    emit8(op);
                }
                addPC(1);
                return;
            }
            if (peek() == SM_TOK_HL) {
                eat();
                expect(']');
                eat();
                expect(',');
                eat();
                if (reg8Offset(peek(), 0x70, &op)) {
                    eat();
                    if (emit) {
                        emit8(op);
                    }
                    addPC(1);
                    return;
                }
                buf = eatExprPos(&expr_pos);
                if (emit) {
                    emit8(0x36);
                    if (solve(buf, &num)) {
                        if (!canReprU8(num)) {
                            fatalPos(expr_pos,
                                     "expression does not fit in byte: $%08X\n",
                                     num);
                        }
                        emit8(num);
                    } else {
                        emit8(0xFD);
                        reloc(1, 1, buf, expr_pos, 0);
                    }
                }
                addPC(2);
                return;
            }
            buf = eatExprPos(&expr_pos);
            expect(']');
            eat();
            expect(',');
            eat();
            if (peek() == SM_TOK_SP) {
                eat();
                op = 0x08;
            } else {
                expect('A');
                op = 0xEA;
            }
            if (emit) {
                emit8(op);
                if (solve(buf, &num)) {
                    if (!canReprU16(num)) {
                        fatalPos(expr_pos,
                                 "expression does not fit in a word: $%08X\n",
                                 num);
                    }
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(1, 2, buf, expr_pos, 0);
                }
            }
            addPC(3);
            return;
        default:
            if (reg16OffsetSP(peek(), 0x01, &op)) {
                eat();
                expect(',');
                eat();
                buf = eatExprPos(&expr_pos);
                if (emit) {
                    emit8(op);
                    if (solve(buf, &num)) {
                        if (!canReprU16(num)) {
                            fatalPos(
                                expr_pos,
                                "expression does not fit in a word: $%08X\n",
                                num);
                        }
                        emit16(num);
                    } else {
                        emit16(0xFDFD);
                        reloc(1, 2, buf, expr_pos, 0);
                    }
                }
                addPC(3);
                return;
            }
            fatal("illegal operand\n");
        }
    case SM_MNE_LDD:
        loadStoreIncDec(0x3A, 0x32);
        return;
    case SM_MNE_LDI:
        loadStoreIncDec(0x2A, 0x22);
        return;
    case SM_MNE_LDH:
        eat();
        if (peek() == 'A') {
            expect(',');
            eat();
            expect('[');
            eat();
            if (peek() == 'C') {
                eat();
                expect(']');
                eat();
                if (emit) {
                    emit8(0xE2);
                }
                addPC(1);
                return;
            }
            buf = eatExprPos(&expr_pos);
            expect(']');
            eat();
            if (emit) {
                emit8(0xF0);
                if (solve(buf, &num)) {
                    if ((num < 0xFF00) || (num > 0xFFFF)) {
                        fatalPos(expr_pos, "address not in high memory: %08X\n",
                                 num);
                    }
                    emit8(num & 0x00FF);
                } else {
                    emit8(0xFD);
                    reloc(1, 1, buf, expr_pos, SM_RELOC_HI);
                }
            }
            addPC(2);
            return;
        }
        expect('[');
        eat();
        if (peek() == 'C') {
            eat();
            expect(']');
            eat();
            expect(',');
            eat();
            expect('A');
            eat();
            if (emit) {
                emit8(0xF2);
            }
            addPC(1);
            return;
        }
        buf = eatExprPos(&expr_pos);
        expect(']');
        eat();
        expect(',');
        eat();
        expect('A');
        eat();
        if (emit) {
            emit8(0xE0);
            if (solve(buf, &num)) {
                if ((num < 0xFF00) || (num > 0xFFFF)) {
                    fatalPos(expr_pos, "address not in high memory: %08X\n",
                             num);
                }
                emit8(num & 0x00FF);
            } else {
                emit8(0xFD);
                reloc(1, 1, buf, expr_pos, SM_RELOC_HI);
            }
        }
        addPC(2);
        return;
    case SM_MNE_PUSH:
        pushPop(0xC5);
        return;
    case SM_MNE_POP:
        pushPop(0xC1);
        return;
    case SM_MNE_ADD:
        eat();
        switch (peek()) {
        case SM_TOK_HL:
            eat();
            expect(',');
            eat();
            if (!reg16OffsetSP(peek(), 0x09, &op)) {
                fatal("illegal operand\n");
            }
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        case SM_TOK_SP:
            eat();
            expect(',');
            eat();
            buf = eatExprPos(&expr_pos);
            if (emit) {
                emit8(0xE8);
                if (solve(buf, &num)) {
                    if (!canReprU8(num)) {
                        fatalPos(expr_pos,
                                 "expression does not fit in a byte: $%08X\n",
                                 num);
                    }
                    emit8(num);
                } else {
                    emit8(0xFD);
                    reloc(1, 1, buf, expr_pos, 0);
                }
            }
            addPC(2);
            return;
        default:
            expect('A');
            aluReg8(0x80, 0xC6);
            return;
        }
    case SM_MNE_ADC:
        eat();
        expect('A');
        aluReg8(0x88, 0xCE);
        return;
    case SM_MNE_SUB:
        eat();
        expect('A');
        aluReg8(0x90, 0xD6);
        return;
    case SM_MNE_SBC:
        eat();
        expect('A');
        aluReg8(0x98, 0xDE);
        return;
    case SM_MNE_AND:
        eat();
        expect('A');
        aluReg8(0xA0, 0xE6);
        return;
    case SM_MNE_XOR:
        eat();
        expect('A');
        aluReg8(0xA8, 0xEE);
        return;
    case SM_MNE_OR:
        eat();
        expect('A');
        aluReg8(0xB0, 0xF6);
        return;
    case SM_MNE_CP:
        eat();
        expect('A');
        aluReg8(0xB8, 0xFE);
        return;
    case SM_MNE_INC:
        if (reg16OffsetSP(peek(), 0x03, &op)) {
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        }
        switch (peek()) {
        case 'B':
            op = 0x04;
            break;
        case 'C':
            op = 0x0C;
            break;
        case 'D':
            op = 0x14;
            break;
        case 'E':
            op = 0x1C;
            break;
        case 'H':
            op = 0x24;
            break;
        case 'L':
            op = 0x2C;
            break;
        case '[':
            eat();
            expect(SM_TOK_HL);
            eat();
            expect(']');
            op = 0x34;
            break;
        case 'A':
            op = 0x3C;
            break;
        default:
            fatal("illegal operand\n");
        }
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(1);
        return;
    case SM_MNE_DEC:
        if (reg16OffsetSP(peek(), 0x0B, &op)) {
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        }
        switch (peek()) {
        case 'B':
            op = 0x05;
            break;
        case 'C':
            op = 0x0D;
            break;
        case 'D':
            op = 0x15;
            break;
        case 'E':
            op = 0x1D;
            break;
        case 'H':
            op = 0x25;
            break;
        case 'L':
            op = 0x2D;
            break;
        case '[':
            eat();
            expect(SM_TOK_HL);
            eat();
            expect(']');
            op = 0x35;
            break;
        case 'A':
            op = 0x3D;
            break;
        default:
            fatal("illegal operand\n");
        }
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(1);
        return;
    case SM_MNE_DAA:
        eat();
        if (emit) {
            emit8(0x27);
        }
        addPC(1);
        return;
    case SM_MNE_CPL:
        eat();
        if (emit) {
            emit8(0x2F);
        }
        addPC(1);
        return;
    case SM_MNE_CCF:
        eat();
        if (emit) {
            emit8(0x3F);
        }
        addPC(1);
        return;
    case SM_MNE_SCF:
        eat();
        if (emit) {
            emit8(0x37);
        }
        addPC(1);
        return;
    case SM_MNE_NOP:
        eat();
        if (emit) {
            emit8(0x00);
        }
        addPC(1);
        return;
    case SM_MNE_HALT:
        eat();
        if (emit) {
            emit8(0x76);
            emit8(0x00);
        }
        addPC(2);
        return;
    case SM_MNE_STOP:
        eat();
        if (emit) {
            emit8(0x10);
            emit8(0x00);
        }
        addPC(2);
        return;
    case SM_MNE_DI:
        eat();
        if (emit) {
            emit8(0xF3);
        }
        addPC(1);
        return;
    case SM_MNE_EI:
        eat();
        if (emit) {
            emit8(0xFB);
        }
        addPC(1);
        return;
    case SM_MNE_RETI:
        eat();
        if (emit) {
            emit8(0xD9);
        }
        addPC(1);
        return;
    case SM_MNE_RLCA:
        eat();
        if (emit) {
            emit8(0x07);
        }
        addPC(1);
        return;
    case SM_MNE_RLA:
        eat();
        if (emit) {
            emit8(0x17);
        }
        addPC(1);
        return;
    case SM_MNE_RRCA:
        eat();
        if (emit) {
            emit8(0x0F);
        }
        addPC(1);
        return;
    case SM_MNE_RRA:
        eat();
        if (emit) {
            emit8(0x1F);
        }
        addPC(1);
        return;
    case SM_MNE_RLC:
        doAluReg8Cb(0x00);
        return;
    case SM_MNE_RRC:
        doAluReg8Cb(0x08);
        return;
    case SM_MNE_RL:
        doAluReg8Cb(0x10);
        return;
    case SM_MNE_RR:
        doAluReg8Cb(0x18);
        return;
    case SM_MNE_SLA:
        doAluReg8Cb(0x20);
        return;
    case SM_MNE_SRA:
        doAluReg8Cb(0x28);
        return;
    case SM_MNE_SWAP:
        doAluReg8Cb(0x30);
        return;
    case SM_MNE_SRL:
        doAluReg8Cb(0x38);
        return;
    case SM_MNE_BIT:
        doBitCb(0x40);
        return;
    case SM_MNE_RES:
        doBitCb(0x80);
        return;
    case SM_MNE_SET:
        doBitCb(0xC0);
        return;
    case SM_MNE_JP:
        eat();
        if (flag(peek(), 0xC2, &op)) {
            eat();
            expect(',');
            eat();
            buf = eatExprPos(&expr_pos);
            if (emit) {
                emit8(op);
                if (solve(buf, &num)) {
                    if (!canReprU16(num)) {
                        fatalPos(expr_pos,
                                 "expression does not fit in a word: $%08X\n",
                                 num);
                    }
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(1, 2, buf, expr_pos, SM_RELOC_JP);
                }
            }
            addPC(3);
            return;
        }
        if (peek() == SM_TOK_HL) {
            eat();
            if (emit) {
                emit8(0xE9);
            }
            addPC(1);
            return;
        }
        buf = eatExprPos(&expr_pos);
        if (emit) {
            emit8(0xC3);
            if (solve(buf, &num)) {
                if (!canReprU16(num)) {
                    fatalPos(expr_pos,
                             "expression does not fit in a word: $%08X\n", num);
                }
                emit16(num);
            } else {
                emit16(0xFDFD);
                reloc(1, 2, buf, expr_pos, SM_RELOC_JP);
            }
        }
        addPC(3);
        return;
    case SM_MNE_JR:
        eat();
        if (flag(peek(), 0x20, &op)) {
            eat();
            expect(',');
            eat();
            buf = eatExprPos(&expr_pos);
            if (emit) {
                emit8(op);
                if (!solve(buf, &num)) {
                    fatalPos(expr_pos, "branch distance must be constant");
                }
                I32 offset = num - ((I32)(U32)getPC()) - 2;
                if (!canReprI8(offset)) {
                    fatalPos(expr_pos, "branch distance too far\n");
                }
                emit8(offset);
            }
            addPC(2);
            return;
        }
        buf = eatExprPos(&expr_pos);
        if (emit) {
            emit8(0x18);
            if (!solve(buf, &num)) {
                fatalPos(expr_pos, "branch distance must be constant");
            }
            I32 offset = num - ((I32)(U32)getPC()) - 2;
            if (!canReprI8(offset)) {
                fatalPos(expr_pos, "branch distance too far\n");
            }
            emit8(offset);
        }
        addPC(2);
        return;
    case SM_MNE_CALL:
        eat();
        if (flag(peek(), 0xC4, &op)) {
            eat();
            expect(',');
            eat();
            buf = eatExprPos(&expr_pos);
            if (emit) {
                emit8(op);
                if (solve(buf, &num)) {
                    if (!canReprU16(num)) {
                        fatalPos(expr_pos,
                                 "expression does not fit in a word: $%08X\n",
                                 num);
                    }
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(1, 2, buf, expr_pos, SM_RELOC_JP);
                }
            }
            addPC(3);
            return;
        }
        buf = eatExprPos(&expr_pos);
        if (emit) {
            emit8(0xCD);
            if (solve(buf, &num)) {
                if (!canReprU16(num)) {
                    fatalPos(expr_pos,
                             "expression does not fit in a word: $%08X\n", num);
                }
                emit16(num);
            } else {
                emit16(0xFDFD);
                reloc(1, 2, buf, expr_pos, SM_RELOC_JP);
            }
        }
        addPC(3);
        return;
    case SM_MNE_RET:
        eat();
        if (flag(peek(), 0xC0, &op)) {
            eat();
            expect(',');
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        }
        if (emit) {
            emit8(0xC9);
        }
        addPC(1);
        return;
    case SM_MNE_RST:
        eat();
        buf = eatExprPos(&expr_pos);
        if (emit) {
            if (solve(buf, &num)) {
                switch (num) {
                case 0x00:
                    break;
                case 0x08:
                    break;
                case 0x10:
                    break;
                case 0x18:
                    break;
                case 0x20:
                    break;
                case 0x28:
                    break;
                case 0x30:
                    break;
                case 0x38:
                    break;
                default:
                    fatalPos(expr_pos, "illegal reset vector: $%08X\n", num);
                }
                emit8(0xC7 + num);
            } else {
                emit8(0xFD);
                reloc(0, 1, buf, expr_pos, SM_RELOC_RST);
            }
        }
        addPC(1);
        return;
    default:
        smUnreachable();
    }
}

static void eatDirective() { smUnimplemented(); }

static SmLbl const NULL_LABEL = {0};

static SmExprBuf addrExprBuf(SmBuf section, U16 offset) {
    SmExpr expr = addrExpr(section, offset);
    return smExprIntern(&EXPRS, (SmExprBuf){&expr, 1});
}

static SmExprBuf constExprBuf(I32 num) {
    SmExpr expr = constExpr(num);
    return smExprIntern(&EXPRS, (SmExprBuf){&expr, 1});
}

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
                eatMne(*mne);
                expectEOL();
                eat();
                continue;
            }
            Macro *macro = macroFind(&MACS, tokBuf());
            if (macro) {
                invoke(*macro);
                continue;
            }
            SmPos pos = tokPos();
            SmLbl lbl = tokLbl();
            eat();
            SmSym *sym = smSymTabFind(&SYMS, lbl);
            if (!sym) {
                // create a placeholder symbol that we'll fill in soon
                sym = smSymTabAdd(&SYMS, (SmSym){
                                             .lbl     = lbl,
                                             .value   = constExprBuf(0),
                                             .unit    = STATIC_UNIT,
                                             .section = getSection()->name,
                                             .pos     = pos,
                                             .flags   = 0,
                                         });
            } else if (!emit) {
                // we are allowed to redefine labels for the second
                // pass.
                // TODO: but we need to ensure the value of the label
                // never changes.
                // TODO: also we want to create weak/redefinable symbols
                fatalPos(pos,
                         "symbol already defined\n\t%.*s:%zu:%zu: "
                         "defined previously here\n",
                         sym->pos.file.len, sym->pos.file.bytes, sym->pos.line,
                         sym->pos.col);
            }
            switch (peek()) {
            case ':':
                eat();
                break;
            case SM_TOK_DCOLON:
                eat();
                if (emit) {
                    sym->unit = EXPORT_UNIT;
                }
                break;
            case '=':
                eat();
                I32 num;
                if (emit) {
                    sym->value = constExprBuf(eatSolvedPos(&pos));
                    sym->flags = SM_SYM_EQU;
                } else if (solve(eatExpr(), &num)) {
                    sym->value = constExprBuf(num);
                    sym->flags = SM_SYM_EQU;
                } else {
                    sym->lbl = NULL_LABEL;
                }
                expectEOL();
                eat();
                continue;
            default:
                if (!smLblIsGlobal(lbl)) {
                    fatal("expected `:` or `=`\n");
                }
                fatalPos(pos, "unrecognized instruction\n");
            }
            // This just a new label then
            if (smLblIsGlobal(sym->lbl)) {
                scope = sym->lbl.name;
            }
            sym->value = addrExprBuf(getSection()->name, getPC());
            continue;
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
    // write digits to a small buffer (at least big enough to hold 32
    // bits of binary)
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
        fatal("expression does not fit in a word: $%08X\n", bignum);
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
