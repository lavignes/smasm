#include "cfg.h"

#include <smasm/fatal.h>
#include <smasm/path.h>
#include <smasm/serde.h>
#include <smasm/sym.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void help() {
    fprintf(
        stderr,
        "SMOLD: A linker for the SM83 (Gameboy) CPU\n"
        "\n"
        "Usage: smold [OPTIONS] --config <CONFIG> [OBJECTS]...\n"
        "\n"
        "Arguments:\n"
        "  [OBJECTS]...  Object files\n"
        "\n"
        "Options:\n"
        "  -c, --config <CONFIG>        Config file\n"
        "  -o, --output <OUTPUT>        Output file (default: stdout)\n"
        "  -g, --debug <DEBUG>          Output file for `SYM` debug symbols\n"
        "      --tags <TAGS>            Output file for ctags\n"
        "  -D, --define <KEY1=val>      Pre-defined symbols (repeatable)\n"
        "  -h, --help                   Print help\n");
}

static FILE     *openFileCstr(char const *path, char const *modes);
static void      closeFile(FILE *hnd);
static SmLbl     globalLbl(SmBuf name);
static SmExprBuf constExprBuf(I32 num);
static SmBuf     intern(SmBuf buf);
static void      parseCfg();
static void      loadObj(SmBuf path);
static void      allocate(SmSect *sect);
static void      solveSyms();
static void      link(SmSect *sect);
static void      serialize();
static void      writeSyms();
static void      writeTags();

static FILE *cfgfile      = NULL;
static char *cfgfile_name = NULL;
static FILE *outfile      = NULL;
static char *outfile_name = NULL;
static char *symfile_name = NULL;
static char *tagfile_name = NULL;

static SmBufIntern  STRS  = {0};
static SmSymTab     SYMS  = {0};
static SmExprIntern EXPRS = {0};
static SmPathSet    OBJS  = {0};
static SmSectGBuf   SECTS = {0};

static CfgOutGBuf CFGS    = {0};

static SmBuf DEFINES_SECTION;
static SmBuf STATIC_UNIT;
static SmBuf EXPORT_UNIT;

int main(int argc, char **argv) {
    outfile = stdout;
    if (argc == 1) {
        help();
        return 0;
    }
    DEFINES_SECTION = intern(SM_BUF("@DEFINES"));
    STATIC_UNIT     = intern(SM_BUF("@STATIC"));
    EXPORT_UNIT     = intern(SM_BUF("@EXPORT"));
    for (int argi = 1; argi < argc; ++argi) {
        if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
            help();
            return 0;
        }
        if (!strcmp(argv[argi], "-c") || !strcmp(argv[argi], "--config")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            cfgfile      = openFileCstr(argv[argi], "rb");
            cfgfile_name = argv[argi];
            continue;
        }
        if (!strcmp(argv[argi], "-o") || !strcmp(argv[argi], "--output")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            outfile_name = argv[argi];
            continue;
        }
        if (!strcmp(argv[argi], "-g") || !strcmp(argv[argi], "--debug")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            symfile_name = argv[argi];
            continue;
        }
        if (!strcmp(argv[argi], "--tags")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            tagfile_name = argv[argi];
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
            UInt  name_len  = offset - argv[argi];
            SmBuf name      = intern((SmBuf){(U8 *)argv[argi], name_len});
            UInt  value_len = strlen(argv[argi]) - name_len - 1;
            // TODO: should expose general-purpose expression parsing
            // for use here and for expressions in the linker scripts
            // Could even use a tok stream here.
            I32   num       = smBufParse(
                (SmBuf){(U8 *)(argv[argi] + name_len + 1), value_len});
            smSymTabAdd(&SYMS, (SmSym){
                                   .lbl     = globalLbl(name),
                                   .value   = constExprBuf(num),
                                   .unit    = EXPORT_UNIT,
                                   .section = DEFINES_SECTION,
                                   .pos     = {DEFINES_SECTION, 1, 1},
                                   .flags   = SM_SYM_EQU,
                               });
            continue;
        }
        smPathSetAdd(&OBJS, (SmBuf){(U8 *)argv[argi], strlen(argv[argi])});
    }
    if (!cfgfile) {
        smFatal("config file is required\n");
    }
    if (OBJS.bufs.inner.len == 0) {
        smFatal("at least 1 object file is required\n");
    }

    parseCfg();

    for (UInt i = 0; i < OBJS.bufs.inner.len; ++i) {
        loadObj(OBJS.bufs.inner.items[i]);
    }
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        allocate(sect);
    }
    solveSyms();
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        link(sect);
    }

    if (outfile_name) {
        outfile = openFileCstr(outfile_name, "wb+");
    } else {
        outfile_name = "stdout";
    }

    if (symfile_name) {
        writeSyms();
    }
    if (tagfile_name) {
        writeTags();
    }

    serialize();
    closeFile(outfile);
    return EXIT_SUCCESS;
}

