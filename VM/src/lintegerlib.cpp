// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"
#include "lstate.h"
#include "lobject.h"
#include "lapi.h"

#include <stdint.h>

LUAU_FASTFLAGVARIABLE(LuauIntegerLibrary)

static const TValue* check_integer(lua_State* L, int idx)
{
    luaL_checktype(L, idx, LUA_TINTEGER);
    return luaA_toobject(L, idx);
}

static int integer_fromstring(lua_State* L)
{
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);
    luaZ_integer_fromstring(L, str, L->top);
    L->top++;
    return 1;
}

static int integer_dynamic(lua_State* L)
{
    const TValue* b = check_integer(L, 1);
    if (ttype(b) == LUA_TINTEGER)
        setintegersmi(L->top, b->value.l, IntegerMode_Dynamic);
    else
        setintegerheap(L->top, (HeapInteger*)b->value.gc, IntegerMode_Dynamic);
    L->top++;
    return 1;
}

#define INTEGER_MODE_WRAP(name, mode_enum, c_type, is_unsigned) \
static int integer_##name(lua_State* L) \
{ \
    const TValue* b = check_integer(L, 1); \
    c_type casted = (c_type)luaZ_integer_get_bottom_64(b); \
    int64_t res_smi = is_unsigned ? (int64_t)(uint64_t)casted : (int64_t)casted; \
    setintegersmi(L->top, res_smi, mode_enum); \
    L->top++; \
    return 1; \
}

INTEGER_MODE_WRAP(i8, IntegerMode_I8, int8_t, false)
INTEGER_MODE_WRAP(u8, IntegerMode_U8, uint8_t, true)
INTEGER_MODE_WRAP(i16, IntegerMode_I16, int16_t, false)
INTEGER_MODE_WRAP(u16, IntegerMode_U16, uint16_t, true)
INTEGER_MODE_WRAP(i32, IntegerMode_I32, int32_t, false)
INTEGER_MODE_WRAP(u32, IntegerMode_U32, uint32_t, true)
INTEGER_MODE_WRAP(i64, IntegerMode_I64, int64_t, false)
INTEGER_MODE_WRAP(u64, IntegerMode_U64, uint64_t, true)

