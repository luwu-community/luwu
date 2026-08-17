// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lbigint.h"
#include "lmem.h"
#include "lgc.h"
#include "ldebug.h"
#include "lnumutils.h"
#include <string.h>
#include <stdint.h>

#define kBitsPerLimb 64

HeapInteger* luau_newheapinteger(lua_State* L, uint32_t capacity)
{
    HeapInteger* h = luaM_newgco(L, HeapInteger, sizeof(HeapInteger), L->activememcat);
    luaC_init(L, h, LUA_THEAPINTEGER);
    h->capacity = capacity;
    h->size = 0;
    h->isNegative = false;
    if (capacity > 0)
        h->digits = luaM_newarray(L, capacity, uint64_t, L->activememcat);
    else
        h->digits = nullptr;
    return h;
}

void lua_freeinteger(lua_State* L, HeapInteger* h, struct lua_Page* page)
{
    if (h->capacity > 0)
        luaM_freearray(L, h->digits, h->capacity, uint64_t, h->memcat);
    luaM_freegco(L, h, sizeof(HeapInteger), h->memcat, page);
}

static void normalize(HeapInteger* h) {
    while (h->size > 0 && h->digits[h->size - 1] == 0) {
        h->size--;
    }
    if (h->size == 0) {
        h->isNegative = false;
    }
}

int luau_heapint_cmp(const HeapInteger* a, const HeapInteger* b) {
    if (a->size != b->size) {
        return a->size < b->size ? -1 : 1;
    }
    for (int i = (int)a->size - 1; i >= 0; --i) {
        if (a->digits[i] != b->digits[i]) {
            return a->digits[i] < b->digits[i] ? -1 : 1;
        }
    }
    return 0;
}

// Adds the absolute values of two BigInts (a + b) and stores the result in `res`.
// This implements long addition adapted for base-2^64 (array of limbs).
//
// It iterates through the limbs from least significant to most significant,
// adding corresponding limbs along with any carry from the previous column.
static void add_abs(HeapInteger* res, const HeapInteger* a, const HeapInteger* b) {
    uint32_t max_size = a->size > b->size ? a->size : b->size;
    uint64_t carry = 0;
    
    // Continue looping as long as there are limbs to add OR a lingering carry bit
    for (uint32_t i = 0; i < max_size || carry; ++i) {
        uint64_t sum = carry;
        uint64_t next_carry = 0;
        
        if (i < a->size) {
            sum += a->digits[i];
            // If the sum became smaller than the operand, we overflowed 64 bits.
            if (sum < a->digits[i]) next_carry = 1;
        }
        if (i < b->size) {
            sum += b->digits[i];
            // Again, check for overflow.
            if (sum < b->digits[i]) next_carry = 1;
        }
        
        res->digits[res->size++] = sum;
        carry = next_carry;
    }
}

// Subtracts the absolute values (a - b) and stores the result in `res`.
//
// The caller MUST ensure that absolute value of `a` >= absolute value of `b`.
// This guarantees that the final borrow at the end of the loop is always 0.
static void sub_abs(HeapInteger* res, const HeapInteger* a, const HeapInteger* b) {
    uint64_t borrow = 0;
    
    for (uint32_t i = 0; i < a->size; ++i) {
        uint64_t a_val = a->digits[i];
        uint64_t b_val = (i < b->size) ? b->digits[i] : 0;
        
        uint64_t diff = a_val - b_val - borrow;
        
        // If a_val was smaller than b_val, we underflowed and need to borrow from the next limb.
        // Also, if they were equal but we already had a borrow, we still underflow.
        borrow = (a_val < b_val) || (a_val == b_val && borrow) ? 1 : 0;
        
        res->digits[res->size++] = diff;
    }
}

