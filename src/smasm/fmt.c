#include "fmt.h"
#include "expr.h"
#include "state.h"

#include <smasm/fatal.h>
#include <smasm/utf8.h>

#include <ctype.h>
#include <stdlib.h>

enum FmtState {
    FMT_STATE_INIT,
    FMT_STATE_FLAG_OPT,
    FMT_STATE_WIDTH_OPT,
    FMT_STATE_PREC_DOT_OPT,
    FMT_STATE_PREC_OPT,
    FMT_STATE_SPEC,
};

enum FmtFlags {
    FMT_FLAG_JUSTIFY_LEFT = 1 << 0,
    FMT_FLAG_FORCE_SIGN   = 1 << 1,
    FMT_FLAG_PAD_SIGN     = 1 << 2,
    FMT_FLAG_NUM_MOD      = 1 << 3,
    FMT_FLAG_ZERO_JUSTIFY = 1 << 4,
    // technically not a format flag, but easier to repr this way
    FMT_FLAG_UPPERCASE    = 1 << 5,
};

static const U8 DIGITS[]       = "0123456789abcdef";
static const U8 DIGITS_UPPER[] = "0123456789ABCDEF";

void fmtUInt(SmBuf *buf, I32 num, I32 radix, U8 flags, U16 width, U16 prec,
             Bool negative) {
    // write digits to a small buffer (at least big enough to hold 32
    // bits of binary)
    U8        numbytes[32];
    U8       *end    = numbytes + 32;
    U8 const *digits = DIGITS;
    if (flags & FMT_FLAG_UPPERCASE) {
        digits = DIGITS_UPPER;
    }
    do {
        *(--end) = digits[num % radix];
        num /= radix;
    } while (num);
    SmView nums = {end, (numbytes + 32) - end};
    prec        = uIntMax(prec, nums.len);
    UInt len    = uIntMax(width, prec);
    if (negative || (flags & (FMT_FLAG_FORCE_SIGN | FMT_FLAG_PAD_SIGN))) {
        ++len;
    }
    UInt i   = 0;
    UInt pad = len - prec;
    // if we are not left-justifying, then we will write padding
    if (!(flags & FMT_FLAG_JUSTIFY_LEFT)) {
        U8 c = ' ';
        if (flags & FMT_FLAG_ZERO_JUSTIFY) {
            c = '0';
        }
        for (; i < pad; ++i) {
            smBufCat(buf, (SmView){&c, 1});
        }
    }
    // write sign
    if (i < len) {
        if (negative) {
            smBufCat(buf, SM_VIEW("-"));
        } else if (flags & FMT_FLAG_PAD_SIGN) {
            smBufCat(buf, SM_VIEW(" "));
        } else if (flags & FMT_FLAG_FORCE_SIGN) {
            smBufCat(buf, SM_VIEW("+"));
        }
        ++i;
    }
    // add leading zeros to reach the desired precision
    for (pad = prec - nums.len; pad > 0; --pad) {
        smBufCat(buf, SM_VIEW("0"));
        ++i;
    }
    // write the actual number
    smBufCat(buf, nums);
    i += nums.len;
    // write out any leftover padding
    for (; i < len; ++i) {
        smBufCat(buf, SM_VIEW(" "));
    }
}

static void fmtStr(SmBuf *buf, SmView str, U8 flags, U16 width, U16 prec) {
    UInt len = 0;
    if (prec == 0) {
        prec = str.len;
    }
    len += uIntMax(width, prec);
    UInt i   = 0;
    UInt pad = len - prec;
    // if we are not left-justifying, then we will write padding
    if (!(flags & FMT_FLAG_JUSTIFY_LEFT)) {
        U8 c = ' ';
        if (flags & FMT_FLAG_ZERO_JUSTIFY) {
            c = '0';
        }
        for (; i < pad; ++i) {
            smBufCat(buf, (SmView){&c, 1});
        }
    }
    // write the str
    smBufCat(buf, (SmView){str.bytes, uIntMin(prec, str.len)});
    i += uIntMin(prec, str.len);
    // write out any leftover padding
    for (; i < len; ++i) {
        smBufCat(buf, SM_VIEW(" "));
    }
}

static void fmtInt(SmBuf *buf, I32 num, I32 radix, U8 flags, U16 width,
                   U8 prec) {
    Bool negative = false;
    if (num < 0) {
        num      = -num;
        negative = true;
    }
    fmtUInt(buf, num, radix, flags, width, prec, negative);
}

static UInt scanDigits(SmView fmt, U16 *num) {
    UInt len;
    for (len = 0; len < fmt.len; ++len) {
        if (!isdigit(fmt.bytes[len])) {
            break;
        }
    }
    I32 bignum = smViewParse((SmView){fmt.bytes, len});
    if (!exprCanReprU16(bignum)) {
        fatal("expression does not fit in a word: $%08X\n", bignum);
    }
    *num = (U16)bignum;
    return len;
}

