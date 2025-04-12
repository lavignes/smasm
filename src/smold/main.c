#include "cfg.h"

#include <smasm/fatal.h>
#include <smasm/path.h>
#include <smasm/serde.h>
#include <smasm/sym.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>

static void help() {
    fprintf(
        stderr,
        "Usage: smold [OPTIONS] --config <CONFIG> [OBJECTS]...\n"
        "\n"
        "Arguments:\n"
        "  [OBJECTS]...  Object files\n"
        "\n"
        "Options:\n"
        "  -c, --config <CONFIG>        Config file\n"
        "  -o, --output <OUTPUT>        Output file (default: stdout)\n"
        "  -g, --debug <DEBUG>          Output file for `SYM` debug symbols\n"
        "      --tags <TAGS>            Output file for VIM tags\n"
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
static void      serialize();
static void      writeSyms();
static void      writeTags();

static FILE *cfgfile      = NULL;
static char *cfgfile_name = NULL;
static FILE *outfile      = NULL;
static char *outfile_name = NULL;
static FILE *symfile      = NULL;
static char *symfile_name = NULL;
static FILE *tagfile      = NULL;
static char *tagfile_name = NULL;

static SmBufIntern  STRS  = {0};
static SmSymTab     SYMS  = {0};
static SmExprIntern EXPRS = {0};
static SmPathSet    OBJS  = {0};

static CfgMemGBuf  MEMS   = {0};
static CfgSectGBuf SECTS  = {0};

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
    smGBufCat(&buf, (SmBuf){(U8 *)"\0", 1});
    return openFileCstr((char const *)buf.inner.bytes, modes);
}

static _Noreturn void objFatalV(SmBuf path, char const *fmt, va_list args) {
    smFatal("%.*s: ", path.len, path.bytes);
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

static void loadObj(SmBuf path) {
    // TODO free mem
    SmBufIntern  TMPSTRS  = {0};
    SmExprIntern TMPEXPRS = {0};
    SmSymTab     TMPSYMS  = {0};
    FILE        *hnd      = openFile(path, "rb");
    SmSerde      ser      = {hnd, path};
    U32          magic    = smDeserializeU32(&ser);
    if (magic != *(U32 *)"SM00") {
        objFatal(path, "bad magic: %04x\n", magic);
    }
    smDeserializeBufIntern(&ser, &TMPSTRS);
    smDeserializeExprIntern(&ser, &TMPEXPRS, &TMPSTRS);
    smDeserializeSymTab(&ser, &TMPSYMS, &TMPSTRS, &TMPEXPRS);
    // Merge into main symtab
    for (UInt i = 0; i < TMPSYMS.len; ++i) {
        SmSym *sym = TMPSYMS.syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        // Hide static symbols under file-specific unit
        SmBuf unit;
        if (smBufEqual(sym->unit, STATIC_UNIT)) {
            unit = intern(path);
        } else {
            unit = intern(sym->unit);
        }
        SmSym *whence = smSymTabFind(&SYMS, sym->lbl);
        if (whence && smBufEqual(whence->unit, unit)) {
            smFatal("duplicate exported symbol: %.*s\n"
                    "\tdefined at %.*s:%u:%u\n"
                    "\tagain at %.*s:%u:%u\n",
                    whence->lbl.name.len, whence->lbl.name.bytes,
                    whence->pos.file.len, whence->pos.file.bytes,
                    whence->pos.line, whence->pos.col, sym->pos.file.len,
                    sym->pos.file.bytes, sym->pos.line, sym->pos.col);
        }
        smSymTabAdd(&SYMS, (SmSym){
                               .lbl     = internLbl(sym->lbl),
                               .value   = sym->value,
                               .unit    = unit,
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
    smUnimplemented();
    closeFile(hnd);
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
    if (peek() != tok) {
        // TODO utf8
        if (isprint(tok)) {
            smTokStreamFatal(&TS, "expected `%c`\n", tok);
        } else {
            SmBuf name = smTokName(tok);
            smTokStreamFatal(&TS, "expected %.*s\n", name.len, name.bytes);
        }
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

static Bool canReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }
static Bool canReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }

static U8 eatU8() {
    expect(SM_TOK_NUM);
    I32 num = tokNum();
    if (!canReprU8(num)) {
        smTokStreamFatal(&TS, "number does not fit in a byte\n");
    }
    eat();
    return (U8)num;
}

static U16 eatU16() {
    expect(SM_TOK_NUM);
    I32 num = tokNum();
    if (!canReprU16(num)) {
        smTokStreamFatal(&TS, "number does not fit in a word\n");
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
                    mem.fill = eatU8();
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
                        fatal("unrecognized memory kind: %.*s\n", buf.len,
                              buf.bytes);
                    }
                    default:
                        expect(SM_TOK_STR);
                    }
                }
                SmBuf buf = tokBuf();
                fatal("unrecognized memory attribute: %.*s\n", buf.len,
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
            cfgMemGBufAdd(&MEMS, mem);
            if (peek() == '}') {
                break;
            }
            expectEOL();
            eat();
        }
    }
    expect('}');
    if (MEMS.inner.len == 0) {
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
                                                      SM_BUF("zeropage"))) {
                            eat();
                            sect.kind = CFG_SECT_ZEROPAGE;
                            continue;
                        }
                        SmBuf buf = tokBuf();
                        fatal("unrecognized section kind: %.*s\n", buf.len,
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
                    continue;
                }
                if (smBufEqualIgnoreAsciiCase(tokBuf(), SM_BUF("define"))) {
                    eat();
                    sect.define = true;
                    continue;
                }
                SmBuf buf = tokBuf();
                fatal("unrecognized section attribute: %.*s\n", buf.len,
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
                              buf.len, buf.bytes);
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
            cfgSectGrowBufAdd(&SECTS, sect);
            if (peek() == '}') {
                break;
            }
            expectEOL();
            eat();
        }
    }
    expect('}');
    eat();
    if (SECTS.inner.len == 0) {
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
            smTokStreamFatal(&TS, "unrecognized config area: %.*s\n", buf.len,
                             buf.bytes);
        }
        default:
            if (isprint(peek())) {
                smTokStreamFatal(&TS, "unexpected `%c`\n", peek());
            } else {
                SmBuf name = smTokName(peek());
                smTokStreamFatal(&TS, "unexpected %.*s\n", name.len,
                                 name.bytes);
            }
        }
    }
    if (!memories) {
        smTokStreamFatal(&TS, "no `memories` config area was defined\n");
    }
    if (!sections) {
        smTokStreamFatal(&TS, "no `sections` config area was defined\n");
    }
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        CfgSect *sect = SECTS.inner.items + i;
        for (UInt j = 0; j < MEMS.inner.len; ++j) {
            CfgMem *mem = MEMS.inner.items + j;
            if (smBufEqual(sect->load, mem->name)) {
                switch (mem->kind) {
                case CFG_MEM_READONLY:
                    if (sect->kind != CFG_SECT_CODE) {
                        fatal("section %.*s is not kind-compatible "
                              "with memory %.*s\n",
                              sect->name.len, sect->name.bytes, mem->name.len,
                              mem->name.bytes);
                    }
                case CFG_MEM_READWRITE:
                    goto nextsection;
                default:
                    smUnreachable();
                }
            }
        }
        fatal("no memory found with name: %.*s\n", sect->load.len,
              sect->load.bytes);
    nextsection:
        (void)0;
    }
}

static void serialize() { smUnimplemented(); }

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
    FILE   *hnd = openFileCstr(symfile_name, "wb+");
    SmSerde ser = {hnd, {(U8 *)symfile_name, strlen(symfile_name)}};

    smUnimplemented();
    closeFile(hnd);
}

static void writeTags() {
    FILE   *hnd = openFileCstr(tagfile_name, "wb+");
    SmSerde ser = {hnd, {(U8 *)tagfile_name, strlen(tagfile_name)}};

    smUnimplemented();
    closeFile(hnd);
}
