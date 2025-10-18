#include <smasm/fatal.h>

#include <stdio.h>
#include <stdlib.h>

_Noreturn void smFatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    smFatalV(fmt, args);
}

_Noreturn void smFatalV(char const *fmt, va_list args) {
    vfprintf(stderr, fmt, args);
    exit(EXIT_FAILURE);
}
