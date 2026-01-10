#include "expr.h"
#include "state.h"

#include <smasm/fatal.h>

#include <assert.h>

static SmExprBuf expr_stack = {0};
static SmOpBuf   op_stack   = {0};

static void pushExpr(SmExpr expr) { smExprBufAdd(&expr_stack, expr); }

static U8 precedence(SmOp op) {
    if (op.unary) {
        return 0;
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
        SM_UNREACHABLE();
    }
}

static void pushApply(SmOp op) {
    // pratt parser magic
    if (op.tok == '(') {
        smOpBufAdd(&op_stack, op);
        return;
    }
    while (op_stack.view.len > 0) {
        --op_stack.view.len;
        SmOp top = op_stack.view.items[op_stack.view.len];
        if (precedence(top) >= precedence(op)) {
            smOpBufAdd(&op_stack, top);
            break;
        }
        pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = top});
    }
    smOpBufAdd(&op_stack, op);
}

static void pushApplyBinary(U32 tok) { pushApply((SmOp){tok, false}); }

static void pushApplyUnary(U32 tok) { pushApply((SmOp){tok, true}); }

SmExprView exprEat() {
    expr_stack.view.len = 0;
    op_stack.view.len   = 0;
    Bool seen_value     = false;
    UInt paren_depth    = 0;
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
            smOpBufAdd(&op_stack, (SmOp){'(', true});
            eat();
            seen_value = false;
            continue;
        case ')':
            if (!seen_value) {
                fatal("expected a value\n");
            }
            --paren_depth;
            while (true) {
                if (op_stack.view.len == 0) {
                    fatal("unmatched parentheses\n");
                }
                --op_stack.view.len;
                SmOp op = op_stack.view.items[op_stack.view.len];
                if (op.tok == '(') {
                    break;
                }
                pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
            }
            eat();
            continue;
        case SM_TOK_ID: {
            if (seen_value) {
                fatal("expected an operator\n");
            }
            pushExpr((SmExpr){.kind = SM_EXPR_LABEL, .lbl = tokLbl()});
            eat();
            seen_value = true;
            continue;
        }
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
            pushExpr((SmExpr){.kind = SM_EXPR_CONST, .num = tokView().len});
            eat();
            seen_value = true;
            continue;
        case SM_TOK_TAG: {
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            Bool braced = false;
            if (peek() == '{') {
                eat();
                braced = true;
            }
            expect(SM_TOK_ID);
            SmLbl lbl = tokLbl();
            eat();
            expect(',');
            eat();
            expect(SM_TOK_STR);
            pushExpr(
                (SmExpr){.kind = SM_EXPR_TAG, .tag = {lbl, intern(tokView())}});
            eat();
            if (braced) {
                expect('}');
                eat();
            }
            seen_value = true;
            continue;
        }
        case SM_TOK_REL:
            if (seen_value) {
                fatal("expected an operator\n");
            }
            eat();
            expect(SM_TOK_ID);
            pushExpr((SmExpr){.kind = SM_EXPR_REL, .lbl = tokLbl()});
            eat();
            seen_value = true;
            continue;
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
    while (op_stack.view.len > 0) {
        --op_stack.view.len;
        SmOp op = op_stack.view.items[op_stack.view.len];
        pushExpr((SmExpr){.kind = SM_EXPR_OP, .op = op});
    }
    return smExprIntern(&EXPRS, expr_stack.view);
}

