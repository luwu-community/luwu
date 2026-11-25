// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include "Digits.h"

namespace Luau {
namespace BigInt {

// ToStringContext provides all the needed context needed for ToString to allocate the needed scratch memory
// and then finally produce the resulting string
struct ToStringContext {
    void* userContext;
    
    // Allocates a temporary array of uint64_t limbs. The caller is responsible 
    // for freeing this memory once ToString returns.
    uint64_t* (*allocScratch)(void* userContext, size_t limbs);
    
    // Called when the string generation is complete
    void (*appendString)(void* userContext, const char* str, size_t len);
};

void ToString(SignedDigits d, ToStringContext ctx);
void ToString16(SignedDigits d, ToStringContext ctx);

} // namespace BigInt
} // namespace Luau
