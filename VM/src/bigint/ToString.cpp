// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "ToString.h"
#include "DivMod.h"

namespace Luau {
namespace BigInt {

constexpr uint64_t kMaxPowerOf10 = 10000000000000000000ULL;

// Computes ToString on a set of SignedDigits d
//
// See README.md for an explanation of this algorithm
void ToString(SignedDigits d, ToStringContext ctx) {
    if (d.digits.len == 0) {
        ctx.appendString(ctx.userContext, "0", 1);
        return;
    }

    size_t max_digits = d.digits.len * 20 + 2;
    size_t buf_limbs = (max_digits + 7) / 8;
    
    // Instead of making 5 temporary heapints (and the 2 in DivMode) = 7,
    // we can insyead calculate the total scratch space needed
    // and ask caller to allocate in one shot:
    //
    // 1. char buffer (buf_limbs)
    // 2. current (d.digits.len)
    // 3. chunk (1)
    // 4. quotient (d.digits.len)
    // 5. remainder (1)
    // 6. shift_d for DivMod (d.digits.len + 2)
    // 7. temp_rem for DivMod (d.digits.len)
    uint32_t current_len = d.digits.len;
    size_t total_scratch = buf_limbs + current_len + 1 + current_len + current_len + (current_len + 2);
    
    uint64_t* scratch = ctx.allocScratch(ctx.userContext, total_scratch);
    size_t offset = 0;
    
    char* buf = (char*)(scratch + offset);
    offset += buf_limbs;
    
    RWDigits current(scratch + offset, current_len, current_len);
    offset += current_len;
    
    for (uint32_t i = 0; i < current_len; i++) {
        current[i] = d.digits[i];
    }
    
    RWDigits chunk(scratch + offset, 1, 1);
    offset += 1;
    chunk[0] = kMaxPowerOf10;
    
    RWDigits quotient(scratch + offset, 0, current_len);
    offset += current_len;
    
    RWDigits remainder(scratch + offset, 0, current_len);
    offset += current_len;
    
    RWDigits shift_d(scratch + offset, 0, current_len + 2);
    offset += current_len + 2;
    
    
    
    int pos = 0;
    while (current.len > 0) {
        remainder.len = current.len;
        for (uint32_t i = 0; i < current.len; i++) remainder.ptr[i] = current.ptr[i];
        
        DivModAbs(&quotient, &remainder, current, chunk, shift_d);
        uint64_t rem_val = remainder.len > 0 ? remainder[0] : 0;
        
        current.len = quotient.len;
        for (uint32_t i = 0; i < quotient.len; i++) {
            current[i] = quotient[i];
        }
        
        bool is_last = (current.len == 0);
        
        if (is_last) {
            while (rem_val > 0) {
                buf[pos++] = '0' + (rem_val % 10);
                rem_val /= 10;
            }
        } else {
            for (int i = 0; i < 19; i++) {
                buf[pos++] = '0' + (rem_val % 10);
                rem_val /= 10;
            }
        }
    }
    
    if (pos == 0) {
        buf[pos++] = '0';
    } else if (d.isNegative) {
        buf[pos++] = '-';
    }
    
    // Reverse string
    for (int i = 0; i < pos / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[pos - 1 - i];
        buf[pos - 1 - i] = tmp;
    }
    
    ctx.appendString(ctx.userContext, buf, pos);
}

} // namespace BigInt
} // namespace Luau
