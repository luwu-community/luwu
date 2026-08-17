// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"
#include "lmem.h"
#include "luaconf.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lbigint.h"

constexpr uint64_t kInt64MinAbs = 0x8000000000000000ull;
constexpr uint32_t kHashNegativeFlag = 0x80000000;
constexpr uint32_t kMurmurHashMixConstant = 0x5bd1e995;
constexpr int kMurmurHashMixShift = 24;

struct Integer {
    int64_t smi;
    HeapInteger* heap;
    IntegerMode mode;
};

inline Integer unpack_integer(const TValue* o) {
    Integer b;
    if (ttype(o) == LUA_TINTEGER) {
        b.smi = o->value.l;
        b.heap = nullptr;
    } else {
        b.smi = 0;
        b.heap = (HeapInteger*)o->value.gc;
    }
    b.mode = (IntegerMode)o->extra[0];
    return b;
}

inline void pack_integer(TValue* obj, Integer b) {
    if (b.heap) {
        setintegerheap(obj, b.heap, b.mode);
    } else {
        setintegersmi(obj, b.smi, b.mode);
    }
}

inline Integer new_integer(int64_t v, IntegerMode mode = IntegerMode_Dynamic) {
    Integer b;
    b.smi = v;
    b.heap = nullptr;
    b.mode = mode;
    return b;
}

inline Integer new_integer_from_heap(HeapInteger* h) {
    Integer b;
    b.smi = 0;
    b.heap = h;
    b.mode = IntegerMode_Dynamic;
    return b;
}

uint64_t internal_get_bottom_64(Integer b) {
    if (!b.heap) return (uint64_t)b.smi;
    uint64_t val = 0;
    if (b.heap->size > 0) val |= b.heap->digits[0];
    if (b.heap->size > 1) val |= ((uint64_t)b.heap->digits[1] << 32);
    if (b.heap->isNegative) val = ~val + 1;
    return val;
}

uint64_t luaZ_integer_get_bottom_64(const TValue* o) {
    return internal_get_bottom_64(unpack_integer(o));
}

#define HANDLE_TYPED_MATH(L, a, b, OP) \
    if (a.mode != IntegerMode_Dynamic || b.mode != IntegerMode_Dynamic) { \
        if (a.mode != b.mode) { \
            luaG_runerror(L, "attempt to perform arithmetic on mixed typed integers"); \
        } \
        IntegerMode mode = a.mode; \
        uint64_t va = internal_get_bottom_64(a); \
        uint64_t vb = internal_get_bottom_64(b); \
        uint64_t raw_res = va OP vb; \
        uint8_t shift = luau_int_shifts[mode]; \
        uint64_t res = 0; \
        if (luau_int_signed[mode]) \
            res = (int64_t)(raw_res << shift) >> shift; \
        else \
            res = (raw_res << shift) >> shift; \
        return new_integer((int64_t)res, mode); \
    }

#define DO_TYPED_DIV(TYPE, UTYPE, OP) { UTYPE ta = (UTYPE)va, tb = (UTYPE)vb; if (tb == (UTYPE)-1 && ta == (UTYPE)((TYPE)1 << (sizeof(TYPE)*8-1))) { res = (int64_t)(TYPE)ta; } else { res = (int64_t)(TYPE)((TYPE)ta OP (TYPE)tb); } }
#define DO_TYPED_UDIV(UTYPE, OP) { res = (uint64_t)(UTYPE)((UTYPE)va OP (UTYPE)vb); }

#define HANDLE_TYPED_DIV(L, a, b, OP, is_mod) \
    if (a.mode != IntegerMode_Dynamic || b.mode != IntegerMode_Dynamic) { \
        if (a.mode != b.mode) { \
            luaG_runerror(L, "attempt to perform arithmetic on mixed typed integers"); \
        } \
        IntegerMode mode = a.mode; \
        uint64_t va = internal_get_bottom_64(a); \
        uint64_t vb = internal_get_bottom_64(b); \
        if (vb == 0) luaG_runerror(L, is_mod ? "attempt to perform modulo by zero" : "attempt to divide by zero"); \
        uint64_t res = 0; \
        switch (mode) { \
            case IntegerMode_I8: DO_TYPED_DIV(int8_t, uint8_t, OP); break; \
            case IntegerMode_U8: DO_TYPED_UDIV(uint8_t, OP); break; \
            case IntegerMode_I16: DO_TYPED_DIV(int16_t, uint16_t, OP); break; \
            case IntegerMode_U16: DO_TYPED_UDIV(uint16_t, OP); break; \
            case IntegerMode_I32: DO_TYPED_DIV(int32_t, uint32_t, OP); break; \
            case IntegerMode_U32: DO_TYPED_UDIV(uint32_t, OP); break; \
            case IntegerMode_I64: DO_TYPED_DIV(int64_t, uint64_t, OP); break; \
            case IntegerMode_U64: DO_TYPED_UDIV(uint64_t, OP); break; \
            default: break; \
        } \
        return new_integer((int64_t)res, mode); \
    }


