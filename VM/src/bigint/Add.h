// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include "Digits.h"

namespace Luau {
namespace BigInt {

void AddAbs(RWDigits& res, Digits a, Digits b);
bool Add(RWDigits& res, SignedDigits a, SignedDigits b);

} // namespace BigInt
} // namespace Luau
