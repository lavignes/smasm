#include <smasm/fatal.h>
#include <smasm/utf8.h>

U32 smUtf8Decode(SmView view, UInt *len) {
    if ((view.len > 0) && (view.bytes[0] < 0x80)) {
        *len = 1;
        return view.bytes[0];
    }
    if ((view.len > 1) && ((view.bytes[0] & 0xE0) == 0xC0)) {
        U32 c = view.bytes[0] & 0x1F;
        c     = (c << 6) | (view.bytes[1] & 0x3F);
        *len  = 2;
        return c;
    }
    if ((view.len > 2) && ((view.bytes[0] & 0xF0) == 0xE0)) {
        U32 c = view.bytes[0] & 0x0F;
        c     = (c << 6) | (view.bytes[1] & 0x3F);
        c     = (c << 6) | (view.bytes[2] & 0x3F);
        *len  = 3;
        return c;
    }
    if ((view.len > 3) && ((view.bytes[0] & 0xF8) == 0xF0)) {
        U32 c = view.bytes[0] & 0x07;
        c     = (c << 6) | (view.bytes[1] & 0x3F);
        c     = (c << 6) | (view.bytes[2] & 0x3F);
        c     = (c << 6) | (view.bytes[3] & 0x3F);
        *len  = 4;
        return c;
    }
    *len = 0;
    return 0xFFFD;
}

UInt smUtf8Encode(SmView view, U32 c) {
    if (c < 0x80) {
        view.bytes[0] = c;
        return 1;
    }
    if (c < 0x800) {
        view.bytes[0] = 0xC0 | (c >> 6);
        view.bytes[1] = 0x80 | (c & 0x3F);
        return 2;
    }
    if (c < 0x10000) {
        view.bytes[0] = 0xE0 | (c >> 12);
        view.bytes[1] = 0x80 | ((c >> 6) & 0x3F);
        view.bytes[2] = 0x80 | (c & 0x3F);
        return 3;
    }
    if (c < 0x200000) {
        view.bytes[0] = 0xF0 | (c >> 18);
        view.bytes[1] = 0x80 | ((c >> 12) & 0x3F);
        view.bytes[2] = 0x80 | ((c >> 6) & 0x3F);
        view.bytes[3] = 0x80 | (c & 0x3F);
        return 4;
    }
    return 0;
}

UInt smUtf8Len(SmView view) {
    UInt len = 0;
    for (UInt i = 0; i < view.len; ++i) {
        if ((view.bytes[i] & 0xC0) != 0x80) {
            ++len;
        }
    }
    return len;
}

void smUtf8Cat(SmBuf *buf, U32 c) {
    U8   tmp[4];
    UInt len = smUtf8Encode((SmView){tmp, 4}, c);
    if (len == 0) {
        smFatal("invalid UTF-8 codepoint");
    }
    smBufCat(buf, (SmView){tmp, len});
}
