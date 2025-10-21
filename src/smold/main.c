#include "cfg.h"

#include <smasm/fatal.h>
#include <smasm/path.h>
#include <smasm/serde.h>
#include <smasm/sym.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void help(char const *name) {
    fprintf(
        stderr,
        "SMOLD: A linker for the SM83 (Gameboy) CPU\n"
        "\n"
        "Usage: %s [OPTIONS] --config <CONFIG> [OBJECTS]...\n"
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
        "  -h, --help                   Print help\n",
        name);
}

static FILE      *openFileCstr(char const *path, char const *modes);
static void       closeFile(FILE *hnd);
static SmLbl      globalLbl(SmView name);
static SmExprView constExprBuf(I32 num);
static SmView     intern(SmView view);
static void       parseCfg();
static void       loadObj(SmView path);
static void       allocate(SmSect *sect);
static void       solveSyms();
static void       link(SmSect *sect);
static void       serialize();
static void       writeSyms();
static void       writeTags();

static FILE *cfgfile      = NULL;
static char *cfgfile_name = NULL;
static FILE *outfile      = NULL;
static char *outfile_name = NULL;
static char *symfile_name = NULL;
static char *tagfile_name = NULL;

static SmViewIntern STRS  = {0};
static SmSymTab     SYMS  = {0};
static SmExprIntern EXPRS = {0};
static SmPathSet    OBJS  = {0};
static SmSectBuf    SECTS = {0};

static CfgOutBuf CFGS     = {0};

static SmView DEFINES_SECTION;
static SmView STATIC_UNIT;
static SmView EXPORT_UNIT;

