// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"

#include "lapi.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "linteger.h"
#include "lstate.h"
#include "lstring.h"

#include "Luau/IntegerParsing.h"

#include <math.h>
#include <string.h>

// Luwu Integers (rfcs/integers.md): the `integer` library. Each mode is an object of the builtin class `integer.Mode`
// (`integer.u8`, ..., `integer.dynamic`), which casts when called. Most functions are methods of integers too, through
// the integers' metatable.

// The registry key of the array of Mode objects, indexed by IntegerMode + 1
static const char* const kModesKey = "integer.modes";

// The fields of a Mode object, in order
enum ModeField
{
    ModeField_Min,
    ModeField_Max,
    ModeField_Name,
};

// The most bits a shift or a field of a dynamically-sized integer may reach, so a typo can't ask for gigabytes
static const uint64_t kMaxDynamicBits = uint64_t(1) << 32;

static LuauClass* modeclass(lua_State* L)
{
    return L->global->builtinclasses[LUWU_BUILTIN_INTEGERMODE];
}

void luaZ_pushmode(lua_State* L, IntegerMode mode)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kModesKey);
    lua_rawgeti(L, -1, int(mode) + 1);
    lua_remove(L, -2);
}

// The mode of the Mode object at `idx`, or -1 when it isn't one
static int tomode(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    if (!o || !ttisobject(o) || objectvalue(o)->lclass != modeclass(L))
        return -1;

    const TValue* name = &luaR_members(objectvalue(o))[ModeField_Name];
    for (int mode = 0; mode <= IntegerMode_U64; mode++)
    {
        if (ttisstring(name) && tsvalue(name) == L->global->integermodename[mode])
            return mode;
    }
    return -1;
}

IntegerMode luaZ_checkmode(lua_State* L, int idx)
{
    int mode = tomode(L, idx);
    if (mode < 0)
        luaL_typeerrorL(L, idx, "integer mode such as integer.u8");
    return IntegerMode(mode);
}

static const TValue* checkinteger(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    if (!o || !ttisinteger(o))
        luaL_typeerrorL(L, idx, "integer");
    return o;
}

// The integer at `idx`, which has to have the mode of `first`
static const TValue* checksamemode(lua_State* L, int idx, const TValue* first)
{
    const TValue* o = checkinteger(L, idx);
    if (integermode(o) != integermode(first))
    {
        const char* message = lua_pushfstring(L, "expected %s, got %s", luaZ_modename(integermode(first)), luaZ_modename(integermode(o)));
        luaL_argerrorL(L, idx, message);
    }
    return o;
}

static void pushinteger(lua_State* L, const TValue* o)
{
    setobj2s(L, L->top, o);
    incr_top(L);
}

static void pushint(lua_State* L, int64_t value, IntegerMode mode)
{
    luaZ_setint(L->top, value, mode);
    incr_top(L);
}

// Raises error variant `kind` with the value at stack `idx` and the Mode object of `mode`
static l_noret raisewithmode(lua_State* L, LuwuBuiltinClass kind, int idx, IntegerMode mode)
{
    lua_pushvalue(L, idx);
    luaZ_pushmode(L, mode);
    luaR_raisevariant(L, kind, 2);
}

// Raises Error.InvalidArgument with the value at stack `idx` and `message`
static l_noret invalidargument(lua_State* L, int idx, const char* message)
{
    lua_pushvalue(L, idx);
    lua_pushstring(L, message);
    luaR_raisevariant(L, LUWU_BUILTIN_ERR_INVALIDARGUMENT, 2);
}

// The count at `idx` (a shift, exponent or bit position): an integer of any mode that isn't negative. A bigint count
// becomes a count past every width with the same parity, which is all a power of 0, 1 or -1 needs.
static uint64_t checkcount(lua_State* L, int idx, const char* what)
{
    const TValue* o = checkinteger(L, idx);
    if (luaZ_sign(o) < 0)
        invalidargument(L, idx, lua_pushfstring(L, "%s can't be negative", what));

    if (ttype(o) == LUA_THEAPINTEGER)
    {
        bool odd = (((const HeapInteger*)o->value.gc)->digits[0] & 1) != 0;
        return odd ? UINT64_MAX : UINT64_MAX - 1;
    }
    return luaZ_bits(o);
}

// The bits of an inline sized integer, masked to its width
static uint64_t widthbits(const TValue* o)
{
    int width = luaZ_modewidth(integermode(o));
    uint64_t bits = luaZ_bits(o);
    return width == 64 ? bits : bits & ((uint64_t(1) << width) - 1);
}

static uint64_t largestunsigned(int width)
{
    return width == 64 ? UINT64_MAX : (uint64_t(1) << width) - 1;
}

// The smallest and largest values of sized `mode`
static int64_t smallestof(IntegerMode mode)
{
    int width = luaZ_modewidth(mode);
    return isUnsignedIntegerMode(mode) ? 0 : int64_t(0 - (uint64_t(1) << (width - 1)));
}

static int64_t largestof(IntegerMode mode)
{
    int width = luaZ_modewidth(mode);
    return int64_t(isUnsignedIntegerMode(mode) ? largestunsigned(width) : (uint64_t(1) << (width - 1)) - 1);
}

