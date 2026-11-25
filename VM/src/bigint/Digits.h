// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include <stdint.h>
#include "../ldebug.h"

namespace Luau {
namespace BigInt {

struct Digits {
    const uint64_t* ptr;
    uint32_t len;

    Digits() : ptr(nullptr), len(0) {}
    Digits(const uint64_t* p, uint32_t l) : ptr(p), len(l) {}
    
    const uint64_t& operator[](uint32_t i) const {
        LUAU_ASSERT(i < len);
        return ptr[i];
    }
};

struct RWDigits {
    uint64_t* ptr;
    uint32_t len;
    uint32_t cap;

    RWDigits() : ptr(nullptr), len(0), cap(0) {}
    RWDigits(uint64_t* p, uint32_t l, uint32_t c) : ptr(p), len(l), cap(c) {}

    uint64_t& operator[](uint32_t i) {
        LUAU_ASSERT(i < cap);
        return ptr[i];
    }
    
    const uint64_t& operator[](uint32_t i) const {
        LUAU_ASSERT(i < len);
        return ptr[i];
    }

    operator Digits() const {
        return Digits(ptr, len);
    }
};

inline void normalize(RWDigits& digits) {
    while (digits.len > 0 && digits.ptr[digits.len - 1] == 0) {
        digits.len--;
    }
}


struct SignedDigits {
    Digits digits;
    bool isNegative;

    SignedDigits() : isNegative(false) {}
    SignedDigits(Digits d, bool neg) : digits(d), isNegative(neg) {}
};

} // namespace BigInt
} // namespace Luau
