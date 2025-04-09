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
    U32   tok;
    SmBuf buf;
} const TOK_NAMES[] = {
    {SM_TOK_EOF, SM_BUF("end of file")},
    {SM_TOK_ID, SM_BUF("identifier")},
    {SM_TOK_NUM, SM_BUF("number")},
    {SM_TOK_STR, SM_BUF("string")},
    {SM_TOK_DB, SM_BUF("@DB")},
    {SM_TOK_DW, SM_BUF("@DW")},
    {SM_TOK_DS, SM_BUF("@DS")},
    {SM_TOK_SECTION, SM_BUF("@SECTION")},
    {SM_TOK_INCLUDE, SM_BUF("@INCLUDE")},
    {SM_TOK_INCBIN, SM_BUF("@INCBIN")},
    {SM_TOK_IF, SM_BUF("@IF")},
    {SM_TOK_END, SM_BUF("@END")},
    {SM_TOK_MACRO, SM_BUF("@MACRO")},
    {SM_TOK_REPEAT, SM_BUF("@REPEAT")},
    {SM_TOK_STRFMT, SM_BUF("@STRFMT")},
    {SM_TOK_IDFMT, SM_BUF("@IDFMT")},
    {SM_TOK_DEFINED, SM_BUF("@DEFINED")},
    {SM_TOK_STRLEN, SM_BUF("@STRLEN")},
    {SM_TOK_TAG, SM_BUF("@TAG")},
    {SM_TOK_ASL, SM_BUF("`<<`")},
    {SM_TOK_ASR, SM_BUF("`>>`")},
    {SM_TOK_LSR, SM_BUF("`~>`")},
    {SM_TOK_LTE, SM_BUF("`<=`")},
    {SM_TOK_GTE, SM_BUF("`>=`")},
    {SM_TOK_DEQ, SM_BUF("`==`")},
    {SM_TOK_NEQ, SM_BUF("`!=`")},
    {SM_TOK_AND, SM_BUF("`&&`")},
    {SM_TOK_OR, SM_BUF("`||`")},
    {SM_TOK_DCOLON, SM_BUF("`::`")},
    {SM_TOK_DSTAR, SM_BUF("`**`")},
    {SM_TOK_AF, SM_BUF("register `AF`")},
    {SM_TOK_BC, SM_BUF("register `BC`")},
    {SM_TOK_DE, SM_BUF("register `DE`")},
    {SM_TOK_HL, SM_BUF("register `HL`")},
    {SM_TOK_SP, SM_BUF("register `SP`")},
    {SM_TOK_NC, SM_BUF("condition `NC`")},
    {SM_TOK_NZ, SM_BUF("condition `NZ`")},
    {SM_TOK_ARG, SM_BUF("@ARG")},
    {SM_TOK_NARG, SM_BUF("@NARG")},
    {SM_TOK_SHIFT, SM_BUF("@SHIFT")},
    {SM_TOK_UNIQUE, SM_BUF("@UNIQUE")},
};

SmBuf smTokName(U32 c) {
    for (size_t i = 0; i < (sizeof(TOK_NAMES) / sizeof(TOK_NAMES[0])); ++i) {
        if (c == TOK_NAMES[i].tok) {
            return TOK_NAMES[i].buf;
        }
    }
    return SM_BUF("character");
}

void smMacroTokGBufAdd(SmMacroTokGBuf *buf, SmMacroTok item) {
    SM_GBUF_ADD_IMPL(SmMacroTok);
}

void smMacroTokGBufFini(SmMacroTokGBuf *buf) {
    if (!buf->inner.items) {
        return;
    }
    free(buf->inner.items);
    memset(buf, 0, sizeof(SmMacroTokGBuf));
}

SmMacroTokBuf smMacroTokIntern(SmMacroTokIntern *in, SmMacroTokBuf buf) {
    SM_INTERN_IMPL(SmMacroTok, SmMacroTokBuf, SmMacroTokGBuf);
}

void smMacroArgEnqueue(SmMacroArgQueue *q, SmMacroTokBuf toks) {
    if (!q->buf) {
        q->buf = malloc(sizeof(SmMacroTokBuf) * 8);
        if (!q->buf) {
            smFatal("out of memory\n");
        }
        q->len  = 0;
        q->size = 8;
    }
    if ((q->size - q->len) == 0) {
        q->buf = realloc(q->buf, sizeof(SmMacroTokBuf) * q->size * 2);
        if (!q->buf) {
            smFatal("out of memory\n");
        }
        q->size *= 2;
    }
    q->buf[q->len] = toks;
    ++q->len;
}

