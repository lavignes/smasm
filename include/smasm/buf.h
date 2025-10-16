#ifndef SMASM_BUF_H
#define SMASM_BUF_H

#include <smasm/abi.h>

struct SmBuf {
    U8  *bytes;
    UInt len;
};
typedef struct SmBuf SmBuf;

static SmBuf const SM_BUF_NULL = {0};

#define SM_BUF(cstr)        ((SmBuf){(U8 *)(cstr), (sizeof((cstr)) - 1)})

#define SM_BUF_FMT          "%.*s"
#define SM_BUF_FMT_ARG(buf) ((int)(buf).len), ((char *)(buf).bytes)

Bool smBufEqual(SmBuf lhs, SmBuf rhs);
Bool smBufEqualIgnoreAsciiCase(SmBuf lhs, SmBuf rhs);
Bool smBufStartsWith(SmBuf buf, SmBuf prefix);
UInt smBufHash(SmBuf buf);
UInt smBufParse(SmBuf buf);

struct SmGBuf {
    SmBuf inner;
    UInt  size;
};
typedef struct SmGBuf SmGBuf;

void smGBufCat(SmGBuf *buf, SmBuf bytes);
void smGBufFini(SmGBuf *buf);

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

#define SM_GBUF_ADD_IMPL()                                                     \
    if (!buf->inner.items) {                                                   \
        buf->inner.items = malloc(sizeof(*buf->inner.items) * 16);             \
        if (!buf->inner.items) {                                               \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->inner.len = 0;                                                    \
        buf->size      = 16;                                                   \
    }                                                                          \
    if ((buf->size - buf->inner.len) == 0) {                                   \
        buf->inner.items = realloc(buf->inner.items,                           \
                                   sizeof(*buf->inner.items) * buf->size * 2); \
        if (!buf->inner.items) {                                               \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->size *= 2;                                                        \
    }                                                                          \
    buf->inner.items[buf->inner.len] = item;                                   \
    ++buf->inner.len;

#define SM_GBUF_FINI_IMPL()                                                    \
    if (!buf->inner.items) {                                                   \
        return;                                                                \
    }                                                                          \
    free(buf->inner.items);                                                    \
    memset(buf, 0, sizeof(*buf));

struct SmBufIntern {
    SmGBuf *bufs;
    UInt    len;
    UInt    size;
};
typedef struct SmBufIntern SmBufIntern;

SmBuf smBufIntern(SmBufIntern *in, SmBuf buf);
void  smBufInternFini(SmBufIntern *in);

#ifndef typeof
#define typeof __typeof__
#endif

#define SM_INTERN_IMPL()                                                       \
    if (!in->bufs) {                                                           \
        in->bufs = malloc(sizeof(*in->bufs) * 16);                             \
        if (!in->bufs) {                                                       \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        in->len  = 0;                                                          \
        in->size = 16;                                                         \
    }                                                                          \
    typeof(*in->bufs) *has_space = NULL;                                       \
    for (UInt i = 0; i < in->len; ++i) {                                       \
        typeof(*in->bufs)          *gbuf  = in->bufs + i;                      \
        typeof(*gbuf->inner.items) *items = memmem(                            \
            gbuf->inner.items, sizeof(*gbuf->inner.items) * gbuf->inner.len,   \
            buf.items, sizeof(*gbuf->inner.items) * buf.len);                  \
        if (items) {                                                           \
            return (typeof(gbuf->inner)){items, buf.len};                      \
        }                                                                      \
        if (!has_space) {                                                      \
            if ((gbuf->size - gbuf->inner.len) >= buf.len) {                   \
                has_space = gbuf;                                              \
            }                                                                  \
        }                                                                      \
    }                                                                          \
    if (!has_space) {                                                          \
        if ((in->size - in->len) == 0) {                                       \
            in->bufs = realloc(in->bufs, sizeof(*in->bufs) * in->size * 2);    \
            if (!in->bufs) {                                                   \
                smFatal("out of memory\n");                                    \
            }                                                                  \
            in->size *= 2;                                                     \
        }                                                                      \
        has_space = in->bufs + in->len;                                        \
        has_space->inner.items =                                               \
            malloc(sizeof(*has_space->inner.items) * buf.len);                 \
        has_space->inner.len = 0;                                              \
        has_space->size      = buf.len;                                        \
        if (!has_space->inner.items) {                                         \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        ++in->len;                                                             \
    }                                                                          \
    typeof(*has_space->inner.items) *items =                                   \
        has_space->inner.items + has_space->inner.len;                         \
    memcpy(items, buf.items, sizeof(*has_space->inner.items) * buf.len);       \
    has_space->inner.len += buf.len;                                           \
    return (typeof(has_space->inner)){items, buf.len};

#define SM_INTERN_FINI_IMPL(GBufFiniFn)                                        \
    if (!in->bufs) {                                                           \
        return;                                                                \
    }                                                                          \
    for (UInt i = 0; i < in->len; ++i) {                                       \
        (GBufFiniFn)(in->bufs + i);                                            \
    }                                                                          \
    free(in->bufs);                                                            \
    memset(in, 0, sizeof(*in));

#endif // SMASM_BUF_H
