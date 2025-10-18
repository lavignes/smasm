#include <smasm/fatal.h>
#include <smasm/tok.h>
#include <smasm/utf8.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static struct {
    U32    tok;
    SmView view;
} const TOK_NAMES[] = {
    {SM_TOK_EOF, SM_VIEW("end of file")},
    {SM_TOK_ID, SM_VIEW("identifier")},
    {SM_TOK_NUM, SM_VIEW("number")},
    {SM_TOK_STR, SM_VIEW("string")},
    {SM_TOK_DB, SM_VIEW("@DB")},
    {SM_TOK_DW, SM_VIEW("@DW")},
    {SM_TOK_DS, SM_VIEW("@DS")},
    {SM_TOK_SECTION, SM_VIEW("@SECTION")},
    {SM_TOK_SECTPUSH, SM_VIEW("@SECTPUSH")},
    {SM_TOK_SECTPOP, SM_VIEW("@SECTPOP")},
    {SM_TOK_INCLUDE, SM_VIEW("@INCLUDE")},
    {SM_TOK_INCBIN, SM_VIEW("@INCBIN")},
    {SM_TOK_IF, SM_VIEW("@IF")},
    {SM_TOK_ELSE, SM_VIEW("@ELSE")},
    {SM_TOK_END, SM_VIEW("@END")},
    {SM_TOK_MACRO, SM_VIEW("@MACRO")},
    {SM_TOK_REPEAT, SM_VIEW("@REPEAT")},
    {SM_TOK_STRUCT, SM_VIEW("@STRUCT")},
    {SM_TOK_UNION, SM_VIEW("@UNION")},
    {SM_TOK_STRFMT, SM_VIEW("@STRFMT")},
    {SM_TOK_IDFMT, SM_VIEW("@IDFMT")},
    {SM_TOK_ALLOC, SM_VIEW("@ALLOC")},
    {SM_TOK_FATAL, SM_VIEW("@FATAL")},
    {SM_TOK_PRINT, SM_VIEW("@PRINT")},
    {SM_TOK_DEFINED, SM_VIEW("@DEFINED")},
    {SM_TOK_STRLEN, SM_VIEW("@STRLEN")},
    {SM_TOK_TAG, SM_VIEW("@TAG")},
    {SM_TOK_REL, SM_VIEW("@REL")},
    {SM_TOK_ASL, SM_VIEW("`<<`")},
    {SM_TOK_ASR, SM_VIEW("`>>`")},
    {SM_TOK_LSR, SM_VIEW("`~>`")},
    {SM_TOK_LTE, SM_VIEW("`<=`")},
    {SM_TOK_GTE, SM_VIEW("`>=`")},
    {SM_TOK_DEQ, SM_VIEW("`==`")},
    {SM_TOK_NEQ, SM_VIEW("`!=`")},
    {SM_TOK_AND, SM_VIEW("`&&`")},
    {SM_TOK_OR, SM_VIEW("`||`")},
    {SM_TOK_DCOLON, SM_VIEW("`::`")},
    {SM_TOK_EXPEQU, SM_VIEW("`=:`")},
    {SM_TOK_DSTAR, SM_VIEW("`**`")},
    {SM_TOK_AF, SM_VIEW("register `AF`")},
    {SM_TOK_BC, SM_VIEW("register `BC`")},
    {SM_TOK_DE, SM_VIEW("register `DE`")},
    {SM_TOK_HL, SM_VIEW("register `HL`")},
    {SM_TOK_SP, SM_VIEW("register `SP`")},
    {SM_TOK_NC, SM_VIEW("condition `NC`")},
    {SM_TOK_NZ, SM_VIEW("condition `NZ`")},
    {SM_TOK_ARG, SM_VIEW("@ARG")},
    {SM_TOK_NARG, SM_VIEW("@NARG")},
    {SM_TOK_SHIFT, SM_VIEW("@SHIFT")},
    {SM_TOK_UNIQUE, SM_VIEW("@UNIQUE")},
};

static SmViewIntern CHAR_NAMES = {0};

SmView smTokName(U32 c) {
    for (size_t i = 0; i < (sizeof(TOK_NAMES) / sizeof(TOK_NAMES[0])); ++i) {
        if (c == TOK_NAMES[i].tok) {
            return TOK_NAMES[i].view;
        }
    }
    SmBuf buf = {0};
    smBufCat(&buf, SM_VIEW("`"));
    smUtf8Cat(&buf, c);
    smBufCat(&buf, SM_VIEW("`"));
    SmView view = smViewIntern(&CHAR_NAMES, buf.view);
    smBufFini(&buf);
    return view;
}

