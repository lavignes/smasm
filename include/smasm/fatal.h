#ifndef SMASM_FATAL_H
#define SMASM_FATAL_H

#include <stdarg.h>

#define SM_FORMAT(n)
#ifdef __has_attribute
#if __has_attribute(format)
#undef SM_FORMAT
#define SM_FORMAT(n) __attribute__((format(printf, (n), (n + 1))))
#endif
#endif

SM_FORMAT(1) _Noreturn void smFatal(char const *fmt, ...);
_Noreturn void smFatalV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define SM_UNREACHABLE() __builtin_unreachable()
#else
#define SM_UNREACHABLE()                                                       \
    (smFatal("%s:%d is not meant to be reachable\n", __FILE__, __LINE__))
#endif

#define SM_TODO(msg)                                                           \
    (smFatal("%s:%d: not implemented: %s\n", __FILE__, __LINE__, (msg)))

#endif // SMASM_FATAL_H