void smMacroArgDequeue(SmMacroArgQueue *q) {
    if (q->len) {
        --q->len;
        memmove(q->buf, q->buf + 1, sizeof(SmMacroTokBuf) * q->len);
    }
}

void smRepeatTokGBufAdd(SmRepeatTokGBuf *buf, SmRepeatTok item) {
    SM_GBUF_ADD_IMPL(SmRepeatTok);
}

SmRepeatTokBuf smRepeatTokIntern(SmRepeatTokIntern *in, SmRepeatTokBuf buf) {
    SM_INTERN_IMPL(SmRepeatTok, SmRepeatTokBuf, SmRepeatTokGBuf);
}

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
    smTokStreamFatalPosV(ts, ts->pos, fmt, args);
}

_Noreturn void smTokStreamFatalPosV(SmTokStream *ts, SmPos pos, char const *fmt,
                                    va_list args) {

    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        fprintf(stderr, "%.*s:%zu:%zu: ", (int)pos.file.len, pos.file.bytes,
                (size_t)pos.line, (size_t)pos.col);
        break;
    default:
        smUnimplemented();
    }
    smFatalV(fmt, args);
}

void smTokStreamFileInit(SmTokStream *ts, SmBuf name, FILE *hnd) {
    ts->kind          = SM_TOK_STREAM_FILE;
    ts->pos           = (SmPos){.file = name, .line = 1, .col = 1};
    ts->file.hnd      = hnd;
    ts->file.stashed  = false;
    ts->file.cstashed = false;
    ts->file.cline    = 1;
    ts->file.ccol     = 1;
    ts->file.buf      = (SmGBuf){0};
}

void smTokStreamMacroInit(SmTokStream *ts, SmBuf name, SmPos pos,
                          SmMacroTokBuf buf, SmMacroArgQueue args, UInt nonce) {
    ts->kind        = SM_TOK_STREAM_MACRO;
    ts->pos         = pos;
    ts->macro.name  = name;
    ts->macro.buf   = buf;
    ts->macro.pos   = 0;
    ts->macro.args  = args;
    ts->macro.argi  = 0;
    ts->macro.nonce = nonce;
}

void smTokStreamRepeatInit(SmTokStream *ts, SmBuf name, SmPos pos,
                           SmRepeatTokBuf buf, UInt cnt) {
    (void)ts;
    (void)name;
    (void)pos;
    (void)buf;
    (void)cnt;
    smUnimplemented();
}

void smTokStreamFmtInit(SmTokStream *ts, SmBuf buf, SmPos pos, U32 tok) {
    ts->kind    = SM_TOK_STREAM_FMT;
    ts->pos     = pos;
    ts->fmt.buf = buf;
    ts->fmt.tok = tok;
}

void smTokStreamFini(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        if (fclose(ts->file.hnd) == EOF) {
            int err = errno;
            fprintf(stderr, "%.*s:%zu:%zu: ", (int)ts->pos.file.len,
                    ts->pos.file.bytes, (size_t)ts->pos.line,
                    (size_t)ts->pos.col);
            smFatal("failed to close file: %s\n", strerror(err));
        }

        return;
    case SM_TOK_STREAM_MACRO:
        if (ts->macro.args.buf->items) {
            free(ts->macro.args.buf->items);
        }
        return;
    case SM_TOK_STREAM_FMT:
        return;
    default:
        smUnimplemented();
        break;
    }
}

static _Noreturn void fatalChar(SmTokStream *ts, char const *fmt, ...) {
    fprintf(stderr, "%.*s:%zu:%zu: ", (int)ts->file.buf.inner.len,
            ts->file.buf.inner.bytes, (size_t)ts->file.cline,
            (size_t)ts->file.ccol);
    va_list args;
    va_start(args, fmt);
    smFatalV(fmt, args);
    va_end(args);
}

