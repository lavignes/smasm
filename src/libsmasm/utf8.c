#include <smasm/utf8.h>

U32 smUtf8Decode(U8 const buf[4], UInt *len) {
    if (buf[0] < 0x80) {
        *len = 1;
        return buf[0];
    }
    if ((buf[0] & 0xE0) == 0xC0) {
        U32 c = buf[0] & 0x1F;
        c     = (c << 6) | (buf[1] & 0x3F);
        *len  = 2;
        return c;
    }
    if ((buf[0] & 0xF0) == 0xE0) {
        U32 c = buf[0] & 0x0F;
        c     = (c << 6) | (buf[1] & 0x3F);
        c     = (c << 6) | (buf[2] & 0x3F);
        *len  = 3;
        return c;
    }
    if ((buf[0] & 0xF8) == 0xF0) {
        U32 c = buf[0] & 0x07;
        c     = (c << 6) | (buf[1] & 0x3F);
        c     = (c << 6) | (buf[2] & 0x3F);
        c     = (c << 6) | (buf[3] & 0x3F);
        *len  = 4;
        return c;
    }
    *len = 0;
    return 0xFFFD;
}

UInt smUtf8Encode(U32 c, U8 buf[4]) {
    if (c < 0x7F) {
        buf[0] = c;
        return 1;
    }
    if (c <= 0x7FF) {
        buf[0] = 0xC0 | (c >> 6);
        buf[1] = 0x80 | (c & 0x3F);
        return 2;
    }
    if (c <= 0xFFFF) {
        buf[0] = 0xE0 | (c >> 12);
        buf[1] = 0x80 | ((c >> 6) & 0x3F);
        buf[2] = 0x80 | (c & 0x3F);
        return 3;
    }
    if (c <= 0x10FFFF) {
        buf[0] = 0xF0 | (c >> 18);
        buf[1] = 0x80 | ((c >> 12) & 0x3F);
        buf[2] = 0x80 | ((c >> 6) & 0x3F);
        buf[3] = 0x80 | (c & 0x3F);
        return 4;
    }
    return 0;
}

UInt smUtf8Len(SmBuf buf) {
    UInt len = 0;
    for (UInt i = 0; i < buf.len; ++len) {
        if (buf.bytes[i] < 0x80) {
            ++i;
            continue;
        }
        if ((buf.bytes[i] & 0xE0) == 0xC0) {
            i += 2;
            continue;
        }
        if ((buf.bytes[i] & 0xF0) == 0xE0) {
            i += 3;
            continue;
        }
        if ((buf.bytes[i] & 0xF8) == 0xF0) {
            i += 4;
            continue;
        }
    }
    return len;
}
