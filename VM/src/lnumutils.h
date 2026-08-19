// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include <math.h>
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#endif

// x*y => 128-bit product (lo+hi)
inline uint64_t mul128(uint64_t x, uint64_t y, uint64_t* hi)
{
#if defined(_MSC_VER) && defined(_M_X64)
    return _umul128(x, y, hi);
#elif defined(__SIZEOF_INT128__)
    unsigned __int128 r = x;
    r *= y;
    *hi = uint64_t(r >> 64);
    return uint64_t(r);
#else
    uint32_t x0 = uint32_t(x), x1 = uint32_t(x >> 32);
    uint32_t y0 = uint32_t(y), y1 = uint32_t(y >> 32);
    uint64_t p11 = uint64_t(x1) * y1, p01 = uint64_t(x0) * y1;
    uint64_t p10 = uint64_t(x1) * y0, p00 = uint64_t(x0) * y0;
    uint64_t mid = p10 + (p00 >> 32) + uint32_t(p01);
    uint64_t r0 = (mid << 32) | uint32_t(p00);
    uint64_t r1 = p11 + (mid >> 32) + (p01 >> 32);
    *hi = r1;
    return r0;
#endif
}

#define luai_numadd(a, b) ((a) + (b))
#define luai_numsub(a, b) ((a) - (b))
#define luai_nummul(a, b) ((a) * (b))
#define luai_numdiv(a, b) ((a) / (b))
#define luai_numpow(a, b) (pow(a, b))
#define luai_numunm(a) (-(a))
#define luai_numisnan(a) ((a) != (a))
#define luai_numeq(a, b) ((a) == (b))
#define luai_numlt(a, b) ((a) < (b))
#define luai_numle(a, b) ((a) <= (b))
#define luai_inteq(a, b) ((a) == (b))

inline bool luai_veceq(const float* a, const float* b)
{
#if LUA_VECTOR_SIZE == 4
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
#else
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
#endif
}

inline bool luai_vecisnan(const float* a)
{
#if LUA_VECTOR_SIZE == 4
    return a[0] != a[0] || a[1] != a[1] || a[2] != a[2] || a[3] != a[3];
#else
    return a[0] != a[0] || a[1] != a[1] || a[2] != a[2];
#endif
}

inline float luaui_signf(float v)
{
    return v > 0.0f ? 1.0f : v < 0.0f ? -1.0f : 0.0f;
}

inline float luaui_clampf(float v, float min, float max)
{
    float r = v < min ? min : v;
    return r > max ? max : r;
}

LUAU_FASTMATH_BEGIN
inline double luai_nummod(double a, double b)
{
    return a - floor(a / b) * b;
}
LUAU_FASTMATH_END

LUAU_FASTMATH_BEGIN
inline double luai_numidiv(double a, double b)
{
    return floor(a / b);
}
LUAU_FASTMATH_END

inline float luai_lerpf(float a, float b, float t)
{
    return (t == 1.0) ? b : a + (b - a) * t;
}

#define luai_num2int(i, d) ((i) = (int)(d))

#define luai_num2long(i, d) ((i) = (int64_t)(d))

// On MSVC in 32-bit, double to unsigned cast compiles into a call to __dtoui3, so we invoke x87->int64 conversion path manually
#if defined(_MSC_VER) && defined(_M_IX86)
#define luai_num2unsigned(i, n) \
    { \
        __int64 l; \
        __asm { __asm fld n __asm fistp l} \
        ; \
        i = (unsigned int)l; \
    }
#else
#define luai_num2unsigned(i, n) ((i) = (unsigned)(long long)(n))
#endif

#define LUAI_MAXNUM2STR 48
#define LUAI_MAXINT2STR 30

LUAI_FUNC char* luai_num2str(char* buf, double n);
LUAI_FUNC char* luai_int2str(char* buf, int64_t n);

#define luai_str2num(s, p) strtod((s), (p))
#define luai_str2long(s, p, base) strtoll((s), (p), base)

// Signed 64-bit operations
inline bool luau_add_overflow(int64_t a, int64_t b, int64_t* res) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, res);
#elif defined(_MSC_VER) && _MSC_VER >= 1937
    return _add_overflow_i64(0, a, b, res);
#else
    *res = a + b;
    return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
#endif
}

inline bool luau_sub_overflow(int64_t a, int64_t b, int64_t* res) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sub_overflow(a, b, res);
#elif defined(_MSC_VER) && _MSC_VER >= 1937
    return _sub_overflow_i64(0, a, b, res);
#else
    *res = a - b;
    return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
#endif
}

inline bool luau_mul_overflow(int64_t a, int64_t b, int64_t* res) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, res);
#elif defined(_MSC_VER) && _MSC_VER >= 1937
    return _mul_overflow_i64(a, b, res);
#else
    *res = a * b;
    if (a == 0 || b == 0) return false;
    if (a == -1 && b == INT64_MIN) return true;
    if (b == -1 && a == INT64_MIN) return true;
    return *res / b != a;
#endif
}

// Unsigned 64-bit operations (used by bigint)
inline bool luau_add_overflow(uint64_t a, uint64_t b, uint64_t* res) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, res);
#elif defined(_MSC_VER) && defined(_M_X64)
    return _addcarry_u64(0, a, b, res) != 0;
#else
    *res = a + b;
    return *res < a;
#endif
}

inline bool luau_sub_overflow(uint64_t a, uint64_t b, uint64_t* res) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sub_overflow(a, b, res);
#elif defined(_MSC_VER) && defined(_M_X64)
    return _subborrow_u64(0, a, b, res) != 0;
#else
    *res = a - b;
    return a < b;
#endif
}