// Whether an operation's exact result fits its sized mode, or which way it doesn't
enum Fit
{
    Fit_Fits,
    Fit_Above,
    Fit_Below,
};

// `a op b` (TM_ADD, TM_SUB or TM_MUL) for two sized integers of `mode`: the exact result when it fits, and otherwise
// which side of the range it's on
static Fit exactsized(TMS op, IntegerMode mode, int64_t a, int64_t b, int64_t* result)
{
    if (isUnsignedIntegerMode(mode))
    {
        uint64_t largest = largestunsigned(luaZ_modewidth(mode));
        uint64_t x = uint64_t(a);
        uint64_t y = uint64_t(b);
        uint64_t r = 0;
        bool above = false;
        if (op == TM_SUB)
        {
            if (x < y)
                return Fit_Below;
            r = x - y;
        }
        else if (op == TM_ADD)
        {
            above = luau_add_overflow(x, y, &r) || r > largest;
        }
        else
        {
            uint64_t high = 0;
            r = mul128(x, y, &high);
            above = high != 0 || r > largest;
        }
        *result = int64_t(r);
        return above ? Fit_Above : Fit_Fits;
    }

    int64_t r = 0;
    bool overflow = false;
    if (op == TM_ADD)
        overflow = luau_add_overflow(a, b, &r);
    else if (op == TM_SUB)
        overflow = luau_sub_overflow(a, b, &r);
    else
        overflow = luau_mul_overflow(a, b, &r);

    // only a 64-bit mode overflows int64_t
    if (overflow)
    {
        bool above = op == TM_ADD ? a > 0 : (op == TM_SUB ? b < 0 : (a < 0) == (b < 0));
        return above ? Fit_Above : Fit_Below;
    }

    *result = r;
    if (r > largestof(mode))
        return Fit_Above;
    if (r < smallestof(mode))
        return Fit_Below;
    return Fit_Fits;
}

// Converts the integer at stack `idx` to `mode` in place, raising Error.Overflow or Error.Underflow with the integer
// and the mode when it doesn't fit
static void converttomode(lua_State* L, int idx, IntegerMode mode)
{
    idx = lua_absindex(L, idx);

    TValue converted;
    IntegerConversion conversion = luaZ_convert(L, &converted, luaA_toobject(L, idx), mode);
    if (conversion == IntegerConversion_Overflow)
        raisewithmode(L, LUWU_BUILTIN_ERR_OVERFLOW, idx, mode);
    if (conversion == IntegerConversion_Underflow)
        raisewithmode(L, LUWU_BUILTIN_ERR_UNDERFLOW, idx, mode);

    pushinteger(L, &converted);
    lua_replace(L, idx);
}

// The mode whose suffix ends `text` (`len` characters), or -1. The longest suffix wins, so "u16" isn't read as "u1".
static int findsuffix(const char* text, size_t len, size_t* suffixlen)
{
    int found = -1;
    *suffixlen = 0;
    for (int mode = 0; mode <= IntegerMode_U64; mode++)
    {
        const char* name = luaZ_modename(IntegerMode(mode));
        size_t namelen = strlen(name);
        bool matches = namelen < len && namelen > *suffixlen && memcmp(text + len - namelen, name, namelen) == 0;
        if (matches)
        {
            found = mode;
            *suffixlen = namelen;
        }
    }
    return found;
}

static l_noret cannotbeparsed(lua_State* L, int text, const char* message)
{
    lua_pushvalue(L, text);
    lua_pushstring(L, message);
    luaR_raisevariant(L, LUWU_BUILTIN_ERR_CANNOTBEPARSED, 2);
}

// Parses the string at stack `text` and pushes it as an integer. Without a `mode` (-1) the integer has the string's
// suffix, or is dynamically-sized.
static void pushparsed(lua_State* L, int text, int mode)
{
    const char* const kExpected = "expected an integer such as 42, -7, 0xff or 255u8";

    size_t len = 0;
    const char* s = lua_tolstring(L, text, &len);

    size_t suffixlen = 0;
    int suffix = findsuffix(s, len, &suffixlen);
    len -= suffixlen;

    bool negative = len > 0 && s[0] == '-';
    size_t start = negative ? 1 : 0;

    int base = 10;
    bool prefixed = len > start + 2 && s[start] == '0';
    if (prefixed && (s[start + 1] == 'x' || s[start + 1] == 'X'))
        base = 16;
    else if (prefixed && (s[start + 1] == 'b' || s[start + 1] == 'B'))
        base = 2;
    else if (prefixed && (s[start + 1] == 'o' || s[start + 1] == 'O'))
        base = 8;
    if (base != 10)
        start += 2;

    // underscores separate digits the way they do in a literal, but can't start or end them
    bool wellFormed = start < len && s[start] != '_' && s[len - 1] != '_';
    if (!wellFormed)
        cannotbeparsed(L, text, kExpected);

    luaL_Strbuf digits;
    luaL_buffinit(L, &digits);
    if (negative)
        luaL_addchar(&digits, '-');
    for (size_t i = start; i < len; i++)
    {
        if (s[i] != '_')
            luaL_addchar(&digits, s[i]);
    }
    luaL_pushresult(&digits);
    const char* clean = lua_tostring(L, -1);

    uint64_t magnitude = 0;
    Luau::ParseIntResult parsed = Luau::parseIntegerMagnitude(clean + (negative ? 1 : 0), base, magnitude);
    if (parsed == Luau::ParseIntResult::Malformed)
        cannotbeparsed(L, text, kExpected);

    // the value as a dynamically-sized integer first, then converted to the mode it's written in and the one asked for
    if (parsed == Luau::ParseIntResult::Ok)
    {
        luaZ_setunsigned(L, L->top, magnitude, IntegerMode_Dynamic);
        incr_top(L);
        if (negative)
            luaZ_arith(L, L->top - 1, L->top - 1, L->top - 1, TM_UNM);
    }
    else if (base != 10)
    {
        cannotbeparsed(L, text, "write integers past 64 bits in decimal");
    }
    else
    {
        bool fromDecimal = luaZ_fromdecimal(L, L->top, clean);
        if (!fromDecimal)
            cannotbeparsed(L, text, kExpected);
        incr_top(L);
    }
    lua_remove(L, -2);

    if (suffix >= 0)
        converttomode(L, -1, IntegerMode(suffix));
    if (mode >= 0)
        converttomode(L, -1, IntegerMode(mode));
}