static SmBuf intern(SmBuf buf) { return smBufIntern(&STRS, buf); }

static FILE *openFile(SmBuf path, char const *modes) {
    static SmGBuf buf = {0};
    buf.inner.len     = 0;
    smGBufCat(&buf, path);
    smGBufCat(&buf, SM_BUF("\0"));
    return openFileCstr((char const *)buf.inner.bytes, modes);
}

static _Noreturn void objFatalV(SmBuf path, char const *fmt, va_list args) {
    fprintf(stderr, "%.*s: ", (int)path.len, path.bytes);
    smFatalV(fmt, args);
}

static _Noreturn void objFatal(SmBuf path, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    objFatalV(path, fmt, args);
    va_end(args);
}

static SmLbl internLbl(SmLbl lbl) {
    if (smBufEqual(lbl.scope, SM_BUF_NULL)) {
        return globalLbl(intern(lbl.name));
    }
    return (SmLbl){intern(lbl.scope), intern(lbl.name)};
}

static SmBuf fullLblName(SmLbl lbl) { return smLblFullName(lbl, &STRS); }

static SmSect *findSect(SmBuf name) {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        if (smBufEqual(sect->name, name)) {
            return sect;
        }
    }
    return NULL;
}

static SmExprBuf internExpr(SmExprBuf buf) {
    for (UInt i = 0; i < buf.len; ++i) {
        SmExpr *expr = buf.items + i;
        switch (expr->kind) {
        case SM_EXPR_ADDR:
            expr->addr.sect = intern(expr->addr.sect);
            break;
        case SM_EXPR_TAG:
            expr->tag.lbl  = internLbl(expr->tag.lbl);
            expr->tag.name = intern(expr->tag.name);
            break;
        case SM_EXPR_LABEL:
        case SM_EXPR_REL: {
            expr->lbl = internLbl(expr->lbl);
            break;
        }
        default:
            break;
        }
    }
    return smExprIntern(&EXPRS, buf);
}

static void loadObj(SmBuf path) {
    FILE   *hnd   = openFile(path, "rb");
    SmSerde ser   = {hnd, path};
    U32     magic = smDeserializeU32(&ser);
    if (magic != *(U32 *)"SM00") {
        objFatal(path, "bad magic: $%04x\n", magic);
    }
    SmBufIntern  tmpstrs  = smDeserializeBufIntern(&ser);
    SmExprIntern tmpexprs = smDeserializeExprIntern(&ser, &tmpstrs);
    // Fixup addresses to be absolute
    for (UInt i = 0; i < tmpexprs.len; ++i) {
        SmExprGBuf *gbuf = tmpexprs.bufs + i;
        for (UInt j = 0; j < gbuf->inner.len; ++j) {
            SmExpr *expr = gbuf->inner.items + j;
            if (expr->kind != SM_EXPR_ADDR) {
                continue;
            }
            SmSect *sect = findSect(expr->addr.sect);
            if (!sect) {
                objFatal(path,
                         "output section %.*s is not defined in config\n"
                         "\tyou may have forgot to add a @SECTION directive "
                         "before a label\n",
                         (int)expr->addr.sect.len, expr->addr.sect.bytes);
            }
            expr->addr.pc += sect->pc;
        }
    }
    SmSymTab tmpsyms = smDeserializeSymTab(&ser, &tmpstrs, &tmpexprs);
    // Merge into main symtab
    for (UInt i = 0; i < tmpsyms.size; ++i) {
        SmSym *sym = tmpsyms.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        // Hide static symbols under file-specific unit
        SmBuf unit;
        if (smBufEqual(sym->unit, STATIC_UNIT)) {
            unit = path;
        } else {
            unit = sym->unit;
        }
        SmSym *whence = smSymTabFind(&SYMS, sym->lbl);
        if (whence && smBufEqual(whence->unit, unit)) {
            SmBuf name = fullLblName(sym->lbl);
            objFatal(path,
                     "duplicate exported symbol: %.*s\n"
                     "\tdefined at %.*s:%zu:%zu\n"
                     "\tagain at %.*s:%zu:%zu\n",
                     (int)name.len, name.bytes, (int)whence->pos.file.len,
                     whence->pos.file.bytes, whence->pos.line, whence->pos.col,
                     (int)sym->pos.file.len, sym->pos.file.bytes, sym->pos.line,
                     sym->pos.col);
        }
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = internLbl(sym->lbl),
                               .value   = internExpr(sym->value),
                               .unit    = intern(unit),
                               .section = intern(sym->section),
                               .pos =
                                   {
                                       intern(sym->pos.file),
                                       sym->pos.line,
                                       sym->pos.col,
                                   },
                               .flags = sym->flags,
                           });
    }
    SmSectGBuf tmpsects = smDeserializeSectBuf(&ser, &tmpstrs, &tmpexprs);
    closeFile(hnd);
    for (UInt i = 0; i < tmpsects.inner.len; ++i) {
        SmSect *sect    = tmpsects.inner.items + i;
        SmSect *dstsect = findSect(sect->name);
        if (!dstsect) {
            objFatal(path, "output section %.*s is not defined in config\n",
                     (int)sect->name.len, sect->name.bytes);
        }
        // copy relocations
        for (UInt j = 0; j < sect->relocs.inner.len; ++j) {
            SmReloc *reloc = sect->relocs.inner.items + j;
            SmBuf    unit;
            // fixup relocation units
            if (smBufEqual(reloc->unit, STATIC_UNIT)) {
                unit = path;
            } else {
                unit = EXPORT_UNIT;
            }
            smRelocGBufAdd(
                &dstsect->relocs,
                (SmReloc){
                    // adjust relocations relative to destination section
                    .offset = dstsect->pc + reloc->offset,
                    .width  = reloc->width,
                    .value  = internExpr(reloc->value),
                    .unit   = intern(unit),
                    .pos =
                        {
                            intern(reloc->pos.file),
                            reloc->pos.line,
                            reloc->pos.col,
                        },
                    .flags = reloc->flags,
                });
        }
        // extend destination section
        smGBufCat(&dstsect->data, sect->data.inner);
        dstsect->pc += sect->data.inner.len;
        // free up
        smRelocGBufFini(&sect->relocs);
        smGBufFini(&sect->data);
    }
    smSectGBufFini(&tmpsects);
    smSymTabFini(&tmpsyms);
    smExprInternFini(&tmpexprs);
    smBufInternFini(&tmpstrs);
}

