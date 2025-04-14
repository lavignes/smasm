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

static FILE *cfgfile        = NULL;
static char *cfgfile_name   = NULL;
static FILE *outfile        = NULL;
static char *outfile_name   = NULL;
static char *symfile_name   = NULL;
static char *tagfile_name   = NULL;

static SmBufIntern  STRS    = {0};
static SmSymTab     SYMS    = {0};
static SmExprIntern EXPRS   = {0};
static SmPathSet    OBJS    = {0};
static SmSectGBuf   SECTS   = {0};

static CfgMemGBuf  CFGMEMS  = {0};
static CfgSectGBuf CFGSECTS = {0};

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
    return 0;
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

static SmBuf fullLblName(SmLbl lbl) {
    static SmGBuf buf = {0};
    buf.inner.len     = 0;
    if (!smBufEqual(lbl.scope, SM_BUF_NULL)) {
        smGBufCat(&buf, lbl.scope);
        smGBufCat(&buf, SM_BUF("."));
    }
    smGBufCat(&buf, lbl.name);
    return intern(buf.inner);
}

static SmSect *findSect(SmBuf name) {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        if (smBufEqual(sect->name, name)) {
            return sect;
        }
    }
    return NULL;
}

static CfgSect *findCfgSect(SmBuf name) {
    for (UInt i = 0; i < CFGSECTS.inner.len; ++i) {
        CfgSect *sect = CFGSECTS.inner.items + i;
        if (smBufEqual(sect->name, name)) {
            return sect;
        }
    }
    return NULL;
}

static CfgMem *findCfgMem(SmBuf name) {
    for (UInt i = 0; i < CFGMEMS.inner.len; ++i) {
        CfgMem *mem = CFGMEMS.inner.items + i;
        if (smBufEqual(mem->name, name)) {
            return mem;
        }
    }
    return NULL;
}

