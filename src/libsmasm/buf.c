#include <smasm/buf.h>
#include <smasm/fatal.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

Bool smBufEqual(SmBuf lhs, SmBuf rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    if (lhs.bytes == rhs.bytes) {
        return true;
    }
    return memcmp(lhs.bytes, rhs.bytes, lhs.len) == 0;
}

Bool smBufEqualIgnoreAsciiCase(SmBuf lhs, SmBuf rhs) {
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

Bool smBufStartsWith(SmBuf buf, SmBuf prefix) {
    if (buf.len < prefix.len) {
        return false;
    }
    if (buf.bytes == prefix.bytes) {
        return true;
    }
    return memcmp(buf.bytes, prefix.bytes, prefix.len) == 0;
}

UInt smBufHash(SmBuf buf) {
    UInt hash = 5381;
    for (UInt i = 0; i < buf.len; ++i) {
        hash = ((hash << 5) + hash) + buf.bytes[i];
    }
    return hash;
}

static char const DIGITS[] = "0123456789ABCDEF";

UInt smBufParse(SmBuf buf) {
    if (buf.len == 0) {
        smFatal("empty number\n");
    }
    I32  radix = 10;
    UInt i     = 0;
    if (buf.bytes[0] == '%') {
        radix = 2;
        ++i;
    } else if (buf.bytes[0] == '$') {
        radix = 16;
        ++i;
    }
    if (i == buf.len) {
        smFatal("invalid number: " SM_BUF_FMT "\n", SM_BUF_FMT_ARG(buf));
    }
    UInt value = 0;
    for (; i < buf.len; ++i) {
        for (UInt j = 0; j < (sizeof(DIGITS) / sizeof(DIGITS[0])); ++j) {
            if (toupper(buf.bytes[i]) == DIGITS[j]) {
                if (j >= (UInt)radix) {
                    smFatal("invalid number: " SM_BUF_FMT "\n",
                            SM_BUF_FMT_ARG(buf));
                }
                value *= radix;
                value += j;
                goto next;
            }
        }
        smFatal("invalid number: " SM_BUF_FMT "\n", SM_BUF_FMT_ARG(buf));
    next:
        (void)0;
    }
    return value;
}

void smGBufCat(SmGBuf *buf, SmBuf bytes) {
    if (!buf->inner.bytes) {
        buf->inner.bytes = malloc(bytes.len);
        if (!buf->inner.bytes) {
            smFatal("out of memory\n");
        }
        buf->inner.len = 0;
        buf->size      = bytes.len;
    }
    if ((buf->inner.len + bytes.len) > buf->size) {
        buf->inner.bytes = realloc(buf->inner.bytes, buf->size + bytes.len);
        if (!buf->inner.bytes) {
            smFatal("out of memory\n");
        }
        buf->size += bytes.len;
    }
    memcpy(buf->inner.bytes + buf->inner.len, bytes.bytes, bytes.len);
    buf->inner.len += bytes.len;
}

void smGBufFini(SmGBuf *buf) {
    if (!buf->inner.bytes) {
        return;
    }
    free(buf->inner.bytes);
    memset(buf, 0, sizeof(SmGBuf));
}

void smBufGBufAdd(SmBufGBuf *buf, SmBuf item) { SM_GBUF_ADD_IMPL(); }

static UInt roundUp(UInt num) {
    num--;
    num |= num >> 1;
    num |= num >> 2;
    num |= num >> 4;
    num |= num >> 8;
    num |= num >> 16;
#if SMASM_ABI64
    num |= num >> 32;
#endif
    num++;
    return num;
}

SmBuf smBufIntern(SmBufIntern *in, SmBuf buf) {
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
            memmem(gbuf->inner.bytes, gbuf->inner.len, buf.bytes, buf.len);
        if (bytes) {
            return (SmBuf){bytes, buf.len};
        }
        if (!has_space) {
            if ((gbuf->size - gbuf->inner.len) >= buf.len) {
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
        UInt size              = uIntMax(roundUp(buf.len), 256);
        has_space              = in->bufs + in->len;
        has_space->inner.bytes = malloc(size);
        if (!has_space->inner.bytes) {
            smFatal("out of memory\n");
        }
        has_space->inner.len = 0;
        has_space->size      = size;
        ++in->len;
    }
    U8 *bytes = has_space->inner.bytes + has_space->inner.len;
    memcpy(bytes, buf.bytes, buf.len);
    has_space->inner.len += buf.len;
    return (SmBuf){bytes, buf.len};
}

void smBufInternFini(SmBufIntern *in) { SM_INTERN_FINI_IMPL(smGBufFini); }