// get_heap_view constructs a temporary HeapInteger on the C stack (or reuses an existing one).
// For SMIs (Small Integers), this entirely avoids GC allocations by pointing the dummy's
// digits pointer directly to the stack-allocated `digit_storage` variable
static HeapInteger* get_heap_view(const Integer& b, HeapInteger* temp, uint64_t* digit_storage) {
    // If it's already a heap integer, just return it directly
    if (b.heap) return b.heap;
    
    // Handle 0 explicitly (empty size)
    if (b.smi == 0) {
        temp->digits = digit_storage;
        temp->size = 0;
        temp->isNegative = false;
        return temp;
    }
    
    bool isNeg = false;
    uint64_t abs_val = (uint64_t)b.smi;
    
    // Convert signed SMI to absolute value
    if (b.mode == IntegerMode_Dynamic) {
        if (b.smi < 0) {
            isNeg = true;
            abs_val = (b.smi == INT64_MIN) ? (uint64_t)INT64_MAX + 1 : (uint64_t)-b.smi;
        }
    } else if (luau_int_signed[b.mode] && b.smi < 0) {
        isNeg = true;
        uint64_t bot = internal_get_bottom_64(b);
        abs_val = (bot == kInt64MinAbs) ? kInt64MinAbs : -bot;
    } else {
        abs_val = internal_get_bottom_64(b);
    }
    
    // As our digits/limbs are 64-bit in size, an SMI perfectly fits in a single limb.
    // We just write to the caller's stack variable and point to it.
    *digit_storage = abs_val;
    temp->size = 1;
    temp->isNegative = isNeg;
    temp->digits = digit_storage;
    return temp;
}

bool luaZ_integer_eq(const TValue* a_val, const TValue* b_val)
{
    if (a_val->extra[0] != b_val->extra[0]) return false;
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    if (ha->isNegative != hb->isNegative) return false;
    return luau_heapint_cmp(ha, hb) == 0;
}

bool luaZ_integer_eq_key(const TKey* a_key, const TValue* b_val)
{
    if (a_key->extra[0] != b_val->extra[0]) return false;
    Integer a;
    if (a_key->tt == LUA_TINTEGER)
    {
        a.smi = a_key->value.l;
        a.heap = nullptr;
    }
    else // LUA_THEAPINTEGER
    {
        a.smi = 0;
        a.heap = (HeapInteger*)a_key->value.gc;
    }
    a.mode = (IntegerMode)a_key->extra[0];

    Integer b = unpack_integer(b_val);
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    if (ha->isNegative != hb->isNegative) return false;
    return luau_heapint_cmp(ha, hb) == 0;
}

uint32_t luaZ_integer_hash(const TValue* b_val)
{
    Integer b = unpack_integer(b_val);
    HeapInteger tb;
    uint64_t db;
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    
    uint32_t mode = b_val->extra[0];
    uint32_t h = hb->size ^ (hb->isNegative ? kHashNegativeFlag : 0) ^ (mode << 16);
    
    // Hash each 64-bit limb by folding it into 32-bits via XOR
    for (uint32_t i = 0; i < hb->size; i++) {
        uint64_t k64 = hb->digits[i];
        uint32_t k = (uint32_t)(k64 ^ (k64 >> 32));
        k *= kMurmurHashMixConstant;
        k ^= k >> kMurmurHashMixShift;
        k *= kMurmurHashMixConstant;
        h *= kMurmurHashMixConstant;
        h ^= k;
    }
    
    return h;
}

static Integer pack_integer_impl(lua_State* L, HeapInteger* h) {
    if (h->size == 0) {
        return new_integer(0);
    }
    if (h->size == 1) {
        uint64_t val = h->digits[0];
        if (!h->isNegative && val <= (uint64_t)INT64_MAX) {
            return new_integer((int64_t)val);
        } else if (h->isNegative && val <= (uint64_t)INT64_MAX + 1) {
            return new_integer(val == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)val);
        }
    }
    return new_integer_from_heap(h);
}

static Integer integer_add_impl(lua_State* L, Integer a, Integer b)
{
    HANDLE_TYPED_MATH(L, a, b, +)
    if (!a.heap && !b.heap) {
        int64_t sum;
        if (!__builtin_add_overflow(a.smi, b.smi, &sum))
            return new_integer(sum);
    }
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    HeapInteger* res = luau_heapint_add(L, ha, hb);
    return pack_integer_impl(L, res);
}