static U32 peek(SmTokStream *ts) {
    assert(ts->kind == SM_TOK_STREAM_FILE);
    if (ts->file.cstashed) {
        return ts->file.cstash;
    }
    ts->file.cstashed = true;
    if (ts->file.clen == 0) {
        ts->file.clen = fread(ts->file.cbuf, 1, 1, ts->file.hnd);
        if (ts->file.clen == 0) {
            int err = ferror(ts->file.hnd);
            if (err) {
                fatalChar(ts, "failed to read file: %s\n", strerror(err));
            }
            if (feof(ts->file.hnd)) {
                ts->file.cstash = SM_TOK_EOF;
                return ts->file.cstash;
            }
        }
    }
    UInt len = 0;
    while (len == 0) {
        ts->file.cstash =
            smUtf8Decode((SmBuf){ts->file.cbuf, ts->file.clen}, &len);
        ts->file.clen -= len;
        memmove(ts->file.cbuf, ts->file.cbuf + len, ts->file.clen);
        if (len == 0) {
            if (ts->file.clen == 4) {
                fatalChar(ts, "invalid UTF-8 data\n");
            }
            UInt read =
                fread(ts->file.cbuf + ts->file.clen, 1, 1, ts->file.hnd);
            if (read == 0) {
                int err = ferror(ts->file.hnd);
                if (err) {
                    fatalChar(ts, "failed to read file: %s\n", strerror(err));
                }
                if (feof(ts->file.hnd)) {
                    fatalChar(ts, "unexpected end of file\n");
                }
            }
            ts->file.clen += read;
        }
    }
    return ts->file.cstash;
}

static void eat(SmTokStream *ts) {
    assert(ts->kind == SM_TOK_STREAM_FILE);
    ts->file.cstashed = false;
    ++ts->file.ccol;
    if (ts->file.cstash == '\n') {
        ++ts->file.cline;
        ts->file.ccol = 1;
    }
}

static void pushChar(SmTokStream *ts, U32 c) {
    assert(ts->kind == SM_TOK_STREAM_FILE);
    U8   tmp[4];
    UInt len = smUtf8Encode((SmBuf){tmp, 4}, c);
    smGBufCat(&ts->file.buf, (SmBuf){tmp, len});
}

static char const DIGITS[] = "0123456789ABCDEF";

static I32 parse(SmTokStream *ts, I32 radix) {
    I32    value = 0;
    SmBuf *buf   = &ts->file.buf.inner;
    if (buf->len == 0) {
        smTokStreamFatal(ts, "empty number\n");
    }
    for (UInt i = 0; i < buf->len; ++i) {
        for (UInt j = 0; j < (sizeof(DIGITS) / sizeof(DIGITS[0])); ++j) {
            if (buf->bytes[i] == DIGITS[j]) {
                if (j >= (UInt)radix) {
                    smTokStreamFatal(ts, "invalid number: %.*s\n", buf->len,
                                     buf->bytes);
                }
                value *= radix;
                value += j;
                goto next;
            }
        }
        smTokStreamFatal(ts, "invalid number: %.*s\n", buf->len, buf->bytes);
    next:
        (void)0;
    }
    return value;
}

static struct {
    char const *name;
    U32         tok;
} const DIRECTIVES[] = {
    {"DB", SM_TOK_DB},           {"DW", SM_TOK_DW},
    {"DS", SM_TOK_DS},           {"SECTION", SM_TOK_SECTION},
    {"INCLUDE", SM_TOK_INCLUDE}, {"INCBIN", SM_TOK_INCBIN},
    {"IF", SM_TOK_IF},           {"END", SM_TOK_END},
    {"MACRO", SM_TOK_MACRO},     {"REPEAT", SM_TOK_REPEAT},
    {"STRFMT", SM_TOK_STRFMT},   {"IDFMT", SM_TOK_IDFMT},

    {"DEFINED", SM_TOK_DEFINED}, {"STRLEN", SM_TOK_STRLEN},
    {"TAG", SM_TOK_TAG},

    {"ARG", SM_TOK_ARG},         {"NARG", SM_TOK_NARG},
    {"SHIFT", SM_TOK_SHIFT},     {"UNIQUE", SM_TOK_UNIQUE},
};

static struct {
    U8  digraph[2];
    U32 tok;
} const DIGRAPHS[] = {
    {"<<", SM_TOK_ASL},    {">>", SM_TOK_ASR},   {"~>", SM_TOK_LSR},
    {"<=", SM_TOK_LTE},    {">=", SM_TOK_GTE},   {"==", SM_TOK_DEQ},
    {"!=", SM_TOK_NEQ},    {"&&", SM_TOK_AND},   {"||", SM_TOK_OR},
    {"::", SM_TOK_DCOLON}, {"**", SM_TOK_DSTAR},
};