// Pushes the whole number at stack `idx` as an integer in `mode`, raising the error variant for anything else
static void pushfromnumber(lua_State* L, int idx, IntegerMode mode)
{
    double n = lua_tonumber(L, idx);
    if (n != n)
        invalidargument(L, idx, "NaN isn't an integer");

    bool fractional = !isinf(n) && n != floor(n);
    if (fractional)
    {
        lua_pushvalue(L, idx);
        luaR_raisevariant(L, LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT, 1);
    }

    TValue result;
    IntegerConversion conversion = luaZ_fromnumber(L, &result, n, mode);
    if (conversion == IntegerConversion_Overflow)
        raisewithmode(L, LUWU_BUILTIN_ERR_OVERFLOW, idx, mode);
    if (conversion == IntegerConversion_Underflow)
        raisewithmode(L, LUWU_BUILTIN_ERR_UNDERFLOW, idx, mode);
    pushinteger(L, &result);
}

// `integer.cast(from, mode)` and calling a mode. `mode` is -1 when the cast was given none.
static int castto(lua_State* L, int from, int mode)
{
    IntegerMode target = mode < 0 ? IntegerMode_Dynamic : IntegerMode(mode);

    switch (lua_type(L, from))
    {
    case LUA_TINTEGER:
    case LUA_THEAPINTEGER:
        lua_pushvalue(L, from);
        converttomode(L, -1, target);
        return 1;
    case LUA_TNUMBER:
        pushfromnumber(L, from, target);
        return 1;
    case LUA_TSTRING:
        pushparsed(L, from, mode);
        return 1;
    default:
        break;
    }

    // a value that converts itself, to any mode; the result is converted to `target` like any integer
    if (!luaL_getmetafield(L, from, "__tointeger"))
        luaL_typeerrorL(L, from, "integer, number, string or a value with __tointeger");

    lua_pushvalue(L, from);
    luaZ_pushmode(L, target);
    lua_call(L, 2, 1);
    if (!lua_isinteger64(L, -1) && !lua_isheapinteger(L, -1))
        invalidargument(L, -1, "__tointeger has to return an integer");

    converttomode(L, -1, target);
    return 1;
}

static int integer_cast(lua_State* L)
{
    luaL_checkany(L, 1);
    int mode = lua_isnoneornil(L, 2) ? -1 : int(luaZ_checkmode(L, 2));
    return castto(L, 1, mode);
}

// `i:cast(mode)`, which only casts integers and needs its mode
static int integer_castmethod(lua_State* L)
{
    checkinteger(L, 1);
    return castto(L, 1, int(luaZ_checkmode(L, 2)));
}

static int integer_tonumber(lua_State* L)
{
    double n = 0;
    bool exact = luaZ_tonumber(checkinteger(L, 1), &n);

    TValue number;
    setnvalue(&number, n);
    setobjectvalue(L, L->top, luaR_newpayload(L, exact ? LUWU_BUILTIN_OK : LUWU_BUILTIN_ERR, &number));
    incr_top(L);
    return 1;
}

static int integer_mode(lua_State* L)
{
    luaZ_pushmode(L, integermode(checkinteger(L, 1)));
    return 1;
}

static int integer_isbig(lua_State* L)
{
    lua_pushboolean(L, ttype(checkinteger(L, 1)) == LUA_THEAPINTEGER);
    return 1;
}

// `integer.min` and `integer.max`, which keep the first of equal integers like `math.min` and `math.max`
template<bool Max>
static int minmax(lua_State* L)
{
    const TValue* best = checkinteger(L, 1);
    int count = lua_gettop(L);
    for (int i = 2; i <= count; i++)
    {
        const TValue* o = checksamemode(L, i, best);
        int order = luaZ_compare(o, best);
        if (Max ? order > 0 : order < 0)
            best = o;
    }
    pushinteger(L, best);
    return 1;
}

