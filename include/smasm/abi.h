#ifndef SMASM_ABI_H
#define SMASM_ABI_H

typedef _Bool Bool;
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

typedef unsigned char U8;
typedef signed char   I8;

#define U8_C(value) ((U8)value##U)
#define I8_C(value) ((I8)value)
#define U8_MAX      U8_C(255)
#define I8_MIN      I8_C(-128)
#define I8_MAX      I8_C(127)

typedef unsigned short U16;
typedef signed short   I16;

#define U16_C(value) ((U16)value##U)
#define I16_C(value) ((I16)value)
#define U16_MAX      U16_C(65535)
#define I16_MIN      I16_C(-32768)
#define I16_MAX      I16_C(32767)

#ifdef SMASM_ABI32
typedef unsigned long U32;
typedef signed long   I32;

#define U32_C(value) ((U32)value##UL)
#define I32_C(value) ((I32)value##L)

_Static_assert(sizeof(U32) == 4, "unsigned long must be 32-bit");
_Static_assert(sizeof(void *) == sizeof(U32), "pointers must be 32-bit");

typedef unsigned long long U64;
typedef signed long long   I64;

#define U64_C(value) ((U64)value##ULL)
#define I64_C(value) ((I64)value##LL)

_Static_assert(sizeof(U64) == 8, "unsigned long long must be 64-bit");

typedef U32 UInt;
typedef I32 Int;

#define UINT_C   U32_C
#define INT_C    I32_C

#define UINT_MAX U32_MAX
#define INT_MIN  I32_MIN
#define INT_MAX  I32_MAX

#endif

#ifdef SMASM_ABI64
typedef unsigned int U32;
typedef signed int   I32;

#define U32_C(value) ((U32)value##U)
#define I32_C(value) ((I32)value)

_Static_assert(sizeof(U32) == 4, "unsigned int must be 32-bit");

typedef unsigned long U64;
typedef signed long   I64;

#define U64_C(value) ((U64)value##UL)
#define I64_C(value) ((I64)value##L)

_Static_assert(sizeof(U64) == 8, "unsigned long must be 64-bit");
_Static_assert(sizeof(void *) == sizeof(U64), "pointers must be 64-bit");

typedef U64 UInt;
typedef I64 Int;

#define UINT_C   U64_C
#define INT_C    I64_C

#define UINT_MAX U64_MAX
#define INT_MIN  I64_MIN
#define INT_MAX  I64_MAX

#endif

#define U32_MAX U32_C(4294967295)
#define I32_MIN I32_C(-2147483648)
#define I32_MAX I32_C(2147483647)

#define U64_MAX U64_C(18446744073709551615)
#define I64_MIN I64_C(-9223372036854775808)
#define I64_MAX I64_C(9223372036854775807)

static inline UInt uIntMax(UInt lhs, UInt rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline UInt uIntMin(UInt lhs, UInt rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

#endif // SMASM_ABI_H
