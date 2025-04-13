#include "expr.h"
#include "macro.h"
#include "state.h"

#include <smasm/fatal.h>

#include <assert.h>

static SmExprGBuf expr_stack = {0};
static SmOpGBuf   op_stack   = {0};

static void pushExpr(SmExpr expr) { smExprGBufAdd(&expr_stack, expr); }

static U8 precedence(SmOp op) {
    if (op.unary) {
        // lowest precedence
        if (op.tok == '(') {
            return 0;
        }
        return U8_MAX;
    }
    switch (op.tok) {
    case '/':
    case '%':
    case '*':
        return 1;
    case '+':
    case '-':
        return 2;
    case SM_TOK_ASL:
    case SM_TOK_ASR:
    case SM_TOK_LSR:
        return 3;
    case '<':
    case '>':
    case SM_TOK_LTE:
    case SM_TOK_GTE:
        return 4;
    case SM_TOK_DEQ:
    case SM_TOK_NEQ:
        return 5;
    case '&':
        return 6;
    case '^':
        return 7;
    case '|':
        return 8;
    case SM_TOK_AND:
        return 9;
    case SM_TOK_OR:
        return 10;
    default:
        smUnreachable();
    }
}

static void pushApply(SmOp op) {
    // pratt parser magic
    while (op_stack.inner.len > 0) {
        --op_stack.inner.len;
        SmOp top = op_stack.inner.items[op_stack.inner.len];
        if (precedence(top) >= precedence(op)) {
            break;
        }
        pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
    }
    smOpGBufAdd(&op_stack, op);
}

static void pushApplyBinary(U32 tok) { pushApply((SmOp){tok, false}); }

static void pushApplyUnary(U32 tok) { pushApply((SmOp){tok, true}); }