static void loadObj(SmBuf path) {
    FILE   *hnd   = openFile(path, "rb");
    SmSerde ser   = {hnd, path};
    U32     magic = smDeserializeU32(&ser);
    if (magic != *(U32 *)"SM00") {
        objFatal(path, "bad magic: $%04x\n", magic);
    }
    // TODO free mem
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
            assert(sect);
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
                               .value   = smExprIntern(&EXPRS, sym->value),
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
    // TODO free mem
    SmSectBuf tmpsects = smDeserializeSectBuf(&ser, &tmpstrs, &tmpexprs);
    closeFile(hnd);
    for (UInt i = 0; i < tmpsects.len; ++i) {
        SmSect *sect    = tmpsects.items + i;
        SmSect *dstsect = findSect(sect->name);
        if (!dstsect) {
            objFatal(path, "section %.*s is not defined in config\n",
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
                    .value  = smExprIntern(&EXPRS, reloc->value),
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
    }
}

struct Mem {
    SmBuf name;
    U32   pc;
    U32   end;
};
typedef struct Mem Mem;

struct MemBuf {
    Mem *items;
    UInt len;
};
typedef struct MemBuf MemBuf;

struct MemGBuf {
    MemBuf inner;
    UInt   size;
};
typedef struct MemGBuf MemGBuf;

static MemGBuf MEMS = {0};

static void memGBufAdd(Mem item) {
    MemGBuf *buf = &MEMS;
    SM_GBUF_ADD_IMPL(Mem);
}

static Mem *findMem(SmBuf name) {
    for (UInt i = 0; i < MEMS.inner.len; ++i) {
        Mem *mem = MEMS.inner.items + i;
        if (smBufEqual(mem->name, name)) {
            return mem;
        }
    }
    return NULL;
}

static void allocate(SmSect *sect) {
    CfgSect *cfgsect = findCfgSect(sect->name);
    assert(cfgsect);
    Mem *mem = findMem(cfgsect->load);
    assert(mem);
    U32 aligned =
        ((mem->pc + cfgsect->align - 1) / cfgsect->align) * cfgsect->align;
    sect->pc = aligned;
    for (UInt i = 0; i < cfgsect->files.inner.len; ++i) {
        SmBuf   path = cfgsect->files.inner.items[i];
        FILE   *hnd  = openFile(path, "rb");
        SmSerde ser  = {hnd, path};
        smDeserializeToEnd(&ser, &sect->data);
    }
    mem->pc = aligned + sect->data.inner.len;
    if (mem->pc > mem->end) {
        smFatal("no room in memory %.*s for section %.*s\n", (int)mem->name.len,
                mem->name.bytes, (int)sect->name.len, sect->name.bytes);
    }
    if (cfgsect->define) {
        static SmGBuf buf = {0};
        buf.inner.len     = 0;
        smGBufCat(&buf, SM_BUF("__"));
        smGBufCat(&buf, cfgsect->name);
        smGBufCat(&buf, SM_BUF("_START__"));
        SmBuf start = intern(buf.inner);
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = globalLbl(start),
                               .value   = constExprBuf(sect->pc),
                               .unit    = EXPORT_UNIT,
                               .section = DEFINES_SECTION,
                               .pos     = {DEFINES_SECTION, 1, 1},
                               .flags   = SM_SYM_EQU,
                           });
        buf.inner.len = 0;
        smGBufCat(&buf, SM_BUF("__"));
        smGBufCat(&buf, cfgsect->name);
        smGBufCat(&buf, SM_BUF("_SIZE__"));
        SmBuf end = intern(buf.inner);
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = globalLbl(end),
                               .value   = constExprBuf(sect->data.inner.len),
                               .unit    = EXPORT_UNIT,
                               .section = DEFINES_SECTION,
                               .pos     = {DEFINES_SECTION, 1, 1},
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
            CfgSect     *sect = findCfgSect(sym->section);
            // find the tag in the section
            CfgI32Entry *tag  = cfgI32TabFind(&sect->tags, expr->tag.name);
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
                if (!(reloc->flags & SM_RELOC_HI)) {
                    goto done;
                }
                for (UInt j = 0; j < SECTS.inner.len; ++j) {
                    SmSect  *sect    = SECTS.inner.items + j;
                    CfgSect *cfgsect = findCfgSect(sect->name);
                    if (!cfgsect) {
                        continue;
                    }
                    if (cfgsect->kind != CFG_SECT_HIGHPAGE) {
                        break;
                    }
                    if ((num >= (I32)(sect->pc)) &&
                        (num < (I32)(sect->pc + sect->data.inner.len))) {
                        legal = true;
                    }
                    break;
                }
            done:
                if (!legal) {
                    smFatal("expression does not fit in a byte: $%08X\n",
                            "\treferenced at %.*s:%zu:%zu\n", num,
                            (int)reloc->pos.file.len, reloc->pos.file.bytes,
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
            smUnreachable();
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

static void parseMems() {
    expect('{');
    eat();
    while (true) {
        if (peek() == '\n') {
            eat();
            continue;
        }
        if (peek() == '}') {
            break;
        }
        if ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
            CfgMem mem = {0};
            SmPos  pos = tokPos();
            mem.name   = intern(tokBuf());
            eat();
            Bool start = false;
            Bool size  = false;
            Bool kind  = false;
            while ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("start"))) {
                    eat();
                    expect('=');
                    eat();
                    start     = true;
                    mem.start = eatU16();
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("size"))) {
                    eat();
                    expect('=');
                    eat();
                    size     = true;
                    mem.size = eatU16();
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("fill"))) {
                    eat();
                    expect('=');
                    eat();
                    mem.fill    = true;
                    mem.fillval = eatU8();
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
                                                      SM_BUF("readonly"))) {
                            eat();
                            mem.kind = CFG_MEM_READONLY;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("readwrite"))) {
                            eat();
                            mem.kind = CFG_MEM_READWRITE;
                            continue;
                        }
                        SmBuf buf = tokBuf();
                        fatal("unrecognized memory kind: %.*s\n", (int)buf.len,
                              buf.bytes);
                    }
                    default:
                        expect(SM_TOK_STR);
                    }
                }
                SmBuf buf = tokBuf();
                fatal("unrecognized memory attribute: %.*s\n", (int)buf.len,
                      buf.bytes);
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
            cfgMemGBufAdd(&CFGMEMS, mem);
            if (peek() == '}') {
                break;
            }
            expectEOL();
            eat();
        }
    }
    expect('}');
    if (CFGMEMS.inner.len == 0) {
        fatal("at least 1 memory area is required for linking\n");
    }
    eat();
}