void smMacroTokBufAdd(SmMacroTokBuf *buf, SmMacroTok item) {
    SM_BUF_ADD_IMPL();
}

void smMacroTokBufFini(SmMacroTokBuf *buf) { SM_BUF_FINI_IMPL(); }

SmMacroTokView smMacroTokIntern(SmMacroTokIntern *in, SmMacroTokView view) {
    SM_INTERN_IMPL();
}

void smMacroArgEnqueue(SmMacroArgQueue *q, SmMacroTokView toks) {
    if (!q->buf) {
        q->buf = malloc(sizeof(SmMacroTokView) * 8);
        if (!q->buf) {
            smFatal("out of memory\n");
        }
        q->len = 0;
        q->cap = 8;
    }
    if ((q->cap - q->len) == 0) {
        q->buf = realloc(q->buf, sizeof(SmMacroTokView) * q->cap * 2);
        if (!q->buf) {
            smFatal("out of memory\n");
        }
        q->cap *= 2;
    }
    q->buf[q->len] = toks;
    ++q->len;
}

void smMacroArgDequeue(SmMacroArgQueue *q) {
    if (q->len) {
        --q->len;
        memmove(q->buf, q->buf + 1, sizeof(SmMacroTokView) * q->len);
    }
}

void smMacroArgQueueFini(SmMacroArgQueue *q) {
    if (!q->buf) {
        return;
    }
    free(q->buf);
    memset(q, 0, sizeof(SmMacroArgQueue));
}

void smRepeatTokBufAdd(SmRepeatTokBuf *buf, SmRepeatTok item) {
    SM_BUF_ADD_IMPL();
}

void smRepeatTokBufFini(SmRepeatTokBuf *buf) { SM_BUF_FINI_IMPL(); }

void smPosTokBufAdd(SmPosTokBuf *buf, SmPosTok item) { SM_BUF_ADD_IMPL(); }

void smPosTokBufFini(SmPosTokBuf *buf) { SM_BUF_FINI_IMPL(); }

_Noreturn void smTokStreamFatal(SmTokStream *ts, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(ts, ts->pos, fmt, args);
    va_end(args);
}

_Noreturn void smTokStreamFatalPos(SmTokStream *ts, SmPos pos, char const *fmt,
                                   ...) {
    va_list args;
    va_start(args, fmt);
    smTokStreamFatalPosV(ts, pos, fmt, args);
    va_end(args);
}

_Noreturn void smTokStreamFatalV(SmTokStream *ts, char const *fmt,
                                 va_list args) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
    case SM_TOK_STREAM_FMT:
        smTokStreamFatalPosV(ts, ts->pos, fmt, args);
    case SM_TOK_STREAM_MACRO:
        // TODO macro arg position
        smTokStreamFatalPosV(ts, ts->macro.view.items[ts->macro.pos].pos, fmt,
                             args);
    case SM_TOK_STREAM_REPEAT:
        smTokStreamFatalPosV(ts, ts->repeat.buf.view.items[ts->repeat.pos].pos,
                             fmt, args);
    case SM_TOK_STREAM_IFELSE:
        smTokStreamFatalPosV(ts, ts->ifelse.buf.view.items[ts->ifelse.pos].pos,
                             fmt, args);
    default:
        SM_UNREACHABLE();
    }
}

_Noreturn void smTokStreamFatalPosV(SmTokStream *ts, SmPos pos, char const *fmt,
                                    va_list args) {

    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
    case SM_TOK_STREAM_FMT:
    case SM_TOK_STREAM_IFELSE:
        fprintf(stderr, SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                SM_VIEW_FMT_ARG(pos.file), pos.line, pos.col);
        break;
    case SM_TOK_STREAM_MACRO:
        // TODO macro arg position
        fprintf(stderr,
                SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": in macro " SM_VIEW_FMT
                            "\n\t" SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                SM_VIEW_FMT_ARG(ts->pos.file), ts->pos.line, ts->pos.col,
                SM_VIEW_FMT_ARG(ts->macro.name), SM_VIEW_FMT_ARG(pos.file),
                pos.line, pos.col);
        break;
    case SM_TOK_STREAM_REPEAT:
        fprintf(stderr,
                SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT
                            ": at repeat index " UINT_FMT "\n\t" SM_VIEW_FMT
                            ":" UINT_FMT ":" UINT_FMT ": ",
                SM_VIEW_FMT_ARG(ts->pos.file), ts->pos.line, ts->pos.col,
                ts->repeat.idx, SM_VIEW_FMT_ARG(pos.file), pos.line, pos.col);
        break;
    default:
        SM_UNREACHABLE();
    }
    smFatalV(fmt, args);
}