static int integer_clamp(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    const TValue* lower = checksamemode(L, 2, i);
    const TValue* upper = checksamemode(L, 3, i);
    if (luaZ_compare(lower, upper) > 0)
        luaL_argerrorL(L, 3, "max must be greater than or equal to min");

    if (luaZ_compare(i, lower) < 0)
        pushinteger(L, lower);
    else if (luaZ_compare(i, upper) > 0)
        pushinteger(L, upper);
    else
        pushinteger(L, i);
    return 1;
}

static bool issmallest(const TValue* i)
{
    IntegerMode mode = integermode(i);
    return mode != IntegerMode_Dynamic && i->value.l == smallestof(mode);
}

static int integer_abs(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    if (luaZ_sign(i) >= 0)
    {
        pushinteger(L, i);
        return 1;
    }

    if (issmallest(i))
        raisewithmode(L, LUWU_BUILTIN_ERR_OVERFLOW, 1, integermode(i));

    luaZ_arith(L, L->top, i, i, TM_UNM);
    incr_top(L);
    return 1;
}

static int integer_signum(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    pushint(L, luaZ_sign(i), integermode(i));
    return 1;
}

// Pushes `a op b` computed as dynamically-sized integers: the exact result
static void pushexact(lua_State* L, const TValue* a, const TValue* b, TMS op)
{
    TValue x;
    TValue y;
    luaZ_convert(L, &x, a, IntegerMode_Dynamic);
    luaZ_convert(L, &y, b, IntegerMode_Dynamic);
    luaZ_arith(L, L->top, &x, &y, op);
    incr_top(L);
}

// `a op b` on two integers of one mode that saturates or raises instead of wrapping a sized result
template<TMS Op, bool Saturate>
static int boundedarith(lua_State* L)
{
    const TValue* a = checkinteger(L, 1);
    const TValue* b = checksamemode(L, 2, a);
    IntegerMode mode = integermode(a);

    if (mode == IntegerMode_Dynamic)
    {
        luaZ_arith(L, L->top, a, b, Op);
        incr_top(L);
        return 1;
    }

    int64_t result = 0;
    Fit fit = exactsized(Op, mode, a->value.l, b->value.l, &result);
    if (fit == Fit_Fits)
    {
        pushint(L, result, mode);
        return 1;
    }

    if (Saturate)
    {
        pushint(L, fit == Fit_Above ? largestof(mode) : smallestof(mode), mode);
        return 1;
    }

    // the exact result is what didn't fit
    pushexact(L, a, b, Op);
    raisewithmode(L, fit == Fit_Above ? LUWU_BUILTIN_ERR_OVERFLOW : LUWU_BUILTIN_ERR_UNDERFLOW, -1, mode);
}

static int integer_cdiv(lua_State* L)
{
    const TValue* a = checkinteger(L, 1);
    const TValue* b = checksamemode(L, 2, a);

    // the one division that overflows: the smallest signed value over -1
    bool overflows = !isUnsignedIntegerMode(integermode(a)) && issmallest(a) && b->value.l == -1;
    if (overflows)
    {
        pushexact(L, a, b, TM_DIV);
        raisewithmode(L, LUWU_BUILTIN_ERR_OVERFLOW, -1, integermode(a));
    }

    luaZ_arith(L, L->top, a, b, TM_DIV);
    incr_top(L);
    return 1;
}

// `base ^ exponent` wrapped to 64 bits, which wraps to any sized mode
static uint64_t wrappingpower(uint64_t base, uint64_t exponent)
{
    uint64_t result = 1;
    while (exponent != 0)
    {
        if (exponent & 1)
            result *= base;
        base *= base;
        exponent >>= 1;
    }
    return result;
}

// `base ^ exponent` in sized `mode`, or which side of the range it's on when it doesn't fit
static Fit exactpower(IntegerMode mode, int64_t base, uint64_t exponent, int64_t* result)
{
    // once a step leaves the range, the result's magnitude is past it too, and only its sign is left to know
    bool negativeResult = base < 0 && (exponent & 1) != 0;
    Fit outside = negativeResult ? Fit_Below : Fit_Above;

    int64_t accumulated = 1;
    int64_t square = base;
    while (exponent != 0)
    {
        if ((exponent & 1) != 0 && exactsized(TM_MUL, mode, accumulated, square, &accumulated) != Fit_Fits)
            return outside;

        exponent >>= 1;
        if (exponent != 0 && exactsized(TM_MUL, mode, square, square, &square) != Fit_Fits)
            return outside;
    }

    *result = accumulated;
    return Fit_Fits;
}

// Pushes `i ^ exponent` for the dynamically-sized integer at stack `idx`, keeping every step on the stack
static void pushdynamicpower(lua_State* L, int idx, uint64_t exponent)
{
    int result = lua_gettop(L) + 1;
    pushint(L, 1, IntegerMode_Dynamic);
    lua_pushvalue(L, idx);
    int square = result + 1;

    while (exponent != 0)
    {
        // the collector can reallocate the stack, so the slots are found again each step
        StkId accumulated = L->base + (result - 1);
        StkId squared = L->base + (square - 1);
        if (exponent & 1)
            luaZ_arith(L, accumulated, accumulated, squared, TM_MUL);
        exponent >>= 1;
        if (exponent != 0)
            luaZ_arith(L, squared, squared, squared, TM_MUL);
        luaC_checkGC(L);
    }
    lua_pop(L, 1);
}

