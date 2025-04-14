#ifndef SMASM_TOK_H
#define SMASM_TOK_H

#include <smasm/buf.h>

#include <stdio.h>

enum SmTok {
    SM_TOK_EOF     = 26,

    SM_TOK_ID      = 0xF0000,
    SM_TOK_NUM     = 0xF0001,
    SM_TOK_STR     = 0xF0002,

    SM_TOK_DB      = 0xF0010,
    SM_TOK_DW      = 0xF0011,
    SM_TOK_DS      = 0xF0012,
    SM_TOK_SECTION = 0xF0013,
    SM_TOK_INCLUDE = 0xF0014,
    SM_TOK_INCBIN  = 0xF0015,
    SM_TOK_IF      = 0xF0016,
    SM_TOK_END     = 0xF0017,
    SM_TOK_MACRO   = 0xF0018,
    SM_TOK_REPEAT  = 0xF0019,
    SM_TOK_STRUCT  = 0xF001A,
    SM_TOK_STRFMT  = 0xF001B,
    SM_TOK_IDFMT   = 0xF001C,
    SM_TOK_CREATE  = 0xF001D,
    SM_TOK_FATAL   = 0xF001E,
    SM_TOK_PRINT   = 0xF001F,

    SM_TOK_DEFINED = 0xF0020,
    SM_TOK_STRLEN  = 0xF0021,
    SM_TOK_TAG     = 0xF0022,
    SM_TOK_REL     = 0xF0023,

    SM_TOK_ASL     = 0xF0030, // <<
    SM_TOK_ASR     = 0xF0031, // >>
    SM_TOK_LSR     = 0xF0032, // ~>
    SM_TOK_LTE     = 0xF0033, // <=
    SM_TOK_GTE     = 0xF0034, // >=
    SM_TOK_DEQ     = 0xF0035, // ==
    SM_TOK_NEQ     = 0xF0036, // !=
    SM_TOK_AND     = 0xF0037, // &&
    SM_TOK_OR      = 0xF0038, // ||
    SM_TOK_DCOLON  = 0xF0039, // ::
    SM_TOK_DSTAR   = 0xF003A, // **

    SM_TOK_AF      = 0xF0040,
    SM_TOK_BC      = 0xF0041,
    SM_TOK_DE      = 0xF0042,
    SM_TOK_HL      = 0xF0043,
    SM_TOK_SP      = 0xF0044,
    SM_TOK_NC      = 0xF0045,
    SM_TOK_NZ      = 0xF0046,

    SM_TOK_ARG     = 0xF0050,
    SM_TOK_NARG    = 0xF0051,
    SM_TOK_SHIFT   = 0xF0052,
    SM_TOK_UNIQUE  = 0xF0053,
};

SmBuf smTokName(U32 c);

struct SmPos {
    SmBuf file;
    UInt  line;
    UInt  col;
};
typedef struct SmPos SmPos;

enum SmMacroTokKind {
    SM_MACRO_TOK_TOK,
    SM_MACRO_TOK_ID,
    SM_MACRO_TOK_NUM,
    SM_MACRO_TOK_STR,
    SM_MACRO_TOK_ARG,
    SM_MACRO_TOK_NARG,
    SM_MACRO_TOK_SHIFT,
    SM_MACRO_TOK_UNIQUE,
};

struct SmMacroTok {
    U8    kind;
    SmPos pos;
    union {
        U32   tok;
        SmBuf buf;
        I32   num;
        UInt  arg;
    };
};
typedef struct SmMacroTok SmMacroTok;

struct SmMacroTokBuf {
    SmMacroTok *items;
    UInt        len;
};
typedef struct SmMacroTokBuf SmMacroTokBuf;

struct SmMacroTokGBuf {
    SmMacroTokBuf inner;
    UInt          size;
};
typedef struct SmMacroTokGBuf SmMacroTokGBuf;

void smMacroTokGBufAdd(SmMacroTokGBuf *buf, SmMacroTok tok);
void smMacroTokGBufFini(SmMacroTokGBuf *buf);

struct SmMacroTokIntern {
    SmMacroTokGBuf *bufs;
    UInt            len;
    UInt            size;
};
typedef struct SmMacroTokIntern SmMacroTokIntern;

