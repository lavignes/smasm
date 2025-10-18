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
    UInt   cap;
} SmBuf;

void smBufCat(SmBuf *buf, SmView view);
void smBufFini(SmBuf *buf);

typedef struct {
    SmView *items;
    UInt    len;
} SmViewView;

typedef struct {
    SmViewView view;
    UInt       cap;
} SmViewBuf;

void smViewBufAdd(SmViewBuf *buf, SmView item);

#define SM_BUF_ADD_IMPL()                                                      \
    if (!buf->view.items) {                                                    \
        buf->view.items = malloc(sizeof(*buf->view.items) * 16);               \
        if (!buf->view.items) {                                                \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->view.len = 0;                                                     \
        buf->cap      = 16;                                                    \
    }                                                                          \
    if ((buf->cap - buf->view.len) == 0) {                                     \
        buf->view.items =                                                      \
            realloc(buf->view.items, sizeof(*buf->view.items) * buf->cap * 2); \
        if (!buf->view.items) {                                                \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        buf->cap *= 2;                                                         \
    }                                                                          \
    buf->view.items[buf->view.len] = item;                                     \
    ++buf->view.len;

#define SM_BUF_FINI_IMPL()                                                     \
    if (!buf->view.items) {                                                    \
        return;                                                                \
    }                                                                          \
    free(buf->view.items);                                                     \
    memset(buf, 0, sizeof(*buf));

typedef struct {
    SmBuf *bufs;
    UInt   len;
    UInt   cap;
} SmViewIntern;

SmView smViewIntern(SmViewIntern *in, SmView view);
void   smViewInternFini(SmViewIntern *in);

#ifndef typeof
#define typeof __typeof__
#endif

#define SM_INTERN_IMPL()                                                       \
    if (!in->bufs) {                                                           \
        in->bufs = malloc(sizeof(*in->bufs) * 16);                             \
        if (!in->bufs) {                                                       \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        in->len = 0;                                                           \
        in->cap = 16;                                                          \
    }                                                                          \
    typeof(*in->bufs) *has_space = NULL;                                       \
    for (UInt i = 0; i < in->len; ++i) {                                       \
        typeof(*in->bufs)        *buf = in->bufs + i;                          \
        typeof(*buf->view.items) *items =                                      \
            memmem(buf->view.items, sizeof(*buf->view.items) * buf->view.len,  \
                   view.items, sizeof(*buf->view.items) * view.len);           \
        if (items) {                                                           \
            return (typeof(buf->view)){items, view.len};                       \
        }                                                                      \
        if (!has_space) {                                                      \
            if ((buf->cap - buf->view.len) >= view.len) {                      \
                has_space = buf;                                               \
            }                                                                  \
        }                                                                      \
    }                                                                          \
    if (!has_space) {                                                          \
        if ((in->cap - in->len) == 0) {                                        \
            in->bufs = realloc(in->bufs, sizeof(*in->bufs) * in->cap * 2);     \
            if (!in->bufs) {                                                   \
                smFatal("out of memory\n");                                    \
            }                                                                  \
            in->cap *= 2;                                                      \
        }                                                                      \
        has_space = in->bufs + in->len;                                        \
        has_space->view.items =                                                \
            malloc(sizeof(*has_space->view.items) * view.len);                 \
        has_space->view.len = 0;                                               \
        has_space->cap      = view.len;                                        \
        if (!has_space->view.items) {                                          \
            smFatal("out of memory\n");                                        \
        }                                                                      \
        ++in->len;                                                             \
    }                                                                          \
    typeof(*has_space->view.items) *items =                                    \
        has_space->view.items + has_space->view.len;                           \
    memcpy(items, view.items, sizeof(*has_space->view.items) * view.len);      \
    has_space->view.len += view.len;                                           \
    return (typeof(has_space->view)){items, view.len};

#define SM_INTERN_FINI_IMPL(BufFiniFn)                                         \
    if (!in->bufs) {                                                           \
        return;                                                                \
    }                                                                          \
    for (UInt i = 0; i < in->len; ++i) {                                       \
        (BufFiniFn)(in->bufs + i);                                             \
    }                                                                          \
    free(in->bufs);                                                            \
    memset(in, 0, sizeof(*in));

#endif // SMASM_BUF_H
