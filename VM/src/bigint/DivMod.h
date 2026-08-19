// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include "Digits.h"

namespace Luau {
namespace BigInt {

void DivModAbs(RWDigits* q, RWDigits* rem, Digits n, Digits d, RWDigits shift_d);

} // namespace BigInt
} // namespace Luau
