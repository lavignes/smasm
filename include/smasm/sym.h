#ifndef SMASM_SYM_H
#define SMASM_SYM_H

#include <smasm/tok.h>

struct SmLbl {
    SmBuf scope;
    SmBuf name;
};
typedef struct SmLbl SmLbl;

Bool smLblEqual(SmLbl lhs, SmLbl rhs);
Bool smLblIsGlobal(SmLbl lbl);

struct SmOp {
    U32  tok;
    Bool unary;
};
typedef struct SmOp SmOp;

struct SmOpBuf {
    SmOp *items;
    UInt  len;
};
typedef struct SmOpBuf SmOpBuf;

struct SmOpGBuf {
    SmOpBuf inner;
    UInt    size;
};
typedef struct SmOpGBuf SmOpGBuf;

void smOpGBufAdd(SmOpGBuf *buf, SmOp op);

enum SmExprKind {
    SM_EXPR_CONST,
    SM_EXPR_ADDR,
    SM_EXPR_OP,
    SM_EXPR_LABEL,
    SM_EXPR_TAG,
};

struct SmExpr {
    U8 kind;
    union {
        I32   num;
        SmOp  op;
        SmLbl lbl;

        struct {
            SmBuf sect;
            U32   pc;
        } addr;

        struct {
            SmLbl lbl;
            SmBuf name;
        } tag;
    };
};
typedef struct SmExpr SmExpr;

struct SmExprBuf {
    SmExpr *items;
    UInt    len;
};
typedef struct SmExprBuf SmExprBuf;

struct SmExprGBuf {
    SmExprBuf inner;
    UInt      size;
};
typedef struct SmExprGBuf SmExprGBuf;

void smExprGBufAdd(SmExprGBuf *buf, SmExpr expr);

struct SmExprIntern {
    SmExprGBuf *bufs;
    UInt        len;
    UInt        size;
};
typedef struct SmExprIntern SmExprIntern;

SmExprBuf smExprIntern(SmExprIntern *in, SmExprBuf buf);

struct SmI32Buf {
    I32 *items;
    UInt len;
};
typedef struct SmI32Buf SmI32Buf;

struct SmI32GBuf {
    SmI32Buf inner;
    UInt     size;
};
typedef struct SmI32GBuf SmI32GBuf;

void smI32GBufAdd(SmI32GBuf *buf, I32 num);
void smI32GBufFini(SmI32GBuf *buf);

enum SmSymFlags { SM_SYM_EQU = 1 << 0 };

struct SmSym {
    SmLbl     lbl;
    SmExprBuf value;
    SmBuf     unit;
    SmBuf     section;
    SmPos     pos;
    U8        flags;
};
typedef struct SmSym SmSym;

struct SmSymTab {
    SmSym *syms;
    UInt   len;
    UInt   size;
};
typedef struct SmSymTab SmSymTab;

SmSym *smSymTabAdd(SmSymTab *tab, SmSym sym);
SmSym *smSymTabFind(SmSymTab *tab, SmLbl lbl);

#endif // SMASM_SYM_H