struct Out {
    SmBuf name;
    U32   pc;
    U32   end;
};
typedef struct Out Out;

struct OutBuf {
    Out *items;
    UInt len;
};
typedef struct OutBuf OutBuf;

struct OutGBuf {
    OutBuf inner;
    UInt   size;
};
typedef struct OutGBuf OutGBuf;

static OutGBuf OUTS = {0};

static void outGBufAdd(Out item) {
    OutGBuf *buf = &OUTS;
    SM_GBUF_ADD_IMPL();
}

static Out *findOut(SmBuf name) {
    for (UInt i = 0; i < OUTS.inner.len; ++i) {
        Out *out = OUTS.inner.items + i;
        if (smBufEqual(out->name, name)) {
            return out;
        }
    }
    return NULL;
}

static CfgIn *findCfgIn(SmBuf name, CfgOut **out) {
    for (UInt i = 0; i < CFGS.inner.len; ++i) {
        *out = CFGS.inner.items + i;
        for (UInt j = 0; j < (*out)->ins.len; ++j) {
            CfgIn *in = (*out)->ins.items + j;
            if (smBufEqual(in->name, name)) {
                return in;
            }
        }
    }
    *out = NULL;
    return NULL;
}

static void allocate(SmSect *sect) {
    CfgOut *cfgout = NULL;
    CfgIn  *in     = findCfgIn(sect->name, &cfgout);
    assert(cfgout);
    assert(in);
    Out *out     = findOut(cfgout->name);
    U32  aligned = ((out->pc + in->align - 1) / in->align) * in->align;
    sect->pc     = aligned;
    for (UInt i = 0; i < in->files.len; ++i) {
        SmBuf   path = in->files.items[i];
        FILE   *hnd  = openFile(path, "rb");
        SmSerde ser  = {hnd, path};
        smDeserializeToEnd(&ser, &sect->data);
    }
    if (in->size) {
        if (sect->data.inner.len > in->sizeval) {
            smFatal("input section %.*s size ($%08zX) is larger than "
                    "configured size: $%08zX\n",
                    (int)sect->name.len, sect->name.bytes, sect->data.inner.len,
                    (UInt)in->sizeval);
        }
        if (in->fill) {
            while (sect->data.inner.len < in->sizeval) {
                smGBufCat(&sect->data, (SmBuf){&in->fillval, 1});
            }
        }
    }
    out->pc = aligned + sect->data.inner.len;
    if (out->pc > out->end) {
        smFatal("no room in output section %.*s for input section %.*s\n",
                (int)out->name.len, out->name.bytes, (int)sect->name.len,
                sect->name.bytes);
    }
    if (!smBufEqual(in->define, SM_BUF_NULL)) {
        static SmGBuf buf = {0};
        buf.inner.len     = 0;
        smGBufCat(&buf, in->define);
        smGBufCat(&buf, SM_BUF("_START"));
        SmBuf start = intern(buf.inner);
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = globalLbl(start),
                               .value   = constExprBuf(sect->pc),
                               .unit    = EXPORT_UNIT,
                               .section = DEFINES_SECTION,
                               .pos     = in->defpos,
                               .flags   = SM_SYM_EQU,
                           });
        buf.inner.len = 0;
        smGBufCat(&buf, in->define);
        smGBufCat(&buf, SM_BUF("_SIZE"));
        SmBuf size = intern(buf.inner);
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = globalLbl(size),
                               .value   = constExprBuf(sect->data.inner.len),
                               .unit    = EXPORT_UNIT,
                               .section = DEFINES_SECTION,
                               .pos     = in->defpos,
                               .flags   = SM_SYM_EQU,
                           });
    }
}