void smTokStreamFileInit(SmTokStream *ts, SmView name, FILE *hnd) {
    memset(ts, 0, sizeof(SmTokStream));
    ts->kind             = SM_TOK_STREAM_FILE;
    ts->pos              = (SmPos){name, 1, 1};
    ts->chardev.file.hnd = hnd;
    ts->chardev.cline    = 1;
    ts->chardev.ccol     = 1;
}

void smTokStreamViewInit(SmTokStream *ts, SmView name, SmView view) {
    memset(ts, 0, sizeof(SmTokStream));
    ts->kind             = SM_TOK_STREAM_VIEW;
    ts->pos              = (SmPos){name, 1, 1};
    ts->chardev.src.view = view;
    ts->chardev.cline    = 1;
    ts->chardev.ccol     = 1;
}

void smTokStreamMacroInit(SmTokStream *ts, SmView name, SmPos pos,
                          SmMacroTokView buf, SmMacroArgQueue args,
                          UInt nonce) {
    memset(ts, 0, sizeof(SmTokStream));
    ts->kind        = SM_TOK_STREAM_MACRO;
    ts->pos         = pos;
    ts->macro.name  = name;
    ts->macro.view  = buf;
    ts->macro.args  = args;
    ts->macro.nonce = nonce;
}

void smTokStreamRepeatInit(SmTokStream *ts, SmPos pos, SmRepeatTokBuf buf,
                           UInt cnt) {
    memset(ts, 0, sizeof(SmTokStream));
    ts->kind       = SM_TOK_STREAM_REPEAT;
    ts->pos        = pos;
    ts->repeat.buf = buf;
    ts->repeat.cnt = cnt;
}

void smTokStreamFmtInit(SmTokStream *ts, SmPos pos, SmView fmt, U32 tok) {
    memset(ts, 0, sizeof(SmTokStream));
    ts->kind     = SM_TOK_STREAM_FMT;
    ts->pos      = pos;
    ts->fmt.view = fmt;
    ts->fmt.tok  = tok;
}

void smTokStreamIfElseInit(SmTokStream *ts, SmPos pos, SmPosTokBuf buf) {
    memset(ts, 0, sizeof(SmTokStream));
    ts->kind       = SM_TOK_STREAM_IFELSE;
    ts->pos        = pos;
    ts->ifelse.buf = buf;
}

void smTokStreamFini(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        if (fclose(ts->chardev.file.hnd) == EOF) {
            int err = errno;
            fprintf(stderr, SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                    SM_VIEW_FMT_ARG(ts->pos.file), ts->pos.line, ts->pos.col);
            smFatal("failed to close file: %s\n", strerror(err));
        }
        smBufFini(&ts->chardev.buf);
        return;
    case SM_TOK_STREAM_VIEW:
        return;
    case SM_TOK_STREAM_MACRO:
        smMacroArgQueueFini(&ts->macro.args);
        return;
    case SM_TOK_STREAM_REPEAT:
        smRepeatTokBufFini(&ts->repeat.buf);
        return;
    case SM_TOK_STREAM_FMT:
        return;
    case SM_TOK_STREAM_IFELSE:
        smPosTokBufFini(&ts->ifelse.buf);
        return;
    default:
        SM_UNREACHABLE();
    }
}

static _Noreturn void fatalChar(SmTokStream *ts, char const *fmt, ...) {
    fprintf(stderr, SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
            SM_VIEW_FMT_ARG(ts->pos.file), ts->chardev.cline, ts->chardev.ccol);
    va_list args;
    va_start(args, fmt);
    smFatalV(fmt, args);
}

