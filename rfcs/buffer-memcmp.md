# buffer.memcmp

Status: Implemented (Flagged)

FFlag: LuwuBufferMemcmp

## Summary

Adds `buffer.memcmp` for comparing byte ranges between two buffers.

The implementation uses C `memcmp` internally and normalizes the result **exactly** to `-1`, `0`, or `1`.

## Motivation

There is currently no direct way to compare buffer contents.

Users either have to compare bytes manually with `buffer.readu8`, or convert buffers to strings first.

`buffer.memcmp` makes this simpler and avoids unnecessary Luau loops or temporary strings.

## Design


### buffer.memcmp(b1: buffer, b2: buffer, offset1: number?, offset2: number?, count: number?): number

`offset1` and `offset2` default to `0`.

If `count` is omitted, it defaults to:

```text
min(buffer.len(b1) - offset1, buffer.len(b2) - offset2)
```

The function compares bytes lexicographically and returns **exactly** `-1`, `0`, or `1` where:

- `-1` means the first differing byte in `b1` is smaller than the corresponding byte in `b2`

- `1` means the first differing byte in `b1` is greater than the corresponding byte in `b2`

- `0` means no differing byte was found in the compared range

For example:

```luau
local a = buffer.fromstring("abc")
local b = buffer.fromstring("abd")

-- 'c' (99) is smaller than 'd' (100)
assert(buffer.memcmp(a, b) == -1)

-- Reversing the arguments reverses the ordering.
assert(buffer.memcmp(b, a) == 1)
```

`buffer.memcmp` returns a number instead of a boolean because it provides both equality and ordering information. A boolean could only answer whether the ranges are equal, while `-1` and `1` also indicate which range compares smaller or greater. This is useful for sorting or ordered comparisons.

Invalid offsets, a negative `count` or ranges outside either buffer result in a `buffer access out of bounds` error.

### Examples

```luau
local a = buffer.fromstring("abcdeg")
local b = buffer.fromstring("abcdeg")

assert(buffer.memcmp(a, b) == 0)
```

Ranges can also be compared:

```luau
local a = buffer.fromstring("xxxxabc")
local b = buffer.fromstring("yyyyabc")

assert(buffer.memcmp(a, b, 4, 4) == 0)
```

When `count` is omitted, the smaller remaining range of the two buffers is used as the count. For example:

```luau
local a = buffer.fromstring("abc")
local b = buffer.fromstring("abcdef")

assert(buffer.memcmp(a, b) == 0)
```

## Compatibility

This is an additive library API and does not change existing language behavior or introduce new syntax.

Existing Luwu and Luau code is unaffected.

Upstream Luau does not currently provide `buffer.memcmp`, so code using it would currently be Luwu-specific.

## Drawbacks

This adds another function to the public `buffer` API for something that can already be implemented in Luau.

## Alternatives

The comparison can be implemented manually:

```luau
local function memcmp(b1: buffer, b2: buffer, offset1: number?, offset2: number?, count: number?): number
    local len1 = buffer.len(b1)
    local len2 = buffer.len(b2)

    offset1 = offset1 or 0
    offset2 = offset2 or 0

    if offset1 < 0 or offset2 < 0 then
        error("buffer access out of bounds", 2)
    end

    local remaining1 = len1 - offset1
    local remaining2 = len2 - offset2

    if remaining1 < 0 or remaining2 < 0 then
        error("buffer access out of bounds", 2)
    end

    if count == nil then
        count = math.min(remaining1, remaining2)
    end

    if count < 0 or count > remaining1 or count > remaining2 then
        error("buffer access out of bounds", 2)
    end

    for i = 0, count - 1 do
        local a = buffer.readu8(b1, offset1 + i)
        local b = buffer.readu8(b2, offset2 + i)

        if a < b then
            return -1
        elseif a > b then
            return 1
        end
    end

    return 0
end
```

This works, but requires a Luau loop and a buffer read for every byte.

For equality checks, buffers can also be converted to strings:

```luau
local equal = buffer.tostring(buf1) == buffer.tostring(buf2)
```

This is simple, but creates temporary strings just to compare the data.

Without `buffer.memcmp`, users have to keep using one of these approaches.