enum PowerKind
{
    PowerKind_Wrapping,
    PowerKind_Saturating,
    PowerKind_Checked,
};

template<PowerKind Kind>
static int power(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    uint64_t exponent = checkcount(L, 2, "an exponent");
    IntegerMode mode = integermode(i);

    if (mode == IntegerMode_Dynamic)
    {
        pushdynamicpower(L, 1, exponent);
        return 1;
    }

    if (Kind == PowerKind_Wrapping)
    {
        pushint(L, int64_t(wrappingpower(luaZ_bits(i), exponent)), mode);
        return 1;
    }

    int64_t result = 0;
    Fit fit = exactpower(mode, i->value.l, exponent, &result);
    if (fit == Fit_Fits)
    {
        pushint(L, result, mode);
        return 1;
    }

    if (Kind == PowerKind_Saturating)
    {
        pushint(L, fit == Fit_Above ? largestof(mode) : smallestof(mode), mode);
        return 1;
    }

    // the exact power can be far too large to build, so the error holds no value
    lua_pushnil(L);
    luaZ_pushmode(L, mode);
    luaR_raisevariant(L, fit == Fit_Above ? LUWU_BUILTIN_ERR_OVERFLOW : LUWU_BUILTIN_ERR_UNDERFLOW, 2);
}

// The integer square root of a 64-bit value: the estimate from a double, corrected
static uint64_t isqrt64(uint64_t n)
{
    uint64_t root = uint64_t(sqrt(double(n)));
    while (root > 0 && (root > UINT32_MAX || root * root > n))
        root--;
    while (root < UINT32_MAX && (root + 1) * (root + 1) <= n)
        root++;
    return root;
}

static int integer_sqrt(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    if (luaZ_sign(i) < 0)
        invalidargument(L, 1, "can't take the square root of a negative integer");

    if (ttype(i) == LUA_TINTEGER)
    {
        pushint(L, int64_t(isqrt64(luaZ_bits(i))), integermode(i));
        return 1;
    }

    // Newton's method from a power of two above the root: x = (x + i // x) // 2 until it stops decreasing
    int x = lua_gettop(L) + 1;
    luaZ_powerof2(L, L->top, (luaZ_bitlength(i) + 1) / 2);
    incr_top(L);
    pushint(L, 2, IntegerMode_Dynamic);
    int two = x + 1;

    for (;;)
    {
        lua_pushvalue(L, 1);
        StkId next = L->top - 1;
        luaZ_arith(L, next, next, luaA_toobject(L, x), TM_IDIV);
        luaZ_arith(L, next, next, luaA_toobject(L, x), TM_ADD);
        luaZ_arith(L, next, next, luaA_toobject(L, two), TM_IDIV);

        bool decreased = luaZ_compare(next, luaA_toobject(L, x)) < 0;
        if (!decreased)
        {
            lua_pop(L, 2);
            return 1;
        }
        lua_replace(L, x);
        luaC_checkGC(L);
    }
}

static void checkpositive(lua_State* L, const TValue* i)
{
    if (luaZ_sign(i) <= 0)
        invalidargument(L, 1, "the logarithm of an integer needs a positive integer");
}

static int integer_log2(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    checkpositive(L, i);
    pushint(L, int64_t(luaZ_bitlength(i) - 1), integermode(i));
    return 1;
}

static int integer_log10(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    checkpositive(L, i);

    // one less than the number of decimal digits
    IntegerMode mode = integermode(i);
    luaZ_pushstring(L, i);
    size_t digits = 0;
    lua_tolstring(L, -1, &digits);
    lua_pop(L, 1);
    pushint(L, int64_t(digits - 1), mode);
    return 1;
}

template<IntegerBitwise Op>
static int bitwise(lua_State* L)
{
    const TValue* first = checkinteger(L, 1);
    int count = lua_gettop(L);
    lua_pushvalue(L, 1);
    for (int i = 2; i <= count; i++)
    {
        const TValue* o = checksamemode(L, i, first);
        luaZ_bitwise(L, L->top - 1, L->top - 1, o, Op);
    }
    return 1;
}

static int integer_btest(lua_State* L)
{
    bitwise<IntegerBitwise_And>(L);
    lua_pushboolean(L, luaZ_sign(luaA_toobject(L, -1)) != 0);
    return 1;
}

static int integer_bnot(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    IntegerMode mode = integermode(i);
    if (mode != IntegerMode_Dynamic)
    {
        pushint(L, int64_t(~luaZ_bits(i)), mode);
        return 1;
    }

    // with infinitely many sign bits, ~i is -i - 1
    TValue one;
    luaZ_setint(&one, 1, IntegerMode_Dynamic);
    luaZ_arith(L, L->top, i, i, TM_UNM);
    luaZ_arith(L, L->top, L->top, &one, TM_SUB);
    incr_top(L);
    return 1;
}

static uint64_t checkdynamicbits(lua_State* L, int idx, uint64_t bits)
{
    if (bits > kMaxDynamicBits)
        invalidargument(L, idx, "shifts and fields of a dynamically-sized integer can reach 2^32 bits at most");
    return bits;
}

