#ifndef SMASM_BUF_H
#define SMASM_BUF_H

#include <smasm/abi.h>

struct SmBuf {
    U8  *bytes;
    UInt len;
};
typedef struct SmBuf SmBuf;

#define SM_BUF(cstr) ((SmBuf){(U8 *)(cstr), (sizeof((cstr)) - 1)})

#endif // SMASM_BUF_H