SmExprBuf exprEat() {
    expr_stack.inner.len = 0;
    op_stack.inner.len   = 0;
    Bool seen_value      = false;
    UInt paren_depth     = 0;
    while (true) {
        switch (peek()) {
        case '*':
            eat();
            // * must be the relative PC
            if (!seen_value) {
                pushExpr((SmExpr){.kind = SM_EXPR_CONST, .num = getPC()});
                seen_value = true;
                continue;
            }
            pushApplyBinary('*');
            seen_value = false;
            continue;
        case SM_TOK_DSTAR:
            // ** the absolute PC
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            pushExpr((SmExpr){.kind = SM_EXPR_ADDR,
                              .addr = {sectGet()->name, getPC()}});
            seen_value = true;
            continue;
        case '+':
        case '-':
        case '^':
        case '<':
        case '>':
            // sometimes unary
            if (seen_value) {
                pushApplyBinary(peek());
            } else {
                pushApplyUnary(peek());
            }
            eat();
            seen_value = false;
            continue;
        case '!':
        case '~':
            // always unary
            pushApplyUnary(peek());
            eat();
            seen_value = false;
            continue;
        case '&':
        case SM_TOK_AND:
        case SM_TOK_OR:
        case '/':
        case '%':
        case '|':
        case SM_TOK_ASL:
        case SM_TOK_ASR:
        case SM_TOK_LSR:
        case SM_TOK_LTE:
        case SM_TOK_GTE:
        case SM_TOK_DEQ:
        case SM_TOK_NEQ:
            // binary
            if (!seen_value) {
                fatal("expected a value\n");
            }
            pushApplyBinary(peek());
            eat();
            seen_value = false;
            continue;
        case SM_TOK_NUM:
            if (seen_value) {
                fatal("expected an operator\n");
            }
            pushExpr((SmExpr){.kind = SM_EXPR_CONST, .num = tokNum()});
            eat();
            seen_value = true;
            continue;
        case '(':
            if (seen_value) {
                fatal("expected an operator\n");
            }
            ++paren_depth;
            pushApplyUnary('(');
            eat();
            seen_value = false;
            continue;
        case ')':
            if (!seen_value) {
                fatal("expected a value\n");
            }
            --paren_depth;
            while (op_stack.inner.len > 0) {
                --op_stack.inner.len;
                SmOp op = op_stack.inner.items[op_stack.inner.len];
                if (op.tok == '(') {
                    eat();
                    goto matched;
                }
                pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
            }
            fatal("unmatched parentheses\n");
        matched:
            continue;
        case SM_TOK_ID: {
            // is this a macro?
            Macro *macro = macroFind(tokBuf());
            if (macro) {
                macroInvoke(*macro);
                continue;
            }
            if (seen_value) {
                fatal("expected an operator\n");
            }
            pushExpr((SmExpr){.kind = SM_EXPR_LABEL, .lbl = tokLbl()});
            eat();
            seen_value = true;
            continue;
        }
        case SM_TOK_IDFMT:
            // TODO
            smUnimplemented("@IDFMT in expressions");
            continue;
        case SM_TOK_DEFINED: {
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            expect(SM_TOK_ID);
            SmLbl lbl = tokLbl();
            pushExpr((SmExpr){.kind = SM_EXPR_CONST,
                              .num  = (smSymTabFind(&SYMS, lbl) != NULL)});
            eat();
            seen_value = true;
            continue;
        }
        case SM_TOK_STRLEN:
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            expect(SM_TOK_STR);
            pushExpr((SmExpr){.kind = SM_EXPR_CONST, .num = tokBuf().len});
            eat();
            seen_value = true;
            continue;
        case SM_TOK_TAG: {
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            expect(SM_TOK_ID);
            SmLbl lbl = tokLbl();
            eat();
            expect(',');
            eat();
            expect(SM_TOK_STR);
            pushExpr(
                (SmExpr){.kind = SM_EXPR_TAG, .tag = {lbl, intern(tokBuf())}});
            eat();
            seen_value = true;
            continue;
        }
        default:
            if (!seen_value) {
                fatal("expected a value\n");
            }
            if (paren_depth > 0) {
                fatal("unmatched parentheses\n");
            }
            goto complete;
        }
    }
complete:
    while (op_stack.inner.len > 0) {
        // TODO: what ops would remain here?
        --op_stack.inner.len;
        SmOp op = op_stack.inner.items[op_stack.inner.len];
        pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
    }
    return smExprIntern(&EXPRS, expr_stack.inner);
}

SmExprBuf exprEatPos(SmPos *pos) {
    // advance to get the location of the expr
    peek();
    *pos = tokPos();
    return exprEat();
}

I32 exprEatSolvedPos(SmPos *pos) {
    I32 num;
    if (!exprSolve(exprEatPos(pos), &num)) {
        fatalPos(*pos, "expression must be constant\n");
    }
    return num;
}

U8 exprEatSolvedU8() {
    SmPos pos;
    I32   num = exprEatSolvedPos(&pos);
    if (!exprCanReprU8(num)) {
        fatalPos(pos, "expression does not fit in a byte: $%08X\n", num);
    }
    return (U8)num;
}

U16 exprEatSolvedU16() {
    SmPos pos;
    I32   num = exprEatSolvedPos(&pos);
    if (!exprCanReprU16(num)) {
        fatalPos(pos, "expression does not fit in a word: $%08X\n", num);
    }
    return (U16)num;
}