// Multiplies the absolute values of two BigInts (a * b) and stores the result in `res`.
// This uses the O(N^2) long multiplication algorithm.
// For every limb in `a`, we multiply it against every limb in `b`, keeping track of a 64-bit carry.
//
// Assumes `res->digits` has been zero-initialized to a size of at least (a->size + b->size).
static void mul_abs(HeapInteger* res, const HeapInteger* a, const HeapInteger* b) {
    if (a->size == 0 || b->size == 0) return;
    
    // Iterate over limbs of A
    for (uint32_t i = 0; i < a->size; ++i) {
        uint64_t carry = 0;
        
        // Phase 1: Multiply limb A[i] against all limbs of B
        for (uint32_t j = 0; j < b->size; ++j) {
            uint64_t prod_hi = 0;
            uint64_t prod_lo = mul128(a->digits[i], b->digits[j], &prod_hi);
            
            // Add the carry from the previous loop iteration to the current result limb.
            // We use c1 to track if this addition overflowed.
            uint64_t sum1 = res->digits[i + j] + carry;
            uint64_t c1 = (sum1 < carry) ? 1 : 0;
            
            // Add the lower 64-bits of our new product to the result limb.
            // We use c2 to track if this addition overflowed.
            uint64_t sum2 = sum1 + prod_lo;
            uint64_t c2 = (sum2 < prod_lo) ? 1 : 0;
            
            res->digits[i + j] = sum2;
            
            // The new carry for the next iteration is the upper 64-bits of the product
            // plus any overflow bits from the additions we just did.
            carry = prod_hi + c1 + c2;
        }
        
        // Phase 2: Drop the remaining carry into the untouched next limb.
        // Because res->digits was zero-initialized and no previous outer-loop iteration
        // has reached `i + b->size` yet, it is guaranteed to be 0. Thus, adding the carry
        // cannot overflow, and we avoid needing a ripple loop entirely.
        res->digits[i + b->size] = carry;
    }
    res->size = a->size + b->size;
}

// Performs division and modulo operations simultaneously on absolute values (n / d).
// Stores quotient in `q` (if not null) and remainder in `r` (if not null).
// This uses a shift-and-subtract approach (long division in base-2).
static void div_mod_abs(lua_State* L, const HeapInteger* n, const HeapInteger* d, HeapInteger* q, HeapInteger* r) {
    if (d->size == 0) {
        luaG_runerror(L, "attempt to divide by zero");
    }
    
    int cmp = luau_heapint_cmp(n, d);
    
    // Fast path: if numerator is smaller than denominator (1/2 etc.), quotient is 0 and remainder is n.
    if (cmp < 0) {
        if (q) q->size = 0;
        if (r) {
            for (uint32_t i = 0; i < n->size; ++i) r->digits[r->size++] = n->digits[i];
        }
        return;
    }
    
    // Fast path: if they are exactly equal, quotient is 1 and remainder is 0.
    if (cmp == 0) {
        if (q) q->digits[q->size++] = 1;
        if (r) r->size = 0;
        return;
    }
    
    // Create a working copy of the numerator that will eventually become the remainder.
    HeapInteger* rem = luau_newheapinteger(L, n->size);
    rem->size = n->size;
    for(uint32_t i = 0; i < n->size; i++) rem->digits[i] = n->digits[i];

    // shift_d will hold the denominator shifted left by 'i' bits
    HeapInteger* shift_d = luau_newheapinteger(L, n->size + 1);
    
    if (q) q->size = n->size;

    // We align the highest bit of `d` with the highest bit of `n` and process downwards bit-by-bit.
    int bit_diff = (n->size - d->size) * kBitsPerLimb;
    for (int i = bit_diff + kBitsPerLimb; i >= 0; --i) {
        shift_d->size = 0;
        uint32_t word_shift = i / kBitsPerLimb;
        uint32_t bit_shift = i % kBitsPerLimb;
        uint64_t carry = 0;
        
        // Apply the word-level shift (pad with empty zero limbs)
        for(uint32_t j = 0; j < word_shift; j++) shift_d->digits[shift_d->size++] = 0;
        
        // Apply the bit-level shift across the remaining limbs
        for(uint32_t j = 0; j < d->size || carry; j++) {
            uint64_t val = carry;
            if (j < d->size) {
                if (bit_shift > 0) val += (d->digits[j] << bit_shift);
                else val += d->digits[j];
            }
            shift_d->digits[shift_d->size++] = val;
            
            if (j < d->size && bit_shift > 0) {
                carry = d->digits[j] >> (kBitsPerLimb - bit_shift);
            } else {
                carry = 0;
            }
        }
        normalize(shift_d);
        
        // If our shifted denominator is less than or equal to the current remainder,
        // we can subtract it out and set the corresponding bit in the quotient to 1.
        if (luau_heapint_cmp(rem, shift_d) >= 0) {
            HeapInteger temp_rem;
            temp_rem.size = rem->size;
            temp_rem.digits = rem->digits;
            rem->size = 0;
            
            sub_abs(rem, &temp_rem, shift_d);
            normalize(rem);
            
            if (q) {
                q->digits[i / kBitsPerLimb] |= (1ull << (i % kBitsPerLimb));
            }
        }
    }
    
    if (r) {
        r->size = 0;
        for (uint32_t i = 0; i < rem->size; ++i) {
            r->digits[r->size++] = rem->digits[i];
        }
    }
}

