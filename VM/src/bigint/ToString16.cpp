// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "ToString.h"

namespace Luau {
namespace BigInt {

void ToString16(SignedDigits d, ToStringContext ctx) {
    if (d.digits.len == 0) {
        ctx.appendString(ctx.userContext, "0", 1);
        return;
    }

    // A 64-bit limb is exactly 16 hex characters (64/4 = 16)
    //
    // Add 1 for an optional '-' sign.
    size_t max_chars = d.digits.len * 16 + 1;
    
    // We only need scratch space for the final character buffer
    size_t buf_limbs = (max_chars + 7) / 8;
    uint64_t* scratch = ctx.allocScratch(ctx.userContext, buf_limbs);
    char* buf = (char*)scratch;
    
    int pos = 0;
    if (d.isNegative) {
        buf[pos++] = '-';
    }

    const char* hex_chars = "0123456789abcdef";
    bool skipping_leading_zeros = true;

    // Iterate limbs from most-significant to least-significant
    for (int i = (int)d.digits.len - 1; i >= 0; --i) {
        uint64_t limb = d.digits[i];
        
        // Extract 4 bits at a time (16 hex chars per limb), starting from the highest 4 bits
        //
        // Top most bits are at position (60, 61, 62, 63) initially so start at 60
        for (int shift = 60; shift >= 0; shift -= 4) {
            uint32_t nibble = (limb >> shift) & 0xF;
            
            // Skip leading zeros on the most-significant limb
            if (skipping_leading_zeros && nibble == 0) {
                continue;
            }
            skipping_leading_zeros = false;
            
            buf[pos++] = hex_chars[nibble];
        }
    }

    // If the entire number was just leading zeros, it's just "0"
    if (skipping_leading_zeros) {
        buf[pos++] = '0';
    }

    ctx.appendString(ctx.userContext, buf, pos);
}

} // namespace BigInt
} // namespace Luau