static Bool solve(SmExprBuf buf, SmBuf unit, I32 *num) {
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
            if (!smBufEqual(sym->unit, unit) &&
                !smBufEqual(sym->unit, EXPORT_UNIT)) {
                goto fail;
            }
            I32 num;
            // yuck
            if (!solve(sym->value, unit, &num)) {
                goto fail;
            }
            smI32GBufAdd(&stack, num);
            break;
        }
        case SM_EXPR_TAG: {
            SmSym *sym = smSymTabFind(&SYMS, expr->tag.lbl);
            if (!sym) {
                goto fail;
            }
            if (!smBufEqual(sym->unit, unit) &&
                !smBufEqual(sym->unit, EXPORT_UNIT)) {
                goto fail;
            }
            CfgOut *cfgout = NULL;
            CfgIn  *in     = findCfgIn(sym->section, &cfgout);
            assert(cfgout);
            assert(in);
            // find the tag in the section
            CfgI32Entry *tag = cfgI32TabFind(&in->tags, expr->tag.name);
            if (!tag) {
                goto fail;
            }
            smI32GBufAdd(&stack, tag->num);
            break;
        }
        case SM_EXPR_OP:
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
                    SM_UNREACHABLE();
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
                    SM_UNREACHABLE();
                }
            }
            break;
        case SM_EXPR_ADDR: {
            SmSect *sect = findSect(expr->addr.sect);
            if (!sect) {
                goto fail;
            }
            smI32GBufAdd(&stack, sect->pc + expr->addr.pc);
            break;
        }
        default:
            SM_UNREACHABLE();
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

static void solveSyms() {
    // 2 passes over symbol table should be enough to solve all
    for (UInt i = 0; i < 2; ++i) {
        for (UInt j = 0; j < SYMS.size; ++j) {
            SmSym *sym = SYMS.syms + j;
            if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
                continue;
            }
            if ((sym->value.len == 1) &&
                (sym->value.items[0].kind == SM_EXPR_CONST)) {
                continue;
            }
            I32 num;
            if (solve(sym->value, sym->unit, &num)) {
                sym->value = constExprBuf(num);
            }
        }
    }
    for (UInt i = 0; i < SYMS.size; ++i) {
        SmSym *sym = SYMS.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        if ((sym->value.len == 1) &&
            (sym->value.items[0].kind == SM_EXPR_CONST)) {
            continue;
        }
        SmBuf name = fullLblName(sym->lbl);
        smFatal("undefined symbol: %.*s\n"
                "\treferenced at %.*s:%zu:%zu\n",
                (int)name.len, name.bytes, (int)sym->pos.file.len,
                sym->pos.file.bytes, sym->pos.line, sym->pos.col);
    }
}

static Bool canReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }
static Bool canReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }

static void link(SmSect *sect) {
    for (UInt i = 0; i < sect->relocs.inner.len; ++i) {
        SmReloc *reloc = sect->relocs.inner.items + i;
        I32      num;
        if (!solve(reloc->value, reloc->unit, &num)) {
            smFatal("expression cannot be solved\n"
                    "\treferenced at %.*s:%zu:%zu\n",
                    (int)reloc->pos.file.len, reloc->pos.file.bytes,
                    reloc->pos.line, reloc->pos.col);
        }
        switch (reloc->width) {
        case 1:
            if (!canReprU8(num)) {
                Bool legal = false;
                if (reloc->flags & SM_RELOC_HI) {
                    for (UInt j = 0; j < SECTS.inner.len; ++j) {
                        SmSect *sect   = SECTS.inner.items + j;
                        CfgOut *cfgout = NULL;
                        CfgIn  *in     = findCfgIn(sect->name, &cfgout);
                        assert(cfgout);
                        assert(in);
                        if (in->kind != CFG_IN_HIGHPAGE) {
                            continue;
                        }
                        if ((num >= (I32)(sect->pc)) &&
                            (num < (I32)(sect->pc + sect->data.inner.len))) {
                            legal = true;
                            break;
                        }
                    }
                }
                if (!legal) {
                    smFatal("expression does not fit in a byte: $%08X\n"
                            "\treferenced at %.*s:%zu:%zu\n",
                            num, (int)reloc->pos.file.len,
                            reloc->pos.file.bytes, reloc->pos.line,
                            reloc->pos.col);
                }
            }
            if (reloc->flags & SM_RELOC_RST) {
                U8 op;
                switch (num) {
                case 0x00:
                    op = 0xC7;
                    break;
                case 0x08:
                    op = 0xCF;
                    break;
                case 0x10:
                    op = 0xD7;
                    break;
                case 0x18:
                    op = 0xDF;
                    break;
                case 0x20:
                    op = 0xE7;
                    break;
                case 0x28:
                    op = 0xEF;
                    break;
                case 0x30:
                    op = 0xF7;
                    break;
                case 0x38:
                    op = 0xFF;
                    break;
                default:
                    smFatal("illegal reset vector: $%08X\n"
                            "\treferenced at %.*s:%zu:%zu\n",
                            num, (int)reloc->pos.file.len,
                            reloc->pos.file.bytes, reloc->pos.line,
                            reloc->pos.col);
                }
                sect->data.inner.bytes[reloc->offset] = op;
                continue;
            }
            sect->data.inner.bytes[reloc->offset] = (U8)num;
            break;
        case 2:
            // TODO check if src and dst banks are the same
            // also a JP within bank0 is always legal for GB
            if (!canReprU16(num)) {
                smFatal("expression does not fit in a word: $%08X\n"
                        "\treferenced at %.*s:%zu:%zu\n",
                        num, (int)reloc->pos.file.len, reloc->pos.file.bytes,
                        reloc->pos.line, reloc->pos.col);
            }
            sect->data.inner.bytes[reloc->offset]     = (U8)(num & 0xFF);
            sect->data.inner.bytes[reloc->offset + 1] = (U8)((num >> 8) & 0xFF);
            break;
        default:
            SM_UNREACHABLE();
        }
    }
}

static SmTokStream TS = {0};

_Noreturn void fatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalV(&TS, fmt, args);
    va_end(args);
}

_Noreturn void fatalPos(SmPos pos, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(&TS, pos, fmt, args);
    va_end(args);
}

static U32   peek() { return smTokStreamPeek(&TS); }
static void  eat() { smTokStreamEat(&TS); }
static SmPos tokPos() { return smTokStreamPos(&TS); }
static I32   tokNum() { return smTokStreamNum(&TS); }
static SmBuf tokBuf() { return smTokStreamBuf(&TS); }

static void expect(U32 tok) {
    U32 peeked = peek();
    if (peeked != tok) {
        SmBuf expected = smTokName(tok);
        SmBuf found    = smTokName(peeked);
        fatal("expected %.*s, got %.*s\n", (int)expected.len, expected.bytes,
              (int)found.len, found.bytes);
    }
}

static void expectEOL() {
    switch (peek()) {
    case SM_TOK_EOF:
    case '\n':
        return;
    default:
        smTokStreamFatal(&TS, "expected end of line\n");
    }
}

static U8 eatU8() {
    expect(SM_TOK_NUM);
    I32 num = tokNum();
    if (!canReprU8(num)) {
        smTokStreamFatal(&TS, "number does not fit in a byte: $%08X\n", num);
    }
    eat();
    return (U8)num;
}

static U16 eatU16() {
    expect(SM_TOK_NUM);
    I32 num = tokNum();
    if (!canReprU16(num)) {
        smTokStreamFatal(&TS, "number does not fit in a word: $%08X\n", num);
    }
    eat();
    return (U16)num;
}

static CfgI32Tab parseTags() {
    CfgI32Tab tags = {0};
    while (true) {
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case SM_TOK_STR:
        case SM_TOK_ID: {
            SmBuf name = intern(tokBuf());
            eat();
            expect('=');
            eat();
            expect(SM_TOK_NUM);
            cfgI32TabAdd(&tags, (CfgI32Entry){name, tokNum()});
            eat();
            break;
        }
        }
        return tags;
    }
}