static Integer integer_sub_impl(lua_State* L, Integer a, Integer b)
{
    HANDLE_TYPED_MATH(L, a, b, -)
    if (!a.heap && !b.heap) {
        int64_t diff;
        if (!__builtin_sub_overflow(a.smi, b.smi, &diff))
            return new_integer(diff);
    }
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    HeapInteger* res = luau_heapint_sub(L, ha, hb);
    return pack_integer_impl(L, res);
}

static Integer integer_mul_impl(lua_State* L, Integer a, Integer b)
{
    HANDLE_TYPED_MATH(L, a, b, *)
    if (!a.heap && !b.heap) {
        int64_t prod;
        if (!__builtin_mul_overflow(a.smi, b.smi, &prod))
            return new_integer(prod);
    }
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    HeapInteger* res = luau_heapint_mul(L, ha, hb);
    return pack_integer_impl(L, res);
}

static Integer integer_div_impl(lua_State* L, Integer a, Integer b)
{
    HANDLE_TYPED_DIV(L, a, b, /, false)
    if (!a.heap && !b.heap) {
        if (b.smi != 0) {
            if (a.smi == INT64_MIN && b.smi == -1) {
            } else {
                return new_integer(a.smi / b.smi);
            }
        } else {
            luaG_runerror(L, "attempt to divide by zero");
        }
    }
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    HeapInteger* res = luau_heapint_div(L, ha, hb);
    return pack_integer_impl(L, res);
}

static Integer integer_mod_impl(lua_State* L, Integer a, Integer b)
{
    HANDLE_TYPED_DIV(L, a, b, %, true)
    if (!a.heap && !b.heap) {
        if (b.smi != 0) {
            if (a.smi == INT64_MIN && b.smi == -1) return new_integer(0);
            int64_t r = a.smi % b.smi;
            if (r != 0 && (a.smi ^ b.smi) < 0) r += b.smi;
            return new_integer(r);
        } else {
            luaG_runerror(L, "attempt to perform 'n%%0'");
        }
    }
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    HeapInteger* res = luau_heapint_mod(L, ha, hb);
    return pack_integer_impl(L, res);
}

static Integer integer_rem_impl(lua_State* L, Integer a, Integer b)
{
    HANDLE_TYPED_DIV(L, a, b, %, true)
    if (!a.heap && !b.heap) {
        if (b.smi != 0) {
            if (a.smi == INT64_MIN && b.smi == -1) return new_integer(0);
            return new_integer(a.smi % b.smi);
        } else {
            luaG_runerror(L, "attempt to perform 'n%%0'");
        }
    }
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    HeapInteger* res = luau_heapint_rem(L, ha, hb);
    return pack_integer_impl(L, res);
}

static Integer integer_neg_impl(lua_State* L, Integer a)
{
    if (!a.heap) {
        if (a.mode != IntegerMode_Dynamic) {
            uint64_t va = internal_get_bottom_64(a);
            uint64_t res = 0;
            switch (a.mode) {
                case IntegerMode_I8: res = (int64_t)(int8_t)-(int8_t)va; break;
                case IntegerMode_U8: res = (uint64_t)(uint8_t)-(uint8_t)va; break;
                case IntegerMode_I16: res = (int64_t)(int16_t)-(int16_t)va; break;
                case IntegerMode_U16: res = (uint64_t)(uint16_t)-(uint16_t)va; break;
                case IntegerMode_I32: res = (int64_t)(int32_t)-(int32_t)va; break;
                case IntegerMode_U32: res = (uint64_t)(uint32_t)-(uint32_t)va; break;
                case IntegerMode_I64: res = (int64_t)(int64_t)-(int64_t)va; break;
                case IntegerMode_U64: res = (uint64_t)(uint64_t)-(uint64_t)va; break;
                default: break;
            }
            return new_integer((int64_t)res, a.mode);
        }
        
        if (a.smi == INT64_MIN) {
            HeapInteger* res = luau_newheapinteger(L, 1);
            res->isNegative = false;
            res->size = 1;
            res->digits[0] = (uint64_t)INT64_MAX + 1;
            return pack_integer_impl(L, res);
        }
        return new_integer(-a.smi);
    }
    
    HeapInteger* res = luau_heapint_neg(L, a.heap);
    return pack_integer_impl(L, res);
}

static Integer integer_fromstring_impl(lua_State* L, const char* str)
{
    Integer res = new_integer(0);
    bool isNegative = false;
    if (*str == '-')
    {
        isNegative = true;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }

    while (*str)
    {
        if (*str >= '0' && *str <= '9')
        {
            Integer ten = new_integer(10);
            Integer digit = new_integer(*str - '0');
            res = integer_add_impl(L, integer_mul_impl(L, res, ten), digit);
        }
        else
        {
            break; // Malformed or ending
        }
        str++;
    }

    if (isNegative)
    {
        res = integer_neg_impl(L, res);
    }
    return res;
}