static void parseSects() {
    expect('{');
    eat();
    while (true) {
        if (peek() == '\n') {
            eat();
            continue;
        }
        if (peek() == '}') {
            break;
        }
        if ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
            CfgSect sect = {0};
            SmPos   pos  = tokPos();
            sect.name    = intern(tokBuf());
            sect.align   = 1;
            eat();
            Bool load = false;
            Bool kind = false;
            while ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("load"))) {
                    eat();
                    expect('=');
                    eat();
                    load = true;
                    if ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
                        sect.load = intern(tokBuf());
                        eat();
                        continue;
                    }
                    expect(SM_TOK_STR);
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
                                                      SM_BUF("code"))) {
                            eat();
                            sect.kind = CFG_SECT_CODE;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("data"))) {
                            eat();
                            sect.kind = CFG_SECT_DATA;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("uninit"))) {
                            eat();
                            sect.kind = CFG_SECT_UNINIT;
                            continue;
                        }
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("highpage"))) {
                            eat();
                            sect.kind = CFG_SECT_HIGHPAGE;
                            continue;
                        }
                        SmBuf buf = tokBuf();
                        fatal("unrecognized section kind: %.*s\n", (int)buf.len,
                              buf.bytes);
                    }
                    default:
                        expect(SM_TOK_STR);
                    }
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("align"))) {
                    eat();
                    expect('=');
                    eat();
                    sect.align = eatU16();
                    if (sect.align == 0) {
                        fatal("section alignment must be greater than 0\n");
                    }
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("define"))) {
                    eat();
                    sect.define = true;
                    continue;
                }
                SmBuf buf = tokBuf();
                fatal("unrecognized section attribute: %.*s\n", (int)buf.len,
                      buf.bytes);
            }
            if (peek() == '{') {
                eat();
                while (true) {
                    if (peek() == '\n') {
                        eat();
                        continue;
                    }
                    if (peek() == '}') {
                        break;
                    }
                    if ((peek() == SM_TOK_STR) || (peek() == SM_TOK_ID)) {
                        if (smBufEqualIgnoreAsciiCase(tokBuf(),
                                                      SM_BUF("tags"))) {
                            eat();
                            while ((peek() == SM_TOK_STR) ||
                                   (peek() == SM_TOK_ID)) {
                                SmBuf name = intern(tokBuf());
                                eat();
                                expect('=');
                                eat();
                                expect(SM_TOK_NUM);
                                cfgI32TabAdd(&sect.tags,
                                             (CfgI32Entry){name, tokNum()});
                                eat();
                            }
                            continue;
                        }
                        SmBuf buf = tokBuf();
                        fatal("unrecognized section sub-attribute: %.*s\n",
                              (int)buf.len, buf.bytes);
                    }
                    if (peek() == '}') {
                        break;
                    }
                    expectEOL();
                    eat();
                }
                expect('}');
                eat();
            }
            if (!load) {
                fatalPos(pos, "`load` attribute is required\n");
            }
            if (!kind) {
                fatalPos(pos, "`kind` attribute is required\n");
            }
            cfgSectGBufAdd(&CFGSECTS, sect);
            if (peek() == '}') {
                break;
            }
            expectEOL();
            eat();
        }
    }
    expect('}');
    eat();
    if (CFGSECTS.inner.len == 0) {
        fatal("at least 1 section is required for linking\n");
    }
}

