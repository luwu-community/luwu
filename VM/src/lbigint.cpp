// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lbigint.h"
#include "lmem.h"
#include "lgc.h"
#include "ldebug.h"
#include "lnumutils.h"
#include "bigint/Add.h"
#include "bigint/Sub.h"
#include "bigint/Mul.h"
#include "bigint/DivMod.h"
#include "bigint/Cmp.h"
#include "bigint/ToString.h"
#include "bigint/FromString.h"
#include <string.h>
#include <stdint.h>

using namespace Luau::BigInt;

#define kBitsPerLimb 64

// FIXMELATER:
//
// Luau's GC currently operates purely via explicit safe points (`luaC_checkGC`) which are triggered at VM loop 
// boundaries or public API calls.
//
// If this guarantee ever changes, then this entire file needs to change to root all objects
HeapInteger* luaZB_newheapinteger(lua_State* L, uint32_t capacity)
{
    HeapInteger* h = luaM_newgco(L, HeapInteger, sizeof(HeapInteger), L->activememcat);
    luaC_init(L, h, LUA_THEAPINTEGER);
    h->capacity = capacity;
    h->size = 0;
    h->isNegative = false;
    h->digits = nullptr;
    if (capacity > 0)
        h->digits = luaM_newarray(L, capacity, uint64_t, L->activememcat);
    return h;
}

void luaZB_freeheapinteger(lua_State* L, HeapInteger* h, struct lua_Page* page)
{
    if (h->digits && h->capacity > 0)
        luaM_freearray(L, h->digits, h->capacity, uint64_t, h->memcat);
    luaM_freegco(L, h, sizeof(HeapInteger), h->memcat, page);
}

inline Digits toDigits(const HeapInteger* h) {
    return Digits(h->digits, h->size);
}

inline RWDigits toRWDigits(HeapInteger* h) {
    return RWDigits(h->digits, h->size, h->capacity);
}

inline SignedDigits toSignedDigits(const HeapInteger* h) {
    SignedDigits sd;
    sd.digits = toDigits(h);
    sd.isNegative = h->isNegative;
    return sd;
}

// Ensures that exactly zero does not retain a negative sign
static void fixSignIfZero(HeapInteger* h) {
    if (h->size == 0) {
        h->isNegative = false;
    }
}
#define BIGINT_TMP_MAX_STACK_LIMBS 64

// temp_heapint allocates a temporary BigInt struct.
// For small numbers (<= 64 limbs, which is 4096 bits), we can just use the C-stack.
// completely avoiding GC overhead
//
// For extremely large numbers, we fall back to allocating a full GC object.
#define temp_heapint(name, L, req_cap) \
    uint64_t name##_buf[BIGINT_TMP_MAX_STACK_LIMBS]; \
    HeapInteger name##_stack; \
    HeapInteger* name = nullptr; \
    if ((req_cap) <= BIGINT_TMP_MAX_STACK_LIMBS) { \
        name##_stack.capacity = (req_cap); \
        name##_stack.size = 0; \
        name##_stack.isNegative = false; \
        name##_stack.digits = name##_buf; \
        name = &name##_stack; \
    } else { \
        name = luaZB_newheapinteger((L), (req_cap)); \
    }

#define copy_heapint(dst, src) do { \
    (dst)->size = 0; \
    for (uint32_t _i = 0; _i < (src)->size; ++_i) { \
        (dst)->digits[(dst)->size++] = (src)->digits[_i]; \
    } \
} while(0)

#define clear_heapint_digits(h) do { \
    for (uint32_t _i = 0; _i < (h)->capacity; ++_i) { \
        (h)->digits[_i] = 0; \
    } \
} while(0)


int luaZB_heapinteger_cmp_abs(const HeapInteger* a, const HeapInteger* b) {
    return CmpAbs(toDigits(a), toDigits(b));
}

int luaZB_heapinteger_cmp(const HeapInteger* a, const HeapInteger* b) {
    return Cmp(toSignedDigits(a), toSignedDigits(b));
}

HeapInteger* luaZB_heapinteger_add(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luaZB_newheapinteger(L, (a->size > b->size ? a->size : b->size) + 1);
    RWDigits res_d = toRWDigits(res);
    
    if (a->isNegative == b->isNegative) {
        res->isNegative = a->isNegative;
        AddAbs(res_d, toDigits(a), toDigits(b));
    } else {
        int cmp = luaZB_heapinteger_cmp_abs(a, b);
        if (cmp >= 0) {
            res->isNegative = a->isNegative;
            SubAbs(res_d, toDigits(a), toDigits(b));
        } else {
            res->isNegative = b->isNegative;
            SubAbs(res_d, toDigits(b), toDigits(a));
        }
    }
    res->size = res_d.len;
    fixSignIfZero(res);
    return res;
}

HeapInteger* luaZB_heapinteger_sub(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luaZB_newheapinteger(L, (a->size > b->size ? a->size : b->size) + 1);
    RWDigits res_d = toRWDigits(res);
    
    if (a->isNegative != b->isNegative) {
        res->isNegative = a->isNegative;
        AddAbs(res_d, toDigits(a), toDigits(b));
    } else {
        int cmp = luaZB_heapinteger_cmp_abs(a, b);
        if (cmp >= 0) {
            res->isNegative = a->isNegative;
            SubAbs(res_d, toDigits(a), toDigits(b));
        } else {
            res->isNegative = !a->isNegative;
            SubAbs(res_d, toDigits(b), toDigits(a));
        }
    }
    res->size = res_d.len;
    fixSignIfZero(res);
    return res;
}

