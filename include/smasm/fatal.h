#ifndef SMASM_FATAL_H
#define SMASM_FATAL_H

#include <stdarg.h>

_Noreturn void smFatal(char const *fmt, ...);
_Noreturn void smFatalV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define smUnreachable() __builtin_unreachable()
#else
#define smUnreachable() (smFatal("unreachable"))
#endif

#define smUnimplemented()                                                      \
    (smFatal("unimplemented: %s:%d\n", __FILE__, __LINE__))

#endif // SMASM_FATAL_H