static int integer_lshift(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    uint64_t by = checkcount(L, 2, "a shift");
    IntegerMode mode = integermode(i);

    if (mode != IntegerMode_Dynamic)
    {
        bool pastWidth = by >= uint64_t(luaZ_modewidth(mode));
        pushint(L, pastWidth ? 0 : int64_t(luaZ_bits(i) << by), mode);
        return 1;
    }

    luaZ_powerof2(L, L->top, checkdynamicbits(L, 2, by));
    incr_top(L);
    luaZ_arith(L, L->top - 1, luaA_toobject(L, 1), L->top - 1, TM_MUL);
    return 1;
}

static int integer_rshift(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    uint64_t by = checkcount(L, 2, "a shift");
    IntegerMode mode = integermode(i);
    bool negative = luaZ_sign(i) < 0;

    if (mode != IntegerMode_Dynamic)
    {
        bool pastWidth = by >= uint64_t(luaZ_modewidth(mode));
        if (isUnsignedIntegerMode(mode))
            pushint(L, pastWidth ? 0 : int64_t(luaZ_bits(i) >> by), mode);
        else
            pushint(L, pastWidth ? (negative ? -1 : 0) : i->value.l >> by, mode);
        return 1;
    }

    // a dynamically-sized shift rounds toward negative infinity, so shifting out every bit gives 0 or -1
    if (by >= luaZ_bitlength(i))
    {
        pushint(L, negative ? -1 : 0, IntegerMode_Dynamic);
        return 1;
    }

    luaZ_powerof2(L, L->top, by);
    incr_top(L);
    luaZ_arith(L, L->top - 1, luaA_toobject(L, 1), L->top - 1, TM_IDIV);
    return 1;
}

// The sized integer at `idx`; a dynamically-sized one raises, since it has no width to `what`
static const TValue* checksized(lua_State* L, int idx, const char* what)
{
    const TValue* i = checkinteger(L, idx);
    if (integermode(i) == IntegerMode_Dynamic)
        invalidargument(L, idx, lua_pushfstring(L, "a dynamically-sized integer has no width to %s", what));
    return i;
}

template<bool Left>
static int rotate(lua_State* L)
{
    const TValue* i = checksized(L, 1, "rotate within");
    IntegerMode mode = integermode(i);
    int width = luaZ_modewidth(mode);
    checkcount(L, 2, "a rotation");

    // the rotation modulo the width, read as a dynamically-sized integer so a count of any mode or size works
    TValue count;
    TValue modulus;
    TValue by;
    luaZ_convert(L, &count, luaA_toobject(L, 2), IntegerMode_Dynamic);
    luaZ_setint(&modulus, width, IntegerMode_Dynamic);
    luaZ_arith(L, &by, &count, &modulus, TM_MOD);

    uint64_t bits = widthbits(i);
    uint64_t left = Left ? luaZ_bits(&by) : (width - luaZ_bits(&by)) % width;
    uint64_t rotated = left == 0 ? bits : (bits << left) | (bits >> (width - left));
    pushint(L, int64_t(rotated), mode);
    return 1;
}

static int popcount64(uint64_t bits)
{
    int count = 0;
    for (; bits != 0; bits &= bits - 1)
        count++;
    return count;
}

static int integer_countlz(lua_State* L)
{
    const TValue* i = checksized(L, 1, "count leading zeros within");
    int length = 0;
    for (uint64_t bits = widthbits(i); bits != 0; bits >>= 1)
        length++;
    pushint(L, luaZ_modewidth(integermode(i)) - length, integermode(i));
    return 1;
}

static int integer_countrz(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    IntegerMode mode = integermode(i);

    // zero has no lowest set bit: a sized one counts its whole width, and a dynamically-sized one has none
    if (luaZ_sign(i) == 0 && mode == IntegerMode_Dynamic)
        invalidargument(L, 1, "0int has no lowest set bit to count up to");
    if (luaZ_sign(i) == 0)
    {
        pushint(L, luaZ_modewidth(mode), mode);
        return 1;
    }

    // a sized integer's bits within its width, and the absolute value of a dynamically-sized one, which has the same
    // trailing zeros as its two's complement
    pushint(L, int64_t(luaZ_trailingzeros(i)), mode);
    return 1;
}

static int integer_popcount(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    IntegerMode mode = integermode(i);
    if (mode != IntegerMode_Dynamic)
    {
        pushint(L, popcount64(widthbits(i)), mode);
        return 1;
    }

    if (luaZ_sign(i) < 0)
        invalidargument(L, 1, "a negative dynamically-sized integer has infinitely many set bits");

    if (ttype(i) == LUA_TINTEGER)
    {
        pushint(L, popcount64(luaZ_bits(i)), mode);
        return 1;
    }

    const HeapInteger* h = (const HeapInteger*)i->value.gc;
    int64_t count = 0;
    for (uint32_t digit = 0; digit < h->size; digit++)
        count += popcount64(h->digits[digit]);
    pushint(L, count, mode);
    return 1;
}

// Pushes the `width` bits of the dynamically-sized integer at stack `idx` starting at bit `field`. Floored division
// and remainder by powers of two read the bits of its two's complement.
static void pushdynamicfield(lua_State* L, int idx, uint64_t field, uint64_t width)
{
    lua_pushvalue(L, idx);
    luaZ_powerof2(L, L->top, field);
    incr_top(L);
    luaZ_arith(L, L->top - 2, L->top - 2, L->top - 1, TM_IDIV);
    luaZ_powerof2(L, L->top - 1, width);
    luaZ_arith(L, L->top - 2, L->top - 2, L->top - 1, TM_MOD);
    lua_pop(L, 1);
}

