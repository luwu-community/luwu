# Arbitrary-Precision Integer

Status: Draft

## Summary

Introduce an arbitrary-precision `integer` type to Luau. This new type handles large integers while optimizing performance by storing values <= 64 bits inline within the VM's value representation. 

## Motivation

Luau numbers are IEEE 754 doubles, which lose precision beyond `2^53 - 1`. There are many use cases for strict 64-bit precision (e.g., bitfields, Discord permissions) or larger numbers (e.g., simulations). Introducing a `integer` type solves this. This RFC also replaces the upstream `FFlag::LuauIntegerType2` `int64` implementation, which lacks metatable support and other features.

## Design

### Object Representation & SMI

The upstream `LUA_TINTEGER` type will be removed and replaced with `LUA_TINTEGER`. 

To maintain performance, `integer` values use a **Small Integer (SMI)** optimization:

* **Inline (<= 64-bit):** Fits within a 64-bit payload, stored directly inline within the VM's value.
* **Heap (Overflow):** Values dynamically promote to a heap-allocated, garbage-collected object containing a sign flag and an array of 32-bit digits (similar to V8's Integer representation).

**Typed Modes:**
The `TValue` structural `extra[0]` payload is utilized to mark explicitly typed `integer` modes with zero memory overhead. A `IntegerMode` enum distinguishes between untyped/dynamic Integers (which can grow to the heap) and explicitly typed bounds (`u8`, `i8`, `u16`, `i16`, `u32`, `i32`, `u64`, `i64`). 

By default, mathematical operations on typed integers **wrap on overflow** to ensure maximum type safety. 

**Mode Propagation Rules:**
When performing binary operations (e.g., `+`, `-`, `*`) on integers:
1. **Same Typed Modes (e.g. `u16 + u16`)**: Operations are performed explicitly within that bounded type and return a integer of the same typed mode.
2. **Mixed Typed Modes (e.g. `u16 + u32`, `u32 + dynamic`)**: Throws a runtime type error requiring the user to explicitly cast them to identical types. This strictly enforces type safety.

### Arithmetic & Relational Operations

A compliant implementation must support the following core operations on `integer` values:

* `integer + integer -> integer`: Addition
* `integer - integer -> integer`: Subtraction
* `integer * integer -> integer`: Multiplication
* `integer / integer -> integer`: Division
* `integer % integer -> integer`: Modulo
* `-integer -> integer`: Negation
* `integer < integer -> boolean`: Less than
* `integer <= integer -> boolean`: Less than or equal
* `integer > integer -> boolean`: Greater than
* `integer >= integer -> boolean`: Greater than or equal

**Fast-Path:** If both operands are inline 64-bit integers, the VM performs fast hardware math, checking for overflow.

**Slow-Path:** If an overflow occurs, or an operand is heap-allocated, it dispatches to a new arbitrary-precision library.

### Native Code Generation (NCG)

NCG will only emit optimized fast-paths for inline 64-bit integers. Non-int64 cases or overflows will safely fall back to the VM's slow path.

### Language & Standard Library

* **Literals:** Constructed using the `i` suffix (e.g., `123i`), maintaining compatibility with `FFlag::LuauIntegerType2`.
* **Mixed Math:** Mixing `integer` and `number` (e.g., `123i + 1.5`) throws a runtime type error to prevent precision loss.
* **Type System:** A `integer` primitive type will be added to the typechecker.
* **Type & String Conversion:** Calling `type()` on a integer returns `"integer"`. Calling `tostring()` returns the string representation of the integer without any suffix (e.g., `"123"`, not `"123i"`).
### Integer Library

A `integer` standard library will be provided with the following methods:

`function integer.dynamic(n: integer): integer`

Coerces a typed integer back into a standard untyped/arbitrary-precision integer.

- `integer.i8(n)`: Bounds as signed 8-bit integer.
- `integer.u8(n)`: Bounds as unsigned 8-bit integer.
- `integer.i16(n)`: Bounds as signed 16-bit integer.
- `integer.u16(n)`: Bounds as unsigned 16-bit integer.
- `integer.i32(n)`: Bounds as signed 32-bit integer.
- `integer.u32(n)`: Same as above but bounds as unsigned 32-bit integer.
- `integer.i64(n)`: Same as above but bounds as signed 64-bit integer.
- `integer.u64(n)`: Same as above but bounds as unsigned 64-bit integer.
- `integer.wi8(n)`, `integer.wu8(n)`, `integer.wi16(n)`, `integer.wu16(n)`, `integer.wi32(n)`, `integer.wu32(n)`, `integer.wi64(n)`, `integer.wu64(n)`: Same as the respective bounds, but mathematical operations on these types will implicitly wrap instead of throwing an overflow error.

`function integer.min(...: integer): integer`

Returns the minimum value among the arguments.

`function integer.max(...: integer): integer`

Returns the maximum value among the arguments.

`function integer.rem(a: integer, b: integer): integer`

Returns the remainder of dividing `a` by `b`.

`function integer.clamp(n: integer, min: integer, max: integer): integer`

Returns `n` clamped between `min` and `max`.

`function integer.bnot(n: integer): integer`

Returns the bitwise NOT of `n`.

`function integer.band(...: integer): integer`

Returns the bitwise AND of the arguments.

`function integer.bor(...: integer): integer`

Returns the bitwise OR of the arguments.

`function integer.bxor(...: integer): integer`

Returns the bitwise XOR of the arguments.

`function integer.lshift(n: integer, i: integer): integer`

Returns `n` logically shifted left by `i` bits.

`function integer.rshift(n: integer, i: integer): integer`

Returns `n` logically shifted right by `i` bits.

`function integer.arshift(n: integer, i: integer): integer`

Returns `n` arithmetically shifted right by `i` bits.

`function integer.lrotate(n: integer, i: integer): integer`

Rotates `n` to the left by `i` bits (if `i` is negative, a right rotate is performed instead).

`function integer.rrotate(n: integer, i: integer): integer`

Rotates `n` to the right by `i` bits (if `i` is negative, a left rotate is performed instead).

`function integer.wadd(a: integer, b: integer): integer`

Returns the wrapping addition of `a` and `b`.

`function integer.sadd(a: integer, b: integer, max: integer): integer`

Returns the saturating addition of `a` and `b` up to `max`.

## Drawbacks

* **VM Complexity:** Adding branch checks for `LUA_TINTEGER` in the execution loop slightly increases complexity.
* **Maintenance:** A custom arbitrary-precision bignum library requires careful testing and long-term maintenance.

## Alternatives

* **Userdata Wrapping:** Host applications could implement Integers via `userdata`, but this suffers from performance degradation, forces heap allocations for all math, and lacks typechecker support.
