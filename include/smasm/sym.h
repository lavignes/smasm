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
SmView smLblFullName(SmLbl lbl, SmViewIntern *in);

typedef struct {
    U32  tok;
    Bool unary;
} SmOp;

typedef struct {
    SmOp *items;
    UInt  len;
} SmOpView;

typedef struct {
    SmOpView view;
    UInt     cap;
} SmOpBuf;

void smOpBufAdd(SmOpBuf *buf, SmOp op);

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
} SmExprView;

typedef struct {
    SmExprView view;
    UInt       cap;
} SmExprBuf;

void smExprBufAdd(SmExprBuf *buf, SmExpr expr);
void smExprBufFini(SmExprBuf *buf);

typedef struct {
    SmExprBuf *bufs;
    UInt       len;
    UInt       cap;
} SmExprIntern;

SmExprView smExprIntern(SmExprIntern *in, SmExprView view);
void       smExprInternFini(SmExprIntern *in);

typedef struct {
    I32 *items;
    UInt len;
} SmI32View;

typedef struct {
    SmI32View view;
    UInt      cap;
} SmI32Buf;

void smI32BufAdd(SmI32Buf *buf, I32 num);
void smI32BufFini(SmI32Buf *buf);

enum SmSymFlags { SM_SYM_EQU = 1 << 0 };

typedef struct {
    SmLbl      lbl;
    SmExprView value;
    SmView     unit;
    SmView     section;
    SmPos      pos;
    U8         flags;
} SmSym;

typedef struct {
    SmSym *syms;
    UInt   len;
    UInt   cap;
} SmSymTab;

SmSym *smSymTabAdd(SmSymTab *tab, SmSym sym);
SmSym *smSymTabFind(SmSymTab *tab, SmLbl lbl);
void   smSymTabFini(SmSymTab *tab);

#endif // SMASM_SYM_H