static U32 peek(SmTokStream *ts) {
    assert((ts->kind == SM_TOK_STREAM_FILE) ||
           (ts->kind == SM_TOK_STREAM_VIEW));
    if (ts->chardev.cstashed) {
        return ts->chardev.cstash;
    }
    ts->chardev.cstashed = true;
    if (ts->chardev.clen == 0) {
        switch (ts->kind) {
        case SM_TOK_STREAM_FILE:
            ts->chardev.clen =
                fread(ts->chardev.cbuf, 1, 1, ts->chardev.file.hnd);
            if (ts->chardev.clen != 1) {
                int err = ferror(ts->chardev.file.hnd);
                if (err) {
                    fatalChar(ts, "failed to read file: %s\n", strerror(err));
                }
                if (feof(ts->chardev.file.hnd)) {
                    ts->chardev.cstash = SM_TOK_EOF;
                    return ts->chardev.cstash;
                }
            }
            break;
        case SM_TOK_STREAM_VIEW:
            if (ts->chardev.src.offset >= ts->chardev.src.view.len) {
                ts->chardev.cstash = SM_TOK_EOF;
                return ts->chardev.cstash;
            }
            ts->chardev.cbuf[0] =
                ts->chardev.src.view.bytes[ts->chardev.src.offset];
            ++ts->chardev.src.offset;
            ts->chardev.clen = 1;
            break;
        default:
            SM_UNREACHABLE();
        }
    }
    UInt len = 0;
    while (len == 0) {
        ts->chardev.cstash =
            smUtf8Decode((SmView){ts->chardev.cbuf, ts->chardev.clen}, &len);
        ts->chardev.clen -= len;
        memmove(ts->chardev.cbuf, ts->chardev.cbuf + len, ts->chardev.clen);
        if (len == 0) {
            if (ts->chardev.clen == 4) {
                fatalChar(ts, "invalid UTF-8 data\n");
            }
            switch (ts->kind) {
            case SM_TOK_STREAM_FILE: {
                UInt read = fread(ts->chardev.cbuf + ts->chardev.clen, 1, 1,
                                  ts->chardev.file.hnd);
                if (read != 0) {
                    int err = ferror(ts->chardev.file.hnd);
                    if (err) {
                        fatalChar(ts, "failed to read file: %s\n",
                                  strerror(err));
                    }
                    if (feof(ts->chardev.file.hnd)) {
                        fatalChar(ts, "unexpected end of file\n");
                    }
                }
                ts->chardev.clen += read;
                break;
            }
            case SM_TOK_STREAM_VIEW:
                if (ts->chardev.src.offset >= ts->chardev.src.view.len) {
                    fatalChar(ts, "unexpected end of file\n");
                }
                ts->chardev.cbuf[ts->chardev.clen] =
                    ts->chardev.src.view.bytes[ts->chardev.src.offset];
                ++ts->chardev.src.offset;
                ts->chardev.clen += 1;
                break;
            default:
                SM_UNREACHABLE();
            }
        }
    }
    return ts->chardev.cstash;
}

static void eat(SmTokStream *ts) {
    assert((ts->kind == SM_TOK_STREAM_FILE) ||
           (ts->kind == SM_TOK_STREAM_VIEW));
    ts->chardev.cstashed = false;
    ++ts->chardev.ccol;
    if (ts->chardev.cstash == '\n') {
        ++ts->chardev.cline;
        ts->chardev.ccol = 1;
    }
}

static void pushChar(SmTokStream *ts, U32 c) {
    assert((ts->kind == SM_TOK_STREAM_FILE) ||
           (ts->kind == SM_TOK_STREAM_VIEW));
    U8   tmp[4];
    UInt len = smUtf8Encode((SmView){tmp, 4}, c);
    smBufCat(&ts->chardev.buf, (SmView){tmp, len});
}

static char const DIGITS[] = "0123456789ABCDEF";

static I32 parseChardev(SmTokStream *ts, I32 radix) {
    I32     value = 0;
    SmView *view  = &ts->chardev.buf.view;
    if (view->len == 0) {
        smTokStreamFatal(ts, "empty number\n");
    }
    for (UInt i = 0; i < view->len; ++i) {
        for (UInt j = 0; j < (sizeof(DIGITS) / sizeof(DIGITS[0])); ++j) {
            if (view->bytes[i] == DIGITS[j]) {
                if (j >= (UInt)radix) {
                    smTokStreamFatal(ts, "invalid number: " SM_VIEW_FMT "\n",
                                     SM_VIEW_FMT_ARG(*view));
                }
                value *= radix;
                value += j;
                goto next;
            }
        }
        smTokStreamFatal(ts, "invalid number: " SM_VIEW_FMT "\n",
                         SM_VIEW_FMT_ARG(*view));
    next:
        (void)0;
    }
    return value;
}

static struct {
    char const *name;
    U32         tok;
} const DIRECTIVES[] = {
    {"DB", SM_TOK_DB},
    {"DW", SM_TOK_DW},
    {"DS", SM_TOK_DS},
    {"SECTION", SM_TOK_SECTION},
    {"SECTPUSH", SM_TOK_SECTPUSH},
    {"SECTPOP", SM_TOK_SECTPOP},
    {"INCLUDE", SM_TOK_INCLUDE},
    {"INCBIN", SM_TOK_INCBIN},
    {"IF", SM_TOK_IF},
    {"ELSE", SM_TOK_ELSE},
    {"END", SM_TOK_END},
    {"MACRO", SM_TOK_MACRO},
    {"REPEAT", SM_TOK_REPEAT},
    {"STRUCT", SM_TOK_STRUCT},
    {"UNION", SM_TOK_UNION},
    {"STRFMT", SM_TOK_STRFMT},
    {"IDFMT", SM_TOK_IDFMT},
    {"ALLOC", SM_TOK_ALLOC},
    {"FATAL", SM_TOK_FATAL},
    {"PRINT", SM_TOK_PRINT},

    {"DEFINED", SM_TOK_DEFINED},
    {"STRLEN", SM_TOK_STRLEN},
    {"TAG", SM_TOK_TAG},
    {"REL", SM_TOK_REL},

    {"ARG", SM_TOK_ARG},
    {"NARG", SM_TOK_NARG},
    {"SHIFT", SM_TOK_SHIFT},
    {"UNIQUE", SM_TOK_UNIQUE},
};

