#include <smasm/buf.h>
#include <smasm/fatal.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

Bool smViewEqual(SmView lhs, SmView rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    if (lhs.bytes == rhs.bytes) {
        return true;
    }
    return memcmp(lhs.bytes, rhs.bytes, lhs.len) == 0;
}

Bool smViewEqualIgnoreAsciiCase(SmView lhs, SmView rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    if (lhs.bytes == rhs.bytes) {
        return true;
    }
    for (UInt i = 0; i < lhs.len; ++i) {
        if (tolower(lhs.bytes[i]) != tolower(rhs.bytes[i])) {
            return false;
        }
    }
    return true;
}

Bool smViewStartsWith(SmView view, SmView prefix) {
    if (view.len < prefix.len) {
        return false;
    }
    if (view.bytes == prefix.bytes) {
        return true;
    }
    return memcmp(view.bytes, prefix.bytes, prefix.len) == 0;
}

UInt smViewHash(SmView view) {
    UInt hash = 5381;
    for (UInt i = 0; i < view.len; ++i) {
        hash = ((hash << 5) + hash) + view.bytes[i];
    }
    return hash;
}

static char const DIGITS[] = "0123456789ABCDEF";

UInt smViewParse(SmView view) {
    if (view.len == 0) {
        smFatal("empty number\n");
    }
    I32  radix = 10;
    UInt i     = 0;
    if (view.bytes[0] == '%') {
        radix = 2;
        ++i;
    } else if (view.bytes[0] == '$') {
        radix = 16;
        ++i;
    }
    if (i == view.len) {
        smFatal("invalid number: " SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
    }
    UInt value = 0;
    for (; i < view.len; ++i) {
        for (UInt j = 0; j < (sizeof(DIGITS) / sizeof(DIGITS[0])); ++j) {
            if (toupper(view.bytes[i]) == DIGITS[j]) {
                if (j >= (UInt)radix) {
                    smFatal("invalid number: " SM_VIEW_FMT "\n",
                            SM_VIEW_FMT_ARG(view));
                }
                value *= radix;
                value += j;
                goto next;
            }
        }
        smFatal("invalid number: " SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
    next:
        (void)0;
    }
    return value;
}

void smGBufCat(SmGBuf *buf, SmView view) {
    if (!buf->view.bytes) {
        buf->view.bytes = malloc(view.len);
        if (!buf->view.bytes) {
            smFatal("out of memory\n");
        }
        buf->view.len = 0;
        buf->size     = view.len;
    }
    if ((buf->view.len + view.len) > buf->size) {
        buf->view.bytes = realloc(buf->view.bytes, buf->size + view.len);
        if (!buf->view.bytes) {
            smFatal("out of memory\n");
        }
        buf->size += view.len;
    }
    memcpy(buf->view.bytes + buf->view.len, view.bytes, view.len);
    buf->view.len += view.len;
}

void smGBufFini(SmGBuf *buf) {
    if (!buf->view.bytes) {
        return;
    }
    free(buf->view.bytes);
    memset(buf, 0, sizeof(SmGBuf));
}

void smBufGBufAdd(SmBufGBuf *buf, SmView item) { SM_GBUF_ADD_IMPL(); }

static UInt roundUp(UInt num) {
    num--;
    num |= num >> 1;
    num |= num >> 2;
    num |= num >> 4;
    num |= num >> 8;
    num |= num >> 16;
    if (sizeof(UInt) == sizeof(U64)) {
        num |= num >> 32;
    }
    num++;
    return num;
}

SmView smBufIntern(SmBufIntern *in, SmView view) {
    if (!in->bufs) {
        in->bufs = malloc(sizeof(SmGBuf) * 16);
        if (!in->bufs) {
            smFatal("out of memory\n");
        }
        in->len  = 0;
        in->size = 16;
    }
    SmGBuf *has_space = NULL;
    for (UInt i = 0; i < in->len; ++i) {
        SmGBuf *gbuf = in->bufs + i;
        U8     *bytes =
            memmem(gbuf->view.bytes, gbuf->view.len, view.bytes, view.len);
        if (bytes) {
            return (SmView){bytes, view.len};
        }
        if (!has_space) {
            if ((gbuf->size - gbuf->view.len) >= view.len) {
                has_space = gbuf;
            }
        }
    }
    if (!has_space) {
        if ((in->size - in->len) == 0) {
            in->bufs = realloc(in->bufs, sizeof(SmGBuf) * in->size * 2);
            if (!in->bufs) {
                smFatal("out of memory\n");
            }
            in->size *= 2;
        }
        UInt size             = uIntMax(roundUp(view.len), 256);
        has_space             = in->bufs + in->len;
        has_space->view.bytes = malloc(size);
        if (!has_space->view.bytes) {
            smFatal("out of memory\n");
        }
        has_space->view.len = 0;
        has_space->size     = size;
        ++in->len;
    }
    U8 *bytes = has_space->view.bytes + has_space->view.len;
    memcpy(bytes, view.bytes, view.len);
    has_space->view.len += view.len;
    return (SmView){bytes, view.len};
}

void smBufInternFini(SmBufIntern *in) { SM_INTERN_FINI_IMPL(smGBufFini); }
