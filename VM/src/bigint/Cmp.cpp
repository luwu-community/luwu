// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Cmp.h"

namespace Luau {
namespace BigInt {

int CmpAbs(Digits a, Digits b) {
    if (a.len != b.len) {
        return a.len < b.len ? -1 : 1;
    }
    for (uint32_t i = a.len; i > 0; --i) {
        if (a[i - 1] != b[i - 1]) {
            return a[i - 1] < b[i - 1] ? -1 : 1;
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
