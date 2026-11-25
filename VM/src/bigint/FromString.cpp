// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "FromString.h"
#include "Add.h"
#include "Mul.h"

namespace Luau {
namespace BigInt {

ParseResult FromString(const char* str, FromStringContext ctx) {
    if (!str) return ParseResult::empty(str);

    bool isNegative = false;
    if (*str == '-') {
        isNegative = true;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Calculate length of digits
    size_t len = 0;
    while (str[len] >= '0' && str[len] <= '9') {
        len++;
    }
    
    const char* endptr = str + len;
    
    if (len == 0) {
        return ParseResult::empty(endptr); // Not a valid number
    }
    
    // Instead of making 5 temporary heapints (and the 2 in DivMode) = 7,
    // we can insyead calculate the total scratch space needed
    // and ask caller to allocate in one shot:
    // 1. max limbs (we use a safe upper bound/estimate as max digits per limb is ~19 bc int64), this is `temp_res` below
    // 2. chunk + multiplier (so +2)
    size_t max_limbs = (len * 4) / 10 + 2;
    size_t total_scratch = 1 + 1 + max_limbs;
    uint64_t* scratch = ctx.allocScratch(ctx.userContext, total_scratch);
    
    RWDigits chunk(scratch, 1, 1);
    RWDigits multiplier(scratch + 1, 1, 1);
    RWDigits temp_res(scratch + 2, 0, max_limbs);
    
    RWDigits accum = ctx.allocResult(ctx.userContext, max_limbs);
    accum.len = 0;
    
    size_t pos = 0;
    while (pos < len) {
        size_t chunk_len = len - pos;
        if (chunk_len > 19) chunk_len = 19;
        
        uint64_t chunk_val = 0;
        uint64_t mult_val = 1;
        for (size_t i = 0; i < chunk_len; ++i) {
            chunk_val = chunk_val * 10 + (str[pos + i] - '0');
            mult_val *= 10;
        }
        
        // accum = accum * mult_val + chunk_val
        if (accum.len > 0) {
            multiplier[0] = mult_val;
            
            for (uint32_t i = 0; i < temp_res.cap; i++) {
                temp_res.ptr[i] = 0;
            }
            
            MulAbs(temp_res, accum, multiplier);
            
            chunk[0] = chunk_val;
            chunk.len = (chunk_val > 0) ? 1 : 0;
            
            accum.len = 0;
            AddAbs(accum, temp_res, chunk);
        } else {
            if (chunk_val > 0) {
                accum[0] = chunk_val;
                accum.len = 1;
            }
        }
        
        pos += chunk_len;
    }
    
    ParseResult res;
    res.success = true;
    res.endptr = endptr;
    res.value.digits = accum;
    // Don't set isNegative if the parsed value is exactly zero
    res.value.isNegative = (accum.len > 0) ? isNegative : false;
    
    return res;
}

} // namespace BigInt
} // namespace Luau