// The field and width of `extract` and `replace` at stack `fieldidx`, checked against the width of sized `mode`
static void checkfield(lua_State* L, int fieldidx, IntegerMode mode, uint64_t* field, uint64_t* width)
{
    *field = checkcount(L, fieldidx, "a field");
    *width = lua_isnoneornil(L, fieldidx + 1) ? 1 : checkcount(L, fieldidx + 1, "a width");
    if (*width == 0)
        invalidargument(L, fieldidx + 1, "a width must be at least 1");

    if (mode == IntegerMode_Dynamic)
    {
        checkdynamicbits(L, fieldidx, *field);
        checkdynamicbits(L, fieldidx + 1, *width);
        return;
    }

    uint64_t modewidth = uint64_t(luaZ_modewidth(mode));
    bool fits = *field < modewidth && *width <= modewidth - *field;
    if (!fits)
        invalidargument(L, fieldidx, lua_pushfstring(L, "the field ends past the %d bits of a %s", int(modewidth), luaZ_modename(mode)));
}

static uint64_t fieldmask(uint64_t width)
{
    return width >= 64 ? UINT64_MAX : (uint64_t(1) << width) - 1;
}

static int integer_extract(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    IntegerMode mode = integermode(i);
    uint64_t field = 0;
    uint64_t width = 0;
    checkfield(L, 2, mode, &field, &width);

    if (mode == IntegerMode_Dynamic)
        pushdynamicfield(L, 1, field, width);
    else
        pushint(L, int64_t((luaZ_bits(i) >> field) & fieldmask(width)), mode);
    return 1;
}

static int integer_replace(lua_State* L)
{
    const TValue* i = checkinteger(L, 1);
    const TValue* value = checksamemode(L, 2, i);
    IntegerMode mode = integermode(i);
    uint64_t field = 0;
    uint64_t width = 0;
    checkfield(L, 3, mode, &field, &width);

    if (mode != IntegerMode_Dynamic)
    {
        uint64_t mask = fieldmask(width) << field;
        uint64_t replaced = (luaZ_bits(i) & ~mask) | ((luaZ_bits(value) << field) & mask);
        pushint(L, int64_t(replaced), mode);
        return 1;
    }

    // i + (extract(value, 0, width) - extract(i, field, width)) * 2^field
    pushdynamicfield(L, 2, 0, width);
    pushdynamicfield(L, 1, field, width);
    luaZ_arith(L, L->top - 2, L->top - 2, L->top - 1, TM_SUB);
    luaZ_powerof2(L, L->top - 1, field);
    luaZ_arith(L, L->top - 2, L->top - 2, L->top - 1, TM_MUL);
    luaZ_arith(L, L->top - 2, L->top - 2, luaA_toobject(L, 1), TM_ADD);
    lua_pop(L, 1);
    return 1;
}

static int integer_bswap(lua_State* L)
{
    const TValue* i = checksized(L, 1, "reverse the bytes of");
    int width = luaZ_modewidth(integermode(i));
    uint64_t bits = widthbits(i);
    uint64_t swapped = 0;
    for (int byte = 0; byte < width / 8; byte++)
        swapped = (swapped << 8) | ((bits >> (byte * 8)) & 0xff);
    pushint(L, int64_t(swapped), integermode(i));
    return 1;
}

// Calling a mode casts to it, like integer.cast
static int mode_call(lua_State* L)
{
    IntegerMode mode = luaZ_checkmode(L, 1);
    luaL_checkany(L, 2);
    return castto(L, 2, int(mode));
}

static int mode_tostring(lua_State* L)
{
    lua_pushstring(L, luaZ_modename(luaZ_checkmode(L, 1)));
    return 1;
}

static const luaR_NativeMember kModeMembers[] = {
    {"min", NULL, LBC_CLASSMEMBER_CONST},
    {"max", NULL, LBC_CLASSMEMBER_CONST},
    {"name", NULL, LBC_CLASSMEMBER_CONST | LBC_CLASSMEMBER_PRIVATE},
    {"__call", mode_call, 0},
    {"__tostring", mode_tostring, 0},
};

// The functions that are methods of integers too, after `cast`
#define LUWU_INTEGER_METHODS(X) \
    X("tonumber", integer_tonumber) \
    X("clamp", integer_clamp) \
    X("abs", integer_abs) \
    X("signum", integer_signum) \
    X("sadd", (boundedarith<TM_ADD, true>)) \
    X("ssub", (boundedarith<TM_SUB, true>)) \
    X("smul", (boundedarith<TM_MUL, true>)) \
    X("cadd", (boundedarith<TM_ADD, false>)) \
    X("csub", (boundedarith<TM_SUB, false>)) \
    X("cmul", (boundedarith<TM_MUL, false>)) \
    X("cdiv", integer_cdiv) \
    X("pow", power<PowerKind_Wrapping>) \
    X("spow", power<PowerKind_Saturating>) \
    X("cpow", power<PowerKind_Checked>) \
    X("sqrt", integer_sqrt) \
    X("log2", integer_log2) \
    X("log10", integer_log10) \
    X("band", bitwise<IntegerBitwise_And>) \
    X("bor", bitwise<IntegerBitwise_Or>) \
    X("bxor", bitwise<IntegerBitwise_Xor>) \
    X("bnot", integer_bnot) \
    X("lshift", integer_lshift) \
    X("rshift", integer_rshift) \
    X("lrotate", rotate<true>) \
    X("rrotate", rotate<false>) \
    X("countlz", integer_countlz) \
    X("countrz", integer_countrz) \
    X("popcount", integer_popcount) \
    X("extract", integer_extract) \
    X("replace", integer_replace) \
    X("btest", integer_btest) \
    X("bswap", integer_bswap)

