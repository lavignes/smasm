#ifndef SMASM_UTF8_H
#define SMASM_UTF8_H

#include <smasm/buf.h>

U32 smUtf8Decode(SmView view, UInt *len);

UInt smUtf8Encode(SmView view, U32 c);

UInt smUtf8Len(SmView view);

void smUtf8Cat(SmGBuf *buf, U32 c);

#endif // SMASM_UTF8_H
