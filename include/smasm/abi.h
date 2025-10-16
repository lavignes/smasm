#ifndef SMASM_ABI_H
#define SMASM_ABI_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool Bool;

typedef uint8_t U8;
typedef int8_t  I8;

#define U8_MAX UINT8_MAX
#define I8_MAX INT8_MAX
#define I8_MIN INT8_MIN
#define U8_FMT "%" PRIu8
#define I8_FMT "%" PRId8

typedef uint16_t U16;
typedef int16_t  I16;

#define U16_MAX UINT16_MAX
#define I16_MAX INT16_MAX
#define I16_MIN INT16_MIN
#define U16_FMT "%" PRIu16
#define I16_FMT "%" PRIi16

typedef uint32_t U32;
typedef int32_t  I32;

#define U32_MAX UINT32_MAX
#define I32_MAX INT32_MAX
#define I32_MIN INT32_MIN
#define U32_FMT "%" PRIu32
#define I32_FMT "%" PRIi32

typedef uint64_t U64;
typedef int64_t  I64;

#define U64_MAX UINT64_MAX
#define I64_MAX INT64_MAX
#define I64_MIN INT64_MIN
#define U64_FMT "%" PRIu64
#define I64_FMT "%" PRIi64

typedef uintptr_t UInt;
typedef intptr_t  Int;

#define UINT_MAX UINTPTR_MAX
#define INT_MAX  INTPTR_MAX
#define INT_MIN  INTPTR_MIN
#define UINT_FMT "%" PRIuPTR
#define INT_FMT  "%" PRIiPTR

static inline UInt uIntMax(UInt lhs, UInt rhs) {
    return (lhs > rhs) ? lhs : rhs;
}

static inline UInt uIntMin(UInt lhs, UInt rhs) {
    return (lhs < rhs) ? lhs : rhs;
}

#endif // SMASM_ABI_H
