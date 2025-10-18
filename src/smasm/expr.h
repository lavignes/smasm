#ifndef EXPR_H
#define EXPR_H

#include <smasm/sym.h>

SmExprView exprEat();
SmExprView exprEatPos(SmPos *pos);
I32        exprEatSolvedPos(SmPos *pos);
U8         exprEatSolvedU8();
U16        exprEatSolvedU16();

Bool exprSolve(SmExprView buf, I32 *num);
Bool exprSolveRelative(SmExprView view, I32 *num);

Bool exprCanReprU16(I32 num);
Bool exprCanReprU8(I32 num);
Bool exprCanReprI8(I32 num);

#endif // EXPR_H
