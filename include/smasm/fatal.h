#ifndef SMASM_FATAL_H
#define SMASM_FATAL_H

#include <stdarg.h>

_Noreturn void smFatal(char const *fmt, ...);
_Noreturn void smFatalV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define smUnreachable() __builtin_unreachable()
#else
#define smUnreachable() (smFatal("%s:%d unreachable\n", __FILE__, __LINE__))
#endif

#define smUnimplemented(msg)                                                   \
    (smFatal("%s:%d: not implemented: %s\n", __FILE__, __LINE__, (msg)))

#endif // SMASM_FATAL_H
