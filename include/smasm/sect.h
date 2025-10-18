#ifndef SMASM_SECT_H
#define SMASM_SECT_H

#include <smasm/sym.h>

enum SmRelocFlags {
    SM_RELOC_HRAM = 1 << 0,
    SM_RELOC_RST  = 1 << 1,
    SM_RELOC_JP   = 1 << 2,
};

typedef struct {
    UInt       offset;
    U8         width;
    SmExprView value;
    SmView     unit;
    SmPos      pos;
    U8         flags;
} SmReloc;

typedef struct {
    SmReloc *items;
    UInt     len;
} SmRelocView;

typedef struct {
    SmRelocView view;
    UInt        cap;
} SmRelocBuf;

void smRelocBufAdd(SmRelocBuf *buf, SmReloc reloc);
void smRelocBufFini(SmRelocBuf *buf);

typedef struct {
    SmView     name;
    U32        pc;
    SmBuf      data;
    SmRelocBuf relocs;
} SmSect;

typedef struct {
    SmSect *items;
    UInt    len;
} SmSectView;

typedef struct {
    SmSectView view;
    UInt       cap;
} SmSectBuf;

void smSectBufAdd(SmSectBuf *buf, SmSect sect);
void smSectBufFini(SmSectBuf *buf);

#endif // SMASM_SECT_H
