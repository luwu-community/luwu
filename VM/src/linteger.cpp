// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "linteger.h"

#include "lualib.h"

#include "lbigint.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lnumutils.h"
#include "lstate.h"
#include "lstring.h"

#include <math.h>
#include <string.h>

static const char* const kModeNames[] = {"int", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64"};
static const int kModeWidths[] = {0, 8, 8, 16, 16, 32, 32, 64, 64};

static_assert(sizeof(kModeNames) / sizeof(kModeNames[0]) == IntegerMode_U64 + 1, "one name per IntegerMode");
static_assert(sizeof(kModeWidths) / sizeof(kModeWidths[0]) == IntegerMode_U64 + 1, "one width per IntegerMode");

// The bits in a bigint's digit
static const int kDigitBits = 64;

const char* luaZ_modename(IntegerMode mode)
{
    LUAU_ASSERT(mode <= IntegerMode_U64);
    return kModeNames[mode];
}

int luaZ_modewidth(IntegerMode mode)
{
    LUAU_ASSERT(mode <= IntegerMode_U64);
    return kModeWidths[mode];
}

static int countleadingzeros(uint64_t value)
{
    LUAU_ASSERT(value != 0);
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(value);
#else
    int count = 0;
    while (!(value & (uint64_t(1) << 63)))
    {
        value <<= 1;
        count++;
    }
    return count;
#endif
}

static int counttrailingzeros(uint64_t value)
{
    LUAU_ASSERT(value != 0);
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(value);
#else
    int count = 0;
    while (!(value & 1))
    {
        value >>= 1;
        count++;
    }
    return count;
#endif
}

// An integer split into its inline value or its bigint
struct Integer
{
    int64_t value;
    HeapInteger* heap;
    IntegerMode mode;
};

static Integer unpack(const TValue* o)
{
    LUAU_ASSERT(ttisinteger(o));
    Integer i;
    i.mode = integermode(o);
    i.value = ttype(o) == LUA_TINTEGER ? o->value.l : 0;
    i.heap = ttype(o) == LUA_TINTEGER ? NULL : (HeapInteger*)o->value.gc;
    return i;
}

// An integer as a sign and the digits of its absolute value, the form the bigint functions take. An inline integer
// is viewed as a one-digit bigint on the C stack.
struct IntegerView
{
    HeapInteger inlined;
    uint64_t digit;
    const HeapInteger* heap;

    explicit IntegerView(const Integer& i)
    {
        if (i.heap)
        {
            heap = i.heap;
            return;
        }

        bool negative = !isUnsignedIntegerMode(i.mode) && i.value < 0;
        digit = negative ? 0 - uint64_t(i.value) : uint64_t(i.value);
        inlined.isNegative = negative;
        inlined.size = digit == 0 ? 0 : 1;
        inlined.capacity = 1;
        inlined.digits = &digit;
        heap = &inlined;
    }

private:
    IntegerView(const IntegerView&);
    IntegerView& operator=(const IntegerView&);
};

void luaZ_setint(TValue* res, int64_t value, IntegerMode mode)
{
    setintegersmi(res, wrapIntegerToMode(value, mode), mode);
}

static HeapInteger* newbigint(lua_State* L, uint64_t magnitude, bool negative)
{
    HeapInteger* h = luaZB_newheapinteger(L, 1);
    h->digits[0] = magnitude;
    h->size = magnitude == 0 ? 0 : 1;
    h->isNegative = negative && magnitude != 0;
    return h;
}

// Stores the dynamically-sized integer `h`, inline when it fits in 64 bits
static void setbigint(TValue* res, HeapInteger* h)
{
    const uint64_t kInt64MinMagnitude = uint64_t(1) << 63;

    if (h->size == 0)
    {
        luaZ_setint(res, 0, IntegerMode_Dynamic);
        return;
    }

    if (h->size == 1)
    {
        uint64_t magnitude = h->digits[0];
        bool fitsPositive = !h->isNegative && magnitude < kInt64MinMagnitude;
        bool fitsNegative = h->isNegative && magnitude <= kInt64MinMagnitude;
        if (fitsPositive || fitsNegative)
        {
            luaZ_setint(res, int64_t(h->isNegative ? 0 - magnitude : magnitude), IntegerMode_Dynamic);
            return;
        }
    }

    setintegerheap(res, h, IntegerMode_Dynamic);
}

void luaZ_setunsigned(lua_State* L, TValue* res, uint64_t value, IntegerMode mode)
{
    if (mode == IntegerMode_Dynamic && value > uint64_t(INT64_MAX))
        setintegerheap(res, newbigint(L, value, false), IntegerMode_Dynamic);
    else
        luaZ_setint(res, int64_t(value), mode);
}

static l_noret divisionbyzero(lua_State* L)
{
    luaR_raisevariant(L, LUWU_BUILTIN_ERR_DIVISIONBYZERO, 0);
}

// Luwu structured runtime errors: ordering `l` and `r` with `op` (TM_LT or TM_LE) raises an Error.InvalidOperation
static l_noret invalidcomparison(lua_State* L, const TValue* l, const TValue* r, TMS op)
{
    luaG_ordererror(L, l, r, op);
}

// `a / b`, `a // b` or `a % b` for a nonzero `b`, wrapping INT64_MIN / -1 to INT64_MIN
static int64_t divide(int64_t a, int64_t b, TMS op)
{
    LUAU_ASSERT(b != 0);
    if (b == -1)
        return op == TM_MOD ? 0 : int64_t(0 - uint64_t(a));

    int64_t quotient = a / b;
    int64_t remainder = a % b;
    bool roundsDown = remainder != 0 && ((remainder < 0) != (b < 0));

    if (op == TM_DIV)
        return quotient;
    if (op == TM_IDIV)
        return roundsDown ? quotient - 1 : quotient;
    return roundsDown ? remainder + b : remainder;
}

static l_noret negatedunsigned(lua_State* L, IntegerMode mode)
{
    const char* name = luaZ_modename(mode);
    luaG_runerror(L, "attempt to negate a %s, which can't be negative; write 0%s - x for its wrapped negation", name, name);
}

static l_noret integerpower(lua_State* L)
{
    luaG_runerror(L, "integers have no '^' operator; use x:pow(e), which wraps, or x:spow(e) or x:cpow(e)");
}

// An operator on two sized integers of `mode`
static void sizedarith(lua_State* L, StkId ra, int64_t a, int64_t b, IntegerMode mode, TMS op)
{
    uint64_t x = uint64_t(a);
    uint64_t y = uint64_t(b);
    bool isSigned = !isUnsignedIntegerMode(mode);
    uint64_t result = 0;

    switch (op)
    {
    case TM_ADD:
        result = x + y;
        break;
    case TM_SUB:
        result = x - y;
        break;
    case TM_MUL:
        result = x * y;
        break;
    case TM_DIV:
    case TM_IDIV:
    case TM_MOD:
        if (y == 0)
            divisionbyzero(L);
        if (isSigned)
            result = uint64_t(divide(a, b, op));
        else
            result = op == TM_MOD ? x % y : x / y;
        break;
    case TM_UNM:
        if (!isSigned)
            negatedunsigned(L, mode);
        result = 0 - x;
        break;
    case TM_POW:
        integerpower(L);
    default:
        LUAU_ASSERT(!"not an integer operator");
    }

    luaZ_setint(ra, int64_t(result), mode);
}

// An operator on two dynamically-sized integers that fit in 64 bits; returns false when the result doesn't
static bool inlinearith(lua_State* L, StkId ra, int64_t a, int64_t b, TMS op)
{
    int64_t result = 0;

    switch (op)
    {
    case TM_ADD:
        if (luau_add_overflow(a, b, &result))
            return false;
        break;
    case TM_SUB:
        if (luau_sub_overflow(a, b, &result))
            return false;
        break;
    case TM_MUL:
        if (luau_mul_overflow(a, b, &result))
            return false;
        break;
    case TM_DIV:
    case TM_IDIV:
    case TM_MOD:
        if (b == 0)
            divisionbyzero(L);
        if (a == INT64_MIN && b == -1 && op != TM_MOD)
            return false;
        result = divide(a, b, op);
        break;
    case TM_UNM:
        if (a == INT64_MIN)
            return false;
        result = -a;
        break;
    case TM_POW:
        integerpower(L);
    default:
        LUAU_ASSERT(!"not an integer operator");
    }

    luaZ_setint(ra, result, IntegerMode_Dynamic);
    return true;
}

// An operator on two dynamically-sized integers through the bigint functions
static void bigarith(lua_State* L, StkId ra, const Integer& a, const Integer& b, TMS op)
{
    IntegerView x(a);
    IntegerView y(b);
    HeapInteger* result = NULL;

    switch (op)
    {
    case TM_ADD:
        result = luaZB_heapinteger_add(L, x.heap, y.heap);
        break;
    case TM_SUB:
        result = luaZB_heapinteger_sub(L, x.heap, y.heap);
        break;
    case TM_MUL:
        result = luaZB_heapinteger_mul(L, x.heap, y.heap);
        break;
    case TM_DIV:
    case TM_IDIV:
        if (y.heap->size == 0)
            divisionbyzero(L);
        result = luaZB_heapinteger_div(L, x.heap, y.heap);
        if (op == TM_IDIV && x.heap->isNegative != y.heap->isNegative)
        {
            // the quotient rounded toward zero is one too large when there's a remainder
            HeapInteger* remainder = luaZB_heapinteger_rem(L, x.heap, y.heap);
            if (remainder->size != 0)
            {
                Integer one = {1, NULL, IntegerMode_Dynamic};
                IntegerView step(one);
                result = luaZB_heapinteger_sub(L, result, step.heap);
            }
        }
        break;
    case TM_MOD:
        if (y.heap->size == 0)
            divisionbyzero(L);
        result = luaZB_heapinteger_mod(L, x.heap, y.heap);
        break;
    case TM_UNM:
        result = luaZB_heapinteger_neg(L, x.heap);
        break;
    case TM_POW:
        integerpower(L);
    default:
        LUAU_ASSERT(!"not an integer operator");
    }

    setbigint(ra, result);
}

void luaZ_arith(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, TMS op)
{
    Integer a = unpack(rb);
    Integer b = op == TM_UNM ? a : unpack(rc);

    if (a.mode != b.mode)
        luaG_aritherror(L, rb, rc, op);

    if (a.mode != IntegerMode_Dynamic)
    {
        sizedarith(L, ra, a.value, b.value, a.mode, op);
        return;
    }

    bool bothInline = !a.heap && !b.heap;
    if (bothInline && inlinearith(L, ra, a.value, b.value, op))
        return;

    bigarith(L, ra, a, b, op);
}

int luaZ_compare(const TValue* a, const TValue* b)
{
    LUAU_ASSERT(integermode(a) == integermode(b));

    bool bothInline = ttype(a) == LUA_TINTEGER && ttype(b) == LUA_TINTEGER;
    if (bothInline && !isUnsignedIntegerMode(integermode(a)))
        return a->value.l < b->value.l ? -1 : (a->value.l > b->value.l ? 1 : 0);

    if (bothInline)
    {
        uint64_t x = uint64_t(a->value.l);
        uint64_t y = uint64_t(b->value.l);
        return x < y ? -1 : (x > y ? 1 : 0);
    }

    IntegerView x(unpack(a));
    IntegerView y(unpack(b));
    int order = luaZB_heapinteger_cmp(x.heap, y.heap);
    return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

int luaZ_sign(const TValue* o)
{
    if (ttype(o) == LUA_THEAPINTEGER)
        return ((HeapInteger*)o->value.gc)->isNegative ? -1 : 1;

    if (o->value.l == 0)
        return 0;

    bool negative = !isUnsignedIntegerMode(integermode(o)) && o->value.l < 0;
    return negative ? -1 : 1;
}

static bool comparable(const TValue* l, const TValue* r)
{
    return ttisinteger(l) && ttisinteger(r) && integermode(l) == integermode(r);
}

int luaZ_lessthan(lua_State* L, const TValue* l, const TValue* r)
{
    if (!comparable(l, r))
        invalidcomparison(L, l, r, TM_LT);
    return luaZ_compare(l, r) < 0;
}

int luaZ_lessequal(lua_State* L, const TValue* l, const TValue* r)
{
    if (!comparable(l, r))
        invalidcomparison(L, l, r, TM_LE);
    return luaZ_compare(l, r) <= 0;
}

int luaZ_equal(const TValue* a, const TValue* b)
{
    // equal integers have the same representation, so a bigint never equals an inline integer
    if (ttype(a) != ttype(b) || integermode(a) != integermode(b))
        return 0;

    if (ttype(a) == LUA_TINTEGER)
        return a->value.l == b->value.l;

    return luaZ_compare(a, b) == 0;
}

int luaZ_equalkey(const TKey* a, const TValue* b)
{
    TValue key;
    key.value = a->value;
    key.extra[0] = a->extra[0];
    key.tt = a->tt;
    return luaZ_equal(&key, b);
}

// The finalizer of MurmurHash64B, truncated to 32 bits
static unsigned mixhash(uint64_t bits)
{
    const uint32_t m = 0x5bd1e995;

    uint32_t h1 = uint32_t(bits);
    uint32_t h2 = uint32_t(bits >> 32);

    h1 ^= h2 >> 18;
    h1 *= m;
    h2 ^= h1 >> 22;
    h2 *= m;
    h1 ^= h2 >> 17;
    h1 *= m;
    h2 ^= h1 >> 19;
    h2 *= m;

    return h2;
}

unsigned luaZ_hash(const TValue* o)
{
    unsigned mode = unsigned(integermode(o));
    if (ttype(o) == LUA_TINTEGER)
        return mixhash(uint64_t(o->value.l)) ^ mode;

    const HeapInteger* h = (const HeapInteger*)o->value.gc;
    unsigned hash = h->isNegative ? 1 : 0;
    for (uint32_t i = 0; i < h->size; i++)
        hash = mixhash(h->digits[i] ^ (uint64_t(hash) << 32));
    return hash;
}

IntegerConversion luaZ_convert(lua_State* L, TValue* res, const TValue* o, IntegerMode mode)
{
    Integer i = unpack(o);

    if (i.mode == mode)
    {
        setobj(L, res, o);
        return IntegerConversion_Ok;
    }

    if (mode == IntegerMode_Dynamic)
    {
        if (!isUnsignedIntegerMode(i.mode))
            luaZ_setint(res, i.value, IntegerMode_Dynamic);
        else
            luaZ_setunsigned(L, res, uint64_t(i.value), IntegerMode_Dynamic);
        return IntegerConversion_Ok;
    }

    IntegerView view(i);
    bool negative = view.heap->isNegative;
    if (view.heap->size > 1)
        return negative ? IntegerConversion_Underflow : IntegerConversion_Overflow;

    uint64_t magnitude = view.heap->size == 0 ? 0 : view.heap->digits[0];
    int width = luaZ_modewidth(mode);

    if (!isUnsignedIntegerMode(mode))
    {
        uint64_t limit = uint64_t(1) << (width - 1);
        if (negative && magnitude > limit)
            return IntegerConversion_Underflow;
        if (!negative && magnitude >= limit)
            return IntegerConversion_Overflow;
    }
    else
    {
        uint64_t largest = width == 64 ? UINT64_MAX : (uint64_t(1) << width) - 1;
        if (negative)
            return IntegerConversion_Underflow;
        if (magnitude > largest)
            return IntegerConversion_Overflow;
    }

    luaZ_setint(res, int64_t(negative ? 0 - magnitude : magnitude), mode);
    return IntegerConversion_Ok;
}

// A whole number past INT64_MAX in magnitude as a bigint
static HeapInteger* bigintfromnumber(lua_State* L, double n)
{
    int exponent = 0;
    double fraction = frexp(fabs(n), &exponent);
    uint64_t mantissa = uint64_t(ldexp(fraction, 53));
    int shift = exponent - 53;
    LUAU_ASSERT(shift > 0);

    uint32_t size = uint32_t(exponent / kDigitBits + 1);
    HeapInteger* h = luaZB_newheapinteger(L, size);
    for (uint32_t i = 0; i < size; i++)
        h->digits[i] = 0;

    int digit = shift / kDigitBits;
    int offset = shift % kDigitBits;
    h->digits[digit] = mantissa << offset;
    if (offset != 0 && uint32_t(digit + 1) < size)
        h->digits[digit + 1] = mantissa >> (kDigitBits - offset);

    h->size = size;
    while (h->size > 0 && h->digits[h->size - 1] == 0)
        h->size--;
    h->isNegative = n < 0;
    return h;
}

IntegerConversion luaZ_fromnumber(lua_State* L, TValue* res, double n, IntegerMode mode)
{
    LUAU_ASSERT(n == n && n == floor(n));

    // 2^63, past every int64_t
    const double kTwoTo63 = 9223372036854775808.0;

    if (mode == IntegerMode_Dynamic)
    {
        if (isinf(n))
            return n > 0 ? IntegerConversion_Overflow : IntegerConversion_Underflow;

        if (n >= -kTwoTo63 && n < kTwoTo63)
            luaZ_setint(res, int64_t(n), IntegerMode_Dynamic);
        else
            setintegerheap(res, bigintfromnumber(L, n), IntegerMode_Dynamic);
        return IntegerConversion_Ok;
    }

    int width = luaZ_modewidth(mode);
    if (!isUnsignedIntegerMode(mode))
    {
        double limit = ldexp(1.0, width - 1);
        if (n < -limit)
            return IntegerConversion_Underflow;
        if (n >= limit)
            return IntegerConversion_Overflow;
        luaZ_setint(res, int64_t(n), mode);
        return IntegerConversion_Ok;
    }

    if (n < 0)
        return IntegerConversion_Underflow;
    if (n >= ldexp(1.0, width))
        return IntegerConversion_Overflow;

    luaZ_setint(res, int64_t(uint64_t(n)), mode);
    return IntegerConversion_Ok;
}

bool luaZ_fromdecimal(lua_State* L, TValue* res, const char* digits)
{
    HeapInteger* h = luaZB_heapinteger_fromstring(L, digits, NULL);
    if (!h)
        return false;

    setbigint(res, h);
    return true;
}

bool luaZ_tonumber(const TValue* o, double* n)
{
    IntegerView view(unpack(o));
    const HeapInteger* h = view.heap;

    if (h->size == 0)
    {
        *n = 0;
        return true;
    }

    uint64_t bits = luaZ_bitlength(o);
    uint64_t trailing = luaZ_trailingzeros(o);

    // the top 64 bits, with the lowest one set when any bit below them is, so converting rounds to nearest
    uint64_t top = 0;
    int shift = int(bits) - kDigitBits;
    if (shift <= 0)
    {
        top = h->digits[0];
        shift = 0;
    }
    else
    {
        uint32_t digit = uint32_t(shift / kDigitBits);
        int offset = shift % kDigitBits;
        top = h->digits[digit] >> offset;
        if (offset != 0)
            top |= h->digits[digit + 1] << (kDigitBits - offset);
        bool sticky = trailing < uint64_t(shift);
        if (sticky)
            top |= 1;
    }

    double magnitude = bits > 1024 ? HUGE_VAL : ldexp(double(top), shift);
    *n = h->isNegative ? -magnitude : magnitude;

    // a double holds 53 significant bits
    return bits <= 1024 && bits - trailing <= 53;
}

uint64_t luaZ_bits(const TValue* o)
{
    LUAU_ASSERT(ttype(o) == LUA_TINTEGER);
    return uint64_t(o->value.l);
}

uint64_t luaZ_trailingzeros(const TValue* o)
{
    IntegerView view(unpack(o));
    const HeapInteger* h = view.heap;
    LUAU_ASSERT(h->size != 0);

    uint32_t lowest = 0;
    while (h->digits[lowest] == 0)
        lowest++;
    return uint64_t(lowest) * kDigitBits + counttrailingzeros(h->digits[lowest]);
}

uint64_t luaZ_bitlength(const TValue* o)
{
    IntegerView view(unpack(o));
    const HeapInteger* h = view.heap;
    if (h->size == 0)
        return 0;

    uint64_t top = h->digits[h->size - 1];
    return uint64_t(h->size) * kDigitBits - countleadingzeros(top);
}

void luaZ_powerof2(lua_State* L, TValue* res, uint64_t exponent)
{
    if (exponent < 63)
    {
        luaZ_setint(res, int64_t(1) << exponent, IntegerMode_Dynamic);
        return;
    }

    uint32_t size = uint32_t(exponent / kDigitBits + 1);
    HeapInteger* h = luaZB_newheapinteger(L, size);
    for (uint32_t i = 0; i < size; i++)
        h->digits[i] = 0;
    h->digits[size - 1] = uint64_t(1) << (exponent % kDigitBits);
    h->size = size;
    setintegerheap(res, h, IntegerMode_Dynamic);
}

// Writes `count` digits of `h` in two's complement to `out`
static void tocomplement(const HeapInteger* h, uint64_t* out, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        out[i] = i < h->size ? h->digits[i] : 0;

    if (!h->isNegative)
        return;

    uint64_t carry = 1;
    for (uint32_t i = 0; i < count; i++)
    {
        out[i] = ~out[i] + carry;
        carry = carry && out[i] == 0;
    }
}

void luaZ_bitwise(lua_State* L, TValue* res, const TValue* a, const TValue* b, IntegerBitwise op)
{
    LUAU_ASSERT(integermode(a) == integermode(b));

    if (ttype(a) == LUA_TINTEGER && ttype(b) == LUA_TINTEGER)
    {
        uint64_t x = uint64_t(a->value.l);
        uint64_t y = uint64_t(b->value.l);
        uint64_t result = op == IntegerBitwise_And ? x & y : (op == IntegerBitwise_Or ? x | y : x ^ y);
        luaZ_setint(res, int64_t(result), integermode(a));
        return;
    }

    IntegerView x(unpack(a));
    IntegerView y(unpack(b));
    uint32_t count = (x.heap->size > y.heap->size ? x.heap->size : y.heap->size) + 1;

    HeapInteger* left = luaZB_newheapinteger(L, count);
    HeapInteger* right = luaZB_newheapinteger(L, count);
    tocomplement(x.heap, left->digits, count);
    tocomplement(y.heap, right->digits, count);

    for (uint32_t i = 0; i < count; i++)
    {
        uint64_t l = left->digits[i];
        uint64_t r = right->digits[i];
        left->digits[i] = op == IntegerBitwise_And ? l & r : (op == IntegerBitwise_Or ? l | r : l ^ r);
    }

    // back from two's complement: the top bit is the sign, and negating again gives the absolute value
    left->size = count;
    bool negative = (left->digits[count - 1] >> (kDigitBits - 1)) != 0;
    if (negative)
    {
        HeapInteger flipped = *left;
        flipped.isNegative = true;
        tocomplement(&flipped, left->digits, count);
    }

    while (left->size > 0 && left->digits[left->size - 1] == 0)
        left->size--;
    left->isNegative = negative && left->size != 0;
    setbigint(res, left);
}

char* luaZ_formatinline(const TValue* o, char* buf)
{
    LUAU_ASSERT(ttype(o) == LUA_TINTEGER);
    if (!isUnsignedIntegerMode(integermode(o)))
        return luai_int2str(buf, o->value.l);

    // the digits are written backwards, then reversed
    uint64_t value = uint64_t(o->value.l);
    char* end = buf;
    do
    {
        *end++ = char('0' + value % 10);
        value /= 10;
    } while (value != 0);

    for (char *l = buf, *r = end - 1; l < r; l++, r--)
    {
        char c = *l;
        *l = *r;
        *r = c;
    }
    return end;
}

void luaZ_pushstring(lua_State* L, const TValue* o)
{
    if (ttype(o) == LUA_THEAPINTEGER)
    {
        // the bigint is on the stack or in a constant while its digits are pushed
        luaZB_heapinteger_pushstring(L, (const HeapInteger*)o->value.gc);
        return;
    }

    char buf[LUAI_MAXINT2STR];
    char* end = luaZ_formatinline(o, buf);
    lua_pushlstring(L, buf, end - buf);
}

// The `width` bits of `digits` (`count` of them) starting at bit `first`
static uint64_t readbits(const uint64_t* digits, uint32_t count, uint64_t first, int width)
{
    uint32_t digit = uint32_t(first / kDigitBits);
    int offset = int(first % kDigitBits);
    uint64_t bits = digit < count ? digits[digit] >> offset : 0;
    if (offset + width > kDigitBits && digit + 1 < count)
        bits |= digits[digit + 1] << (kDigitBits - offset);
    return bits & ((uint64_t(1) << width) - 1);
}

void luaZ_pushpow2digits(lua_State* L, const TValue* o, int bitsperdigit, bool upper)
{
    LUAU_ASSERT(bitsperdigit == 1 || bitsperdigit == 3 || bitsperdigit == 4);
    const char* alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    IntegerMode mode = integermode(o);
    bool negative = false;
    uint64_t sized = 0;
    const uint64_t* digits = &sized;
    uint32_t count = 1;
    uint64_t bits = 0;

    // A sized integer writes the bits of its width; a dynamically-sized one, its sign and absolute value
    IntegerView view(unpack(o));
    if (mode != IntegerMode_Dynamic)
    {
        int width = luaZ_modewidth(mode);
        sized = uint64_t(o->value.l);
        if (width < 64)
            sized &= (uint64_t(1) << width) - 1;
        bits = sized == 0 ? 0 : uint64_t(kDigitBits - countleadingzeros(sized));
    }
    else
    {
        negative = view.heap->isNegative;
        digits = view.heap->digits;
        count = view.heap->size;
        bits = luaZ_bitlength(o);
    }

    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    if (negative)
        luaL_addchar(&b, '-');

    uint64_t length = bits == 0 ? 1 : (bits + bitsperdigit - 1) / bitsperdigit;
    for (uint64_t i = length; i > 0; i--)
        luaL_addchar(&b, alphabet[readbits(digits, count, (i - 1) * bitsperdigit, bitsperdigit)]);

    luaL_pushresult(&b);
}
