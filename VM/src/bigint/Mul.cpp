// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Mul.h"
#include "../lnumutils.h"

namespace Luau {
namespace BigInt {

// Multiplies the absolute values of two BigInts (a * b) and stores the result in `res`.
// This uses the O(N^2) long multiplication algorithm.
// For every limb in `a`, we multiply it against every limb in `b`, keeping track of a 64-bit carry.
//
// Assumes `res` has been zero-initialized to a capacity of at least (a.len + b.len).
void MulAbs(RWDigits& res, Digits a, Digits b) {
    if (a.len == 0 || b.len == 0) {
        res.len = 0;
        return;
    }
    
    // Iterate over limbs of A
    for (uint32_t i = 0; i < a.len; ++i) {
        uint64_t carry = 0;
        
        // Phase 1: Multiply limb A[i] against all limbs of B
        for (uint32_t j = 0; j < b.len; ++j) {
            uint64_t prod_hi = 0;
            uint64_t prod_lo = mul128(a[i], b[j], &prod_hi);
            
            // Add the carry from the previous loop iteration to the current result limb.
            // We use c1 to track if this addition overflowed.
            uint64_t sum1;
            uint64_t c1 = luau_add_overflow(res[i + j], carry, &sum1) ? 1 : 0;
            
            // Add the lower 64-bits of our new product to the result limb.
            // We use c2 to track if this addition overflowed.
            uint64_t sum2;
            uint64_t c2 = luau_add_overflow(sum1, prod_lo, &sum2) ? 1 : 0;
            
            res[i + j] = sum2;
            
            // The new carry for the next iteration is the upper 64-bits of the product
            // plus any overflow bits from the additions we just did.
            carry = prod_hi + c1 + c2;
        }
        
        // Phase 2: Drop the remaining carry into the untouched next limb.
        // Because res was zero-initialized and no previous outer-loop iteration
        // has reached `i + b.len` yet, it is guaranteed to be 0. Thus, adding the carry
        // cannot overflow, and we avoid needing a ripple loop entirely.
        res[i + b.len] = carry;
    }
    res.len = a.len + b.len;
    normalize(res);
}

} // namespace BigInt
} // namespace Luau