void fmtInvoke(U32 tok) {
    SmPos pos = tokPos();
    eat();
    Bool braced = false;
    if (peek() == '{') {
        eat();
        braced = true;
    }
    expect(SM_TOK_STR);
    SmBuf fmt = {0};
    smBufCat(&fmt, tokView());
    eat();
    SmBuf buf      = {0};
    U8    stack[6] = {FMT_STATE_INIT};
    U8    top      = 0;
    U8    flags    = 0;
    U16   width    = 0;
    U16   prec     = 0;
    for (UInt i = 0; i < fmt.view.len; ++i) {
        U8 c = fmt.view.bytes[i];
        switch (stack[top]) {
        case FMT_STATE_INIT:
            if (c == '%') {
                flags        = 0;
                width        = 0;
                prec         = 0;
                stack[++top] = FMT_STATE_SPEC;
                stack[++top] = FMT_STATE_PREC_DOT_OPT;
                stack[++top] = FMT_STATE_WIDTH_OPT;
                stack[++top] = FMT_STATE_FLAG_OPT;
                break;
            }
            smBufCat(&buf, (SmView){&c, 1});
            break;
        case FMT_STATE_FLAG_OPT:
            switch (c) {
            case '%':
                smBufCat(&buf, SM_VIEW("%"));
                top = 0;
                break;
            case '-':
                flags |= FMT_FLAG_JUSTIFY_LEFT;
                break;
            case '+':
                flags |= FMT_FLAG_FORCE_SIGN;
                break;
            case ' ':
                flags |= FMT_FLAG_PAD_SIGN;
                break;
            case '#':
                flags |= FMT_FLAG_NUM_MOD;
                break;
            case '0':
                flags |= FMT_FLAG_ZERO_JUSTIFY;
                break;
            default:
                --top;
                --i;
            }
            break;
        case FMT_STATE_WIDTH_OPT:
            --top;
            if (c == '*') {
                expect(',');
                eat();
                width = exprEatSolvedU16();
            } else if (isdigit(c)) {
                i += scanDigits((SmView){fmt.view.bytes + i, fmt.view.len - i},
                                &width) -
                     1;
            } else {
                --i;
            }
            break;
        case FMT_STATE_PREC_DOT_OPT:
            --top;
            if (c == '.') {
                stack[++top] = FMT_STATE_PREC_OPT;
            } else {
                --i;
            }
            break;
        case FMT_STATE_PREC_OPT:
            --top;
            if (c == '*') {
                expect(',');
                eat();
                prec = exprEatSolvedU16();
            } else if (isdigit(c)) {
                i += scanDigits((SmView){fmt.view.bytes + i, fmt.view.len - i},
                                &width) -
                     1;
            } else {
                --i;
            }
            break;
        case FMT_STATE_SPEC: {
            --top;
            SmPos expr_pos;
            expect(',');
            eat();
            switch (c) {
            case 'c': {
                U32 c = exprEatSolvedPos(&expr_pos);
                smUtf8Cat(&buf, c);
                break;
            }
            case 'b':
                fmtUInt(&buf, exprEatSolvedPos(&expr_pos), 2, flags, width,
                        prec, false);
                break;
            case 'd':
            case 'i':
                fmtInt(&buf, exprEatSolvedPos(&expr_pos), 10, flags, width,
                       prec);
                break;
            case 'u':
                fmtUInt(&buf, exprEatSolvedPos(&expr_pos), 10, flags, width,
                        prec, false);
                break;
            case 'X':
                flags |= FMT_FLAG_UPPERCASE;
                // fall through
            case 'x':
                fmtUInt(&buf, exprEatSolvedPos(&expr_pos), 16, flags, width,
                        prec, false);
                break;
            case 's':
                if ((peek() != SM_TOK_STR) && (peek() != SM_TOK_ID)) {
                    fatal("expected string or identifier\n");
                }
                fmtStr(&buf, tokView(), flags, width, prec);
                eat();
                break;
            default:
                fatalPos(pos, "unrecognized format conversion: %c\n", c);
            }
            break;
        }
        default:
            SM_UNREACHABLE();
        }
    }
    if (braced) {
        expect('}');
        eat();
    }
    if (fmt.view.bytes) {
        free(fmt.view.bytes);
    }
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    switch (tok) {
    case SM_TOK_STR:
    case SM_TOK_ID:
        smTokStreamFmtInit(ts, pos, intern(buf.view), tok);
        return;
    default:
        SM_UNREACHABLE();
    }
}
