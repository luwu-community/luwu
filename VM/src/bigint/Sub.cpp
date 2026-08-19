// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Sub.h"
#include "../lnumutils.h"
#include "Add.h"
#include "Cmp.h"

namespace Luau {
namespace BigInt {

// Subtracts the absolute values (a - b) and stores the result in `res`.
//
// The caller MUST ensure that absolute value of `a` >= absolute value of `b`.
// This guarantees that the final borrow at the end of the loop is always 0.
void SubAbs(RWDigits& res, Digits a, Digits b) {
    uint64_t borrow = 0;
    res.len = 0;
    
    for (uint32_t i = 0; i < a.len; ++i) {
        uint64_t a_val = a[i];
        uint64_t b_val = (i < b.len) ? b[i] : 0;
        
        uint64_t diff;
        bool b1 = luau_sub_overflow(a_val, b_val, &diff);
        bool b2 = luau_sub_overflow(diff, borrow, &diff);
        borrow = (b1 || b2) ? 1 : 0;
        
        res[res.len++] = diff;
    }
    normalize(res);
}

bool Sub(RWDigits& res, SignedDigits a, SignedDigits b) {
    if (a.isNegative != b.isNegative) {
        AddAbs(res, a.digits, b.digits);
        return a.isNegative;
    } else {
        int cmp = CmpAbs(a.digits, b.digits);
        if (cmp >= 0) {
            SubAbs(res, a.digits, b.digits);
            return a.isNegative;
        } else {
            SubAbs(res, b.digits, a.digits);
            return !a.isNegative;
        }
    }
}

} // namespace BigInt
} // namespace Luau
