#ifndef SMASM_UTF8_H
#define SMASM_UTF8_H

#include <smasm/buf.h>

U32 smUtf8Decode(U8 const buf[4], UInt *len);

UInt smUtf8Encode(U32 codepoint, U8 buf[4]);

#endif // SMASM_UTF8_H