static CfgInBuf parseInSects(CfgOut const *out) {
    CfgInGBuf ins = {0};
    while (true) {
        CfgIn in = {0};
        in.tags  = out->tags;
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case '}':
            goto endsects;
        case '*':
            in.name = out->name;
            break;
        case SM_TOK_STR:
        case SM_TOK_ID:
            in.name = intern(tokBuf());
            break;
        }
        SmPos pos = tokPos();
        eat();
        in.align  = 1;
        Bool kind = false;
        while (true) {
            switch (peek()) {
            case SM_TOK_STR:
            case SM_TOK_ID: {
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("kind"))) {
                    eat();
                    expect('=');
                    eat();
                    kind = true;
                    switch (peek()) {
                    case SM_TOK_STR:
                    case SM_TOK_ID: {
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("code"))) {
                            eat();
                            in.kind = CFG_IN_CODE;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("data"))) {
                            eat();
                            in.kind = CFG_IN_DATA;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("uninit"))) {
                            eat();
                            in.kind = CFG_IN_UNINIT;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("hi"))) {
                            eat();
                            in.kind = CFG_IN_HIGHPAGE;
                            continue;
                        }
                        SmBuf buf = tokBuf();
                        fatal("unrecognized input section kind: %.*s\n",
                              (int)buf.len, buf.bytes);
                    }
                    default:
                        expect(SM_TOK_STR);
                    }
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("align"))) {
                    eat();
                    expect('=');
                    eat();
                    in.align = eatU16();
                    if (in.align == 0) {
                        fatal("input section alignment must be greater "
                              "than 0\n");
                    }
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("size"))) {
                    eat();
                    expect('=');
                    eat();
                    in.size    = true;
                    in.sizeval = eatU16();
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("fill"))) {
                    eat();
                    expect('=');
                    eat();
                    in.fill    = true;
                    in.fillval = eatU8();
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("define"))) {
                    in.defpos = tokPos();
                    eat();
                    in.define = in.name;
                    if (peek() == '=') {
                        eat();
                        if ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
                            in.define = intern(tokBuf());
                            in.defpos = tokPos();
                            eat();
                        } else {
                            expect(SM_TOK_STR);
                        }
                    }
                    continue;
                }
                SmBuf buf = tokBuf();
                fatal("unrecognized input section attribute: %.*s\n",
                      (int)buf.len, buf.bytes);
                continue;
            }
            case '[': {
                eat();
                CfgI32Tab oldtags = in.tags;
                in.tags           = parseTags();
                // copy base tags
                for (UInt i = 0; i < oldtags.size; ++i) {
                    CfgI32Entry *oldtag = oldtags.entries + i;
                    if (smBufEqual(oldtag->name, SM_BUF_NULL)) {
                        continue;
                    }
                    // only apply tag if the output section does not already
                    // have it
                    CfgI32Entry *newtag = cfgI32TabFind(&in.tags, oldtag->name);
                    if (smBufEqual(newtag->name, SM_BUF_NULL)) {
                        *newtag = *oldtag;
                    }
                }
                expect(']');
                eat();
                goto endsect;
            }
            default:
                goto endsect;
            }
        endsect:
            break;
        }
        if (!kind) {
            fatalPos(pos, "`kind` attribute is required\n");
        }
        cfgInGBufAdd(&ins, in);
        expectEOL();
        eat();
        continue;
    }
endsects:
    return ins.inner;
}

static void parseOutSects() {
    expect('{');
    eat();
    while (true) {
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case '}':
            goto endsects;
        case SM_TOK_STR:
        case SM_TOK_ID: {
            CfgOut out   = {0};
            Bool   start = false;
            Bool   size  = false;
            Bool   kind  = false;
            out.name     = intern(tokBuf());
            SmPos pos    = tokPos();
            eat();
            while (true) {
                switch (peek()) {
                case SM_TOK_STR:
                case SM_TOK_ID: {
                    if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("start"))) {
                        eat();
                        expect('=');
                        eat();
                        start     = true;
                        out.start = eatU16();
                        continue;
                    }
                    if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("size"))) {
                        eat();
                        expect('=');
                        eat();
                        size     = true;
                        out.size = eatU16();
                        continue;
                    }
                    if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("fill"))) {
                        eat();
                        expect('=');
                        eat();
                        out.fill    = true;
                        out.fillval = eatU8();
                        continue;
                    }
                    if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("kind"))) {
                        eat();
                        expect('=');
                        eat();
                        kind = true;
                        switch (peek()) {
                        case SM_TOK_STR:
                        case SM_TOK_ID: {
                            if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                          SM_BUF("ro"))) {
                                eat();
                                out.kind = CFG_OUT_READONLY;
                                continue;
                            }
                            if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                          SM_BUF("rw"))) {
                                eat();
                                out.kind = CFG_OUT_READWRITE;
                                continue;
                            }
                            SmBuf buf = tokBuf();
                            fatal("unrecognized ouput section kind: %.*s\n",
                                  (int)buf.len, buf.bytes);
                        }
                        default:
                            expect(SM_TOK_STR);
                        }
                    }
                    if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("define"))) {
                        out.defpos = tokPos();
                        eat();
                        out.define = out.name;
                        if (peek() == '=') {
                            eat();
                            if ((peek() == SM_TOK_STR) ||
                                (peek() == SM_TOK_ID)) {
                                out.define = intern(tokBuf());
                                out.defpos = tokPos();
                                eat();
                            } else {
                                expect(SM_TOK_STR);
                            }
                        }
                        continue;
                    }
                    SmBuf buf = tokBuf();
                    fatal("unrecognized output section attribute: %.*s\n",
                          (int)buf.len, buf.bytes);
                    continue;
                }
                case '[':
                    eat();
                    out.tags = parseTags();
                    expect(']');
                    eat();
                    continue;
                case '{':
                    eat();
                    out.ins = parseInSects(&out);
                    expect('}');
                    eat();
                    goto endsect;
                default:
                    goto endsect;
                }
            endsect:
                break;
            }
            if (!start) {
                fatalPos(pos, "`start` attribute is required\n");
            }
            if (!size) {
                fatalPos(pos, "`size` attribute is required\n");
            }
            if (!kind) {
                fatalPos(pos, "`kind` attribute is required\n");
            }
            cfgOutGBufAdd(&CFGS, out);
            expectEOL();
            eat();
            continue;
        }
        default: {
            SmBuf name = smTokName(peek());
            fatal("unexpected %.*s\n", (int)name.len, name.bytes);
        }
        }
    }