/*
// Arithmetic operations exposed to Lua
static int integer_add(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    luaZ_integer_add(L, a, b, L->top);
    L->top++;
    return 1;
}

static int integer_sub(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    luaZ_integer_sub(L, a, b, L->top);
    L->top++;
    return 1;
}

static int integer_mul(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    luaZ_integer_mul(L, a, b, L->top);
    L->top++;
    return 1;
}

static int integer_div(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    luaZ_integer_div(L, a, b, L->top);
    L->top++;
    return 1;
}

// integer.rem(a, b) - C-style remainder (truncating)
static int integer_rem(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    luaZ_integer_rem(L, a, b, L->top);
    L->top++;
    return 1;
}

// integer.min(...) - returns minimum of all arguments
static int integer_min(lua_State* L)
{
    int n = lua_gettop(L);
    luaL_argcheck(L, n >= 1, 1, "integer expected, got no value");

    const TValue* best = check_integer(L, 1);

    for (int i = 2; i <= n; i++)
    {
        const TValue* cur = check_integer(L, i);
        if (luaZ_integer_lt(L, cur, best))
            best = cur;
    }

    // Copy the result to the top
    if (ttype(best) == LUA_TINTEGER)
        setintegersmi(L->top, best->value.l, (IntegerMode)best->extra[0]);
    else
        setintegerheap(L->top, (HeapInteger*)best->value.gc, (IntegerMode)best->extra[0]);
    L->top++;
    return 1;
}

// integer.max(...) - returns maximum of all arguments
static int integer_max(lua_State* L)
{
    int n = lua_gettop(L);
    luaL_argcheck(L, n >= 1, 1, "integer expected, got no value");

    const TValue* best = check_integer(L, 1);

    for (int i = 2; i <= n; i++)
    {
        const TValue* cur = check_integer(L, i);
        if (luaZ_integer_lt(L, best, cur))
            best = cur;
    }

    if (ttype(best) == LUA_TINTEGER)
        setintegersmi(L->top, best->value.l, (IntegerMode)best->extra[0]);
    else
        setintegerheap(L->top, (HeapInteger*)best->value.gc, (IntegerMode)best->extra[0]);
    L->top++;
    return 1;
}

// integer.clamp(n, min, max) - clamp n between min and max
static int integer_clamp(lua_State* L)
{
    const TValue* n = check_integer(L, 1);
    const TValue* lo = check_integer(L, 2);
    const TValue* hi = check_integer(L, 3);

    luaL_argcheck(L, luaZ_integer_le(L, lo, hi), 3, "max must be >= min");

    const TValue* result = n;
    if (luaZ_integer_lt(L, n, lo))
        result = lo;
    else if (luaZ_integer_lt(L, hi, n))
        result = hi;

    if (ttype(result) == LUA_TINTEGER)
        setintegersmi(L->top, result->value.l, (IntegerMode)result->extra[0]);
    else
        setintegerheap(L->top, (HeapInteger*)result->value.gc, (IntegerMode)result->extra[0]);
    L->top++;
    return 1;
}

// Bitwise operations - all operate on the bottom 64 bits
// integer.bnot(n)
static int integer_bnot(lua_State* L)
{
    const TValue* b = check_integer(L, 1);
    uint64_t val = luaZ_integer_get_bottom_64(b);
    setintegersmi(L->top, (int64_t)(~val), IntegerMode_Dynamic);
    L->top++;
    return 1;
}

// Variadic bitwise helper
#define INTEGER_BITWISE_VARIADIC(name, op, identity) \
static int integer_##name(lua_State* L) \
{ \
    int n = lua_gettop(L); \
    uint64_t result = identity; \
    for (int i = 1; i <= n; i++) \
    { \
        const TValue* cur = check_integer(L, i); \
        result = result op luaZ_integer_get_bottom_64(cur); \
    } \
    setintegersmi(L->top, (int64_t)result, IntegerMode_Dynamic); \
    L->top++; \
    return 1; \
}

INTEGER_BITWISE_VARIADIC(band, &, ~(uint64_t)0)
INTEGER_BITWISE_VARIADIC(bor, |, 0)
INTEGER_BITWISE_VARIADIC(bxor, ^, 0)

// integer.lshift(n, i) - logical left shift
static int integer_lshift(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    uint64_t val = luaZ_integer_get_bottom_64(a);
    uint64_t shift = luaZ_integer_get_bottom_64(b);
    setintegersmi(L->top, (int64_t)(val << (shift & 63)), IntegerMode_Dynamic);
    L->top++;
    return 1;
}

// integer.rshift(n, i) - logical right shift
static int integer_rshift(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    uint64_t val = luaZ_integer_get_bottom_64(a);
    uint64_t shift = luaZ_integer_get_bottom_64(b);
    setintegersmi(L->top, (int64_t)(val >> (shift & 63)), IntegerMode_Dynamic);
    L->top++;
    return 1;
}

// integer.arshift(n, i) - arithmetic right shift
static int integer_arshift(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    int64_t val = (int64_t)luaZ_integer_get_bottom_64(a);
    uint64_t shift = luaZ_integer_get_bottom_64(b);
    setintegersmi(L->top, val >> (shift & 63), IntegerMode_Dynamic);
    L->top++;
    return 1;
}

// integer.lrotate(n, i) - rotate left (negative i rotates right)
static int integer_lrotate(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    uint64_t val = luaZ_integer_get_bottom_64(a);
    int64_t shift = (int64_t)luaZ_integer_get_bottom_64(b);
    int r = (int)(shift % 64);
    if (r < 0)
        r += 64;
    uint64_t result = (val << r) | (val >> (64 - r));
    setintegersmi(L->top, (int64_t)result, IntegerMode_Dynamic);
    L->top++;
    return 1;
}

// integer.rrotate(n, i) - rotate right (negative i rotates left)
static int integer_rrotate(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    uint64_t val = luaZ_integer_get_bottom_64(a);
    int64_t shift = (int64_t)luaZ_integer_get_bottom_64(b);
    int r = (int)(shift % 64);
    if (r < 0)
        r += 64;
    uint64_t result = (val >> r) | (val << (64 - r));
    setintegersmi(L->top, (int64_t)result, IntegerMode_Dynamic);
    L->top++;
    return 1;
}


// integer.sadd(a, b, max) - saturating addition up to max
static int integer_sadd(lua_State* L)
{
    const TValue* a = check_integer(L, 1);
    const TValue* b = check_integer(L, 2);
    const TValue* max_val = check_integer(L, 3);

    // Perform the addition
    luaZ_integer_add(L, a, b, L->top);

    // If result > max, return max
    if (luaZ_integer_lt(L, max_val, L->top))
    {
        if (ttype(max_val) == LUA_TINTEGER)
            setintegersmi(L->top, max_val->value.l, (IntegerMode)max_val->extra[0]);
        else
            setintegerheap(L->top, (HeapInteger*)max_val->value.gc, (IntegerMode)max_val->extra[0]);
    }

    L->top++;
    return 1;
}
*/

static const luaL_Reg integerlib[] = {
    {"fromstring", integer_fromstring},
    {"dynamic", integer_dynamic},
    {"i8", integer_i8},
    {"u8", integer_u8},
    {"i16", integer_i16},
    {"u16", integer_u16},
    {"i32", integer_i32},
    {"u32", integer_u32},
    {"i64", integer_i64},
    {"u64", integer_u64},
    //{"add", integer_add},
    //{"sub", integer_sub},
    //{"mul", integer_mul},
    //{"div", integer_div},
    //{"rem", integer_rem},
    //{"min", integer_min},
    //{"max", integer_max},
    //{"clamp", integer_clamp},
    //{"bnot", integer_bnot},
    //{"band", integer_band},
    //{"bor", integer_bor},
    //{"bxor", integer_bxor},
    //{"lshift", integer_lshift},
    //{"rshift", integer_rshift},
    //{"arshift", integer_arshift},
    //{"lrotate", integer_lrotate},
    //{"rrotate", integer_rrotate},
    //{"sadd", integer_sadd},
    {NULL, NULL},
};

LUALIB_API int luaopen_integer(lua_State* L)
{
    luaL_register(L, LUA_INTEGERLIBNAME, integerlib);
    return 1;
}