static struct {
    U8  digraph[2];
    U32 tok;
} const DIGRAPHS[] = {
    {"<<", SM_TOK_ASL},    {">>", SM_TOK_ASR},    {"~>", SM_TOK_LSR},
    {"<=", SM_TOK_LTE},    {">=", SM_TOK_GTE},    {"==", SM_TOK_DEQ},
    {"!=", SM_TOK_NEQ},    {"&&", SM_TOK_AND},    {"||", SM_TOK_OR},
    {"::", SM_TOK_DCOLON}, {"=:", SM_TOK_EXPEQU}, {"**", SM_TOK_DSTAR},
};

static struct {
    U8  pair[2];
    U32 tok;
} const PAIRS[] = {
    {"AF", SM_TOK_AF}, {"BC", SM_TOK_BC}, {"DE", SM_TOK_DE}, {"HL", SM_TOK_HL},
    {"SP", SM_TOK_SP}, {"NC", SM_TOK_NC}, {"NZ", SM_TOK_NZ},
};

static U8 SINGLES[] = "ABCDEHLZ";

static U32 peekChardev(SmTokStream *ts) {
    if (ts->chardev.stashed) {
        return ts->chardev.stash;
    }
    while (true) {
        U32 c = peek(ts);
        if ((c == SM_TOK_EOF) || !isspace(c) || (c == '\n')) {
            break;
        }
        eat(ts);
    }
    if (peek(ts) == ';') {
        while (true) {
            U32 c = peek(ts);
            if ((c == SM_TOK_EOF) || (c == '\n')) {
                break;
            }
            eat(ts);
        }
    }
    ts->pos.line = ts->chardev.cline;
    ts->pos.col  = ts->chardev.ccol;
    if (peek(ts) == SM_TOK_EOF) {
        eat(ts);
        ts->chardev.stashed = true;
        ts->chardev.stash   = SM_TOK_EOF;
        return SM_TOK_EOF;
    }
    if (peek(ts) == '\\') {
        eat(ts);
        if (peek(ts) == '\n') {
            eat(ts);
            return peek(ts); // yuck
        }
        ts->chardev.stashed = true;
        ts->chardev.stash   = '\\';
        return '\\';
    }
    if (peek(ts) == '@') {
        eat(ts);
        // macro arg?
        U32 c = peek(ts);
        if (isdigit(c)) {
            for (; isdigit(c); c = peek(ts)) {
                pushChar(ts, toupper(c));
                eat(ts);
            }
            ts->chardev.num     = parseChardev(ts, 10);
            ts->chardev.stashed = true;
            ts->chardev.stash   = SM_TOK_ARG;
            return SM_TOK_ARG;
        }
        // directive
        for (c = peek(ts); isalnum(c); c = peek(ts)) {
            pushChar(ts, toupper(c));
            eat(ts);
        }
        SmView *view = &ts->chardev.buf.view;
        for (UInt i = 0; i < (sizeof(DIRECTIVES) / sizeof(DIRECTIVES[0]));
             ++i) {
            char const *name = DIRECTIVES[i].name;
            UInt        len  = strlen(name);
            if (len != view->len) {
                continue;
            }
            if (memcmp(name, view->bytes, view->len) == 0) {
                ts->chardev.stashed = true;
                ts->chardev.stash   = DIRECTIVES[i].tok;
                return ts->chardev.stash;
            }
        }
        smTokStreamFatal(ts, "unrecognized directive: @" SM_VIEW_FMT "\n",
                         SM_VIEW_FMT_ARG(*view));
    }
    if (peek(ts) == '"') {
        eat(ts);
        while (true) {
            U32 c = peek(ts);
            switch (c) {
            case SM_TOK_EOF:
                fatalChar(ts, "unexpected end of file\n");
            case '"':
                eat(ts);
                goto stringdone;
            case '\\':
                eat(ts);
                switch (peek(ts)) {
                case 'n':
                    pushChar(ts, '\n');
                    break;
                case 'r':
                    pushChar(ts, '\r');
                    break;
                case 't':
                    pushChar(ts, '\t');
                    break;
                case '\\':
                    pushChar(ts, '\\');
                    break;
                case '"':
                    pushChar(ts, '"');
                    break;
                case '0':
                    pushChar(ts, '\0');
                    break;
                default:
                    fatalChar(ts, "unrecognized character escape\n");
                }
                eat(ts);
                break;
            default:
                pushChar(ts, c);
                eat(ts);
                break;
            }
        }
    stringdone:
        ts->chardev.stashed = true;
        ts->chardev.stash   = SM_TOK_STR;
        return SM_TOK_STR;
    }
    if (peek(ts) == '\'') {
        eat(ts);
        U32 c = peek(ts);
        switch (c) {
        case SM_TOK_EOF:
            fatalChar(ts, "unexpected end of file\n");
        case '\\':
            eat(ts);
            switch (peek(ts)) {
            case 'n':
                ts->chardev.num = '\n';
                break;
            case 'r':
                ts->chardev.num = '\r';
                break;
            case 't':
                ts->chardev.num = '\t';
                break;
            case '\\':
                ts->chardev.num = '\\';
                break;
            case '\'':
                ts->chardev.num = '\'';
                break;
            case '0':
                ts->chardev.num = '\0';
                break;
            default:
                fatalChar(ts, "unrecognized character escape\n");
            }
            break;
        default:
            ts->chardev.num = c;
            break;
        }
        eat(ts);
        if (peek(ts) != '\'') {
            fatalChar(ts, "expected single quote\n");
        }
        eat(ts);
        ts->chardev.stashed = true;
        ts->chardev.stash   = SM_TOK_NUM;
        return SM_TOK_NUM;
    } else {
        U32 c = peek(ts);
        if (isdigit(c) || (c == '%') || (c == '$')) {
            I32 radix = 10;
            if (c == '%') {
                radix = 2;
                eat(ts);
                // edge case. this is a modulus
                c = peek(ts);
                if ((c != '0') && (c != '1')) {
                    ts->chardev.stashed = true;
                    ts->chardev.stash   = '%';
                    return '%';
                }
            } else if (c == '$') {
                radix = 16;
                eat(ts);
                c = peek(ts);
            }
            while (true) {
                // underscores in numbers
                if (c == '_') {
                    eat(ts);
                    c = peek(ts);
                    continue;
                }
                if (!isalnum(c)) {
                    break;
                }
                pushChar(ts, c);
                eat(ts);
                c = peek(ts);
            }
            ts->chardev.num     = parseChardev(ts, radix);
            ts->chardev.stashed = true;
            ts->chardev.stash   = SM_TOK_NUM;
            return SM_TOK_NUM;
        }
        while (true) {
            if (c == SM_TOK_EOF) {
                break;
            }
            if (isascii(c) && !isalnum(c) && (c != '_') && (c != '.')) {
                break;
            }
            pushChar(ts, c);
            eat(ts);
            c = peek(ts);
        }
        // doesn't start with ident symbol. digraph?
        if (ts->chardev.buf.view.len == 0) {
            eat(ts);
            U32 nc = peek(ts);
            for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0]));
                 ++i) {
                if ((DIGRAPHS[i].digraph[0] == c) &&
                    (DIGRAPHS[i].digraph[1] == nc)) {
                    eat(ts);
                    ts->chardev.stashed = true;
                    ts->chardev.stash   = DIGRAPHS[i].tok;
                    return ts->chardev.stash;
                }
            }
        }
        // 1 char identifier?
        if (ts->chardev.buf.view.len == 1) {
            ts->chardev.stashed = true;
            U32 upper           = toupper(ts->chardev.buf.view.bytes[0]);
            for (UInt i = 0; i < sizeof(SINGLES); ++i) {
                if (SINGLES[i] == upper) {
                    ts->chardev.stash = upper;
                    return upper;
                }
            }
            ts->chardev.stash = SM_TOK_ID;
            return ts->chardev.stash;
        }
        // 2 char ident?
        if (ts->chardev.buf.view.len == 2) {
            ts->chardev.stashed = true;
            U32 c0              = toupper(ts->chardev.buf.view.bytes[0]);
            U32 c1              = toupper(ts->chardev.buf.view.bytes[1]);
            for (UInt i = 0; i < (sizeof(PAIRS) / sizeof(PAIRS[0])); ++i) {
                if ((PAIRS[i].pair[0] == c0) && (PAIRS[i].pair[1] == c1)) {
                    ts->chardev.stashed = true;
                    ts->chardev.stash   = PAIRS[i].tok;
                    return ts->chardev.stash;
                }
            }
        }
        // ident?
        if (ts->chardev.buf.view.len != 0) {
            ts->chardev.stashed = true;
            ts->chardev.stash   = SM_TOK_ID;
            return SM_TOK_ID;
        }
        // else uppercase whatever the char is
        ts->chardev.stashed = true;
        ts->chardev.stash   = toupper(c);
        return ts->chardev.stash;
    }
}

