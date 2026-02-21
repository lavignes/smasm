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
        smFatal("invalid number: %" SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
    }
    UInt value = 0;
    for (; i < view.len; ++i) {
        for (UInt j = 0; j < (sizeof(DIGITS) / sizeof(DIGITS[0])); ++j) {
            if (toupper(view.bytes[i]) == DIGITS[j]) {
                if (j >= (UInt)radix) {
                    smFatal("invalid number: %" SM_VIEW_FMT "\n",
                            SM_VIEW_FMT_ARG(view));
                }
                value *= radix;
                value += j;
                goto next;
            }
        }
        smFatal("invalid number: %" SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(view));
    next:
        (void)0;
    }
    return value;
}

void smBufCat(SmBuf *buf, SmView view) {
    if (!buf->view.bytes) {
        buf->view.bytes = malloc(view.len);
        if (!buf->view.bytes) {
            smFatal("out of memory\n");
        }
        buf->view.len = 0;
        buf->cap      = view.len;
    }
    if ((buf->view.len + view.len) > buf->cap) {
        buf->view.bytes = realloc(buf->view.bytes, buf->cap + view.len);
        if (!buf->view.bytes) {
            smFatal("out of memory\n");
        }
        buf->cap += view.len;
    }
    memcpy(buf->view.bytes + buf->view.len, view.bytes, view.len);
    buf->view.len += view.len;
}

void smBufFini(SmBuf *buf) {
    if (!buf->view.bytes) {
        return;
    }
    free(buf->view.bytes);
    memset(buf, 0, sizeof(SmBuf));
}

void smViewBufAdd(SmViewBuf *buf, SmView item) { SM_BUF_ADD_IMPL(); }

void smViewBufFini(SmViewBuf *buf) { SM_BUF_FINI_IMPL(); }

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

SmView smViewIntern(SmViewIntern *in, SmView view) {
    if (!in->bufs) {
        in->bufs = malloc(sizeof(SmBuf) * 16);
        if (!in->bufs) {
            smFatal("out of memory\n");
        }
        in->len = 0;
        in->cap = 16;
    }
    SmBuf *has_space = NULL;
    for (UInt i = 0; i < in->len; ++i) {
        SmBuf *buf = in->bufs + i;
        U8    *bytes =
            memmem(buf->view.bytes, buf->view.len, view.bytes, view.len);
        if (bytes) {
            return (SmView){bytes, view.len};
        }
        if (!has_space) {
            if ((buf->cap - buf->view.len) >= view.len) {
                has_space = buf;
            }
        }
    }
    if (!has_space) {
        if ((in->cap - in->len) == 0) {
            in->bufs = realloc(in->bufs, sizeof(SmBuf) * in->cap * 2);
            if (!in->bufs) {
                smFatal("out of memory\n");
            }
            in->cap *= 2;
        }
        UInt cap              = uIntMax(roundUp(view.len), 256);
        has_space             = in->bufs + in->len;
        has_space->view.bytes = malloc(cap);
        if (!has_space->view.bytes) {
            smFatal("out of memory\n");
        }
        has_space->view.len = 0;
        has_space->cap      = cap;
        ++in->len;
    }
    U8 *bytes = has_space->view.bytes + has_space->view.len;
    memcpy(bytes, view.bytes, view.len);
    has_space->view.len += view.len;
    return (SmView){bytes, view.len};
}

void smViewInternFini(SmViewIntern *in) { SM_INTERN_FINI_IMPL(smBufFini); }