static void integer_push_string_impl(lua_State* L, Integer b)
{
    if (!b.heap)
    {
        char buf[64];
        if (b.mode == IntegerMode_U8 || b.mode == IntegerMode_U16 || b.mode == IntegerMode_U32 || b.mode == IntegerMode_U64) {
            snprintf(buf, 64, "%llu", (unsigned long long)b.smi);
        } else {
            snprintf(buf, 64, "%lld", (long long)b.smi);
        }
        lua_pushstring(L, buf);
        return;
    }

    char buf[400];
    int pos = 0;
    
    Integer ten = new_integer(10);
    Integer current = b;
    bool isNegative = current.heap->isNegative;
    
    Integer absCurrent;
    if (isNegative) {
        absCurrent = integer_neg_impl(L, current);
    } else {
        absCurrent = current;
    }
    
    while (true)
    {
        HeapInteger temp;
        uint64_t view;
        HeapInteger* v = get_heap_view(absCurrent, &temp, &view);
        bool isZero = true;
        for (uint32_t i = 0; i < v->size; i++) {
            if (v->digits[i] != 0) { isZero = false; break; }
        }
        if (isZero) break;
        
        Integer rem = integer_rem_impl(L, absCurrent, ten);
        Integer div = integer_div_impl(L, absCurrent, ten);
        
        buf[pos++] = '0' + (char)(rem.heap ? 0 : rem.smi);
        absCurrent = div;
    }
    
    if (pos == 0) {
        buf[pos++] = '0';
    } else if (isNegative) {
        buf[pos++] = '-';
    }
    
    // Reverse the string
    for (int i = 0; i < pos / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[pos - 1 - i];
        buf[pos - 1 - i] = tmp;
    }
    
    lua_pushlstring(L, buf, pos);
}

void luaZ_integer_add(lua_State* L, const TValue* a_val, const TValue* b_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    pack_integer(res_out, integer_add_impl(L, a, b));
}

void luaZ_integer_sub(lua_State* L, const TValue* a_val, const TValue* b_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    pack_integer(res_out, integer_sub_impl(L, a, b));
}

void luaZ_integer_mul(lua_State* L, const TValue* a_val, const TValue* b_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    pack_integer(res_out, integer_mul_impl(L, a, b));
}

void luaZ_integer_div(lua_State* L, const TValue* a_val, const TValue* b_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    pack_integer(res_out, integer_div_impl(L, a, b));
}

void luaZ_integer_mod(lua_State* L, const TValue* a_val, const TValue* b_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    pack_integer(res_out, integer_mod_impl(L, a, b));
}

void luaZ_integer_rem(lua_State* L, const TValue* a_val, const TValue* b_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    pack_integer(res_out, integer_rem_impl(L, a, b));
}

void luaZ_integer_neg(lua_State* L, const TValue* a_val, TValue* res_out) {
    Integer a = unpack_integer(a_val);
    pack_integer(res_out, integer_neg_impl(L, a));
}

void luaZ_integer_fromstring(lua_State* L, const char* str, TValue* res_out) {
    pack_integer(res_out, integer_fromstring_impl(L, str));
}

void lua_pushinteger_string(lua_State* L, const TValue* b_val) {
    Integer b = unpack_integer(b_val);
    integer_push_string_impl(L, b);
}

bool luaZ_integer_lt(lua_State* L, const TValue* a_val, const TValue* b_val)
{
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    if (a.mode != b.mode)
        luaG_runerror(L, "attempt to compare mixed typed integers");
        
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    
    if (ha->size == 0 && hb->size == 0) return false;
    if (ha->isNegative && !hb->isNegative) return true;
    if (!ha->isNegative && hb->isNegative) return false;
    
    int cmp = luau_heapint_cmp(ha, hb);
    if (ha->isNegative)
        return cmp > 0;
    else
        return cmp < 0;
}

bool luaZ_integer_le(lua_State* L, const TValue* a_val, const TValue* b_val)
{
    Integer a = unpack_integer(a_val);
    Integer b = unpack_integer(b_val);
    if (a.mode != b.mode)
        luaG_runerror(L, "attempt to compare mixed typed integers");
        
    HeapInteger ta, tb;
    uint64_t da, db;
    HeapInteger* ha = get_heap_view(a, &ta, &da);
    HeapInteger* hb = get_heap_view(b, &tb, &db);
    
    if (ha->size == 0 && hb->size == 0) return true;
    if (ha->isNegative && !hb->isNegative) return true;
    if (!ha->isNegative && hb->isNegative) return false;
    
    int cmp = luau_heapint_cmp(ha, hb);
    if (ha->isNegative)
        return cmp >= 0;
    else
        return cmp <= 0;
}