static U32 peekMacro(SmTokStream *ts) {
    if (ts->macro.pos >= ts->macro.view.len) {
        return SM_TOK_EOF;
    }
    SmMacroTok *tok = ts->macro.view.items + ts->macro.pos;
    switch (tok->kind) {
    case SM_MACRO_TOK_TOK:
        return tok->tok;
    case SM_MACRO_TOK_ID:
        return SM_TOK_ID;
    case SM_MACRO_TOK_NUM:
    case SM_MACRO_TOK_UNIQUE:
        return SM_TOK_NUM;
    case SM_MACRO_TOK_STR:
        return SM_TOK_STR;
    case SM_MACRO_TOK_ARG:
        if (((UInt)tok->num) >= ts->macro.args.len) {
            smTokStreamFatalPos(ts, tok->pos, "argument is undefined\n");
        }
        tok = ts->macro.args.buf[tok->num].items + ts->macro.argi;
        switch (tok->kind) {
        case SM_MACRO_TOK_TOK:
            return tok->tok;
        case SM_MACRO_TOK_ID:
            return SM_TOK_ID;
        case SM_MACRO_TOK_NUM:
            return SM_TOK_NUM;
        case SM_MACRO_TOK_STR:
            return SM_TOK_STR;
        default:
            SM_UNREACHABLE();
        }
    case SM_MACRO_TOK_NARG:
        return SM_TOK_NUM;
    case SM_MACRO_TOK_SHIFT:
        if (ts->macro.args.len == 0) {
            smTokStreamFatalPos(ts, tok->pos, "no arguments left to shift\n");
        }
        return '\n';
    default:
        SM_UNREACHABLE();
    }
}

