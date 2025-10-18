#ifndef SMASM_TOK_H
#define SMASM_TOK_H

#include <smasm/buf.h>
#include <smasm/fatal.h>

#include <stdio.h>

enum SmTok {
    SM_TOK_EOF      = 26,

    SM_TOK_ID       = 0xF0000,
    SM_TOK_NUM      = 0xF0001,
    SM_TOK_STR      = 0xF0002,

    SM_TOK_DB       = 0xF000A,
    SM_TOK_DW       = 0xF000B,
    SM_TOK_DS       = 0xF000C,
    SM_TOK_SECTION  = 0xF000D,
    SM_TOK_SECTPUSH = 0xF000E,
    SM_TOK_SECTPOP  = 0xF000F,
    SM_TOK_INCLUDE  = 0xF0010,
    SM_TOK_INCBIN   = 0xF0011,
    SM_TOK_IF       = 0xF0012,
    SM_TOK_ELSE     = 0xF0013,
    SM_TOK_END      = 0xF0014,
    SM_TOK_MACRO    = 0xF0015,
    SM_TOK_REPEAT   = 0xF0016,
    SM_TOK_STRUCT   = 0xF0017,
    SM_TOK_UNION    = 0xF0018,
    SM_TOK_STRFMT   = 0xF0019,
    SM_TOK_IDFMT    = 0xF001A,
    SM_TOK_ALLOC    = 0xF001B,
    SM_TOK_FATAL    = 0xF001C,
    SM_TOK_PRINT    = 0xF001D,

    SM_TOK_DEFINED  = 0xF0020,
    SM_TOK_STRLEN   = 0xF0021,
    SM_TOK_TAG      = 0xF0022,
    SM_TOK_REL      = 0xF0023,

    SM_TOK_ASL      = 0xF0030, // <<
    SM_TOK_ASR      = 0xF0031, // >>
    SM_TOK_LSR      = 0xF0032, // ~>
    SM_TOK_LTE      = 0xF0033, // <=
    SM_TOK_GTE      = 0xF0034, // >=
    SM_TOK_DEQ      = 0xF0035, // ==
    SM_TOK_NEQ      = 0xF0036, // !=
    SM_TOK_AND      = 0xF0037, // &&
    SM_TOK_OR       = 0xF0038, // ||
    SM_TOK_DCOLON   = 0xF0039, // ::
    SM_TOK_EXPEQU   = 0xF003B, // =:
    SM_TOK_DSTAR    = 0xF003C, // **

    SM_TOK_AF       = 0xF0040,
    SM_TOK_BC       = 0xF0041,
    SM_TOK_DE       = 0xF0042,
    SM_TOK_HL       = 0xF0043,
    SM_TOK_SP       = 0xF0044,
    SM_TOK_NC       = 0xF0045,
    SM_TOK_NZ       = 0xF0046,

    SM_TOK_ARG      = 0xF0050,
    SM_TOK_NARG     = 0xF0051,
    SM_TOK_SHIFT    = 0xF0052,
    SM_TOK_UNIQUE   = 0xF0053,
};

SmView smTokName(U32 c);

typedef struct {
    SmView file;
    UInt   line;
    UInt   col;
} SmPos;

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

typedef struct {
    U8    kind;
    SmPos pos;
    union {
        U32    tok;
        SmView view;
        I32    num;
    };
} SmMacroTok;

typedef struct {
    SmMacroTok *items;
    UInt        len;
} SmMacroTokView;

typedef struct {
    SmMacroTokView view;
    UInt           cap;
} SmMacroTokBuf;

void smMacroTokBufAdd(SmMacroTokBuf *buf, SmMacroTok tok);
void smMacroTokBufFini(SmMacroTokBuf *buf);

typedef struct {
    SmMacroTokBuf *bufs;
    UInt           len;
    UInt           cap;
} SmMacroTokIntern;

SmMacroTokView smMacroTokIntern(SmMacroTokIntern *in, SmMacroTokView view);

typedef struct {
    SmMacroTokView *buf;
    UInt            len;
    UInt            cap;
} SmMacroArgQueue;

