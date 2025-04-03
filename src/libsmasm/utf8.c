#include <smasm/utf8.h>

#include <stdlib.h>

U32 smUtf8Decode(U8 const buf[4], UInt *len) {
    if ((buf[0] & 0x80) == 0x00) {
        if (len != NULL) {
            *len = 1;
        }
        return buf[0];
    }
    if (((buf[0] & 0xE0) == 0xC0) && ((buf[1] & 0xC0) == 0x80)) {
        if (len != NULL) {
            *len = 2;
        }
        return (((U32)(buf[0] & 0x1F)) << 6) | (buf[1] & 0x3F);
    }
    if (((buf[0] & 0xF0) == 0xE0) && ((buf[1] & 0xC0) == 0x80) &&
        ((buf[2] & 0xC0) == 0x80)) {
        if (len != NULL) {
            *len = 3;
        }
        return (((U32)(buf[0] & 0x0F)) << 12) | (((U32)(buf[1] & 0x3F)) << 6) |
               (buf[2] & 0x3F);
    }
    if (((buf[0] & 0xF8) == 0xF0) && ((buf[1] & 0xC0) == 0x80) &&
        ((buf[2] & 0xC0) == 0x80) && ((buf[3] & 0xC0) == 0x80)) {
        if (len != NULL) {
            *len = 4;
        }
        return (((U32)(buf[0] & 0x0F)) << 18) | (((U32)(buf[1] & 0x3F)) << 12) |
               (((U32)(buf[2] & 0x3F)) << 6) | ((U32)(buf[3] & 0x3F));
    }
    if (len != NULL) {
        *len = 0;
    }
    return 0xFFFD;
}

UInt smUtf8Encode(U32 codepoint, U8 buf[4]);