HeapInteger* luaZB_heapinteger_mul(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luaZB_newheapinteger(L, a->size + b->size);
    if (res->capacity > 0) {
        memset(res->digits, 0, res->capacity * sizeof(uint64_t));
    }
    RWDigits res_d = toRWDigits(res);
    
    MulAbs(res_d, toDigits(a), toDigits(b));
    res->size = res_d.len;
    
    res->isNegative = a->isNegative != b->isNegative;
    if (res->size == 0) res->isNegative = false;
    return res;
}

HeapInteger* luaZB_heapinteger_div(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    if (b->size == 0) {
        luaG_runerror(L, "attempt to divide by zero");
    }
    HeapInteger* q = luaZB_newheapinteger(L, a->size);
    RWDigits q_d = toRWDigits(q);
    
    temp_heapint(rem, L, a->size);
    copy_heapint(rem, a);
    RWDigits rem_d = toRWDigits(rem);
    
    temp_heapint(shift_d, L, a->size + b->size + 1);
    
    DivModAbs(&q_d, &rem_d, toDigits(a), toDigits(b), toRWDigits(shift_d));
    q->size = q_d.len;
    
    q->isNegative = a->isNegative != b->isNegative;
    fixSignIfZero(q);
    return q;
}

HeapInteger* luaZB_heapinteger_mod(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    if (b->size == 0) {
        luaG_runerror(L, "attempt to divide by zero");
    }
    uint32_t req_cap = (a->size > b->size ? a->size : b->size) + 1;
    HeapInteger* r = luaZB_newheapinteger(L, req_cap);
    copy_heapint(r, a);
    RWDigits r_d = toRWDigits(r);
    
    temp_heapint(shift_d, L, a->size + b->size + 1);
    
    DivModAbs(nullptr, &r_d, toDigits(a), toDigits(b), toRWDigits(shift_d));
    r->size = r_d.len;
    
    r->isNegative = a->isNegative;
    fixSignIfZero(r);
    
    if (r->size > 0 && a->isNegative != b->isNegative) {
        r->isNegative = b->isNegative;
        SubAbs(r_d, toDigits(b), toDigits(r));
        r->size = r_d.len;
        fixSignIfZero(r);
    }

    return r;
}

HeapInteger* luaZB_heapinteger_rem(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    if (b->size == 0) {
        luaG_runerror(L, "attempt to divide by zero");
    }
    HeapInteger* r = luaZB_newheapinteger(L, a->size);
    copy_heapint(r, a);
    RWDigits r_d = toRWDigits(r);
    
    temp_heapint(shift_d, L, a->size + b->size + 1);
    
    DivModAbs(nullptr, &r_d, toDigits(a), toDigits(b), toRWDigits(shift_d));
    r->size = r_d.len;
    
    r->isNegative = a->isNegative;
    fixSignIfZero(r);
    return r;
}

HeapInteger* luaZB_heapinteger_neg(lua_State* L, const HeapInteger* a) {
    HeapInteger* res = luaZB_newheapinteger(L, a->size);
    res->size = a->size;
    res->isNegative = !a->isNegative;
    for (uint32_t i = 0; i < a->size; ++i) {
        res->digits[i] = a->digits[i];
    }
    fixSignIfZero(res);
    return res;
}

struct StringContext {
    lua_State* L;
    HeapInteger* scratchpad;
};

static uint64_t* string_scratch_cb(void* userContext, size_t limbs) {
    StringContext* ctx = (StringContext*)userContext;
    if (limbs > 0) {
        ctx->scratchpad = luaZB_newheapinteger(ctx->L, limbs);
        return ctx->scratchpad->digits;
    }
    return nullptr;
}

static void string_append_cb(void* userContext, const char* str, size_t len) {
    StringContext* ctx = (StringContext*)userContext;
    lua_pushlstring(ctx->L, str, len);
}

void luaZB_heapinteger_pushstring(lua_State* L, const HeapInteger* h) {
    StringContext ctx = { L, nullptr };
    ToStringContext tctx = { &ctx, string_scratch_cb, string_append_cb };
    ToString(toSignedDigits(h), tctx);
    }

struct ParseContext {
    lua_State* L;
    HeapInteger* scratchpad;
    HeapInteger* result;
};

static uint64_t* parse_scratch_cb(void* userContext, size_t limbs) {
    ParseContext* ctx = (ParseContext*)userContext;
    if (limbs > 0) {
        ctx->scratchpad = luaZB_newheapinteger(ctx->L, limbs);
        return ctx->scratchpad->digits;
    }
    return nullptr;
}

static RWDigits parse_result_cb(void* userContext, size_t limbs) {
    ParseContext* ctx = (ParseContext*)userContext;
    ctx->result = luaZB_newheapinteger(ctx->L, limbs);
    return toRWDigits(ctx->result);
}

HeapInteger* luaZB_heapinteger_fromstring(lua_State* L, const char* str, const char** endptr) {
    ParseContext ctx = { L, nullptr, nullptr };
    FromStringContext fctx = { &ctx, parse_scratch_cb, parse_result_cb };
    
    ParseResult res = FromString(str, fctx);
    if (endptr) *endptr = res.endptr;
        
    if (!res.success) {
        return nullptr;
    }
    
    ctx.result->size = res.value.digits.len;
    ctx.result->isNegative = res.value.isNegative;
    return ctx.result;
}