static struct {
    U8  pair[2];
    U32 tok;
} const PAIRS[] = {
    {"AF", SM_TOK_AF}, {"BC", SM_TOK_BC}, {"DE", SM_TOK_DE}, {"HL", SM_TOK_HL},
    {"SP", SM_TOK_SP}, {"NC", SM_TOK_NC}, {"NZ", SM_TOK_NZ},
};

static U8 SINGLES[] = "ABCDEHLZ";

static U32 peekFile(SmTokStream *ts) {
    if (ts->file.stashed) {
        return ts->file.stash;
    }
    while (true) {
        U32 c = peek(ts);
        if ((c == SM_TOK_EOF) || !isspace(c)) {
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
    ts->pos.line = ts->file.cline;
    ts->pos.col  = ts->file.ccol;
    if (peek(ts) == SM_TOK_EOF) {
        eat(ts);
        ts->file.stashed = true;
        ts->file.stash   = SM_TOK_EOF;
        return SM_TOK_EOF;
    }
    if (peek(ts) == '\\') {
        eat(ts);
        if (peek(ts) == '\n') {
            eat(ts);
            return peek(ts); // yuck
        }
        ts->file.stashed = true;
        ts->file.stash   = '\\';
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
            ts->file.num     = parse(ts, 10);
            ts->file.stashed = true;
            ts->file.stash   = SM_TOK_NUM;
            return SM_TOK_NUM;
        }
        // directive
        for (c = peek(ts); isalnum(c); c = peek(ts)) {
            pushChar(ts, toupper(c));
            eat(ts);
        }
        SmBuf *buf = &ts->file.buf.inner;
        for (UInt i = 0; i < (sizeof(DIRECTIVES) / sizeof(DIRECTIVES[0]));
             ++i) {
            char const *name = DIRECTIVES[i].name;
            UInt        len  = strlen(name);
            if (len != buf->len) {
                continue;
            }
            if (memcmp(name, buf->bytes, buf->len) == 0) {
                ts->file.stashed = true;
                ts->file.stash   = DIRECTIVES[i].tok;
                return ts->file.stash;
            }
        }
        smTokStreamFatal(ts, "unrecognized directive: %.*s\n", buf->len,
                         buf->bytes);
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
        ts->file.stashed = true;
        ts->file.stash   = SM_TOK_STR;
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
                ts->file.num = '\n';
                break;
            case 'r':
                ts->file.num = '\r';
                break;
            case 't':
                ts->file.num = '\t';
                break;
            case '\\':
                ts->file.num = '\\';
                break;
            case '\'':
                ts->file.num = '\'';
                break;
            case '0':
                ts->file.num = '\0';
                break;
            default:
                fatalChar(ts, "unrecognized character escape\n");
            }
            break;
        default:
            ts->file.num = c;
            break;
        }
        eat(ts);
        if (peek(ts) != '\'') {
            fatalChar(ts, "expected single quote\n");
        }
        eat(ts);
        ts->file.stashed = true;
        ts->file.stash   = SM_TOK_NUM;
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
                    ts->file.stashed = true;
                    ts->file.stash   = '%';
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
            ts->file.num     = parse(ts, radix);
            ts->file.stashed = true;
            ts->file.stash   = SM_TOK_NUM;
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
        if (ts->file.buf.inner.len == 0) {
            eat(ts);
            U32 nc = peek(ts);
            for (UInt i = 0; i < (sizeof(DIGRAPHS) / sizeof(DIGRAPHS[0]));
                 ++i) {
                if ((DIGRAPHS[i].digraph[0] == c) &&
                    (DIGRAPHS[i].digraph[1] == nc)) {
                    eat(ts);
                    ts->file.stashed = true;
                    ts->file.stash   = DIGRAPHS[i].tok;
                    return ts->file.stash;
                }
            }
        }
        // 1 char identifier?
        if (ts->file.buf.inner.len == 1) {
            ts->file.stashed = true;
            U32 upper        = toupper(ts->file.buf.inner.bytes[0]);
            for (UInt i = 0; i < sizeof(SINGLES); ++i) {
                if (SINGLES[i] == upper) {
                    ts->file.stash = upper;
                    return upper;
                }
            }
            ts->file.stash = SM_TOK_ID;
            return ts->file.stash;
        }
        // 2 char ident?
        if (ts->file.buf.inner.len == 2) {
            ts->file.stashed = true;
            U32 c0           = toupper(ts->file.buf.inner.bytes[0]);
            U32 c1           = toupper(ts->file.buf.inner.bytes[1]);
            for (UInt i = 0; i < (sizeof(PAIRS) / sizeof(PAIRS[0])); ++i) {
                if ((PAIRS[i].pair[0] == c0) && (PAIRS[i].pair[1] == c1)) {
                    eat(ts);
                    ts->file.stashed = true;
                    ts->file.stash   = PAIRS[i].tok;
                    return ts->file.stash;
                }
            }
        }
        // ident?
        if (ts->file.buf.inner.len != 0) {
            ts->file.stashed = true;
            ts->file.stash   = SM_TOK_ID;
            return SM_TOK_ID;
        }
        // else uppercase whatever the char is
        ts->file.stashed = true;
        ts->file.stash   = toupper(c);
        return ts->file.stash;
    }
}