#define LUWU_INTEGER_REG(name, fn) {name, fn},

static const luaL_Reg integerlib[] = {
    {"cast", integer_cast},
    {"mode", integer_mode},
    {"isbig", integer_isbig},
    {"min", minmax<false>},
    {"max", minmax<true>},
    LUWU_INTEGER_METHODS(LUWU_INTEGER_REG)
    {NULL, NULL},
};

static const luaL_Reg integermethods[] = {
    {"cast", integer_castmethod},
    LUWU_INTEGER_METHODS(LUWU_INTEGER_REG)
    {NULL, NULL},
};

#undef LUWU_INTEGER_REG
#undef LUWU_INTEGER_METHODS

// Builds `integer.Mode` and one object of it per mode, as fields of the library on top of the stack and in the registry
static void openmodes(lua_State* L)
{
    static const char* const kFieldNames[] = {"dynamic", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64"};
    static_assert(sizeof(kFieldNames) / sizeof(kFieldNames[0]) == IntegerMode_U64 + 1, "one field per IntegerMode");

    LuauClass* classdef = luaR_newnativeclass(L, "Mode", kModeMembers, int(sizeof(kModeMembers) / sizeof(kModeMembers[0])), NULL);
    classdef->typeofname = luaS_new(L, "integermode");
    L->global->builtinclasses[LUWU_BUILTIN_INTEGERMODE] = classdef;
    lua_setfield(L, -2, "Mode");

    lua_createtable(L, IntegerMode_U64 + 1, 0);
    for (int mode = 0; mode <= IntegerMode_U64; mode++)
    {
        // A new object is white, so storing into it needs no barrier
        LuauObject* object = luaR_newobjectuninit(L, classdef);
        TValue* members = luaR_members(object);
        if (mode == IntegerMode_Dynamic)
        {
            setnilvalue(&members[ModeField_Min]);
            setnilvalue(&members[ModeField_Max]);
        }
        else
        {
            luaZ_setint(&members[ModeField_Min], smallestof(IntegerMode(mode)), IntegerMode(mode));
            luaZ_setint(&members[ModeField_Max], largestof(IntegerMode(mode)), IntegerMode(mode));
        }
        setsvalue(L, &members[ModeField_Name], L->global->integermodename[mode]);

        setobjectvalue(L, L->top, object);
        incr_top(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -4, kFieldNames[mode]);
        lua_rawseti(L, -2, mode + 1);
    }
    lua_setreadonly(L, -1, true);
    lua_setfield(L, LUA_REGISTRYINDEX, kModesKey);
}

// `integer.suffixes`: a bundle when the bundle library is open, and a frozen array otherwise
static void opensuffixes(lua_State* L)
{
    static const IntegerMode kOrder[] = {
        IntegerMode_Dynamic,
        IntegerMode_U8,
        IntegerMode_U16,
        IntegerMode_U32,
        IntegerMode_U64,
        IntegerMode_I8,
        IntegerMode_I16,
        IntegerMode_I32,
        IntegerMode_I64,
    };
    const int count = int(sizeof(kOrder) / sizeof(kOrder[0]));

    bool bundles = L->global->builtinclasses[LUWU_BUILTIN_BIGBUNDLE] != NULL;
    if (!bundles)
        lua_createtable(L, count, 0);
    for (int i = 0; i < count; i++)
    {
        lua_pushstring(L, luaZ_modename(kOrder[i]));
        if (!bundles)
            lua_rawseti(L, -2, i + 1);
    }

    if (bundles)
        lua_packbundle(L, count);
    else
        lua_setreadonly(L, -1, true);
    lua_setfield(L, -2, "suffixes");
}

// The integers' metatable, whose `__index` is the methods. A bigint has its own tag, so it gets the same table.
static void openmethods(lua_State* L)
{
    lua_createtable(L, 0, 1);
    lua_newtable(L);
    luaL_register(L, NULL, integermethods);
    lua_setreadonly(L, -1, true);
    lua_setfield(L, -2, "__index");
    lua_setreadonly(L, -1, true);

    pushint(L, 0, IntegerMode_Dynamic);
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);

    luaZ_powerof2(L, L->top, 64);
    incr_top(L);
    lua_insert(L, -2);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

LUALIB_API int luaopen_integer(lua_State* L)
{
    luaL_register(L, LUA_INTEGERLIBNAME, integerlib);
    openmodes(L);
    opensuffixes(L);
    openmethods(L);
    return 1;
}
