#ifndef STRUCT_H
#define STRUCT_H

#include <smasm/tok.h>

typedef struct {
    SmView    name;
    SmPos     pos;
    SmViewBuf fields;
} Struct;

typedef struct {
    Struct *entries;
    UInt    len;
    UInt    cap;
} StructTab;

Struct *structFind(SmView name);
void    structAdd(SmView name, SmPos pos, SmViewBuf fields);

#endif // STRUCT_H
