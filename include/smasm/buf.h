#ifndef SMASM_BUF_H
#define SMASM_BUF_H

#include <smasm/abi.h>

struct SmBuf {
    U8  *bytes;
    UInt len;
};
typedef struct SmBuf SmBuf;

#define SM_BUF(cstr) ((SmBuf){(U8 *)(cstr), (sizeof((cstr)) - 1)})

Bool smBufEqual(SmBuf lhs, SmBuf rhs);
Bool smBufEqualIgnoreAsciiCase(SmBuf lhs, SmBuf rhs);

struct SmGBuf {
    SmBuf inner;
    UInt  size;
};
typedef struct SmGBuf SmGBuf;

void smGBufCat(SmGBuf *buf, SmBuf bytes);

struct SmBufBuf {
    SmBuf *items;
    UInt   len;
};
typedef struct SmBufBuf SmBufBuf;

struct SmBufGBuf {
    SmBufBuf inner;
    UInt     size;
};
typedef struct SmBufGBuf SmBufGBuf;

void smBufGBufAdd(SmBufGBuf *buf, SmBuf item);

#define SM_GBUF_ADD_IMPL(Type)                                                 \
    if (!buf->inner.items) {                                                   \
        buf->inner.items = malloc(sizeof(Type) * 16);                          \
        if (!buf->inner.items) {                                               \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->inner.len = 0;                                                    \
        buf->size      = 16;                                                   \
    }                                                                          \
    if ((buf->size - buf->inner.len) == 0) {                                   \
        buf->inner.items =                                                     \
            realloc(buf->inner.items, sizeof(Type) * buf->size * 2);           \
        if (!buf->inner.items) {                                               \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->size *= 2;                                                        \
    }                                                                          \
    buf->inner.items[buf->inner.len] = item;                                   \
    ++buf->inner.len;

struct SmBufIntern {
    SmGBuf *bufs;
    UInt    len;
    UInt    size;
};
typedef struct SmBufIntern SmBufIntern;

SmBuf smBufIntern(SmBufIntern *in, SmBuf buf);

#define SM_INTERN_IMPL(Type, BufType, GrowType)                                \
    if (!in->bufs) {                                                           \
        in->bufs = malloc(sizeof(GrowType) * 16);                              \
        if (!in->bufs) {                                                       \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        in->len  = 0;                                                          \
        in->size = 16;                                                         \
    }                                                                          \
    GrowType *has_space = NULL;                                                \
    for (UInt i = 0; i < in->len; ++i) {                                       \
        GrowType *gbuf = in->bufs + i;                                         \
        Type     *items =                                                      \
            memmem(gbuf->inner.items, sizeof(Type) * gbuf->inner.len,          \
                   buf.items, sizeof(Type) * buf.len);                         \
        if (items) {                                                           \
            return (BufType){items, buf.len};                                  \
        }                                                                      \
        if (!has_space) {                                                      \
            if ((gbuf->size - gbuf->inner.len) >= buf.len) {                   \
                has_space = gbuf;                                              \
            }                                                                  \
        }                                                                      \
    }                                                                          \
    if (!has_space) {                                                          \
        if ((in->size - in->len) == 0) {                                       \
            in->bufs = realloc(in->bufs, sizeof(GrowType) * in->size * 2);     \
            if (!in->bufs) {                                                   \
                smFatal("out of memory\n");                                    \
            }                                                                  \
            in->size *= 2;                                                     \
        }                                                                      \
        has_space              = in->bufs + in->len;                           \
        has_space->inner.items = malloc(sizeof(Type) * buf.len);               \
        has_space->inner.len   = 0;                                            \
        has_space->size        = buf.len;                                      \
        if (!has_space->inner.items) {                                         \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        ++in->len;                                                             \
    }                                                                          \
    Type *items = has_space->inner.items + has_space->inner.len;               \
    memcpy(items, buf.items, sizeof(Type) * buf.len);                          \
    has_space->inner.len += buf.len;                                           \
    return (BufType){items, buf.len};

#endif // SMASM_BUF_H
