#ifndef STRUCT_H
#define STRUCT_H

#include <smasm/tok.h>

struct Struct {
    SmBuf     name;
    SmPos     pos;
    SmBufGBuf fields;
};
typedef struct Struct Struct;

struct StructTab {
    Struct *entries;
    UInt    len;
    UInt    size;
};
typedef struct StructTab StructTab;

Struct *structFind(SmBuf name);
void    structAdd(SmBuf name, SmPos pos, SmBufGBuf fields);

#endif // STRUCT_H