int main(int argc, char **argv) {
    outfile = stdout;
    if (argc == 1) {
        help(argv[0]);
        return 0;
    }
    DEFINES_SECTION = intern(SM_VIEW("@DEFINES"));
    STATIC_UNIT     = intern(SM_VIEW("@STATIC"));
    EXPORT_UNIT     = intern(SM_VIEW("@EXPORT"));
    for (int argi = 1; argi < argc; ++argi) {
        if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
            help(argv[0]);
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
            UInt   name_len  = offset - argv[argi];
            SmView name      = intern((SmView){(U8 *)argv[argi], name_len});
            UInt   value_len = strlen(argv[argi]) - name_len - 1;
            // TODO: should expose general-purpose expression parsing
            // for use here and for expressions in the linker scripts
            // Could even use a tok stream here.
            I32    num       = smViewParse(
                (SmView){(U8 *)(argv[argi] + name_len + 1), value_len});
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
        smPathSetAdd(&OBJS, (SmView){(U8 *)argv[argi], strlen(argv[argi])});
    }
    if (!cfgfile) {
        smFatal("config file is required\n");
    }
    if (OBJS.bufs.view.len == 0) {
        smFatal("at least 1 object file is required\n");
    }

    parseCfg();

    for (UInt i = 0; i < OBJS.bufs.view.len; ++i) {
        loadObj(OBJS.bufs.view.items[i]);
    }
    for (UInt i = 0; i < SECTS.view.len; ++i) {
        SmSect *sect = SECTS.view.items + i;
        allocate(sect);
    }
    solveSyms();
    for (UInt i = 0; i < SECTS.view.len; ++i) {
        SmSect *sect = SECTS.view.items + i;
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

static SmView intern(SmView view) { return smViewIntern(&STRS, view); }

static FILE *openFile(SmView path, char const *modes) {
    static SmBuf buf = {0};
    buf.view.len     = 0;
    smBufCat(&buf, path);
    smBufCat(&buf, SM_VIEW("\0"));
    return openFileCstr((char const *)buf.view.bytes, modes);
}

static _Noreturn void objFatalV(SmView path, char const *fmt, va_list args) {
    fprintf(stderr, "%" SM_VIEW_FMT ": ", SM_VIEW_FMT_ARG(path));
    smFatalV(fmt, args);
}

static _Noreturn void objFatal(SmView path, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    objFatalV(path, fmt, args);
}

static SmLbl internLbl(SmLbl lbl) {
    if (smViewEqual(lbl.scope, SM_VIEW_NULL)) {
        return globalLbl(intern(lbl.name));
    }
    return (SmLbl){intern(lbl.scope), intern(lbl.name)};
}

static SmView fullLblName(SmLbl lbl) { return smLblFullName(lbl, &STRS); }

static SmSect *findSect(SmView name) {
    for (UInt i = 0; i < SECTS.view.len; ++i) {
        SmSect *sect = SECTS.view.items + i;
        if (smViewEqual(sect->name, name)) {
            return sect;
        }
    }
    return NULL;
}

static SmExprView internExpr(SmExprView view) {
    for (UInt i = 0; i < view.len; ++i) {
        SmExpr *expr = view.items + i;
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
    return smExprIntern(&EXPRS, view);
}

static void loadObj(SmView path) {
    FILE   *hnd   = openFile(path, "rb");
    SmSerde ser   = {hnd, path};
    U32     magic = smDeserializeU32(&ser);
    if (magic != *(U32 *)"SM00") {
        objFatal(path, "bad magic: $%04" U32_FMTX "\n", magic);
    }
    SmViewIntern tmpstrs  = smDeserializeViewIntern(&ser);
    SmExprIntern tmpexprs = smDeserializeExprIntern(&ser, &tmpstrs);
    // Fixup addresses to be absolute
    for (UInt i = 0; i < tmpexprs.len; ++i) {
        SmExprBuf *buf = tmpexprs.bufs + i;
        for (UInt j = 0; j < buf->view.len; ++j) {
            SmExpr *expr = buf->view.items + j;
            if (expr->kind != SM_EXPR_ADDR) {
                continue;
            }
            SmSect *sect = findSect(expr->addr.sect);
            if (!sect) {
                objFatal(path,
                         "output section %" SM_VIEW_FMT
                         " is not defined in config\n\tyou may have forgot to "
                         "add a @SECTION directive before a label\n",
                         SM_VIEW_FMT_ARG(expr->addr.sect));
            }
            expr->addr.pc += sect->pc;
        }
    }
    SmSymTab tmpsyms = smDeserializeSymTab(&ser, &tmpstrs, &tmpexprs);
    // Merge into main symtab
    for (UInt i = 0; i < tmpsyms.cap; ++i) {
        SmSym *sym = tmpsyms.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        // Hide static symbols under file-specific unit
        SmView unit;
        if (smViewEqual(sym->unit, STATIC_UNIT)) {
            unit = path;
        } else {
            unit = sym->unit;
        }
        SmSym *whence = smSymTabFind(&SYMS, sym->lbl);
        if (whence && smViewEqual(whence->unit, unit)) {
            SmView name = fullLblName(sym->lbl);
            objFatal(
                path,
                "duplicate exported symbol: %" SM_VIEW_FMT
                "\n\tdefined at %" SM_VIEW_FMT ":%" UINT_FMT ":%" UINT_FMT
                "\n\tagain at %" SM_VIEW_FMT ":%" UINT_FMT ":%" UINT_FMT "\n",
                SM_VIEW_FMT_ARG(name), SM_VIEW_FMT_ARG(whence->pos.file),
                whence->pos.line, whence->pos.col,
                SM_VIEW_FMT_ARG(sym->pos.file), sym->pos.line, sym->pos.col);
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
    SmSectBuf tmpsects = smDeserializeSectBuf(&ser, &tmpstrs, &tmpexprs);
    closeFile(hnd);
    for (UInt i = 0; i < tmpsects.view.len; ++i) {
        SmSect *sect    = tmpsects.view.items + i;
        SmSect *dstsect = findSect(sect->name);
        if (!dstsect) {
            objFatal(path,
                     "output section %" SM_VIEW_FMT
                     " is not defined in config\n",
                     SM_VIEW_FMT_ARG(sect->name));
        }
        // copy relocations
        for (UInt j = 0; j < sect->relocs.view.len; ++j) {
            SmReloc *reloc = sect->relocs.view.items + j;
            SmView   unit;
            // fixup relocation units
            if (smViewEqual(reloc->unit, STATIC_UNIT)) {
                unit = path;
            } else {
                unit = EXPORT_UNIT;
            }
            smRelocBufAdd(
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
        smBufCat(&dstsect->data, sect->data.view);
        dstsect->pc += sect->data.view.len;
        // free up
        smRelocBufFini(&sect->relocs);
        smBufFini(&sect->data);
    }
    smSectBufFini(&tmpsects);
    smSymTabFini(&tmpsyms);
    smExprInternFini(&tmpexprs);
    smViewInternFini(&tmpstrs);
}

typedef struct {
    SmView name;
    U32    pc;
    U32    end;
} Out;

typedef struct {
    Out *items;
    UInt len;
} OutView;

typedef struct {
    OutView view;
    UInt    cap;
} OutBuf;

static OutBuf OUTS = {0};

static void outBufAdd(Out item) {
    OutBuf *buf = &OUTS;
    SM_BUF_ADD_IMPL();
}

static Out *findOut(SmView name) {
    for (UInt i = 0; i < OUTS.view.len; ++i) {
        Out *out = OUTS.view.items + i;
        if (smViewEqual(out->name, name)) {
            return out;
        }
    }
    return NULL;
}

static CfgIn *findCfgIn(SmView name, CfgOut **out) {
    for (UInt i = 0; i < CFGS.view.len; ++i) {
        *out = CFGS.view.items + i;
        for (UInt j = 0; j < (*out)->ins.len; ++j) {
            CfgIn *in = (*out)->ins.items + j;
            if (smViewEqual(in->name, name)) {
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
    Out *out = findOut(cfgout->name);
    // TODO apply and adjust padding
    sect->pc = ((out->pc + in->align - 1) / in->align) * in->align;
    for (UInt i = 0; i < in->files.len; ++i) {
        SmView  path = in->files.items[i];
        FILE   *hnd  = openFile(path, "rb");
        SmSerde ser  = {hnd, path};
        smDeserializeToEnd(&ser, &sect->data);
    }
    if (in->size) {
        if (sect->data.view.len > in->sizeval) {
            smFatal("input section %" SM_VIEW_FMT " size ($%08" UINT_FMTX
                    ") is larger than "
                    "configured size: $%08" UINT_FMTX "\n",
                    SM_VIEW_FMT_ARG(sect->name), sect->data.view.len,
                    (UInt)in->sizeval);
        }
        if (in->fill) {
            while (sect->data.view.len < in->sizeval) {
                smBufCat(&sect->data, (SmView){&in->fillval, 1});
            }
        }
    }
    out->pc = sect->pc + sect->data.view.len;
    if (out->pc > out->end) {
        smFatal("no room in output section %" SM_VIEW_FMT
                " for input section %" SM_VIEW_FMT "\n",
                SM_VIEW_FMT_ARG(out->name), SM_VIEW_FMT_ARG(sect->name));
    }
    if (!smViewEqual(in->define, SM_VIEW_NULL)) {
        static SmBuf buf = {0};
        buf.view.len     = 0;
        smBufCat(&buf, in->define);
        smBufCat(&buf, SM_VIEW("_START"));
        SmView start = intern(buf.view);
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = globalLbl(start),
                               .value   = constExprBuf(sect->pc),
                               .unit    = EXPORT_UNIT,
                               .section = DEFINES_SECTION,
                               .pos     = in->defpos,
                               .flags   = SM_SYM_EQU,
                           });
        buf.view.len = 0;
        smBufCat(&buf, in->define);
        smBufCat(&buf, SM_VIEW("_SIZE"));
        SmView size = intern(buf.view);
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = globalLbl(size),
                               .value   = constExprBuf(sect->data.view.len),
                               .unit    = EXPORT_UNIT,
                               .section = DEFINES_SECTION,
                               .pos     = in->defpos,
                               .flags   = SM_SYM_EQU,
                           });
    }
}

static Bool solve(SmExprView view, SmView unit, I32 *num) {
    SmI32Buf stack = {0};
    for (UInt i = 0; i < view.len; ++i) {
        SmExpr *expr = view.items + i;
        switch (expr->kind) {
        case SM_EXPR_CONST:
            smI32BufAdd(&stack, expr->num);
            break;
        case SM_EXPR_LABEL: {
            SmSym *sym = smSymTabFind(&SYMS, expr->lbl);
            if (!sym) {
                goto fail;
            }
            if (!smViewEqual(sym->unit, unit) &&
                !smViewEqual(sym->unit, EXPORT_UNIT)) {
                goto fail;
            }
            I32 num;
            // yuck
            if (!solve(sym->value, unit, &num)) {
                goto fail;
            }
            smI32BufAdd(&stack, num);
            break;
        }
        case SM_EXPR_TAG: {
            SmSym *sym = smSymTabFind(&SYMS, expr->tag.lbl);
            if (!sym) {
                goto fail;
            }
            if (!smViewEqual(sym->unit, unit) &&
                !smViewEqual(sym->unit, EXPORT_UNIT)) {
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
            smI32BufAdd(&stack, tag->num);
            break;
        }
        case SM_EXPR_OP:
            --stack.view.len;
            I32 rhs = stack.view.items[stack.view.len];
            if (expr->op.unary) {
                switch (expr->op.tok) {
                case '+':
                    smI32BufAdd(&stack, rhs);
                    break;
                case '-':
                    smI32BufAdd(&stack, -rhs);
                    break;
                case '~':
                    smI32BufAdd(&stack, ~rhs);
                    break;
                case '!':
                    smI32BufAdd(&stack, !rhs);
                    break;
                case '<':
                    smI32BufAdd(&stack, ((U32)rhs) & 0xFF);
                    break;
                case '>':
                    smI32BufAdd(&stack, ((U32)rhs & 0xFF00) >> 8);
                    break;
                case '^':
                    smI32BufAdd(&stack, ((U32)rhs & 0xFF0000) >> 16);
                    break;
                default:
                    SM_UNREACHABLE();
                }
            } else {
                --stack.view.len;
                I32 lhs = stack.view.items[stack.view.len];
                switch (expr->op.tok) {
                case '+':
                    smI32BufAdd(&stack, lhs + rhs);
                    break;
                case '-':
                    smI32BufAdd(&stack, lhs - rhs);
                    break;
                case '*':
                    smI32BufAdd(&stack, lhs * rhs);
                    break;
                case '/':
                    smI32BufAdd(&stack, lhs / rhs);
                    break;
                case '%':
                    smI32BufAdd(&stack, lhs % rhs);
                    break;
                case SM_TOK_ASL:
                    smI32BufAdd(&stack, lhs << rhs);
                    break;
                case SM_TOK_ASR:
                    smI32BufAdd(&stack, lhs >> rhs);
                    break;
                case SM_TOK_LSR:
                    smI32BufAdd(&stack, ((U32)lhs) >> ((U32)rhs));
                    break;
                case '<':
                    smI32BufAdd(&stack, lhs < rhs);
                    break;
                case SM_TOK_LTE:
                    smI32BufAdd(&stack, lhs <= rhs);
                    break;
                case '>':
                    smI32BufAdd(&stack, lhs > rhs);
                    break;
                case SM_TOK_GTE:
                    smI32BufAdd(&stack, lhs >= rhs);
                    break;
                case SM_TOK_DEQ:
                    smI32BufAdd(&stack, lhs == rhs);
                    break;
                case SM_TOK_NEQ:
                    smI32BufAdd(&stack, lhs != rhs);
                    break;
                case '&':
                    smI32BufAdd(&stack, lhs & rhs);
                    break;
                case '|':
                    smI32BufAdd(&stack, lhs | rhs);
                    break;
                case '^':
                    smI32BufAdd(&stack, lhs ^ rhs);
                    break;
                case SM_TOK_AND:
                    smI32BufAdd(&stack, lhs && rhs);
                    break;
                case SM_TOK_OR:
                    smI32BufAdd(&stack, lhs || rhs);
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
            smI32BufAdd(&stack, sect->pc + expr->addr.pc);
            break;
        }
        default:
            SM_UNREACHABLE();
        }
    }
    assert(stack.view.len == 1);
    *num = *stack.view.items;
    smI32BufFini(&stack);
    return true;
fail:
    smI32BufFini(&stack);
    return false;
}

static void solveSyms() {
    // 2 passes over symbol table should be enough to solve all
    for (UInt i = 0; i < 2; ++i) {
        for (UInt j = 0; j < SYMS.cap; ++j) {
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
    for (UInt i = 0; i < SYMS.cap; ++i) {
        SmSym *sym = SYMS.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        if ((sym->value.len == 1) &&
            (sym->value.items[0].kind == SM_EXPR_CONST)) {
            continue;
        }
        SmView name = fullLblName(sym->lbl);
        smFatal("undefined symbol: %" SM_VIEW_FMT
                "\n\treferenced at %" SM_VIEW_FMT ":%" UINT_FMT ":%" UINT_FMT
                "\n",
                SM_VIEW_FMT_ARG(name), SM_VIEW_FMT_ARG(sym->pos.file),
                sym->pos.line, sym->pos.col);
    }
}

static Bool canReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }
static Bool canReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }

static void link(SmSect *sect) {
    for (UInt i = 0; i < sect->relocs.view.len; ++i) {
        SmReloc *reloc = sect->relocs.view.items + i;
        I32      num;
        if (!solve(reloc->value, reloc->unit, &num)) {
            smFatal("expression cannot be solved\n\treferenced at %" SM_VIEW_FMT
                    ":%" UINT_FMT ":%" UINT_FMT "\n",
                    SM_VIEW_FMT_ARG(reloc->pos.file), reloc->pos.line,
                    reloc->pos.col);
        }
        switch (reloc->width) {
        case 1:
            if (!canReprU8(num)) {
                Bool legal = false;
                if (reloc->flags & SM_RELOC_HRAM) {
                    for (UInt j = 0; j < SECTS.view.len; ++j) {
                        SmSect *sect   = SECTS.view.items + j;
                        CfgOut *cfgout = NULL;
                        CfgIn  *in     = findCfgIn(sect->name, &cfgout);
                        assert(cfgout);
                        assert(in);
                        if (in->kind != CFG_IN_GB_HRAM) {
                            continue;
                        }
                        if ((num >= (I32)(sect->pc)) &&
                            (num < (I32)(sect->pc + sect->data.view.len))) {
                            legal = true;
                            break;
                        }
                    }
                }
                if (!legal) {
                    smFatal("expression does not fit in a byte: "
                            "$%08" U32_FMTX "\n\treferenced at %" SM_VIEW_FMT
                            ":%" UINT_FMT ":%" UINT_FMT "\n",
                            (U32)num, SM_VIEW_FMT_ARG(reloc->pos.file),
                            reloc->pos.line, reloc->pos.col);
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
                    smFatal("illegal reset vector: $%08" U32_FMTX
                            "\n\treferenced "
                            "at %" SM_VIEW_FMT ":%" UINT_FMT ":%" UINT_FMT "\n",
                            (U32)num, SM_VIEW_FMT_ARG(reloc->pos.file),
                            reloc->pos.line, reloc->pos.col);
                }
                sect->data.view.bytes[reloc->offset] = op;
                continue;
            }
            sect->data.view.bytes[reloc->offset] = (U8)num;
            break;
        case 2:
            // TODO check if src and dst banks are the same
            // also a JP within bank0 is always legal for GB
            if (!canReprU16(num)) {
                smFatal("expression does not fit in a word: "
                        "$%08" U32_FMTX "\n\treferenced at %" SM_VIEW_FMT
                        ":%" UINT_FMT ":%" UINT_FMT "\n",
                        (U32)num, SM_VIEW_FMT_ARG(reloc->pos.file),
                        reloc->pos.line, reloc->pos.col);
            }
            sect->data.view.bytes[reloc->offset]     = (U8)(num & 0xFF);
            sect->data.view.bytes[reloc->offset + 1] = (U8)((num >> 8) & 0xFF);
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
}

_Noreturn void fatalPos(SmPos pos, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(&TS, pos, fmt, args);
}

static U32    peek() { return smTokStreamPeek(&TS); }
static void   eat() { smTokStreamEat(&TS); }
static SmPos  tokPos() { return smTokStreamPos(&TS); }
static I32    tokNum() { return smTokStreamNum(&TS); }
static SmView tokView() { return smTokStreamView(&TS); }

static void expect(U32 tok) {
    U32 peeked = peek();
    if (peeked != tok) {
        SmView expected = smTokName(tok);
        SmView found    = smTokName(peeked);
        fatal("expected %" SM_VIEW_FMT ", got %" SM_VIEW_FMT "\n",
              SM_VIEW_FMT_ARG(expected), SM_VIEW_FMT_ARG(found));
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
        smTokStreamFatal(
            &TS, "number does not fit in a byte: $%08" U32_FMTX "\n", (U32)num);
    }
    eat();
    return (U8)num;
}

static U16 eatU16() {
    expect(SM_TOK_NUM);
    I32 num = tokNum();
    if (!canReprU16(num)) {
        smTokStreamFatal(
            &TS, "number does not fit in a word: $%08" U32_FMTX "\n", (U32)num);
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
            SmView name = intern(tokView());
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

static CfgInView parseInSects(CfgOut const *out) {
    CfgInBuf ins = {0};
    while (true) {
        CfgIn in   = {0};
        in.tags    = out->tags;
        in.fill    = out->fill;
        in.fillval = out->fillval;
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
            in.name = intern(tokView());
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
                if (smViewEqualIgnoreAsciiCase(tokView(), SM_VIEW("kind"))) {
                    eat();
                    expect('=');
                    eat();
                    kind = true;
                    switch (peek()) {
                    case SM_TOK_STR:
                    case SM_TOK_ID: {
                        if (smViewEqualIgnoreAsciiCase(tokView(),
                                                       SM_VIEW("code"))) {
                            eat();
                            in.kind = CFG_IN_CODE;
                            continue;
                        }
                        if (smViewEqualIgnoreAsciiCase(tokView(),
                                                       SM_VIEW("data"))) {
                            eat();
                            in.kind = CFG_IN_DATA;
                            continue;
                        }
                        if (smViewEqualIgnoreAsciiCase(tokView(),
                                                       SM_VIEW("uninit"))) {
                            eat();
                            in.kind = CFG_IN_UNINIT;
                            continue;
                        }
                        if (smViewEqualIgnoreAsciiCase(tokView(),
                                                       SM_VIEW("gb_hram"))) {
                            eat();
                            in.kind = CFG_IN_GB_HRAM;
                            continue;
                        }
                        SmView view = tokView();
                        fatal("unrecognized input section kind: %" SM_VIEW_FMT
                              "\n",
                              SM_VIEW_FMT_ARG(view));
                    }
                    default:
                        expect(SM_TOK_STR);
                    }
                }
                if (smViewEqualIgnoreAsciiCase(tokView(), SM_VIEW("align"))) {
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
                if (smViewEqualIgnoreAsciiCase(tokView(), SM_VIEW("size"))) {
                    eat();
                    expect('=');
                    eat();
                    in.size    = true;
                    in.sizeval = eatU16();
                    continue;
                }
                if (smViewEqualIgnoreAsciiCase(tokView(), SM_VIEW("fill"))) {
                    eat();
                    in.fill = true;
                    if (peek() == '=') {
                        expect('=');
                        eat();
                        in.fillval = eatU8();
                    }
                    continue;
                }
                if (smViewEqualIgnoreAsciiCase(tokView(), SM_VIEW("define"))) {
                    in.defpos = tokPos();
                    eat();
                    in.define = in.name;
                    if (peek() == '=') {
                        eat();
                        if ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
                            in.define = intern(tokView());
                            in.defpos = tokPos();
                            eat();
                        } else {
                            expect(SM_TOK_STR);
                        }
                    }
                    continue;
                }
                SmView view = tokView();
                fatal("unrecognized input section attribute: %" SM_VIEW_FMT
                      "\n",
                      SM_VIEW_FMT_ARG(view));
                continue;
            }
            case '[': {
                eat();
                CfgI32Tab oldtags = in.tags;
                in.tags           = parseTags();
                // copy base tags
                for (UInt i = 0; i < oldtags.cap; ++i) {
                    CfgI32Entry *oldtag = oldtags.entries + i;
                    if (smViewEqual(oldtag->name, SM_VIEW_NULL)) {
                        continue;
                    }
                    // only apply tag if the output section does not already
                    // have it
                    CfgI32Entry *newtag = cfgI32TabFind(&in.tags, oldtag->name);
                    if (smViewEqual(newtag->name, SM_VIEW_NULL)) {
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
        cfgInBufAdd(&ins, in);
        expectEOL();
        eat();
        continue;
    }
endsects:
    return ins.view;
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
            out.name     = intern(tokView());
            SmPos pos    = tokPos();
            eat();
            while (true) {
                switch (peek()) {
                case SM_TOK_STR:
                case SM_TOK_ID: {
                    if (smViewEqualIgnoreAsciiCase(tokView(),
                                                   SM_VIEW("start"))) {
                        eat();
                        expect('=');
                        eat();
                        start     = true;
                        out.start = eatU16();
                        continue;
                    }
                    if (smViewEqualIgnoreAsciiCase(tokView(),
                                                   SM_VIEW("size"))) {
                        eat();
                        expect('=');
                        eat();
                        size     = true;
                        out.size = eatU16();
                        continue;
                    }
                    if (smViewEqualIgnoreAsciiCase(tokView(),
                                                   SM_VIEW("fill"))) {
                        eat();
                        out.fill = true;
                        if (peek() == '=') {
                            expect('=');
                            eat();
                            out.fillval = eatU8();
                        }
                        continue;
                    }
                    if (smViewEqualIgnoreAsciiCase(tokView(),
                                                   SM_VIEW("kind"))) {
                        eat();
                        expect('=');
                        eat();
                        kind = true;
                        switch (peek()) {
                        case SM_TOK_STR:
                        case SM_TOK_ID: {
                            if (smViewEqualIgnoreAsciiCase(tokView(),
                                                           SM_VIEW("ro"))) {
                                eat();
                                out.kind = CFG_OUT_READONLY;
                                continue;
                            }
                            if (smViewEqualIgnoreAsciiCase(tokView(),
                                                           SM_VIEW("rw"))) {
                                eat();
                                out.kind = CFG_OUT_READWRITE;
                                continue;
                            }
                            SmView view = tokView();
                            fatal(
                                "unrecognized ouput section kind: %" SM_VIEW_FMT
                                "\n",
                                SM_VIEW_FMT_ARG(view));
                        }
                        default:
                            expect(SM_TOK_STR);
                        }
                    }
                    if (smViewEqualIgnoreAsciiCase(tokView(),
                                                   SM_VIEW("define"))) {
                        out.defpos = tokPos();
                        eat();
                        out.define = out.name;
                        if (peek() == '=') {
                            eat();
                            if ((peek() == SM_TOK_STR) ||
                                (peek() == SM_TOK_ID)) {
                                out.define = intern(tokView());
                                out.defpos = tokPos();
                                eat();
                            } else {
                                expect(SM_TOK_STR);
                            }
                        }
                        continue;
                    }
                    SmView view = tokView();
                    fatal("unrecognized output section attribute: %" SM_VIEW_FMT
                          "\n",
                          SM_VIEW_FMT_ARG(view));
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
            cfgOutBufAdd(&CFGS, out);
            expectEOL();
            eat();
            continue;
        }
        default: {
            SmView view = smTokName(peek());
            fatal("unexpected %" SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
        }
        }
    }
endsects:
    expect('}');
    eat();
    if (CFGS.view.len == 0) {
        fatal("at least 1 output section is required for linking\n");
    }
}

static void parseCfg() {
    SmView cfgname =
        smPathIntern(&STRS, (SmView){(U8 *)cfgfile_name, strlen(cfgfile_name)});
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
            if (smViewEqualIgnoreAsciiCase(tokView(), SM_VIEW("sections"))) {
                eat();
                parseOutSects();
                sections = true;
                continue;
            }
            SmView view = tokView();
            fatal("unrecognized config area: %" SM_VIEW_FMT "\n",
                  SM_VIEW_FMT_ARG(view));
        }
        default: {
            SmView view = smTokName(peek());
            fatal("unexpected %" SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
        }
        }
    }
    if (!sections) {
        fatal("no `sections` config area was defined\n");
    }
    for (UInt i = 0; i < CFGS.view.len; ++i) {
        CfgOut *out = CFGS.view.items + i;
        // Pre-add the output sections defined in the cfg
        outBufAdd((Out){
            .name = out->name,
            .pc   = out->start,
            .end  = out->start + out->size,
        });
        if (!smViewEqual(out->define, SM_VIEW_NULL)) {
            static SmBuf buf = {0};
            buf.view.len     = 0;
            smBufCat(&buf, out->define);
            smBufCat(&buf, SM_VIEW("_START"));
            SmView start = intern(buf.view);
            smSymTabAdd(&SYMS, (SmSym){
                                   .lbl     = globalLbl(start),
                                   .value   = constExprBuf(out->start),
                                   .unit    = EXPORT_UNIT,
                                   .section = DEFINES_SECTION,
                                   .pos     = out->defpos,
                                   .flags   = SM_SYM_EQU,
                               });
            buf.view.len = 0;
            smBufCat(&buf, out->define);
            smBufCat(&buf, SM_VIEW("_SIZE"));
            SmView size = intern(buf.view);
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
            smSectBufAdd(&SECTS, (SmSect){
                                     .name   = in->name,
                                     .pc     = 0,
                                     .data   = {{0}, 0}, // GCC doesnt like {0}
                                     .relocs = {{0}, 0},
                                 });
            switch (out->kind) {
            case CFG_OUT_READONLY:
                // TODO could check this while parsing
                if (in->kind != CFG_IN_CODE) {
                    fatal(
                        "input section %" SM_VIEW_FMT " is not kind-compatible "
                        "with output section %" SM_VIEW_FMT "\n",
                        SM_VIEW_FMT_ARG(in->name), SM_VIEW_FMT_ARG(out->name));
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
    for (UInt i = 0; i < CFGS.view.len; ++i) {
        CfgOut *cfgout = CFGS.view.items + i;
        for (UInt j = 0; j < cfgout->ins.len; ++j) {
            CfgIn *in = cfgout->ins.items + j;
            if ((in->kind == CFG_IN_UNINIT) || (in->kind == CFG_IN_GB_HRAM)) {
                continue;
            }
            SmSect *sect = findSect(in->name);
            assert(sect);
            smSerializeView(&ser, sect->data.view);
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

static SmLbl globalLbl(SmView name) { return (SmLbl){{0}, name}; }

static SmExprView constExprBuf(I32 num) {
    return smExprIntern(
        &EXPRS, (SmExprView){&(SmExpr){.kind = SM_EXPR_CONST, .num = num}, 1});
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
        return -1;
    }
    SmView lname = fullLblName(lhs->lbl);
    SmView rname = fullLblName(rhs->lbl);
    int cmp = memcmp(lname.bytes, rname.bytes, uIntMin(lname.len, rname.len));
    if (!cmp) {
        return lname.len - rname.len;
    }
    return cmp;
}

static SmSymTab sortSyms() {
    // Clone and sort the symbol table
    SmSymTab tab = {0};
    for (UInt i = 0; i < SYMS.cap; ++i) {
        SmSym *sym = SYMS.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        smSymTabAdd(&tab, *sym);
    }
    qsort(tab.syms, tab.cap, sizeof(SmSym), (void *)cmpSym);
    return tab;
}

static void writeSyms() {
    FILE    *hnd = openFileCstr(symfile_name, "wb+");
    SmSymTab tab = sortSyms();
    for (UInt i = 0; i < tab.cap; ++i) {
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
        SmView  name   = fullLblName(sym->lbl);
        CfgOut *cfgout = NULL;
        CfgIn  *in     = findCfgIn(sym->section, &cfgout);
        assert(in);
        I32          bank = 0;
        CfgI32Entry *tag  = cfgI32TabFind(&in->tags, SM_VIEW("bank"));
        if (tag) {
            bank = tag->num;
        }
        char const *fmt = "%02" U8_FMTX ":%04" U16_FMTX " %" SM_VIEW_FMT "\n";
        if (bank > 0xFF) {
            fmt = "%04" U16_FMTX ":%04" U16_FMTX " %" SM_VIEW_FMT "\n";
        }
        if (fprintf(hnd, fmt, (U8)bank, (U16)sym->value.items[0].num,
                    SM_VIEW_FMT_ARG(name)) < 0) {
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
    for (UInt i = 0; i < tab.cap; ++i) {
        SmSym *sym = tab.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        SmView name = fullLblName(sym->lbl);
        if (fprintf(hnd, "%" SM_VIEW_FMT "\t%" SM_VIEW_FMT "\t%" UINT_FMT " \n",
                    SM_VIEW_FMT_ARG(name), SM_VIEW_FMT_ARG(sym->pos.file),
                    sym->pos.line) < 0) {
            smFatal("%s: failed to write file: %s\n", symfile_name,
                    strerror(errno));
        }
    }
    smSymTabFini(&tab);
    closeFile(hnd);
}