static U32 peekRepeat(SmTokStream *ts) {
    if (ts->repeat.idx >= ts->repeat.cnt) {
        return SM_TOK_EOF;
    }
    SmRepeatTok *tok = ts->repeat.buf.view.items + ts->repeat.pos;
    switch (tok->kind) {
    case SM_REPEAT_TOK_TOK:
        return tok->tok;
    case SM_REPEAT_TOK_ID:
        return SM_TOK_ID;
    case SM_REPEAT_TOK_NUM:
        return SM_TOK_NUM;
    case SM_REPEAT_TOK_STR:
        return SM_TOK_STR;
    case SM_REPEAT_TOK_ITER:
        return SM_TOK_NUM;
    default:
        SM_UNREACHABLE();
    }
}

static U32 peekIfElse(SmTokStream *ts) {
    if (ts->ifelse.pos >= ts->ifelse.buf.view.len) {
        return SM_TOK_EOF;
    }
    return ts->ifelse.buf.view.items[ts->ifelse.pos].tok;
}

U32 smTokStreamPeek(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
        return peekChardev(ts);
    case SM_TOK_STREAM_MACRO:
        return peekMacro(ts);
    case SM_TOK_STREAM_REPEAT:
        return peekRepeat(ts);
    case SM_TOK_STREAM_FMT:
        return ts->fmt.tok;
    case SM_TOK_STREAM_IFELSE:
        return peekIfElse(ts);
    default:
        SM_UNREACHABLE();
    }
}

void smTokStreamEat(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
        ts->chardev.stashed      = false;
        ts->chardev.buf.view.len = 0;
        return;
    case SM_TOK_STREAM_MACRO: {
        SmMacroTok *tok = ts->macro.view.items + ts->macro.pos;
        switch (tok->kind) {
        case SM_MACRO_TOK_SHIFT:
            smMacroArgDequeue(&ts->macro.args);
            break;
        case SM_MACRO_TOK_ARG: {
            SmMacroTokView *buf = ts->macro.args.buf + tok->num;
            ++ts->macro.argi;
            if (ts->macro.argi < buf->len) {
                return;
            }
            ts->macro.argi = 0;
            break;
        }
        default:
            break;
        }
        ++ts->macro.pos;
        return;
    }
    case SM_TOK_STREAM_REPEAT:
        ++ts->repeat.pos;
        if (ts->repeat.pos >= ts->repeat.buf.view.len) {
            ts->repeat.pos = 0;
            ++ts->repeat.idx;
        }
        return;
    case SM_TOK_STREAM_FMT:
        ts->fmt.tok = SM_TOK_EOF;
        return;
    case SM_TOK_STREAM_IFELSE:
        ++ts->ifelse.pos;
        return;
    default:
        SM_UNREACHABLE();
    }
}

