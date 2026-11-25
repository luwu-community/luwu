// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

// Luwu Integers (rfcs/integers.md): integer literal parsing shared by the parser and the VM's `integer` library. The VM
// builds as C++11, so this header must too.
namespace Luau
{

enum class ParseIntResult
{
    Ok,
    Malformed,
    Overflow
};

// Parses all of `str` as an integer in `base` (2, 10 or 16). Base 10 takes a sign and gives a signed 64-bit value. Bases
// 2 and 16 give the full unsigned 64-bit bit pattern; hexadecimal keeps its `0x` prefix, binary has it removed.
inline ParseIntResult parseInt(const char* str, int base, int64_t& outResult)
{
    // A bare "-" or "+" or empty string is malformed.
    if (str[0] == '\0')
        return ParseIntResult::Malformed;

    if ((str[0] == '-' || str[0] == '+') && str[1] == '\0')
        return ParseIntResult::Malformed;

    if (base == 2 && str[0] == '0' && (str[1] == 'b' || str[1] == 'B'))
        return ParseIntResult::Malformed;

    char* end = nullptr;

    if (base == 10)
    {
        bool isNegative = false;
        const char* p = str;
        if (*p == '-')
        {
            isNegative = true;
            p++;
        }
        else if (*p == '+')
        {
            p++;
        }

        while (*p == '0')
            p++;

        // "-0" and "+0" properly collapse to positive zero here
        if (*p == '\0')
        {
            outResult = 0;
            return ParseIntResult::Ok;
        }

        unsigned long long u = strtoull(p, &end, 10);

        if (end == p || *end != '\0')
            return ParseIntResult::Malformed;

        if ((u == ULLONG_MAX) && (errno == ERANGE))
        {
            // 'errno' might have been set before we called 'strtoull', but we don't want the overhead of resetting a TLS variable on each call
            // so we only reset it when we get a result that might be an out-of-range error and parse again to make sure
            errno = 0;
            u = strtoull(p, &end, 10);

            if (errno == ERANGE)
                return ParseIntResult::Overflow;
        }

        if (!isNegative && u <= (uint64_t)INT64_MAX)
        {
            outResult = (int64_t)u;
            return ParseIntResult::Ok;
        }
        else if (isNegative && u <= (uint64_t)INT64_MAX + 1)
        {
            outResult = (int64_t)(0ULL - u);
            return ParseIntResult::Ok;
        }
        else
        {
            return ParseIntResult::Overflow;
        }
    }
    else
    {
        // hex and binary literals represent bit patterns covering the full uint64 range
        unsigned long long u = strtoull(str, &end, base);

        if (end == str || *end != '\0')
            return ParseIntResult::Malformed;

        if ((u == ULLONG_MAX) && (errno == ERANGE))
        {
            errno = 0;
            u = strtoull(str, &end, base);

            if (errno == ERANGE)
                return ParseIntResult::Overflow;
        }

        outResult = (int64_t)u;
        return ParseIntResult::Ok;
    }
}

// Parses all of `digits` as an unsigned integer in `base` (2, 8, 10 or 16), with no sign or prefix. A literal's sign is a
// separate unary minus, so this reads the full unsigned 64-bit range.
inline ParseIntResult parseIntegerMagnitude(const char* digits, int base, uint64_t& outResult)
{
    if (digits[0] == '\0')
        return ParseIntResult::Malformed;

    uint64_t value = 0;
    bool overflow = false;

    for (const char* p = digits; *p; ++p)
    {
        int digit = -1;
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f')
            digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F')
            digit = *p - 'A' + 10;

        if (digit < 0 || digit >= base)
            return ParseIntResult::Malformed;

        bool fits = value <= (UINT64_MAX - uint64_t(digit)) / uint64_t(base);
        if (fits)
            value = value * uint64_t(base) + uint64_t(digit);
        else
            overflow = true;
    }

    if (overflow)
        return ParseIntResult::Overflow;

    outResult = value;
    return ParseIntResult::Ok;
}

} // namespace Luau
