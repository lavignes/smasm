#include <smasm/fatal.h>
#include <smasm/fmt.h>
#include <smasm/path.h>
#include <smasm/sect.h>

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
static SmPathSet    INCS   = {0}; // files included in the translation unit

static SmBuf DEFINES_SECTION;
static SmBuf CODE_SECTION;
static SmBuf STATIC_UNIT;
static SmBuf EXPORT_UNIT;

static SmLbl     globalLbl(SmBuf name);
static SmExprBuf constExprBuf(I32 num);
static FILE     *openFileCstr(char const *name, char const *modes);
static void      closeFile(FILE *hnd);
static void      pushFile(SmBuf path);
static void      pass();
static void      rewindToEmit();
static void      pullStream();
static void      writeDepend();
static void      serialize();

int main(int argc, char **argv) {
    outfile = stdout;
    if (argc == 1) {
        help();
        return EXIT_SUCCESS;
    }
    DEFINES_SECTION = smBufIntern(&STRS, SM_BUF("@DEFINES"));
    CODE_SECTION    = smBufIntern(&STRS, SM_BUF("CODE"));
    STATIC_UNIT     = smBufIntern(&STRS, SM_BUF("@STATIC"));
    EXPORT_UNIT     = smBufIntern(&STRS, SM_BUF("@EXPORT"));
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
    rewindToEmit();
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

#define STACK_SIZE 16
static SmTokStream  STACK[STACK_SIZE] = {0};
static SmTokStream *ts                = STACK - 1;

static SmSectGBuf SECTS               = {0};
static UInt       sect;

static SmBuf scope    = NULL_BUF;
static UInt  if_level = 0;
static UInt  nonce    = 0;
static Bool  emit     = false;

static void pass() {}

static void rewindSections() {
    for (UInt i = 0; i < SECTS.inner.len; ++i) {
        SmSect *sect = SECTS.inner.items + i;
        sect->pc     = 0;
    }
}

static void rewindToEmit() {
    smTokStreamRewind(ts);
    rewindSections();
    scope    = NULL_BUF;
    if_level = 0;
    nonce    = 0;
    emit     = true;
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

static SmLbl localLbl(SmBuf name) { return (SmLbl){scope, name}; }

static SmLbl globalLbl(SmBuf name) { return (SmLbl){{0}, name}; }

static SmLbl absLbl(SmBuf scope, SmBuf name) { return (SmLbl){scope, name}; }

static SmExpr constExpr(I32 num) {
    return (SmExpr){.kind = SM_EXPR_CONST, .num = num};
}

static SmExprBuf constExprBuf(I32 num) {
    SmExpr expr = constExpr(num);
    return smExprIntern(&EXPRS, (SmExprBuf){&expr, 1});
}