endsects:
    expect('}');
    eat();
    if (CFGS.inner.len == 0) {
        fatal("at least 1 output section is required for linking\n");
    }
}

static void parseCfg() {
    SmBuf cfgname =
        smPathIntern(&STRS, (SmBuf){(U8 *)cfgfile_name, strlen(cfgfile_name)});
    smTokStreamFileInit(&TS, cfgname, cfgfile);
    Bool sections = false;
    while (peek() != SM_TOK_EOF) {
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case SM_TOK_STR:
        case SM_TOK_ID: {
            if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("sections"))) {
                eat();
                parseOutSects();
                sections = true;
                continue;
            }
            SmBuf buf = tokBuf();
            fatal("unrecognized config area: %.*s\n", (int)buf.len, buf.bytes);
        }
        default: {
            SmBuf name = smTokName(peek());
            fatal("unexpected %.*s\n", (int)name.len, name.bytes);
        }
        }
    }
    if (!sections) {
        fatal("no `sections` config area was defined\n");
    }
    for (UInt i = 0; i < CFGS.inner.len; ++i) {
        CfgOut *out = CFGS.inner.items + i;
        // Pre-add the output sections defined in the cfg
        outGBufAdd((Out){
            .name = out->name,
            .pc   = out->start,
            .end  = out->start + out->size,
        });
        if (!smBufEqual(out->define, SM_BUF_NULL)) {
            static SmGBuf buf = {0};
            buf.inner.len     = 0;
            smGBufCat(&buf, out->define);
            smGBufCat(&buf, SM_BUF("_START"));
            SmBuf start = intern(buf.inner);
            smSymTabAdd(&SYMS, (SmSym){
                                   .lbl     = globalLbl(start),
                                   .value   = constExprBuf(out->start),
                                   .unit    = EXPORT_UNIT,
                                   .section = DEFINES_SECTION,
                                   .pos     = out->defpos,
                                   .flags   = SM_SYM_EQU,
                               });
            buf.inner.len = 0;
            smGBufCat(&buf, out->define);
            smGBufCat(&buf, SM_BUF("_SIZE"));
            SmBuf size = intern(buf.inner);
            smSymTabAdd(&SYMS, (SmSym){
                                   .lbl     = globalLbl(size),
                                   .value   = constExprBuf(out->size),
                                   .unit    = EXPORT_UNIT,
                                   .section = DEFINES_SECTION,
                                   .pos     = out->defpos,
                                   .flags   = SM_SYM_EQU,
                               });
        }
        for (UInt j = 0; j < out->ins.len; ++j) {
            CfgIn *in = out->ins.items + j;
            // Pre-add the sections defined in the cfg
            smSectGBufAdd(&SECTS, (SmSect){
                                      .name   = in->name,
                                      .pc     = 0,
                                      .data   = {{0}, 0}, // GCC doesnt like {0}
                                      .relocs = {{0}, 0},
                                  });
            switch (out->kind) {
            case CFG_OUT_READONLY:
                // TODO could check this while parsing
                if (in->kind != CFG_IN_CODE) {
                    fatal("input section %.*s is not kind-compatible "
                          "with output section %.*s\n",
                          (int)in->name.len, in->name.bytes, (int)out->name.len,
                          out->name.bytes);
                }
            case CFG_OUT_READWRITE:
                continue;
            default:
                SM_UNREACHABLE();
            }
        }
    }
}