void smMacroArgEnqueue(SmMacroArgQueue *q, SmMacroTokView view);
void smMacroArgDequeue(SmMacroArgQueue *q);
void smMacroArgQueueFini(SmMacroArgQueue *q);

enum SmRepeatTokKind {
    SM_REPEAT_TOK_TOK,
    SM_REPEAT_TOK_ID,
    SM_REPEAT_TOK_NUM,
    SM_REPEAT_TOK_STR,
    SM_REPEAT_TOK_ITER,
};

typedef struct {
    U8    kind;
    SmPos pos;
    union {
        U32    tok;
        SmView view;
        I32    num;
    };
} SmRepeatTok;

typedef struct {
    SmRepeatTok *items;
    UInt         len;
} SmRepeatTokView;

typedef struct {
    SmRepeatTokView view;
    UInt            cap;
} SmRepeatTokBuf;

void smRepeatTokBufAdd(SmRepeatTokBuf *buf, SmRepeatTok tok);
void smRepeatTokBufFini(SmRepeatTokBuf *buf);

typedef struct {
    U32   tok;
    SmPos pos;
    union {
        SmView view;
        I32    num;
    };
} SmPosTok;

typedef struct {
    SmPosTok *items;
    UInt      len;
} SmPosTokView;

typedef struct {
    SmPosTokView view;
    UInt         cap;
} SmPosTokBuf;

void smPosTokBufAdd(SmPosTokBuf *buf, SmPosTok tok);
void smPosTokBufFini(SmPosTokBuf *buf);

enum SmTokStreamKind {
    SM_TOK_STREAM_FILE,
    SM_TOK_STREAM_VIEW,
    SM_TOK_STREAM_MACRO,
    SM_TOK_STREAM_REPEAT,
    SM_TOK_STREAM_FMT,
    SM_TOK_STREAM_IFELSE,
};

typedef struct {
    U8    kind;
    SmPos pos;
    union {
        struct {
            union {
                struct {
                    FILE *hnd;
                } file;

                struct {
                    SmView view;
                    UInt   offset;
                } src;
            };
            U32   stash;
            Bool  stashed;
            U32   cstash;
            Bool  cstashed;
            UInt  cline;
            UInt  ccol;
            U8    cbuf[4];
            UInt  clen;
            SmBuf buf;
            I32   num;
        } chardev;

        struct {
            SmView          name;
            SmMacroTokView  view;
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
            SmView view;
            U32    tok;
        } fmt;

        struct {
            SmPosTokBuf buf;
            UInt        pos;
        } ifelse;
    };
} SmTokStream;

SM_FORMAT(2)
_Noreturn void smTokStreamFatal(SmTokStream *ts, char const *fmt, ...);
SM_FORMAT(3)
_Noreturn void smTokStreamFatalPos(SmTokStream *ts, SmPos pos, char const *fmt,
                                   ...);

_Noreturn void smTokStreamFatalV(SmTokStream *ts, char const *fmt,
                                 va_list args);
_Noreturn void smTokStreamFatalPosV(SmTokStream *ts, SmPos pos, char const *fmt,
                                    va_list args);

void smTokStreamFileInit(SmTokStream *ts, SmView name, FILE *hnd);
void smTokStreamViewInit(SmTokStream *ts, SmView name, SmView view);
void smTokStreamMacroInit(SmTokStream *ts, SmView name, SmPos pos,
                          SmMacroTokView buf, SmMacroArgQueue args, UInt nonce);
void smTokStreamRepeatInit(SmTokStream *ts, SmPos pos, SmRepeatTokBuf buf,
                           UInt cnt);
void smTokStreamFmtInit(SmTokStream *ts, SmPos pos, SmView fmt, U32 tok);
void smTokStreamIfElseInit(SmTokStream *ts, SmPos pos, SmPosTokBuf buf);
void smTokStreamFini(SmTokStream *ts);

U32  smTokStreamPeek(SmTokStream *ts);
void smTokStreamEat(SmTokStream *ts);
void smTokStreamRewind(SmTokStream *ts);

SmView smTokStreamView(SmTokStream *ts);
I32    smTokStreamNum(SmTokStream *ts);
SmPos  smTokStreamPos(SmTokStream *ts);

#endif // SMASM_TOK_H
