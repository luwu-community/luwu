// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Cmp.h"

namespace Luau {
namespace BigInt {

int CmpAbs(Digits a, Digits b) {
    // If lens mismatch, then the one with smaller length is smaller
    if (a.len != b.len) {
        int res = (a.len < b.len) ? -1 : 1; // avoid msvc miscompiles
        return res;
    }
    // Otherwise, go from left to right (numerically speaking):
    // If `a[i]` == `b[i]`, then we're good
    // If any `a[i]` < `b[i]`, then `a` is smaller
    // If any `a[i]` > `b[i]`, then `b` is smaller
    for (uint32_t i = a.len; i-- > 0; ) {
        if (a[i] != b[i]) {
            int res = (a[i] < b[i]) ? -1 : 1; // avoid msvc miscompiles
            return res;
        }
    }
    return 0;
}

int Cmp(SignedDigits a, SignedDigits b) {
    if (a.digits.len == 0 && b.digits.len == 0) return 0;
    if (a.isNegative && !b.isNegative) return -1;
    if (!a.isNegative && b.isNegative) return 1;
    
    int cmp = CmpAbs(a.digits, b.digits);
    return a.isNegative ? -cmp : cmp;
}

} // namespace BigInt
} // namespace Luau