void smTokStreamRewind(SmTokStream *ts) {
    smTokStreamEat(ts);
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        if (fseek(ts->chardev.file.hnd, 0, SEEK_SET) < 0) {
            int err = errno;
            fprintf(stderr, SM_VIEW_FMT ":" UINT_FMT ":" UINT_FMT ": ",
                    SM_VIEW_FMT_ARG(ts->pos.file), ts->chardev.cline,
                    ts->chardev.ccol);
            smFatal("failed to rewind file: %s\n", strerror(err));
        }
        break;
    case SM_TOK_STREAM_VIEW:
        ts->chardev.src.offset = 0;
        break;
    default:
        SM_UNREACHABLE();
    }
    ts->pos.line         = 1;
    ts->pos.col          = 1;
    ts->chardev.cstashed = false;
    ts->chardev.cline    = 1;
    ts->chardev.ccol     = 1;
    return;
}

SmView smTokStreamView(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
        return ts->chardev.buf.view;
    case SM_TOK_STREAM_MACRO: {
        SmMacroTok *tok = ts->macro.view.items + ts->macro.pos;
        switch (tok->kind) {
        case SM_MACRO_TOK_STR:
        case SM_MACRO_TOK_ID:
            return tok->view;
        case SM_MACRO_TOK_ARG:
            tok = ts->macro.args.buf[tok->num].items + ts->macro.argi;
            switch (tok->kind) {
            case SM_MACRO_TOK_STR:
            case SM_MACRO_TOK_ID:
                return tok->view;
            default:
                SM_UNREACHABLE();
            }
        default:
            SM_UNREACHABLE();
        }
    }
    case SM_TOK_STREAM_REPEAT: {
        SmRepeatTok *tok = ts->repeat.buf.view.items + ts->repeat.pos;
        switch (tok->kind) {
        case SM_REPEAT_TOK_STR:
        case SM_REPEAT_TOK_ID:
            return tok->view;
        default:
            SM_UNREACHABLE();
        }
    }
    case SM_TOK_STREAM_FMT:
        return ts->fmt.view;
    case SM_TOK_STREAM_IFELSE:
        return ts->ifelse.buf.view.items[ts->ifelse.pos].view;
    default:
        SM_UNREACHABLE();
    }
}

I32 smTokStreamNum(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
        return ts->chardev.num;
    case SM_TOK_STREAM_MACRO: {
        SmMacroTok *tok = ts->macro.view.items + ts->macro.pos;
        switch (tok->kind) {
        case SM_MACRO_TOK_NUM:
            return tok->num;
        case SM_MACRO_TOK_ARG:
            tok = ts->macro.args.buf[tok->num].items + ts->macro.argi;
            switch (tok->kind) {
            case SM_MACRO_TOK_NUM:
                return tok->num;
            default:
                SM_UNREACHABLE();
            }
        case SM_MACRO_TOK_NARG:
            return ts->macro.args.len;
        case SM_MACRO_TOK_UNIQUE:
            return ts->macro.nonce;
        default:
            SM_UNREACHABLE();
        }
    }
    case SM_TOK_STREAM_REPEAT: {
        SmRepeatTok *tok = ts->repeat.buf.view.items + ts->repeat.pos;
        switch (tok->kind) {
        case SM_REPEAT_TOK_NUM:
            return tok->num;
        case SM_REPEAT_TOK_ITER:
            return ts->repeat.idx;
        default:
            SM_UNREACHABLE();
        }
    }
    case SM_TOK_STREAM_IFELSE:
        return ts->ifelse.buf.view.items[ts->ifelse.pos].num;
    case SM_TOK_STREAM_FMT:
    default:
        SM_UNREACHABLE();
    }
}

SmPos smTokStreamPos(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
    case SM_TOK_STREAM_VIEW:
        return ts->pos;
    case SM_TOK_STREAM_MACRO:
        // TODO macro arg pos
        return ts->macro.view.items[ts->macro.pos].pos;
    case SM_TOK_STREAM_REPEAT:
        return ts->repeat.buf.view.items[ts->repeat.pos].pos;
    case SM_TOK_STREAM_FMT:
        return ts->pos;
    case SM_TOK_STREAM_IFELSE:
        return ts->ifelse.buf.view.items[ts->ifelse.pos].pos;
    default:
        SM_UNREACHABLE();
    }
}
