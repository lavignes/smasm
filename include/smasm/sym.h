#ifndef SMASM_SYM_H
#define SMASM_SYM_H

#include <smasm/tok.h>

typedef struct {
    SmView scope;
    SmView name;
} SmLbl;

static SmLbl const SM_LBL_NULL = {0};

Bool   smLblEqual(SmLbl lhs, SmLbl rhs);
Bool   smLblIsGlobal(SmLbl lbl);
SmView smLblFullName(SmLbl lbl, SmBufIntern *in);

typedef struct {
    U32  tok;
    Bool unary;
} SmOp;

typedef struct {
    SmOp *items;
    UInt  len;
} SmOpBuf;

typedef struct {
    SmOpBuf view;
    UInt    size;
} SmOpGBuf;

void smOpGBufAdd(SmOpGBuf *buf, SmOp op);

enum SmExprKind {
    SM_EXPR_CONST,
    SM_EXPR_ADDR,
    SM_EXPR_OP,
    SM_EXPR_LABEL,
    SM_EXPR_TAG,
    SM_EXPR_REL,
};

typedef struct {
    U8 kind;
    union {
        I32   num;
        SmOp  op;
        SmLbl lbl;

        struct {
            SmView sect;
            U32    pc;
        } addr;

        struct {
            SmLbl  lbl;
            SmView name;
        } tag;
    };
} SmExpr;

typedef struct {
    SmExpr *items;
    UInt    len;
} SmExprBuf;

typedef struct {
    SmExprBuf view;
    UInt      size;
} SmExprGBuf;

void smExprGBufAdd(SmExprGBuf *buf, SmExpr expr);
void smExprGBufFini(SmExprGBuf *buf);

typedef struct {
    SmExprGBuf *bufs;
    UInt        len;
    UInt        size;
} SmExprIntern;

SmExprBuf smExprIntern(SmExprIntern *in, SmExprBuf buf);
void      smExprInternFini(SmExprIntern *in);

typedef struct {
    I32 *items;
    UInt len;
} SmI32Buf;

typedef struct {
    SmI32Buf view;
    UInt     size;
} SmI32GBuf;

void smI32GBufAdd(SmI32GBuf *buf, I32 num);
void smI32GBufFini(SmI32GBuf *buf);

enum SmSymFlags { SM_SYM_EQU = 1 << 0 };

typedef struct {
    SmLbl     lbl;
    SmExprBuf value;
    SmView    unit;
    SmView    section;
    SmPos     pos;
    U8        flags;
} SmSym;

typedef struct {
    SmSym *syms;
    UInt   len;
    UInt   size;
} SmSymTab;

SmSym *smSymTabAdd(SmSymTab *tab, SmSym sym);
SmSym *smSymTabFind(SmSymTab *tab, SmLbl lbl);
void   smSymTabFini(SmSymTab *tab);

#endif // SMASM_SYM_H