HeapInteger* luau_heapint_add(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luau_newheapinteger(L, (a->size > b->size ? a->size : b->size) + 1);
    if (a->isNegative == b->isNegative) {
        res->isNegative = a->isNegative;
        add_abs(res, a, b);
    } else {
        int cmp = luau_heapint_cmp(a, b);
        if (cmp >= 0) {
            res->isNegative = a->isNegative;
            sub_abs(res, a, b);
        } else {
            res->isNegative = b->isNegative;
            sub_abs(res, b, a);
        }
    }
    normalize(res);
    return res;
}

HeapInteger* luau_heapint_sub(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luau_newheapinteger(L, (a->size > b->size ? a->size : b->size) + 1);
    if (a->isNegative != b->isNegative) {
        res->isNegative = a->isNegative;
        add_abs(res, a, b);
    } else {
        int cmp = luau_heapint_cmp(a, b);
        if (cmp >= 0) {
            res->isNegative = a->isNegative;
            sub_abs(res, a, b);
        } else {
            res->isNegative = !a->isNegative;
            sub_abs(res, b, a);
        }
    }
    normalize(res);
    return res;
}

HeapInteger* luau_heapint_mul(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luau_newheapinteger(L, a->size + b->size);
    for(uint32_t i=0; i < res->capacity; i++) res->digits[i] = 0;
    res->isNegative = a->isNegative != b->isNegative;
    mul_abs(res, a, b);
    normalize(res);
    return res;
}

HeapInteger* luau_heapint_div(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* q = luau_newheapinteger(L, a->size);
    for(uint32_t i=0; i < q->capacity; i++) q->digits[i] = 0;
    q->isNegative = a->isNegative != b->isNegative;
    div_mod_abs(L, a, b, q, nullptr);
    normalize(q);
    return q;
}

HeapInteger* luau_heapint_mod(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* r = luau_newheapinteger(L, b->size);
    for(uint32_t i=0; i < r->capacity; i++) r->digits[i] = 0;
    r->isNegative = a->isNegative;
    div_mod_abs(L, a, b, nullptr, r);
    normalize(r);
    
    if (r->size > 0 && a->isNegative != b->isNegative) {
        HeapInteger* res = luau_newheapinteger(L, (r->size > b->size ? r->size : b->size) + 1);
        res->isNegative = b->isNegative;
        sub_abs(res, b, r);
        normalize(res);
        return res;
    }
    return r;
}

HeapInteger* luau_heapint_rem(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* r = luau_newheapinteger(L, b->size);
    for(uint32_t i=0; i < r->capacity; i++) r->digits[i] = 0;
    r->isNegative = a->isNegative;
    div_mod_abs(L, a, b, nullptr, r);
    normalize(r);
    return r;
}

HeapInteger* luau_heapint_neg(lua_State* L, const HeapInteger* a) {
    HeapInteger* res = luau_newheapinteger(L, a->size);
    res->size = a->size;
    res->isNegative = !a->isNegative;
    for (uint32_t i = 0; i < a->size; ++i) {
        res->digits[i] = a->digits[i];
    }
    normalize(res);
    return res;
}