static void serialize() {
    SmSerde ser = {outfile, {(U8 *)outfile_name, strlen(outfile_name)}};
    for (UInt i = 0; i < CFGS.inner.len; ++i) {
        CfgOut *cfgout = CFGS.inner.items + i;
        for (UInt j = 0; j < cfgout->ins.len; ++j) {
            CfgIn *in = cfgout->ins.items + j;
            if ((in->kind == CFG_IN_UNINIT) || (in->kind == CFG_IN_HIGHPAGE)) {
                continue;
            }
            SmSect *sect = findSect(in->name);
            assert(sect);
            smSerializeBuf(&ser, sect->data.inner);
        }
        if (cfgout->fill) {
            Out *out = findOut(cfgout->name);
            assert(out);
            for (UInt j = out->pc; j < out->end; ++j) {
                smSerializeU8(&ser, cfgout->fillval);
            }
        }
    }
}

static SmLbl globalLbl(SmBuf name) { return (SmLbl){{0}, name}; }

static SmExprBuf constExprBuf(I32 num) {
    return smExprIntern(
        &EXPRS, (SmExprBuf){&(SmExpr){.kind = SM_EXPR_CONST, .num = num}, 1});
}

static FILE *openFileCstr(char const *name, char const *modes) {
    FILE *hnd = fopen(name, modes);
    if (!hnd) {
        smFatal("could not open file: %s: %s\n", name, strerror(errno));
    }
    return hnd;
}

static void closeFile(FILE *hnd) {
    if (fclose(hnd) == EOF) {
        int err = errno;
        smFatal("failed to close file: %s\n", strerror(err));
    }
}

static int cmpSym(SmSym const *lhs, SmSym const *rhs) {
    if (smLblEqual(lhs->lbl, SM_LBL_NULL)) {
        return 1;
    }
    if (smLblEqual(rhs->lbl, SM_LBL_NULL)) {
        return 0;
    }
    SmBuf lname = fullLblName(lhs->lbl);
    SmBuf rname = fullLblName(rhs->lbl);
    int   cmp = memcmp(lname.bytes, rname.bytes, uIntMin(lname.len, rname.len));
    if (!cmp) {
        return lname.len - rname.len;
    }
    return cmp;
}

static SmSymTab sortSyms() {
    // Clone and sort the symbol table
    SmSymTab tab = {0};
    for (UInt i = 0; i < SYMS.size; ++i) {
        SmSym *sym = SYMS.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        smSymTabAdd(&tab, *sym);
    }
    qsort(tab.syms, tab.size, sizeof(SmSym), (void *)cmpSym);
    return tab;
}

static void writeSyms() {
    FILE    *hnd = openFileCstr(symfile_name, "wb+");
    SmSymTab tab = sortSyms();
    for (UInt i = 0; i < tab.size; ++i) {
        SmSym *sym = tab.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        if (sym->flags & SM_SYM_EQU) {
            continue;
        }
        if ((sym->value.len != 1) ||
            (sym->value.items[0].kind != SM_EXPR_CONST)) {
            continue;
        }
        SmBuf   name   = fullLblName(sym->lbl);
        CfgOut *cfgout = NULL;
        CfgIn  *in     = findCfgIn(sym->section, &cfgout);
        assert(in);
        I32          bank = 0;
        CfgI32Entry *tag  = cfgI32TabFind(&in->tags, SM_BUF("bank"));
        if (tag) {
            bank = tag->num;
        }
        char const *fmt = "%02X:%04X %.*s\n";
        if (bank > 0xFF) {
            fmt = "%04X:%04X %.*s\n";
        }
        if (fprintf(hnd, fmt, bank, sym->value.items[0].num, (int)name.len,
                    name.bytes) < 0) {
            smFatal("%s: failed to write file: %s\n", symfile_name,
                    strerror(errno));
        }
    }
    smSymTabFini(&tab);
    closeFile(hnd);
}

static void writeTags() {
    FILE    *hnd = openFileCstr(tagfile_name, "wb+");
    SmSymTab tab = sortSyms();
    for (UInt i = 0; i < tab.size; ++i) {
        SmSym *sym = tab.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        SmBuf name = fullLblName(sym->lbl);
        if (fprintf(hnd, "%.*s\t%.*s\t%zu\n", (int)name.len, name.bytes,
                    (int)sym->pos.file.len, sym->pos.file.bytes,
                    sym->pos.line) < 0) {
            smFatal("%s: failed to write file: %s\n", symfile_name,
                    strerror(errno));
        }
    }
    smSymTabFini(&tab);
    closeFile(hnd);
}
