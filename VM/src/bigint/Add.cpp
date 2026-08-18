// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Add.h"
#include "../lnumutils.h"

namespace Luau {
namespace BigInt {

// Adds the absolute values of two BigInts (a + b) and stores the result in `res`.
// This implements long addition adapted for base-2^64 (array of limbs).
//
// It iterates through the limbs from least significant to most significant,
// adding corresponding limbs along with any carry from the previous column.
void AddAbs(RWDigits& res, Digits a, Digits b) {
    uint32_t max_size = a.len > b.len ? a.len : b.len;
    uint64_t carry = 0;
    
    res.len = 0;
    
    // Continue looping as long as there are limbs to add OR a lingering carry bit
    for (uint32_t i = 0; i < max_size || carry; ++i) {
        uint64_t sum = carry;
        uint64_t next_carry = 0;
        
        if (i < a.len) {
            if (luau_add_overflow(sum, a[i], &sum)) next_carry = 1;
        }
        if (i < b.len) {
            if (luau_add_overflow(sum, b[i], &sum)) next_carry = 1;
        }
        
        res[res.len++] = sum;
        carry = next_carry;
    }
    normalize(res);
}

} // namespace BigInt
} // namespace Luau
