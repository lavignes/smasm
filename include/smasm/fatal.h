#ifndef SMASM_FATAL_H
#define SMASM_FATAL_H

#include <stdarg.h>

#ifdef __has_attribute
#if __has_attribute(format)
#define SM_FORMAT(n) __attribute__((format(printf, (n), (n + 1))))
#endif
#endif

SM_FORMAT(1) _Noreturn void smFatal(char const *fmt, ...);
_Noreturn void smFatalV(char const *fmt, va_list args);

#ifdef __builtin_unreachable
#define smUnreachable() __builtin_unreachable()
#else
#define smUnreachable() (smFatal("%s:%d unreachable\n", __FILE__, __LINE__))
#endif

#define smUnimplemented(msg)                                                   \
    (smFatal("%s:%d: not implemented: %s\n", __FILE__, __LINE__, (msg)))

#endif // SMASM_FATAL_H