static void parseCfg() {
    smTokStreamFileInit(&TS, (SmBuf){(U8 *)cfgfile_name, strlen(cfgfile_name)},
                        cfgfile);
    Bool memories = false;
    Bool sections = false;
    while (peek() != SM_TOK_EOF) {
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case SM_TOK_STR:
        case SM_TOK_ID: {
            if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("memories"))) {
                eat();
                parseMems();
                memories = true;
                continue;
            }
            if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("sections"))) {
                eat();
                parseSects();
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
    if (!memories) {
        fatal("no `memories` config area was defined\n");
    }
    for (UInt i = 0; i < CFGMEMS.inner.len; ++i) {
        CfgMem *mem = CFGMEMS.inner.items + i;
        // Pre-add the memories defined in the cfg
        memGBufAdd((Mem){
            .name = mem->name,
            .pc   = mem->start,
            .end  = mem->start + mem->size,
        });
    }
    if (!sections) {
        fatal("no `sections` config area was defined\n");
    }
    for (UInt i = 0; i < CFGSECTS.inner.len; ++i) {
        CfgSect *sect = CFGSECTS.inner.items + i;
        // Pre-add the sections defined in the cfg
        smSectGBufAdd(&SECTS, (SmSect){
                                  .name   = sect->name,
                                  .pc     = 0,
                                  .data   = {{0}, 0}, // GCC doesnt like {0}
                                  .relocs = {{0}, 0},
                              });
        CfgMem *mem = findCfgMem(sect->load);
        if (!mem) {
            fatal("no memory found with name: %.*s\n", (int)sect->load.len,
                  sect->load.bytes);
        }
        switch (mem->kind) {
        case CFG_MEM_READONLY:
            if (sect->kind != CFG_SECT_CODE) {
                fatal("section %.*s is not kind-compatible "
                      "with memory %.*s\n",
                      (int)sect->name.len, sect->name.bytes, (int)mem->name.len,
                      mem->name.bytes);
            }
        case CFG_MEM_READWRITE:
            continue;
        default:
            smUnreachable();
        }
    }
}

static void serialize() {
    SmSerde ser = {outfile, {(U8 *)outfile_name, strlen(outfile_name)}};
    for (UInt i = 0; i < CFGMEMS.inner.len; ++i) {
        CfgMem *cfgmem = CFGMEMS.inner.items + i;
        for (UInt j = 0; j < CFGSECTS.inner.len; ++j) {
            CfgSect *cfgsect = CFGSECTS.inner.items + j;
            if (!smBufEqual(cfgsect->load, cfgmem->name)) {
                continue;
            }
            if ((cfgsect->kind == CFG_SECT_UNINIT) ||
                (cfgsect->kind == CFG_SECT_HIGHPAGE)) {
                continue;
            }
            SmSect *sect = findSect(cfgsect->name);
            assert(sect);
            smSerializeBuf(&ser, sect->data.inner);
        }
        if (cfgmem->fill) {
            Mem *mem = findMem(cfgmem->name);
            assert(mem);
            for (UInt k = mem->pc; k < mem->end; ++k) {
                smSerializeU8(&ser, cfgmem->fillval);
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

static void writeSyms() {
    FILE *hnd = openFileCstr(symfile_name, "wb+");
    for (UInt i = 0; i < SYMS.size; ++i) {
        SmSym *sym = SYMS.syms + i;
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
        SmBuf    name    = fullLblName(sym->lbl);
        CfgSect *cfgsect = findCfgSect(sym->section);
        assert(cfgsect);
        I32          bank = 0;
        CfgI32Entry *tag  = cfgI32TabFind(&cfgsect->tags, SM_BUF("bank"));
        if (tag) {
            bank = tag->num;
        }
        if (fprintf(hnd, "%02X:%04X %.*s\n", bank, sym->value.items[0].num,
                    (int)name.len, name.bytes) < 0) {
            smFatal("%s: failed to write file: %s\n", symfile_name,
                    strerror(errno));
        }
    }
    closeFile(hnd);
}

static void writeTags() {
    FILE   *hnd = openFileCstr(tagfile_name, "wb+");
    SmSerde ser = {hnd, {(U8 *)tagfile_name, strlen(tagfile_name)}};
    (void)ser;
    smUnimplemented("writing CTAG files");
    closeFile(hnd);
}
