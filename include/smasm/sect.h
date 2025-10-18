#ifndef SMASM_SECT_H
#define SMASM_SECT_H

#include <smasm/sym.h>

enum SmRelocFlags {
    SM_RELOC_HRAM = 1 << 0,
    SM_RELOC_RST  = 1 << 1,
    SM_RELOC_JP   = 1 << 2,
};

typedef struct {
    UInt      offset;
    U8        width;
    SmExprBuf value;
    SmView    unit;
    SmPos     pos;
    U8        flags;
} SmReloc;

typedef struct {
    SmReloc *items;
    UInt     len;
} SmRelocBuf;

typedef struct {
    SmRelocBuf view;
    UInt       size;
} SmRelocGBuf;

void smRelocGBufAdd(SmRelocGBuf *buf, SmReloc reloc);
void smRelocGBufFini(SmRelocGBuf *buf);

typedef struct {
    SmView      name;
    U32         pc;
    SmGBuf      data;
    SmRelocGBuf relocs;
} SmSect;

typedef struct {
    SmSect *items;
    UInt    len;
} SmSectBuf;

typedef struct {
    SmSectBuf view;
    UInt      size;
} SmSectGBuf;

void smSectGBufAdd(SmSectGBuf *buf, SmSect sect);
void smSectGBufFini(SmSectGBuf *buf);

#endif // SMASM_SECT_H
