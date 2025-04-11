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
    return memcmp(lhs.bytes, rhs.bytes, lhs.len) == 0;
}

Bool smBufEqualIgnoreAsciiCase(SmBuf lhs, SmBuf rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    for (UInt i = 0; i < lhs.len; ++i) {
        if (tolower(lhs.bytes[i]) != tolower(rhs.bytes[i])) {
            return false;
        }
    }
    return true;
}

UInt smBufHash(SmBuf buf) {
    UInt hash = 5381;
    for (UInt i = 0; i < buf.len; ++i) {
        hash = ((hash << 5) + hash) + buf.bytes[i];
    }
    return hash;
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

void smBufGBufAdd(SmBufGBuf *buf, SmBuf item) { SM_GBUF_ADD_IMPL(SmBuf); }

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
        UInt size              = smUIntMax(roundUp(buf.len), 256);
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
