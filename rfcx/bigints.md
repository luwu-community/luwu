# Arbitrary-Precision BigInt

Status: Draft

## Summary

Introduce an arbitrary-precision `bigint` type to Luau. This new type handles large integers while optimizing performance by storing values <= 64 bits inline within the VM's value representation. 

## Motivation

Luau numbers are IEEE 754 doubles, which lose precision beyond `2^53 - 1`. There are many use cases for strict 64-bit precision (e.g., bitfields, Discord permissions) or larger numbers (e.g., simulations). Introducing a `bigint` type solves this. This RFC also replaces the upstream `FFlag::LuauIntegerType2` `int64` implementation, which lacks metatable support and other features.

## Design

### Object Representation & SMI

The upstream `LUA_TINTEGER` type will be removed and replaced with `LUA_TBIGINT`. 

To maintain performance, `bigint` values use a **Small Integer (SMI)** optimization:

* **Inline (<= 64-bit):** Fits within a 64-bit signed integer, stored directly inline within the VM's value.
* **Heap (Overflow):** Values exceeding 64 bits dynamically promote to a heap-allocated, garbage-collected object containing a sign flag and an array of 32-bit digits (similar to V8's BigInt representation).

### Arithmetic & Relational Operations

A compliant implementation must support the following core operations on `bigint` values:

* `bigint + bigint -> bigint`: Addition
* `bigint - bigint -> bigint`: Subtraction
* `bigint * bigint -> bigint`: Multiplication
* `bigint / bigint -> bigint`: Division
* `bigint % bigint -> bigint`: Modulo
* `-bigint -> bigint`: Negation
* `bigint < bigint -> boolean`: Less than
* `bigint <= bigint -> boolean`: Less than or equal
* `bigint > bigint -> boolean`: Greater than
* `bigint >= bigint -> boolean`: Greater than or equal

**Fast-Path:** If both operands are inline 64-bit integers, the VM performs fast hardware math, checking for overflow.

**Slow-Path:** If an overflow occurs, or an operand is heap-allocated, it dispatches to a new arbitrary-precision library.

### Native Code Generation (NCG)

NCG will only emit optimized fast-paths for inline 64-bit integers. Non-int64 cases or overflows will safely fall back to the VM's slow path.

### Language & Standard Library

* **Literals:** Constructed using the `i` suffix (e.g., `123i`), maintaining compatibility with `FFlag::LuauIntegerType2`.
* **Mixed Math:** Mixing `bigint` and `number` (e.g., `123i + 1.5`) throws a runtime type error to prevent precision loss.
* **Type System:** A `bigint` primitive type will be added to the typechecker.
* **Type & String Conversion:** Calling `type()` on a bigint returns `"bigint"`. Calling `tostring()` returns the string representation of the integer without any suffix (e.g., `"123"`, not `"123i"`).
### BigInt Library

A `bigint` standard library will be provided with the following methods:

`function bigint.min(...: bigint): bigint`

Returns the minimum value among the arguments.

`function bigint.max(...: bigint): bigint`

Returns the maximum value among the arguments.

`function bigint.rem(a: bigint, b: bigint): bigint`

Returns the remainder of dividing `a` by `b`.

`function bigint.clamp(n: bigint, min: bigint, max: bigint): bigint`

Returns `n` clamped between `min` and `max`.

`function bigint.bnot(n: bigint): bigint`

Returns the bitwise NOT of `n`.

`function bigint.band(...: bigint): bigint`

Returns the bitwise AND of the arguments.

`function bigint.bor(...: bigint): bigint`

Returns the bitwise OR of the arguments.

`function bigint.bxor(...: bigint): bigint`

Returns the bitwise XOR of the arguments.

`function bigint.lshift(n: bigint, i: bigint): bigint`

Returns `n` logically shifted left by `i` bits.

`function bigint.rshift(n: bigint, i: bigint): bigint`

Returns `n` logically shifted right by `i` bits.

`function bigint.arshift(n: bigint, i: bigint): bigint`

Returns `n` arithmetically shifted right by `i` bits.

`function bigint.lrotate(n: bigint, i: bigint): bigint`

Rotates `n` to the left by `i` bits (if `i` is negative, a right rotate is performed instead).

`function bigint.rrotate(n: bigint, i: bigint): bigint`

Rotates `n` to the right by `i` bits (if `i` is negative, a left rotate is performed instead).

`function bigint.wadd(a: bigint, b: bigint): bigint`

Returns the wrapping addition of `a` and `b`.

`function bigint.sadd(a: bigint, b: bigint, max: bigint): bigint`

Returns the saturating addition of `a` and `b` up to `max`.

## Drawbacks

* **VM Complexity:** Adding branch checks for `LUA_TBIGINT` in the execution loop slightly increases complexity.
* **Maintenance:** A custom arbitrary-precision bignum library requires careful testing and long-term maintenance.

## Alternatives

* **Strict 64-bit Types:** Adding explicit `i64` and `u64` types could be considered later.
* **Userdata Wrapping:** Host applications could implement BigInts via `userdata`, but this suffers from performance degradation, forces heap allocations for all math, and lacks typechecker support.
