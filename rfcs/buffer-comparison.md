# Buffer comparisons

Status: Implemented (Flagged)

FFlag: DebugLuwuBufferComparison

## Summary

Adds `buffer.order` for comparing byte ranges between two buffers and `buffer.equals` which includes buffers' lengths.

`buffer.order` uses C `memcmp` internally and returns strings `less`, `same`, or `greater`.
`buffer.equals` uses C `memcmp` internally too, but also returns `false` either when buffers' lengths are not equal or their contents.

The implementation is currently behind Debug FFlag until native code generation (NCG) support and u64 integration are available.

## Motivation

There is currently no direct way to compare buffer contents.

Users either have to compare bytes manually with `buffer.readu8`, or convert buffers to strings first.

`buffer.order` and `buffer.equals` makes this simpler and avoids unnecessary Luau loops or temporary strings.

## Design

### buffer.order(b1: buffer, b2: buffer, offset1: number?, offset2: number?, count: number?): "less" | "same" | "greater"

`offset1` and `offset2` default to `0`.

If `count` is omitted, it defaults to:

```text
min(buffer.len(b1) - offset1, buffer.len(b2) - offset2)
```

The function compares bytes lexicographically and returns strings `less`, `same`, or `greater` where:

- `less` means the first differing byte in `b1` is smaller than the corresponding byte in `b2`

- `greater` means the first differing byte in `b1` is greater than the corresponding byte in `b2`

- `same` means no differing byte was found in the compared range

For example:

```luau
local a = buffer.fromstring("abc")
local b = buffer.fromstring("abd")

assert(buffer.order(a, b) == "less")
assert(buffer.order(b, a) == "greater")
assert(buffer.order(a, a) == "same")
```

Buffers with identical prefixes can compare as "same" even if their lengths differ:

```luau
local a = buffer.fromstring("abc")
local b = buffer.fromstring("abcdef")

assert(buffer.order(a, b) == "same")
```

Ranges can also be compared:

```luau
local a = buffer.fromstring("xxxxabc")
local b = buffer.fromstring("yyyyabc")

assert(buffer.order(a, b, 4, 4, 3) == "same")
```

### buffer.equals(b1: buffer, b2: buffer, offset1: number?, offset2: number?, count: number?): boolean

`buffer.equals` supports two overloads:

- `buffer.equals(b1: buffer, b2: buffer): boolean` — compares entire buffers, including their lengths.
- `buffer.equals(b1: buffer, b2: buffer, offset1: number, offset2: number, count: number): boolean` — compares exactly `count` bytes at the specified offsets, regardless of the buffers' total lengths.

`offset1` and `offset2` default to `0`.

If `count` is omitted, it defaults to:

```text
min(buffer.len(b1) - offset1, buffer.len(b2) - offset2)
```

The function compares bytes lexicographically including buffers' lengths and returns `boolean`.

For example:

```luau
local a = buffer.fromstring("abc")
local b = buffer.fromstring("abc")
local c = buffer.fromstring("abcdef")

assert(buffer.equals(a, b) == true)
assert(buffer.equals(a, c) == false)
assert(buffer.equals(a, c, 0, 0, 3) == true)
```

Ranges with different starting offsets can also be compared:

```luau
local a = buffer.fromstring("xxxxabc")
local b = buffer.fromstring("yyyyabc")

assert(buffer.equals(a, b, 4, 4, 3) == true)
```

Invalid offsets, a negative `count` or ranges outside either buffer result in a `buffer access out of bounds` error.

## Compatibility

This is an additive library API and does not change existing language behavior or introduce new syntax.

Existing Luwu and Luau code is unaffected.

Upstream Luau does not currently provide `buffer.order` nor `buffer.equals`, so code using it would currently be Luwu-specific.

## Drawbacks

This adds another function to the public `buffer` API for something that can already be implemented in Luau.

## Alternatives

For equality checks, buffers can be converted to strings:

```luau
local equal = buffer.tostring(buf1) == buffer.tostring(buf2)
```

This is simple, but creates temporary strings just to compare the data.

The proposed API avoids those conversions and exposes native comparison operations directly.
