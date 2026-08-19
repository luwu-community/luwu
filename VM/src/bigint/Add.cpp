// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Add.h"
#include "../lnumutils.h"
#include "Sub.h"
#include "Cmp.h"

namespace Luau {
namespace BigInt {

void AddAbs(RWDigits& res, Digits a, Digits b) {
    uint32_t max_size = (a.len > b.len) ? a.len : b.len;
    uint64_t carry = 0;
    
    res.len = 0;
    
    // Continue looping as long as there are limbs to add OR a lingering carry bit
    //
    // Basically the below but applied to uint64 limbs
    //
    // 1 1
    //   9 7
    //   + 4
    // -----
    // 1 0 1
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

bool Add(RWDigits& res, SignedDigits a, SignedDigits b) {
    if (a.isNegative == b.isNegative) {
        AddAbs(res, a.digits, b.digits);
        return a.isNegative;
    } else {
        int cmp = CmpAbs(a.digits, b.digits);
        if (cmp >= 0) {
            SubAbs(res, a.digits, b.digits);
            return a.isNegative;
        } else {
            SubAbs(res, b.digits, a.digits);
            return b.isNegative;
        }
    }
}

} // namespace BigInt
} // namespace Luau
