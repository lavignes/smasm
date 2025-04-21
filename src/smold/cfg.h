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
void         cfgI32TabFini(CfgI32Tab *tab);

enum CfgInKind {
    CFG_IN_CODE,
    CFG_IN_DATA,
    CFG_IN_UNINIT,
    CFG_IN_HIGHPAGE,
};

struct CfgIn {
    SmBuf     name;
    U8        kind;
    U32       align;
    SmPos     defpos;
    SmBuf     define;
    CfgI32Tab tags;
    SmBufBuf  files;
};
typedef struct CfgIn CfgIn;

struct CfgInBuf {
    CfgIn *items;
    UInt   len;
};
typedef struct CfgInBuf CfgInBuf;

struct CfgInGBuf {
    CfgInBuf inner;
    UInt     size;
};
typedef struct CfgInGBuf CfgInGBuf;

void cfgInGBufAdd(CfgInGBuf *buf, CfgIn item);

enum CfgOutKind {
    CFG_OUT_READONLY,
    CFG_OUT_READWRITE,
};

struct CfgOut {
    SmBuf     name;
    U32       start;
    U32       size;
    Bool      fill;
    U8        fillval;
    U8        kind;
    SmPos     defpos;
    SmBuf     define;
    CfgInBuf  ins;
    CfgI32Tab tags;
};
typedef struct CfgOut CfgOut;

struct CfgOutBuf {
    CfgOut *items;
    UInt    len;
};
typedef struct CfgOutBuf CfgOutBuf;

struct CfgOutGBuf {
    CfgOutBuf inner;
    UInt      size;
};
typedef struct CfgOutGBuf CfgOutGBuf;

void cfgOutGBufAdd(CfgOutGBuf *buf, CfgOut item);
