#ifndef SMASM_UTF8_H
#define SMASM_UTF8_H

#include <smasm/buf.h>

U32 smUtf8Decode(SmBuf buf, UInt *len);

UInt smUtf8Encode(SmBuf buf, U32 c);

UInt smUtf8Len(SmBuf buf);

void smUtf8Cat(SmGBuf *buf, U32 c);

#endif // SMASM_UTF8_H
