// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include "Digits.h"

namespace Luau {
namespace BigInt {

int CmpAbs(Digits a, Digits b);
int Cmp(SignedDigits a, SignedDigits b);

} // namespace BigInt
} // namespace Luau
