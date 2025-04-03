#ifndef SMASM_UTF8_H
#define SMASM_UTF8_H

#include <smasm/buf.h>

U32 smUtf8Decode(U8 const buf[4], UInt *len);

UInt smUtf8Encode(U32 c, U8 buf[4]);

UInt smUtf8Len(SmBuf buf);

#endif // SMASM_UTF8_H