SmExprView exprEatPos(SmPos *pos) {
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

static Bool exprSolveFull(SmExprView view, I32 *num, Bool relative) {
    SmI32Buf stack = {0};
    for (UInt i = 0; i < view.len; ++i) {
        SmExpr *expr = view.items + i;
        switch (expr->kind) {
        case SM_EXPR_CONST:
            smI32BufAdd(&stack, expr->num);
            break;
        case SM_EXPR_LABEL: {
            SmSym *sym = smSymTabFind(&SYMS, expr->lbl);
            if (!sym) {
                goto fail;
            }
            I32 num;
            // yuck
            if (!exprSolveFull(sym->value, &num, relative)) {
                goto fail;
            }
            smI32BufAdd(&stack, num);
            break;
        }
        case SM_EXPR_TAG:
            goto fail; // can only solve during link
        case SM_EXPR_OP:
            --stack.view.len;
            I32 rhs = stack.view.items[stack.view.len];
            if (expr->op.unary) {
                switch (expr->op.tok) {
                case '+':
                    smI32BufAdd(&stack, rhs);
                    break;
                case '-':
                    smI32BufAdd(&stack, -rhs);
                    break;
                case '~':
                    smI32BufAdd(&stack, ~rhs);
                    break;
                case '!':
                    smI32BufAdd(&stack, !rhs);
                    break;
                case '<':
                    smI32BufAdd(&stack, ((U32)rhs) & 0xFF);
                    break;
                case '>':
                    smI32BufAdd(&stack, ((U32)rhs & 0xFF00) >> 8);
                    break;
                case '^':
                    smI32BufAdd(&stack, ((U32)rhs & 0xFF0000) >> 16);
                    break;
                default:
                    SM_UNREACHABLE();
                }
            } else {
                --stack.view.len;
                I32 lhs = stack.view.items[stack.view.len];
                switch (expr->op.tok) {
                case '+':
                    smI32BufAdd(&stack, lhs + rhs);
                    break;
                case '-':
                    smI32BufAdd(&stack, lhs - rhs);
                    break;
                case '*':
                    smI32BufAdd(&stack, lhs * rhs);
                    break;
                case '/':
                    smI32BufAdd(&stack, lhs / rhs);
                    break;
                case '%':
                    smI32BufAdd(&stack, lhs % rhs);
                    break;
                case SM_TOK_ASL:
                    smI32BufAdd(&stack, lhs << rhs);
                    break;
                case SM_TOK_ASR:
                    smI32BufAdd(&stack, lhs >> rhs);
                    break;
                case SM_TOK_LSR:
                    smI32BufAdd(&stack, ((U32)lhs) >> ((U32)rhs));
                    break;
                case '<':
                    smI32BufAdd(&stack, lhs < rhs);
                    break;
                case SM_TOK_LTE:
                    smI32BufAdd(&stack, lhs <= rhs);
                    break;
                case '>':
                    smI32BufAdd(&stack, lhs > rhs);
                    break;
                case SM_TOK_GTE:
                    smI32BufAdd(&stack, lhs >= rhs);
                    break;
                case SM_TOK_DEQ:
                    smI32BufAdd(&stack, lhs == rhs);
                    break;
                case SM_TOK_NEQ:
                    smI32BufAdd(&stack, lhs != rhs);
                    break;
                case '&':
                    smI32BufAdd(&stack, lhs & rhs);
                    break;
                case '|':
                    smI32BufAdd(&stack, lhs | rhs);
                    break;
                case '^':
                    smI32BufAdd(&stack, lhs ^ rhs);
                    break;
                case SM_TOK_AND:
                    smI32BufAdd(&stack, lhs && rhs);
                    break;
                case SM_TOK_OR:
                    smI32BufAdd(&stack, lhs || rhs);
                    break;
                default:
                    SM_UNREACHABLE();
                }
            }
            break;
        case SM_EXPR_ADDR:
            // absolute addresses can only be solved at link time
            if (!smViewEqual(expr->addr.sect, sectGet()->name)) {
                goto fail;
            }
            if (!relative) {
                goto fail;
            }
            smI32BufAdd(&stack, expr->addr.pc);
            break;
        case SM_EXPR_REL: {
            SmSym *sym = smSymTabFind(&SYMS, expr->lbl);
            if (!sym) {
                goto fail;
            }
            I32 num;
            // yuck
            if (!exprSolveFull(sym->value, &num, true)) {
                goto fail;
            }
            smI32BufAdd(&stack, num);
            break;
        }
        default:
            SM_UNREACHABLE();
        }
    }
    assert(stack.view.len == 1);
    *num = *stack.view.items;
    smI32BufFini(&stack);
    return true;
fail:
    smI32BufFini(&stack);
    return false;
}

Bool exprSolve(SmExprView view, I32 *num) {
    return exprSolveFull(view, num, false);
}

Bool exprSolveRelative(SmExprView view, I32 *num) {
    return exprSolveFull(view, num, true);
}

Bool exprCanReprU16(I32 num) { return (num >= 0) && (num <= U16_MAX); }
Bool exprCanReprU8(I32 num) { return (num >= 0) && (num <= U8_MAX); }
Bool exprCanReprI8(I32 num) { return (num >= I8_MIN) && (num <= I8_MAX); }
