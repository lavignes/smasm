#include <smasm/sym.h>

typedef struct {
    SmView name;
    I32    num;
} CfgI32Entry;

typedef struct {
    CfgI32Entry *entries;
    UInt         len;
    UInt         size;
} CfgI32Tab;

CfgI32Entry *cfgI32TabAdd(CfgI32Tab *tab, CfgI32Entry entry);
CfgI32Entry *cfgI32TabFind(CfgI32Tab *tab, SmView name);
void         cfgI32TabFini(CfgI32Tab *tab);

enum CfgInKind {
    CFG_IN_CODE,
    CFG_IN_DATA,
    CFG_IN_UNINIT,
    CFG_IN_GB_HRAM,
};

typedef struct {
    SmView    name;
    U8        kind;
    U32       align;
    Bool      size;
    U32       sizeval;
    Bool      fill;
    U8        fillval;
    SmPos     defpos;
    SmView    define;
    CfgI32Tab tags;
    SmBufBuf  files;
} CfgIn;

typedef struct {
    CfgIn *items;
    UInt   len;
} CfgInBuf;

typedef struct {
    CfgInBuf view;
    UInt     size;
} CfgInGBuf;

void cfgInGBufAdd(CfgInGBuf *buf, CfgIn item);

enum CfgOutKind {
    CFG_OUT_READONLY,
    CFG_OUT_READWRITE,
};

typedef struct {
    SmView    name;
    U32       start;
    U32       size;
    Bool      fill;
    U8        fillval;
    U8        kind;
    SmPos     defpos;
    SmView    define;
    CfgInBuf  ins;
    CfgI32Tab tags;
} CfgOut;

typedef struct {
    CfgOut *items;
    UInt    len;
} CfgOutBuf;

typedef struct {
    CfgOutBuf view;
    UInt      size;
} CfgOutGBuf;

void cfgOutGBufAdd(CfgOutGBuf *buf, CfgOut item);