Bool exprSolve(SmExprBuf buf, I32 *num) {
    SmI32GBuf stack = {0};
    for (UInt i = 0; i < buf.len; ++i) {
        SmExpr *expr = buf.items + i;
        switch (expr->kind) {
        case SM_EXPR_CONST:
            smI32GBufAdd(&stack, expr->num);
            break;
        case SM_EXPR_LABEL: {
            SmSym *sym = smSymTabFind(&SYMS, expr->lbl);
            if (!sym) {
                goto fail;
            }
            I32 num;
            // yuck
            if (!exprSolve(sym->value, &num)) {
                goto fail;
            }
            smI32GBufAdd(&stack, num);
            break;
        }
        case SM_EXPR_TAG:
            goto fail; // can only solve during link
        case SM_EXPR_OP:
            while (stack.inner.len > 0) {
                --stack.inner.len;
                I32 rhs = stack.inner.items[stack.inner.len];
                if (expr->op.unary) {
                    switch (expr->op.tok) {
                    case '+':
                        smI32GBufAdd(&stack, rhs);
                        break;
                    case '-':
                        smI32GBufAdd(&stack, -rhs);
                        break;
                    case '~':
                        smI32GBufAdd(&stack, ~rhs);
                        break;
                    case '!':
                        smI32GBufAdd(&stack, !rhs);
                        break;
                    case '<':
                        smI32GBufAdd(&stack, ((U32)rhs) & 0xFF);
                        break;
                    case '>':
                        smI32GBufAdd(&stack, ((U32)rhs & 0xFF00) >> 8);
                        break;
                    case '^':
                        smI32GBufAdd(&stack, ((U32)rhs & 0xFF0000) >> 16);
                        break;
                    default:
                        smUnreachable();
                    }
                } else {
                    --stack.inner.len;
                    I32 lhs = stack.inner.items[stack.inner.len];
                    switch (expr->op.tok) {
                    case '+':
                        smI32GBufAdd(&stack, lhs + rhs);
                        break;
                    case '-':
                        smI32GBufAdd(&stack, lhs - rhs);
                        break;
                    case '*':
                        smI32GBufAdd(&stack, lhs * rhs);
                        break;
                    case '/':
                        smI32GBufAdd(&stack, lhs / rhs);
                        break;
                    case '%':
                        smI32GBufAdd(&stack, lhs % rhs);
                        break;
                    case SM_TOK_ASL:
                        smI32GBufAdd(&stack, lhs << rhs);
                        break;
                    case SM_TOK_ASR:
                        smI32GBufAdd(&stack, lhs >> rhs);
                        break;
                    case SM_TOK_LSR:
                        smI32GBufAdd(&stack, ((U32)lhs) >> ((U32)rhs));
                        break;
                    case '<':
                        smI32GBufAdd(&stack, lhs < rhs);
                        break;
                    case SM_TOK_LTE:
                        smI32GBufAdd(&stack, lhs <= rhs);
                        break;
                    case '>':
                        smI32GBufAdd(&stack, lhs > rhs);
                        break;
                    case SM_TOK_GTE:
                        smI32GBufAdd(&stack, lhs >= rhs);
                        break;
                    case SM_TOK_DEQ:
                        smI32GBufAdd(&stack, lhs == rhs);
                        break;
                    case SM_TOK_NEQ:
                        smI32GBufAdd(&stack, lhs != rhs);
                        break;
                    case '&':
                        smI32GBufAdd(&stack, lhs & rhs);
                        break;
                    case '|':
                        smI32GBufAdd(&stack, lhs | rhs);
                        break;
                    case '^':
                        smI32GBufAdd(&stack, lhs ^ rhs);
                        break;
                    case SM_TOK_AND:
                        smI32GBufAdd(&stack, lhs && rhs);
                        break;
                    case SM_TOK_OR:
                        smI32GBufAdd(&stack, lhs || rhs);
                        break;
                    default:
                        smUnreachable();
                    }
                }
            }
            break;
        case SM_EXPR_ADDR:
            if (!smBufEqual(expr->addr.sect, sectGet()->name)) {
                goto fail;
            }
            // relative offset within same section?
            smI32GBufAdd(&stack, expr->addr.pc);
            break;
        default:
            smUnreachable();
        }
    }
    assert(stack.inner.len == 1);
    *num = *stack.inner.items;
    smI32GBufFini(&stack);
    return true;
fail:
    smI32GBufFini(&stack);
    return false;
}

Bool exprCanReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }
Bool exprCanReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }
Bool exprCanReprI8(I32 num) { return (num >= I8_MIN) && (num <= I8_MAX); }
