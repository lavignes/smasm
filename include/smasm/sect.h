#ifndef SMASM_SECT_H
#define SMASM_SECT_H

#include <smasm/sym.h>

enum SmRelocFlags {
    SM_RELOC_HRAM = 1 << 0,
    SM_RELOC_RST  = 1 << 1,
    SM_RELOC_JP   = 1 << 2,
};

struct SmReloc {
    UInt      offset;
    U8        width;
    SmExprBuf value;
    SmBuf     unit;
    SmPos     pos;
    U8        flags;
};
typedef struct SmReloc SmReloc;

struct SmRelocBuf {
    SmReloc *items;
    UInt     len;
};
typedef struct SmRelocBuf SmRelocBuf;

struct SmRelocGBuf {
    SmRelocBuf inner;
    UInt       size;
};
typedef struct SmRelocGBuf SmRelocGBuf;

void smRelocGBufAdd(SmRelocGBuf *buf, SmReloc reloc);
void smRelocGBufFini(SmRelocGBuf *buf);

struct SmSect {
    SmBuf       name;
    U32         pc;
    SmGBuf      data;
    SmRelocGBuf relocs;
};
typedef struct SmSect SmSect;

struct SmSectBuf {
    SmSect *items;
    UInt    len;
};
typedef struct SmSectBuf SmSectBuf;

struct SmSectGBuf {
    SmSectBuf inner;
    UInt      size;
};
typedef struct SmSectGBuf SmSectGBuf;

void smSectGBufAdd(SmSectGBuf *buf, SmSect sect);
void smSectGBufFini(SmSectGBuf *buf);

#endif // SMASM_SECT_H
