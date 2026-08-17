// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lbigint.h"
#include "lmem.h"
#include "lgc.h"
#include "ldebug.h"
#include "lnumutils.h"
#include <string.h>
#include <stdint.h>

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

static void normalize(HeapInteger* h) {
    while (h->size > 0 && h->digits[h->size - 1] == 0) {
        h->size--;
    }
    if (h->size == 0) {
        h->isNegative = false;
    }
}

int luaZB_heapinteger_cmp_abs(const HeapInteger* a, const HeapInteger* b) {
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

int luaZB_heapinteger_cmp(const HeapInteger* a, const HeapInteger* b) {
    if (a->size == 0 && b->size == 0) return 0;
    if (a->isNegative && !b->isNegative) return -1;
    if (!a->isNegative && b->isNegative) return 1;
    
    int cmp = luaZB_heapinteger_cmp_abs(a, b);
    return a->isNegative ? -cmp : cmp;
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
    normalize(res);
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

#define temp_heapint_copy(name, L, src) \
    temp_heapint(name, L, (src)->size); \
    copy_heapint(name, src)

#define clear_heapint_digits(h) do { \
    for (uint32_t _i = 0; _i < (h)->capacity; ++_i) { \
        (h)->digits[_i] = 0; \
    } \
} while(0)

// Performs division and modulo operations simultaneously on absolute values (n / d).
// Stores quotient in `q` (if not null) and remainder in `r` (if not null).
// This uses a shift-and-subtract approach (long division in base-2).
//
// NOTE: `q` will be automatically cleared to zero and normalized internally, so callers
// do not need to pre-clear the quotient buffer before calling this function.
static void div_mod_abs(lua_State* L, const HeapInteger* n, const HeapInteger* d, HeapInteger* q, HeapInteger* r) {
    if (d->size == 0) {
        luaG_runerror(L, "attempt to divide by zero");
    }
    
    int cmp = luaZB_heapinteger_cmp_abs(n, d);
    
    // Fast path: if numerator is smaller than denominator (1/2 etc.), quotient is 0 and remainder is n.
    if (cmp < 0) {
        if (q) q->size = 0;
        if (r) {
            copy_heapint(r, n);
        }
        return;
    }
    
    // Fast path: if they are exactly equal, quotient is 1 and remainder is 0.
    if (cmp == 0) {
        if (q) q->digits[q->size++] = 1;
        if (r) r->size = 0;
        return;
    }
    
    // Allocate working scratchpads
    temp_heapint_copy(rem, L, n);
    // shift_d will hold the denominator shifted left by 'i' bits
    // n->size + 1 is exactly sufficient because the maximum word-shift 
    // and the maximum carry-flush iteration are mutually exclusive.
    temp_heapint(shift_d, L, n->size + 1);
    
    if (q) {
        clear_heapint_digits(q);
        q->size = n->size;
    }

    // We align the highest bit of `d` with the highest bit of `n` and process downwards bit-by-bit.
    int bit_diff = (n->size - d->size) * kBitsPerLimb;
    for (int i = bit_diff + kBitsPerLimb; i >= 0; --i) {
        shift_d->size = 0;
        uint32_t word_shift = i / kBitsPerLimb;
        uint32_t bit_shift = i % kBitsPerLimb;
        uint64_t carry = 0;
        
        // Apply the word-level shift (pad with empty zero limbs)
        for(uint32_t j = 0; j < word_shift; j++) {
            shift_d->digits[shift_d->size++] = 0;
            LUAU_ASSERT(shift_d->size <= shift_d->capacity);
        }
        
        // Apply the bit-level shift across the remaining limbs
        for(uint32_t j = 0; j < d->size || carry; j++) {
            uint64_t val = carry;
            if (j < d->size) {
                if (bit_shift > 0) val += (d->digits[j] << bit_shift);
                else val += d->digits[j];
            }
            shift_d->digits[shift_d->size++] = val;
            LUAU_ASSERT(shift_d->size <= shift_d->capacity);
            
            if (j < d->size && bit_shift > 0) {
                carry = d->digits[j] >> (kBitsPerLimb - bit_shift);
            } else {
                carry = 0;
            }
        }
        normalize(shift_d);
        if (shift_d->size == 0) continue;
        
        // If our shifted denominator is less than or equal to the current remainder,
        // we can subtract it out and set the corresponding bit in the quotient to 1.
        if (luaZB_heapinteger_cmp_abs(rem, shift_d) >= 0) {
            HeapInteger temp_rem;
            temp_rem.size = rem->size;
            temp_rem.digits = rem->digits;
            rem->size = 0;
            sub_abs(rem, &temp_rem, shift_d);
            normalize(rem);
            
            if (q) {
                uint32_t q_idx = i / kBitsPerLimb;
                uint32_t q_bit = i % kBitsPerLimb;
                LUAU_ASSERT(q_idx < q->capacity);
                if (q_idx < q->capacity) {
                    q->digits[q_idx] |= (1ULL << q_bit);
                }
            }
        }
    }
    
    if (r) {
        copy_heapint(r, rem);
    }
    if (q) {
        normalize(q);
    }
}

HeapInteger* luaZB_heapinteger_add(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luaZB_newheapinteger(L, (a->size > b->size ? a->size : b->size) + 1);
    if (a->isNegative == b->isNegative) {
        res->isNegative = a->isNegative;
        add_abs(res, a, b);
    } else {
        int cmp = luaZB_heapinteger_cmp_abs(a, b);
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

HeapInteger* luaZB_heapinteger_sub(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luaZB_newheapinteger(L, (a->size > b->size ? a->size : b->size) + 1);
    if (a->isNegative != b->isNegative) {
        res->isNegative = a->isNegative;
        add_abs(res, a, b);
    } else {
        int cmp = luaZB_heapinteger_cmp_abs(a, b);
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

HeapInteger* luaZB_heapinteger_mul(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* res = luaZB_newheapinteger(L, a->size + b->size);
    clear_heapint_digits(res);
    mul_abs(res, a, b);
    res->isNegative = a->isNegative != b->isNegative;
    if (res->size == 0) res->isNegative = false;
    return res;
}

HeapInteger* luaZB_heapinteger_div(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* q = luaZB_newheapinteger(L, a->size);
    q->isNegative = a->isNegative != b->isNegative;
    div_mod_abs(L, a, b, q, nullptr);
    return q;
}

HeapInteger* luaZB_heapinteger_mod(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* r = luaZB_newheapinteger(L, b->size);
    clear_heapint_digits(r);
    r->isNegative = a->isNegative;
    div_mod_abs(L, a, b, nullptr, r);
    normalize(r);
    
    if (r->size > 0 && a->isNegative != b->isNegative) {
        HeapInteger* res = luaZB_newheapinteger(L, (r->size > b->size ? r->size : b->size) + 1);
        res->isNegative = b->isNegative;
        sub_abs(res, b, r);
        normalize(res);
        return res;
    }
    return r;
}

HeapInteger* luaZB_heapinteger_rem(lua_State* L, const HeapInteger* a, const HeapInteger* b) {
    HeapInteger* r = luaZB_newheapinteger(L, b->size);
    clear_heapint_digits(r);
    r->isNegative = a->isNegative;
    div_mod_abs(L, a, b, nullptr, r);
    normalize(r);
    return r;
}

HeapInteger* luaZB_heapinteger_neg(lua_State* L, const HeapInteger* a) {
    HeapInteger* res = luaZB_newheapinteger(L, a->size);
    res->size = a->size;
    res->isNegative = !a->isNegative;
    for (uint32_t i = 0; i < a->size; ++i) {
        res->digits[i] = a->digits[i];
    }
    normalize(res);
    return res;
}

void luaZB_heapinteger_pushstring(lua_State* L, const HeapInteger* h) {
    if (h->size == 0) {
        lua_pushstring(L, "0");
        return;
    }

    size_t max_digits = h->size * 20 + 2;
    uint32_t buf_limbs = (uint32_t)((max_digits + 7) / 8);
    temp_heapint(buf_int, L, buf_limbs);
    char* buf = (char*)buf_int->digits;
    int pos = 0;
    
    temp_heapint_copy(current, L, h);
    
    uint64_t chunk_val = 10000000000000000000ULL;
    temp_heapint(chunk, L, 1);
    chunk->size = 1;
    chunk->digits[0] = chunk_val;
    
    temp_heapint(quotient, L, h->size);
    temp_heapint(remainder, L, 1);
    
    while (current->size > 0) {
        div_mod_abs(L, current, chunk, quotient, remainder);
        uint64_t rem_val = remainder->size > 0 ? remainder->digits[0] : 0;
        
        copy_heapint(current, quotient);
        bool is_last = (current->size == 0);
        
        if (is_last) {
            while (rem_val > 0) {
                buf[pos++] = '0' + (rem_val % 10);
                rem_val /= 10;
            }
        } else {
            for (int i = 0; i < 19; i++) {
                buf[pos++] = '0' + (rem_val % 10);
                rem_val /= 10;
            }
        }
    }
    
    if (pos == 0) {
        buf[pos++] = '0';
    } else if (h->isNegative) {
        buf[pos++] = '-';
    }
    
    // Reverse string
    for (int i = 0; i < pos / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[pos - 1 - i];
        buf[pos - 1 - i] = tmp;
    }
    
    lua_pushlstring(L, buf, pos);
}

HeapInteger* luaZB_heapinteger_fromstring(lua_State* L, const char* str, const char** endptr) {
    bool isNegative = false;
    if (*str == '-') {
        isNegative = true;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Calculate length of digits
    size_t len = 0;
    while (str[len] >= '0' && str[len] <= '9') {
        len++;
    }
    
    if (endptr) *endptr = str + len;
    
    if (len == 0) {
        return nullptr; // Not a valid number
    }
    
    // Allocate a scratchpad for the accumulator.
    // Max digits per limb is roughly 19.
    uint32_t max_limbs = (uint32_t)((len * 4) / 10 + 2); // safe upper bound
    temp_heapint(accum, L, max_limbs);
    
    temp_heapint(chunk, L, 1);
    chunk->size = 1;
    
    temp_heapint(multiplier, L, 1);
    multiplier->size = 1;
    
    temp_heapint(temp_res, L, max_limbs);
    
    size_t pos = 0;
    while (pos < len) {
        size_t chunk_len = len - pos;
        if (chunk_len > 19) chunk_len = 19;
        
        uint64_t chunk_val = 0;
        uint64_t mult_val = 1;
        for (size_t i = 0; i < chunk_len; ++i) {
            chunk_val = chunk_val * 10 + (str[pos + i] - '0');
            mult_val *= 10;
        }
        
        // accum = accum * mult_val + chunk_val
        if (accum->size > 0) {
            multiplier->digits[0] = mult_val;
            clear_heapint_digits(temp_res);
            mul_abs(temp_res, accum, multiplier);
            
            chunk->digits[0] = chunk_val;
            chunk->size = (chunk_val > 0) ? 1 : 0;
            
            accum->size = 0;
            add_abs(accum, temp_res, chunk);
            normalize(accum);
        } else {
            if (chunk_val > 0) {
                accum->digits[0] = chunk_val;
                accum->size = 1;
            }
        }
        
        pos += chunk_len;
    }
    
    HeapInteger* res = luaZB_newheapinteger(L, accum->size);
    res->isNegative = isNegative && accum->size > 0;
    copy_heapint(res, accum);
    return res;
}
