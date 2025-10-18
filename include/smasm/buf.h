#ifndef SMASM_BUF_H
#define SMASM_BUF_H

#include <smasm/abi.h>

typedef struct {
    U8  *bytes;
    UInt len;
} SmView;

static SmView const SM_VIEW_NULL = {0};

#define SM_VIEW(cstr)         ((SmView){(U8 *)(cstr), (sizeof((cstr)) - 1)})

#define SM_VIEW_FMT           "%.*s"
#define SM_VIEW_FMT_ARG(view) ((int)(view).len), ((char *)(view).bytes)

Bool smViewEqual(SmView lhs, SmView rhs);
Bool smViewEqualIgnoreAsciiCase(SmView lhs, SmView rhs);
Bool smViewStartsWith(SmView view, SmView prefix);
UInt smViewHash(SmView view);
UInt smViewParse(SmView view);

typedef struct {
    SmView view;
    UInt   size;
} SmGBuf;

void smGBufCat(SmGBuf *buf, SmView bytes);
void smGBufFini(SmGBuf *buf);

typedef struct {
    SmView *items;
    UInt    len;
} SmBufBuf;

typedef struct {
    SmBufBuf view;
    UInt     size;
} SmBufGBuf;

void smBufGBufAdd(SmBufGBuf *buf, SmView item);

#define SM_GBUF_ADD_IMPL()                                                     \
    if (!buf->view.items) {                                                    \
        buf->view.items = malloc(sizeof(*buf->view.items) * 16);               \
        if (!buf->view.items) {                                                \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->view.len = 0;                                                     \
        buf->size     = 16;                                                    \
    }                                                                          \
    if ((buf->size - buf->view.len) == 0) {                                    \
        buf->view.items = realloc(buf->view.items,                             \
                                  sizeof(*buf->view.items) * buf->size * 2);   \
        if (!buf->view.items) {                                                \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->size *= 2;                                                        \
    }                                                                          \
    buf->view.items[buf->view.len] = item;                                     \
    ++buf->view.len;

#define SM_GBUF_FINI_IMPL()                                                    \
    if (!buf->view.items) {                                                    \
        return;                                                                \
    }                                                                          \
    free(buf->view.items);                                                     \
    memset(buf, 0, sizeof(*buf));

typedef struct {
    SmGBuf *bufs;
    UInt    len;
    UInt    size;
} SmBufIntern;

SmView smBufIntern(SmBufIntern *in, SmView view);
void   smBufInternFini(SmBufIntern *in);

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
        typeof(*in->bufs)         *gbuf  = in->bufs + i;                       \
        typeof(*gbuf->view.items) *items = memmem(                             \
            gbuf->view.items, sizeof(*gbuf->view.items) * gbuf->view.len,      \
            buf.items, sizeof(*gbuf->view.items) * buf.len);                   \
        if (items) {                                                           \
            return (typeof(gbuf->view)){items, buf.len};                       \
        }                                                                      \
        if (!has_space) {                                                      \
            if ((gbuf->size - gbuf->view.len) >= buf.len) {                    \
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
        has_space->view.items =                                                \
            malloc(sizeof(*has_space->view.items) * buf.len);                  \
        has_space->view.len = 0;                                               \
        has_space->size     = buf.len;                                         \
        if (!has_space->view.items) {                                          \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        ++in->len;                                                             \
    }                                                                          \
    typeof(*has_space->view.items) *items =                                    \
        has_space->view.items + has_space->view.len;                           \
    memcpy(items, buf.items, sizeof(*has_space->view.items) * buf.len);        \
    has_space->view.len += buf.len;                                            \
    return (typeof(has_space->view)){items, buf.len};

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
