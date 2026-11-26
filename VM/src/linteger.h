// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lobject.h"
#include "ltm.h"
#include "lnumutils.h"

// Luwu Integers (rfcs/integers.md): integers of every mode. An integer that fits in 64 bits is stored inline
// (LUA_TINTEGER) with its mode in `extra[0]`. A sized integer holds its value sign- or zero-extended to 64 bits, so a
// `u64` past INT64_MAX holds its bits. A dynamically-sized integer that doesn't fit is a bigint (LUA_THEAPINTEGER).
// Every result that fits in 64 bits is stored inline, so equal integers always have the same representation.

#define integermode(o) (IntegerMode((o)->extra[0]))

// What converting a value to an integer of some mode gave
enum IntegerConversion
{
    IntegerConversion_Ok,
    // the value is above the mode's largest value
    IntegerConversion_Overflow,
    // the value is below the mode's smallest value
    IntegerConversion_Underflow,
};

// Bitwise operations for luaZ_bitwise
enum IntegerBitwise
{
    IntegerBitwise_And,
    IntegerBitwise_Or,
    IntegerBitwise_Xor,
};

// The suffix of `mode` ("u8", ...), and "int" for a dynamically-sized integer
LUAI_FUNC const char* luaZ_modename(IntegerMode mode);
// The width of `mode` in bits, or 0 for a dynamically-sized integer
LUAI_FUNC int luaZ_modewidth(IntegerMode mode);

// An integer operator (`op` is TM_ADD to TM_UNM) on two integers, which raises for integers of different modes.
// `rc` is ignored for TM_UNM.
LUAI_FUNC void luaZ_arith(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, TMS op);
// `<` and `<=` where `l` or `r` is an integer: raises Error.InvalidComparison unless both have one mode
LUAI_FUNC int luaZ_lessthan(lua_State* L, const TValue* l, const TValue* r);
LUAI_FUNC int luaZ_lessequal(lua_State* L, const TValue* l, const TValue* r);
// -1, 0 or 1 as `a` is below, equal to or above `b`, which has the same mode
LUAI_FUNC int luaZ_compare(const TValue* a, const TValue* b);
// -1, 0 or 1 as `o` is negative, zero or positive
LUAI_FUNC int luaZ_sign(const TValue* o);

LUAI_FUNC int luaZ_equal(const TValue* a, const TValue* b);
LUAI_FUNC int luaZ_equalkey(const TKey* a, const TValue* b);
LUAI_FUNC unsigned luaZ_hash(const TValue* o);

// `value` wrapped to `mode`; a dynamically-sized `value` is taken as signed
LUAI_FUNC void luaZ_setint(TValue* res, int64_t value, IntegerMode mode);
// `value` as an unsigned 64-bit integer in `mode`, wrapped for a sized mode and a bigint past INT64_MAX for a dynamic one
LUAI_FUNC void luaZ_setunsigned(lua_State* L, TValue* res, uint64_t value, IntegerMode mode);
// The integer `o` in `mode`, unless it's out of the mode's range
LUAI_FUNC IntegerConversion luaZ_convert(lua_State* L, TValue* res, const TValue* o, IntegerMode mode);
// The whole number `n` (not NaN) as an integer in `mode`, unless it's out of the mode's range
LUAI_FUNC IntegerConversion luaZ_fromnumber(lua_State* L, TValue* res, double n, IntegerMode mode);
// The dynamically-sized integer whose decimal digits are `digits`, with an optional leading minus. Returns false when
// `digits` isn't a decimal integer.
LUAI_FUNC bool luaZ_fromdecimal(lua_State* L, TValue* res, const char* digits);
// The number nearest to `o`; returns whether it's exactly `o`. Past the range of a number it's an infinity.
LUAI_FUNC bool luaZ_tonumber(const TValue* o, double* n);

// The bits of an inline integer
LUAI_FUNC uint64_t luaZ_bits(const TValue* o);
// The number of bits of `o`'s absolute value: 0 for zero
LUAI_FUNC uint64_t luaZ_bitlength(const TValue* o);
// 2 to the power `exponent` as a dynamically-sized integer
LUAI_FUNC void luaZ_powerof2(lua_State* L, TValue* res, uint64_t exponent);
// `a op b` on integers of one mode; a dynamically-sized integer acts as if it had infinitely many sign bits
LUAI_FUNC void luaZ_bitwise(lua_State* L, TValue* res, const TValue* a, const TValue* b, IntegerBitwise op);

// Pushes `o` in decimal, without its suffix
LUAI_FUNC void luaZ_pushstring(lua_State* L, const TValue* o);
// Writes an inline `o` in decimal to `buf`, which holds at least LUAI_MAXINT2STR characters, and returns the end
LUAI_FUNC char* luaZ_formatinline(const TValue* o, char* buf);
// Pushes the digits of `o` in base 2^`bitsperdigit` (1, 3 or 4). A sized integer writes the bits of its width; a
// dynamically-sized one writes a minus and its absolute value.
LUAI_FUNC void luaZ_pushpow2digits(lua_State* L, const TValue* o, int bitsperdigit, bool upper);

// Luwu Integers (rfcs/integers.md): `rb op rc` (TM_ADD, TM_SUB or TM_MUL) for two inline integers of one mode, the
// interpreter's fast path. Returns false when the result needs the slow path: other operands, or an `int` result past
// 64 bits.
template<TMS op>
LUAU_FORCEINLINE bool luaZ_fastarith(TValue* ra, const TValue* rb, const TValue* rc)
{
    static_assert(op == TM_ADD || op == TM_SUB || op == TM_MUL, "only operators that can't raise");

    bool sameMode = ttype(rb) == LUA_TINTEGER && ttype(rc) == LUA_TINTEGER && rb->extra[0] == rc->extra[0];
    if (!sameMode)
        return false;

    IntegerMode mode = integermode(rb);
    if (mode == IntegerMode_Dynamic)
    {
        int64_t result = 0;
        bool overflow = false;
        if (op == TM_ADD)
            overflow = luau_add_overflow(rb->value.l, rc->value.l, &result);
        else if (op == TM_SUB)
            overflow = luau_sub_overflow(rb->value.l, rc->value.l, &result);
        else
            overflow = luau_mul_overflow(rb->value.l, rc->value.l, &result);

        if (overflow)
            return false;
        setintegersmi(ra, result, IntegerMode_Dynamic);
        return true;
    }

    uint64_t x = uint64_t(rb->value.l);
    uint64_t y = uint64_t(rc->value.l);
    uint64_t result = op == TM_ADD ? x + y : (op == TM_SUB ? x - y : x * y);
    setintegersmi(ra, wrapIntegerToMode(int64_t(result), mode), mode);
    return true;
}

// The number of zero bits below the lowest set bit of nonzero `o`'s absolute value
LUAI_FUNC uint64_t luaZ_trailingzeros(const TValue* o);

// Pushes the `integer.Mode` object of `mode`; the integer library has to be open
LUAI_FUNC void luaZ_pushmode(lua_State* L, IntegerMode mode);
// The mode of the `integer.Mode` object at stack `idx`, or -1 when it isn't one
LUAI_FUNC int luaZ_tomode(lua_State* L, int idx);
// The mode of the `integer.Mode` object at stack `idx`, raising when it isn't one
LUAI_FUNC IntegerMode luaZ_checkmode(lua_State* L, int idx);
