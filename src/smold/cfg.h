#include <smasm/sym.h>

typedef struct {
    SmView name;
    I32    num;
} CfgI32Entry;

typedef struct {
    CfgI32Entry *entries;
    UInt         len;
    UInt         cap;
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
    SmView     name;
    U8         kind;
    U32        align;
    Bool       size;
    U32        sizeval;
    Bool       fill;
    U8         fillval;
    SmPos      defpos;
    SmView     define;
    CfgI32Tab  tags;
    SmViewView files;
} CfgIn;

typedef struct {
    CfgIn *items;
    UInt   len;
} CfgInView;

typedef struct {
    CfgInView view;
    UInt      cap;
} CfgInBuf;

void cfgInBufAdd(CfgInBuf *buf, CfgIn item);

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
    CfgInView ins;
    CfgI32Tab tags;
} CfgOut;

typedef struct {
    CfgOut *items;
    UInt    len;
} CfgOutView;

typedef struct {
    CfgOutView view;
    UInt       cap;
} CfgOutBuf;

void cfgOutBufAdd(CfgOutBuf *buf, CfgOut item);
