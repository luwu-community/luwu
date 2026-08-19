// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include "Digits.h"

namespace Luau {
namespace BigInt {

// FromStringContext provides all the needed context needed for FromString to allocate the needed scratch memory
// and then finally produce the resulting heapinteger
struct FromStringContext {
    void* userContext;
    
    // Allocates a temporary array of uint64_t limbs. The caller is responsible 
    // for eventually freeing this memory once the FromString operation completes.
    uint64_t* (*allocScratch)(void* userContext, size_t limbs);
    
    // Allocates the final destination buffer for the parsed digits.
    // The caller should return a mutable view with a capacity of at least `limbs`.
    RWDigits (*allocResult)(void* userContext, size_t limbs);
};

struct ParseResult {
    SignedDigits value;
    const char* endptr;
    bool success;
    
    static ParseResult empty(const char* ptr) {
        ParseResult res;
        res.success = false;
        res.endptr = ptr;
        res.value.isNegative = false;
        res.value.digits.len = 0;
        res.value.digits.ptr = nullptr;
        return res;
    }
};;

// Parses a string representation of a BigInt into a dynamically allocated buffer.
// Handles optional leading signs (+ or -) and contiguous decimal digits.
ParseResult FromString(const char* str, FromStringContext ctx);

} // namespace BigInt
} // namespace Luau