U32 smTokStreamPeek(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        return peekFile(ts);
    default:
        smUnimplemented();
    }
}

void smTokStreamEat(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        ts->file.stashed       = false;
        ts->file.buf.inner.len = 0;
        return;
    case SM_TOK_STREAM_MACRO: {
        SmMacroTok *tok = ts->macro.buf.items + ts->macro.pos;
        switch (tok->kind) {
        case SM_MACRO_TOK_SHIFT:
            smMacroArgDequeue(&ts->macro.args);
            break;
        case SM_MACRO_TOK_ARG:
            ++ts->macro.argi;
            if (ts->macro.argi < ts->macro.args.len) {
                return;
            }
            ts->macro.argi = 0;
        default:
            break;
        }
        ++ts->macro.pos;
        return;
    }
    case SM_TOK_STREAM_FMT:
        ts->fmt.tok = SM_TOK_EOF;
        return;
    default:
        smUnimplemented();
    }
}

void smTokStreamRewind(SmTokStream *ts) {
    assert(ts->kind == SM_TOK_STREAM_FILE);
    smTokStreamEat(ts);
    if (fseek(ts->file.hnd, 0, SEEK_SET) < 0) {
        int err = errno;
        fprintf(stderr, "%.*s:%zu:%zu: ", (int)ts->pos.file.len,
                ts->pos.file.bytes, (size_t)ts->file.cline,
                (size_t)ts->file.ccol);
        smFatal("failed to rewind file: %s\n", strerror(err));
    }
    ts->pos.line      = 1;
    ts->pos.col       = 1;
    ts->file.cstashed = false;
    ts->file.cline    = 1;
    ts->file.ccol     = 1;
    return;
}

SmBuf smTokStreamBuf(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        return ts->file.buf.inner;
    case SM_TOK_STREAM_MACRO: {
        SmMacroTok *tok = ts->macro.buf.items + ts->macro.pos;
        switch (tok->kind) {
        case SM_MACRO_TOK_STR:
        case SM_MACRO_TOK_ID:
            return tok->buf;
        case SM_MACRO_TOK_ARG:
            tok = ts->macro.args.buf[tok->arg].items + ts->macro.argi;
            switch (tok->kind) {
            case SM_MACRO_TOK_STR:
            case SM_MACRO_TOK_ID:
                return tok->buf;
            default:
                smUnreachable();
            }
        case SM_MACRO_TOK_UNIQUE:
            smUnimplemented();
        default:
            smUnreachable();
        }
    }
    case SM_TOK_STREAM_FMT:
        return ts->fmt.buf;
    default:
        smUnimplemented();
    }
}

I32 smTokStreamNum(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        return ts->file.num;
    case SM_TOK_STREAM_MACRO: {
        SmMacroTok *tok = ts->macro.buf.items + ts->macro.pos;
        switch (tok->kind) {
        case SM_MACRO_TOK_NUM:
            return tok->num;
        case SM_MACRO_TOK_ARG:
            tok = ts->macro.args.buf[tok->arg].items + ts->macro.argi;
            switch (tok->kind) {
            case SM_MACRO_TOK_NUM:
                return tok->num;
            default:
                smUnreachable();
            }
        case SM_MACRO_TOK_NARG:
            return ts->macro.argi;
        default:
            smUnreachable();
        }
    }
    case SM_TOK_STREAM_FMT:
        smUnreachable();
    default:
        smUnimplemented();
    }
}

SmPos smTokStreamPos(SmTokStream *ts) {
    switch (ts->kind) {
    case SM_TOK_STREAM_FILE:
        return ts->pos;
    case SM_TOK_STREAM_MACRO:
        return ts->macro.buf.items[ts->macro.pos].pos;
    case SM_TOK_STREAM_FMT:
        return ts->pos;
    default:
        smUnimplemented();
    }
}
