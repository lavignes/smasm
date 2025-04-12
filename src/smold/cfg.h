#include <smasm/sym.h>

struct CfgI32Entry {
    SmBuf name;
    I32   num;
};
typedef struct CfgI32Entry CfgI32Entry;

struct CfgI32Tab {
    CfgI32Entry *entries;
    UInt         len;
    UInt         size;
};
typedef struct CfgI32Tab CfgI32Tab;

CfgI32Entry *cfgI32TabAdd(CfgI32Tab *tab, CfgI32Entry entry);
CfgI32Entry *cfgI32TabFind(CfgI32Tab *tab, SmBuf name);

enum CfgMemKind {
    CFG_MEM_READONLY,
    CFG_MEM_READWRITE,
};

struct CfgMem {
    SmBuf name;
    U16   start;
    U16   size;
    U8    fill;
    U8    kind;
};
typedef struct CfgMem CfgMem;

struct CfgMemBuf {
    CfgMem *items;
    UInt    len;
};
typedef struct CfgMemBuf CfgMemBuf;

struct CfgMemGBuf {
    CfgMemBuf inner;
    UInt      size;
};
typedef struct CfgMemGBuf CfgMemGBuf;

void cfgMemGBufAdd(CfgMemGBuf *buf, CfgMem item);

enum CfgSectKind {
    CFG_SECT_CODE,
    CFG_SECT_DATA,
    CFG_SECT_UNINIT,
    CFG_SECT_ZEROPAGE,
};

struct CfgSect {
    SmBuf     name;
    SmBuf     load;
    U8        kind;
    U16       align;
    Bool      define;
    CfgI32Tab tags;
    SmBufGBuf files;
};
typedef struct CfgSect CfgSect;

struct CfgSectBuf {
    CfgSect *items;
    UInt     len;
};
typedef struct CfgSectBuf CfgSectBuf;

struct CfgSectGBuf {
    CfgSectBuf inner;
    UInt       size;
};
typedef struct CfgSectGBuf CfgSectGBuf;

void cfgSectGrowBufAdd(CfgSectGBuf *buf, CfgSect item);
