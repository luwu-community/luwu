// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "DivMod.h"
#include "Cmp.h"
#include "Sub.h"

#define kBitsPerLimb 64

namespace Luau {
namespace BigInt {

// Performs division and modulo operations simultaneously on absolute values (n / d).
// Stores quotient in `q` (if not null) and remainder in `rem` (which initially starts as a copy of `n`).
// This uses a shift-and-subtract approach (long division in base-2).
//
// NOTE: `q` will be automatically cleared to zero and normalized internally, so callers
// do not need to pre-clear the quotient buffer before calling this function.
void DivModAbs(RWDigits* q, RWDigits* rem, Digits n, Digits d, RWDigits shift_d) {
    if (d.len == 0) {
        // Handled by caller (luaG_runerror)
        return;
    }
    
    int cmp = CmpAbs(n, d);
    
    // Fast path: if numerator is smaller than denominator (1/2 etc.), quotient is 0 and remainder is n.
    if (cmp < 0) {
        if (q) q->len = 0;
        // rem is already a copy of n
        return;
    }
    
    // Fast path: if they are exactly equal, quotient is 1 and remainder is 0.
    if (cmp == 0) {
        if (q) {
            q->ptr[0] = 1;
            q->len = 1;
        }
        rem->len = 0;
        return;
    }
    
    if (q) {
        for (uint32_t i = 0; i < q->cap; i++) q->ptr[i] = 0;
        q->len = n.len;
    }

    // We align the highest bit of `d` with the highest bit of `n` and process downwards bit-by-bit.
    int bit_diff = (n.len - d.len) * kBitsPerLimb;
    for (int i = bit_diff + kBitsPerLimb; i >= 0; --i) {
        shift_d.len = 0;
        uint32_t word_shift = i / kBitsPerLimb;
        uint32_t bit_shift = i % kBitsPerLimb;
        uint64_t carry = 0;
        
        // Apply the word-level shift (pad with empty zero limbs)
        for(uint32_t j = 0; j < word_shift; j++) {
            shift_d[shift_d.len++] = 0;
        }
        
        // Apply the bit-level shift across the remaining limbs
        for(uint32_t j = 0; j < d.len || carry; j++) {
            uint64_t val = carry;
            if (j < d.len) {
                if (bit_shift > 0) val += (d[j] << bit_shift);
                else val += d[j];
            }
            shift_d[shift_d.len++] = val;
            
            if (j < d.len && bit_shift > 0) {
                carry = d[j] >> (kBitsPerLimb - bit_shift);
            } else {
                carry = 0;
            }
        }
        normalize(shift_d);
        if (shift_d.len == 0) continue;
        
        // If our shifted denominator is less than or equal to the current remainder,
        // we can subtract it out and set the corresponding bit in the quotient to 1.
        if (CmpAbs(*rem, shift_d) >= 0) {
            SubAbs(*rem, *rem, shift_d);
            
            if (q) {
                uint32_t q_idx = i / kBitsPerLimb;
                uint32_t q_bit = i % kBitsPerLimb;
                if (q_idx < q->cap) {
                    q->ptr[q_idx] |= (1ULL << q_bit);
                }
            }
        }
    }
    if (q) normalize(*q);
}

} // namespace BigInt
} // namespace Luau
