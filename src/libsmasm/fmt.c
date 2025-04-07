#include <smasm/fatal.h>
#include <smasm/fmt.h>

#include <ctype.h>

static char const DIGITS[] = "0123456789ABCDEF";

I32 smParse(SmBuf buf) {
    if (buf.len == 0) {
        smFatal("empty number\n");
    }
    I32  radix = 10;
    UInt i     = 0;
    if (buf.bytes[0] == '%') {
        radix = 2;
        ++i;
    } else if (buf.bytes[0] == '$') {
        radix = 16;
        ++i;
    }
    if (i == buf.len) {
        smFatal("invalid number: %.*s\n", buf.len, buf.bytes);
    }
    I32 value = 0;
    for (; i < buf.len; ++i) {
        for (UInt j = 0; j < (sizeof(DIGITS) / sizeof(DIGITS[0])); ++j) {
            if (toupper(buf.bytes[i]) == DIGITS[j]) {
                if (j >= (UInt)radix) {
                    smFatal("invalid number: %.*s\n", buf.len, buf.bytes);
                }
                value *= radix;
                value += j;
                goto next;
            }
        }
        smFatal("invalid number: %.*s\n", buf.len, buf.bytes);
    next:
        (void)0;
    }
    return value;
}