SmMacroTokBuf smMacroTokIntern(SmMacroTokIntern *in, SmMacroTokBuf buf);

struct SmMacroArgQueue {
    SmMacroTokBuf *buf;
    UInt           len;
    UInt           size;
};
typedef struct SmMacroArgQueue SmMacroArgQueue;

void smMacroArgEnqueue(SmMacroArgQueue *q, SmMacroTokBuf toks);
void smMacroArgDequeue(SmMacroArgQueue *q);

enum SmRepeatTokKind {
    SM_REPEAT_TOK_TOK,
    SM_REPEAT_TOK_ID,
    SM_REPEAT_TOK_NUM,
    SM_REPEAT_TOK_STR,
    SM_REPEAT_TOK_ITER,
};

struct SmRepeatTok {
    U8    kind;
    SmPos pos;
    union {
        U32   tok;
        SmBuf buf;
        I32   num;
    };
};

typedef struct SmRepeatTok SmRepeatTok;

struct SmRepeatTokBuf {
    SmRepeatTok *items;
    UInt         len;
};
typedef struct SmRepeatTokBuf SmRepeatTokBuf;

struct SmRepeatTokGBuf {
    SmRepeatTokBuf inner;
    UInt           size;
};
typedef struct SmRepeatTokGBuf SmRepeatTokGBuf;

void smRepeatTokGBufAdd(SmRepeatTokGBuf *buf, SmRepeatTok tok);

struct SmRepeatTokIntern {
    SmRepeatTokGBuf *bufs;
    UInt             len;
    UInt             size;
};
typedef struct SmRepeatTokIntern SmRepeatTokIntern;

SmRepeatTokBuf smRepeatTokIntern(SmRepeatTokIntern *in, SmRepeatTokBuf buf);

enum SmTokStreamKind {
    SM_TOK_STREAM_FILE,
    SM_TOK_STREAM_MACRO,
    SM_TOK_STREAM_REPEAT,
    SM_TOK_STREAM_FMT,
};

struct SmTokStream {
    U8    kind;
    SmPos pos;
    union {
        struct {
            FILE  *hnd;
            U32    stash;
            Bool   stashed;
            U32    cstash;
            Bool   cstashed;
            UInt   cline;
            UInt   ccol;
            U8     cbuf[4];
            UInt   clen;
            SmGBuf buf;
            I32    num;
        } file;

        struct {
            SmBuf           name;
            SmMacroTokBuf   buf;
            UInt            pos;
            SmMacroArgQueue args;
            UInt            argi;
            UInt            nonce;
        } macro;

        struct {
            SmRepeatTokBuf buf;
            UInt           pos;
            UInt           idx;
            UInt           cnt;
        } repeat;

        struct {
            SmBuf buf;
            U32   tok;
        } fmt;
    };
};
typedef struct SmTokStream SmTokStream;

_Noreturn void smTokStreamFatal(SmTokStream *ts, char const *fmt, ...);
_Noreturn void smTokStreamFatalPos(SmTokStream *ts, SmPos pos, char const *fmt,
                                   ...);

_Noreturn void smTokStreamFatalV(SmTokStream *ts, char const *fmt,
                                 va_list args);
_Noreturn void smTokStreamFatalPosV(SmTokStream *ts, SmPos pos, char const *fmt,
                                    va_list args);

void smTokStreamFileInit(SmTokStream *ts, SmBuf name, FILE *hnd);
void smTokStreamMacroInit(SmTokStream *ts, SmBuf name, SmPos pos,
                          SmMacroTokBuf buf, SmMacroArgQueue args, UInt nonce);
void smTokStreamRepeatInit(SmTokStream *ts, SmPos pos, SmRepeatTokBuf buf,
                           UInt cnt);
void smTokStreamFmtInit(SmTokStream *ts, SmBuf buf, SmPos pos, U32 tok);
void smTokStreamFini(SmTokStream *ts);

U32  smTokStreamPeek(SmTokStream *ts);
void smTokStreamEat(SmTokStream *ts);
void smTokStreamRewind(SmTokStream *ts);

SmBuf smTokStreamBuf(SmTokStream *ts);
I32   smTokStreamNum(SmTokStream *ts);
SmPos smTokStreamPos(SmTokStream *ts);

#endif // SMASM_TOK_H
