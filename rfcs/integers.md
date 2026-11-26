# Integers

Status: Proposed

FFlag: DebugLuwuIntegers

## Summary

Redesign the `integer` primitive for Luwu. An integer is a whole number that is either signed or unsigned (can be negative
or is always positive) and is suffixed with one of `i8, i16, i32, i64, u8, u16, u32, u64, int` depending on the signedness
and representation. An `int`-suffixed value is dynamically-sized and graduates into a bigint when it exceeds the precision
of a 64-bit signed integer.

```luwu
const minimum_size = 2048u64
-- raises Error.Overflow if the size doesn't fit in a u64
const length = integer.u64(fs.path("./content.flac"):expect_file():size())
if length < minimum_size then
    error("music file is too small")
end
```

Integers of the same signedness and size may be added/subtracted/arithmetic'd normally; attempting
arithmetic on an integer and a number, or between integers of incorrect signedness or size without an explicit cast,
results in a runtime error. Integers of fixed size wrap on overflow.

An integer is either dynamically-sized, growing past 64 bits into an arbitrary-precision
bigint as it needs to, or sized (`u8`, `i8` up to `u64`, `i64`), wrapping on overflow. Values that fit in 64 bits are
stored inline in the VM's value representation, so most integer math never allocates.

## Background

Luau introduced a 64-bit `integer` primitive in 0.715 (April 3, 2026) implementing the
[64-bit Integer Type RFC](https://rfcs.luau.org/type-long-integer.html) (accepted February 12, 2026), enabled by default in
OSS Luau (luau.exe CLI, lute, and mluau) but not in Roblox. Like Java, their integer design did not distinguish between
signed and unsigned integers, and did not include standard arithmetic operators. They chose this design because they
assumed adding extra operator cases in the VM and NCG would reduce performance for numbers (as it did for vectors) and
because the `integer` feature was primarily intended for advanced users (like buffers) and was not supposed to replace
numbers in everyday use. Although the community wanted an integer feature, the design was criticized for lacking
distinction between signed and unsigned arithmetic, and unwillingness to add operators and other QoL features. A month
after implementation, internal pressure at Roblox for the `integer` feature suddenly disappeared, therefore the feature
received few updates outside bugfixes.

By October 29, 2026, the Luau team decided [not to implement integers](https://github.com/luau-lang/rfcs/pull/269),
removing its implementation in 0.747 (November 13, 2026). They will not accept RFCs for Lua 5.3-style integers.

## Motivation

Luwu numbers are IEEE doubles and lose precision above `2^53 - 1`. They cannot accurately represent numbers that need
64-bit or bigint precision such as Discord snowflakes, bitfields, large amounts of currency, and any usecase
that requires large, whole-number data. Integers, especially ones of fixed-size, are very useful for interop with C
and Rust code and especially when we need to maintain exact wrapping semantics of each size. This makes integers very
useful for the `buffer` library as well.

Outside advanced usecases, integers themselves are a very useful data type for encoding the type-level invariant
that a number cannot and should not have a fractional component. We don't see a reason to limit integers to just
advanced usecases like FFI interop and APIs--we see them being useful across the language.

## Design

### Literals

Decimal, hexadecimal (`0xff_u8`), binary (`0b1010_i8`), and octal (`0o755_u16`) literals may be suffixed:

```luau
local id = 1148720829839826944int  -- dynamic size, smaller than bigint so gets SMI'd
local big = 340282366920938463463374607431768211456int  -- bigint and gets heap allocated
local byte = 255u8
local offset = 0x7fff_ffffi32
local mask = 0b1111_0000u8
```

A sized literal that doesn't fit in its type (such as `300u8`, which cannot fit in a `u8`) is a parse error.
A minus written right before a literal is part of the literal, so `-128i8` is the smallest `i8`. An unsigned literal
can't be negated this way: `-5u8` is a parse error that suggests `-5i8`.

| Suffix | Signed | Smallest value | Largest value |
| --- | --- | --- | --- |
| `int` | yes | unbounded | unbounded |
| `i8` | yes | -128 | 127 |
| `u8` | no | 0 | 255 |
| `i16` | yes | -32768 | 32767 |
| `u16` | no | 0 | 65535 |
| `i32` | yes | -2147483648 | 2147483647 |
| `u32` | no | 0 | 4294967295 |
| `i64` | yes | -9223372036854775808 | 9223372036854775807 |
| `u64` | no | 0 | 18446744073709551615 |

Underscores can separate digits and the suffix, like in any number literal: `7_u64` is the same as `7u64`, and
`1_000_000int` is the same as `1000000int`.

### Semantics

#### Comparisons and hashes

Integers are hashed and compared by value, taking into account an integer's mode.

Comparing `__eq` of integers of different modes evaluates false at runtime.
Comparing integers `__eq` to any other type, including numbers, evaluates false at runtime.
An integer can only be `__eq` to another integer with the same value in the same mode.

Integers' `__lt` and `__le` raise for invalid comparisons when compared to any type that isn't
an integer of the exact same mode:

```luwu
print(try 7int == 7u8) -- Ok(false)
print(try 500u32 == 500u32) -- Ok(true)
print(try 4int < 5u8) -- Err(Error.InvalidComparison)
print(try 5int <= 5int) -- Ok(true)
print(try 5int >= 7u8) -- Err(Error.InvalidComparison)
print(try 5 >= 7u8) -- Err(Error.InvalidComparison)
print(try 5int > -7u8) -- trick one, SyntaxError because code refuses to parse you can't have negative u8
```

Note that all comparisons prod

Integer table keys of the same value but different modes don't collide:

```luwu
const lookup: { [integer]: string } = {
    [5int] = "five",
    [5u8] = "5 ascii characters definitely"
}
print(lookup[5]) -- nil
print(lookup[5u8]) -- 5 ascii characters definitely
print(lookup[5int]) -- five
```

#### Arithmetic and comparisons

The arithmetic operators work on integers and give integers:

- `+`, `-`, `*` and unary `-`;
- `/` and `//`, which both divide to an integer;
- `%`.

`==`, `~=`, `<`, `<=`, `>` and `>=` compare integers.

`^` raises an error on integers. A power overflows a sized integer far sooner than `+` or `*` do (`4u8 ^ 4u8` is
already past 255), and an operator would wrap it silently. Writing `x:pow(4u8)` instead puts the saturating `x:spow()`
and checked `x:cpow()` next to it in autocomplete. *See [Powers, roots and logarithms](#powers-roots-and-logarithms).*

Sized integers wrap on overflow. An operation on two integers of different modes raises an error, so a `u8` and an
`i64` can't be added without converting one of them first. Mixing an integer and a number (`7int + 1.5`) raises an
error, and an integer never equals a number.

Unary `-` on an unsigned integer raises an error at runtime and is a type error in analysis. `0u8 - x` gives the
wrapped negation when you want it.

#### Division and remainder

`/` divides and rounds toward zero like Rust's `/` on integers. `//` divides and rounds toward negative infinity like
`//` on numbers. `%` is the remainder of `//` and has the sign of the divisor like `%` on numbers, so
`a == (a // b) * b + a % b` holds in every mode. The two divisions only give different results when exactly one
operand is negative:

```luwu
print(7int / 2int, 7int // 2int, 7int % 2int) -- 3 3 1
print(-7int / 2int, -7int // 2int, -7int % 2int) -- -3 -4 1
print(7i8 / -2i8, 7i8 // -2i8, 7i8 % -2i8) -- -3 -4 -1
```

Rust's `%` is the remainder of `/` instead and has the sign of the dividend. Luwu's `%` follows `//` so that integer
code gives the same results as the same code written with numbers.

`/`, `//` and `%` raise `Error.DivisionByZero` when the divisor is zero, in every mode, where Rust panics. Dividing the
smallest value of a signed mode by `-1` overflows and wraps like any other sized operation, the same as Rust's
`wrapping_div` and `wrapping_rem`: `-128i8 / -1i8` is `-128i8` and `-128i8 % -1i8` is `0i8`. A dynamically-sized
integer never overflows here.

### VM design

An integer that fits in 64 bits is stored inline in a `TValue` with the type tag `LUA_TINTEGER` and never allocates,
just like a number or vector. This means integers are basically the same size as numbers in interpreted mode.
Note that due to the inherent size of `TValue` every non-heap integer is represented by a 64-bit integer internally.
In NCG, we optimize fixed-size integers to be their expected size when possible, but this is often not possible.

The mode of every integer is stored in its `TValue`'s `extra[0]` byte (unused except for vectors).
This means a sized integer takes no more memory than a dynamically-sized integer.

A dynamically-sized integer that doesn't fit is a bigint with type tag `LUA_THEAPINTEGER`. Just like numbers, vectors,
and smaller integers, bigints are compared and hashed by value. Since integers are immutable, all bigint arithmetic
operations *allocate a new bigint* on the heap.

TODO: measure cost of bigints in luwu vs other languages with bigints to see if we need mutable representation

#### Performance

Integer operators must not make number or vector arithmetic slower. Upstream avoided integer operators because they
expected new operator cases in the VM and NCG to slow numbers down, as adding vectors did.

- **Numbers:** each arithmetic instruction in the interpreter tests for two numbers first, exactly as before, and only
  tests for integers when that test fails. In native code, the integer path is in the fallback block that a failed
  number check jumps to, so code that only sees numbers runs the same instructions as before.
- **Vectors:** the interpreter tests for two vectors before it tests for integers, so vector arithmetic pays no extra
  check either. Native code specializes vector arithmetic from type information and never reaches the integer path.

The implementation shows both with before-and-after benchmarks of number-heavy and vector-heavy code, interpreted and
native, alongside bytecode and IR dumps of the same code. Integers themselves can be as fast as the implementation can
make them, new opcodes included. *See [Implementation details](#implementation-details).*

### `type` and `typeof`

The builtin functions `type` and `typeof` differ for `integers` like they do with builtin classes and bundles.

`type(inty)` returns `integer` on every integer, including bigints.
`typeof(inty)` returns the specific integer *mode*: `"u8" | "i8" | etc...`, and `"int"` for a dynamically-sized
integer, whether it's stored inline or as a bigint.

### `tostring` and `tonumber`

`tostring(inty)` returns a string representation of the integer *without* the mode suffix.
This will match the future `display` implementation for integers.

A new string format option is added to `string.format` to display integers *including* the mode suffix, because
otherwise it'd be difficult to roundtrip integers (especially big ones!) from strings.

`%q` writes an integer as a literal with its suffix. For strings, `%q` already writes a quoted string literal that reads
back as the same string, and the suffixed literal does the same for integers. `%d`, `%i`, `%x`, `%X` and `%o` write
integers of any mode and size exactly, without converting them to a number first, and take the usual flags and widths.

```luwu
string.format("%q", 300u16) -- "300u16"
string.format("%q", 340282366920938463463374607431768211456int) -- "340282366920938463463374607431768211456int"
string.format("%d", 340282366920938463463374607431768211456int) -- "340282366920938463463374607431768211456"
string.format("%08x", 255u32) -- "000000ff"
string.format("%x", -1i8) -- "ff", the bits of the value in its width
```

`tonumber(inty)` returns a number if and only if `inty` can be converted to a number without precision loss, otherwise
returns `nil`.

### Integer library

We replace the existing `integer` library inherited from Luau 0.730.

Types referenced are defined at the bottom.

#### Casting library functions

```luwu
function integer.cast<T, C: table, object, or userdata that implements __tointeger>(
    from: C | integer | string | number,
    to: Mode<T>? -- defaults to integer.dynamic
): integer<T> ! IntegerErrors<ErrorsWhenCasting<C>>
function integer.tonumber(i: integer): Result<number, number>
```

`integer.cast` converts values into integers by `integer.Mode` and allows casting between integers of different modes:

- If `to` is `nil` or unspecified, defaults mode to `integer.dynamic` (`int` suffix) unless `from` is a string with an integer suffix already.
- If `from` is an integer of mode `to`, returns `from` unchanged.
- If `from` is a string, parses `from` as an integer of mode `to`, raising one of `IntegerErrors` as needed.
  - If `from` ends with a suffix and `to` is `nil` or unspecified, casts into an integer of that suffix.
  - Raises underflow/overflow if `from` ends with a suffix (`"100u8"`) but the integer doesn't fit in that suffix.
- If `from` is a number, casts it to mode `to`. If `from` has a fractional component, raises `Error.UnexpectedFloat(f)`.
  A number above the mode's largest value raises `Error.Overflow` and one below its smallest value raises
  `Error.Underflow`, which includes `math.huge` and `-math.huge`. A NaN raises `Error.InvalidArgument`.
- If `from` is an integer but is not an integer of mode `to`, performs a checked cast of `from` into mode `to`.
- If `from` is value with a `__tointeger` metamethod, returns the integer returned by `from:__tointeger(to)`.
  *See [`__tointeger` metamethod](#__tointeger-metamethod).*
- Raises an unspecified runtime error if any of the arguments are invalid.

```luwu
const a = integer.cast(14, integer.u8)
const b = integer.cast("100", integer.u64)
const foo = input.readline("Pick an integer")
if foo:endswith(bundle.unpack(integer.suffixes)) then
    const implicit_cast = integer.cast(foo)
end
const c = integer.cast("100u8", integer.u64) -- treats input text as u8s and converts to u64
const converted = integer.cast(7int, integer.i8)
const oops = integer.cast("1412u8", integer.u64) -- raises error variant Error.Overflow(1412int, integer.u8)
```

`integer.tonumber` converts an `integer` to a `number`. It returns `Ok(n)` when `n` is exactly the integer's
value and `Err(n)` with the nearest number when it isn't. An integer beyond the range of a number gives `Err(math.huge)`
or `Err(-math.huge)`. It raises if its argument isn't an integer. The global `tonumber(i)` only converts exactly and
gives `nil` otherwise.

`integer.tonumber` is a builtin with its own lowering in the compiler and native code, which writes the number and the
variant into registers the way a `try` result is kept in registers. Matching the result with `case is Ok(n)` or
`if res is Ok(n)` doesn't allocate the `Ok` or `Err`.

```luwu
const res = match integer.tonumber(foo())
    case is Ok(f) then f
    case is Err(lossy) then do
        warn(`result of foo() was rounded because a number can't hold it exactly`)
        give lossy
```

We raise error variants (builtin classes), such as `Error.Overflow(got, mode)` instead of returning `nil` because there's
multiple reasons *why* a cast could fail.

We expect casts to be used with Luwu's [try operator](<link the rfc when ready>) and pattern matching syntax, which
makes handling errors much faster and more ergonomic than Luau:

```luwu
const picked = do
    local retries = 10
    const function try_again(): string
        return `Try again ({retries} retries remaining before default = 10int)`
    end

    repeat
        match try integer.cast(input.readline("Pick an integer"))
        case is Ok(int) then
            give int
        case is Err(_ is Error.UnexpectedFloat(got)) then
            retries -= 1
            print(`Integers can't have decimal points (got {got})\n{try_again()}`)
        case is Err(err is Error.CannotBeParsed) then
            retries -= 1
            print(`That's not a valid integer: {err}\n{try_again()}`)
        case is Err(err is Error.Overflow(got, mode) | Error.Underflow(got, mode)) then
            retries -= 1
            print(`The integer you passed ({got}) is not valid for the chosen mode {mode}: {err}\n{try_again()}`)
        else
            error(Error.Unreachable("all cases should be handled here"))
        end
    until retries == 0
    give 10int
```

Since `try` is pratically free on success and is easy to use, this means there's less of a performance and political cost
to raising errors at runtime in Luwu vs. Luau.

#### Integer namespaces with `integer.Mode`

We add namespaces for each supported mode as objects of a builtin class `Mode`, exposed as `integer.Mode`.
We chose to call this class `Mode` instead of `IntegerLibrary` or `IntegerMode` to prevent stuttering.

Although field access is fast, we further optimize field access on `Mode` through the `integer` library to be constant loads in safeenv
(sandboxed mode) when the `integer` library isn't shadowed.

#### Reflection and introspection

```luwu
function integer.mode(i: integer): Mode
function integer.isbig(i: int): boolean
integer.suffixes = bundle.pack("int", "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64")
```

`integer.mode` returns the `integer.Mode` object (the namespace of) an integer primitive.

`integer.isbig` returns `true` if the passed dynamically-sized integer is a bigint and therefore is heap allocated.

`integer.suffixes` is a bundle of all supported mode suffixes for use with string matching and `string.endswith`.

#### `min`, `max`, `clamp`, `abs` and `signum`

```luwu
function integer.min<T>(first: integer<T>, ...: integer<T>): integer<T>
function integer.max<T>(first: integer<T>, ...: integer<T>): integer<T>
function integer.clamp<T>(i: integer<T>, min: integer<T>, max: integer<T>): integer<T>
function integer.abs<T>(i: integer<T>): integer<T> ! Error.Overflow
function integer.signum<T>(i: integer<T>): integer<T>
```

These work like their `math` counterparts on integers of one mode. `integer.abs` raises `Error.Overflow` for the
smallest value of a signed sized mode if its absolute value doesn't fit (`integer.abs(-128i8)`). `integer.signum` gives
`-1`, `0` or `1` in the mode of `i`, and an unsigned mode only ever gives `0` or `1`. Rust has the same function under
the same name, and it's not called `sign` so it isn't mistaken for `math.sign`, which gives a number.

#### Saturating and checked arithmetic

Because arithmetic operators wrap sized integers on overflow, we provide the following saturating and checked
functions on the integer library and as methods on `integer`s.

```luwu
function integer.sadd<T>(a: integer<T>, b: integer<T>): integer<T>
function integer.ssub<T>(a: integer<T>, b: integer<T>): integer<T>
function integer.smul<T>(a: integer<T>, b: integer<T>): integer<T>
function integer.cadd<T>(a: integer<T>, b: integer<T>): integer<T> ! Error.Overflow | Error.Underflow
function integer.csub<T>(a: integer<T>, b: integer<T>): integer<T> ! Error.Overflow | Error.Underflow
function integer.cmul<T>(a: integer<T>, b: integer<T>): integer<T> ! Error.Overflow | Error.Underflow
function integer.cdiv<T>(a: integer<T>, b: integer<T>): integer<T> ! Error.Overflow | Error.DivisionByZero
```

`integer.cdiv` divides like `/`, rounding toward zero. Dividing the smallest value of a signed mode by `-1` is the one
division that overflows, and `integer.cdiv` raises `Error.Overflow` there instead of wrapping.

Saturating functions return an integer that does not exceed its mode's minimum or maximum value:

```luwu
const added = integer.sadd(100u8, 250u8) -- 255u8
const subbed = added:ssub(250u8):ssub(100u8) -- 0u8
```

Using saturating/checked arithmetic on dynamically-sized integers does the exact same thing
as regular arithmetic; we lint against using these functions on `ints` because `int`s can never overflow.

#### Powers, roots and logarithms

```luwu
function integer.pow<T>(i: integer<T>, exp: integer): integer<T>
function integer.spow<T>(i: integer<T>, exp: integer): integer<T>
function integer.cpow<T>(i: integer<T>, exp: integer): integer<T> ! Error.Overflow | Error.Underflow
function integer.sqrt<T>(i: integer<T>): integer<T>
function integer.log2<T>(i: integer<T>): integer<T>
function integer.log10<T>(i: integer<T>): integer<T>
```

`^` raises on integers, so `integer.pow` is how an integer is raised to a power. A sized result wraps like the other
operators, `integer.spow` saturates and `integer.cpow` raises. A dynamically-sized result grows, so
`integer.pow(2int, 100int)` is `2^100` exactly. `exp` can have any mode. A negative `exp` raises an error, since the
result wouldn't be an integer. `integer.pow(0u8, 0u8)` is `1u8`, as in Rust.

`integer.sqrt` is the largest integer whose square is at most `i`. It's exact at any size, where `math.sqrt` on a
number is wrong past `math.exactlimit`. `integer.log2` and `integer.log10` round the logarithm down: `integer.log2`
is one less than the number of bits `i` needs, and `integer.log10` one less than its number of decimal digits.

```luwu
const digits = (12345u32):log10() + 1u32 -- 5u32
const side = (1_000_000int):sqrt() -- 1000int
```

All three raise an error on a negative `i`, and the logarithms raise on `0` too. Their semantics follow Rust's `isqrt`,
`ilog2` and `ilog10`.

#### Bitwise operations

```luwu
function integer.band<T>(first: integer<T>, ...: integer<T>): integer<T>
function integer.bor<T>(first: integer<T>, ...: integer<T>): integer<T>
function integer.bxor<T>(first: integer<T>, ...: integer<T>): integer<T>
function integer.bnot<T>(i: integer<T>): integer<T>
```

`integer.band`, `integer.bor` and `integer.bxor` take integers of one mode and return their bitwise AND, OR and XOR in
that mode. `integer.bnot` flips every bit of a sized integer. A dynamically-sized integer has no width, so the bitwise
functions treat it as if it had infinitely many sign bits like Python does: `integer.bnot(5int)` is `-6int`.

```luwu
const flags = integer.bor(0b0001_u8, 0b0100_u8) -- 0b0101_u8
const masked = integer.band(flags, integer.bnot(0b0001_u8)) -- 0b0100_u8
```

#### Shifts and rotations

```luwu
function integer.lshift<T>(i: integer<T>, by: integer): integer<T>
function integer.rshift<T>(i: integer<T>, by: integer): integer<T>
function integer.lrotate<T>(i: integer<T>, by: integer): integer<T>
function integer.rrotate<T>(i: integer<T>, by: integer): integer<T>
```

`integer.rshift` shifts a signed integer arithmetically (copying the sign bit) and an unsigned one logically (filling
with zeroes). Luau's `bit32` needs separate `rshift` and `arshift` functions for this, which integers don't, because
their mode already says whether they're signed. `by` can have any mode. A negative `by` raises an error.

A sized integer drops the bits shifted past its width. Shifting by the width or more gives `0`, or `-1` when
`integer.rshift` copies the sign bit of a negative value. This is what Rust's `unbounded_shl` and `unbounded_shr` do,
and what `bit32.lshift(x, 32)` does in Luau. Rust's `<<` operator panics there instead, and its `wrapping_shl` takes
the shift modulo the width (`1u8.wrapping_shl(8)` is `1`). Both are easy to trip over when the shift is computed.

A dynamically-sized integer grows on `integer.lshift`: `integer.lshift(1int, 100int)` is `2^100`. `integer.rshift`
on one rounds toward negative infinity like Python's `>>`, so `integer.rshift(-5int, 1int)` is `-3int`.

Rotations take `by` modulo the width like Rust's `rotate_left` and `rotate_right`:
`integer.lrotate(0b1000_0001_u8, 9int)` is `0b0000_0011_u8`. A signed integer rotates the bits of its two's complement
form. Rotating a dynamically-sized integer raises an error, since it has no width to rotate within.

#### Counting bits

```luwu
function integer.countlz<T>(i: integer<T>): integer<T>
function integer.countrz<T>(i: integer<T>): integer<T>
function integer.popcount<T>(i: integer<T>): integer<T>
```

`integer.countlz` and `integer.countrz` count the zero bits above the highest set bit and below the lowest one, and
`integer.popcount` counts the set bits, like Rust's `leading_zeros`, `trailing_zeros` and `count_ones`. The result has
the same mode as `i`. A signed integer is counted in two's complement: `integer.popcount(-1i8)` is `8i8` and
`integer.countlz(-1i8)` is `0i8`. Zero has no set bit, so `integer.countlz(0u8)` and `integer.countrz(0u8)` are both
`8u8`, the width of a `u8`.

A dynamically-sized integer has no width, and `integer.countlz` raises on one. `integer.countrz(0int)` raises too,
since zero has no lowest set bit to count up to. A negative `int` has infinitely many set bits, and `integer.popcount`
raises on one. `integer.countrz` of a negative `int` is the same as of its absolute value: `integer.countrz(-8int)` is
`3int`.

#### Bit fields and byte order

```luwu
function integer.extract<T>(i: integer<T>, field: integer, width: integer?): integer<T>
function integer.replace<T>(i: integer<T>, value: integer<T>, field: integer, width: integer?): integer<T>
function integer.btest<T>(first: integer<T>, ...: integer<T>): boolean
function integer.bswap<T>(i: integer<T>): integer<T>
```

These pack several small values into one integer and read them back. They work like `bit32`'s functions of the same
names, which Luau 0.730's `integer` library also had, at the integer's width instead of 32 bits.

- `integer.extract` gives the `width` bits starting at bit `field` (bit 0 is the lowest), moved down to bit 0. The bits
  above them are zero, so the result isn't negative even in a signed mode. `width` defaults to 1, which reads one flag.
- `integer.replace` gives a copy of `i` with those bits replaced by the lowest `width` bits of `value`. The rest of
  `value` is ignored.
- `integer.btest` is whether the bitwise AND of its arguments isn't zero: whether any flag in a mask is set.
- `integer.bswap` reverses the byte order of a sized integer, for file formats and network protocols that store values
  big-endian while `buffer` is little-endian. It leaves an 8-bit integer as it is.

A negative `field`, a `width` below 1, or a field that ends past a sized integer's width raises an error.
`integer.extract` and `integer.replace` on a dynamically-sized integer treat it as having infinitely many sign bits, like
the other bitwise functions, so any field fits. `integer.bswap` raises on one, since it has no bytes to reverse.

```luwu
const rgba = 0xffa500ff_u32
const green = rgba:extract(16int, 8int) -- 0xa5_u32
const faded = rgba:replace(0x80_u32, 0int, 8int) -- 0xffa50080_u32
const has_alpha = rgba:btest(0xff_u32) -- true
const big_endian = rgba:bswap() -- 0xff00a5ff_u32
```

### Integer metatable/methods

We choose to add a subset of the integer library as methods on integers themselves. This is because many integer
library functions wouldn't be ergonomic to use frequently otherwise, especially for repeated shifts or arithmetic.

Instead of fully eating the runtime cost of this, we reuse the fast method optimization on strings to optimize any call
that looks like a methodcall on an `integer` and fall back to `NAMECALL` if the receiver isn't an integer at runtime.
A user's table or class method with the same name (`replace`, `extract`) barely notices: the miss costs one tag check in
native code and about 1 ns interpreted. *See [Implementation details](#implementation-details).*

Integers feature the following methods:

```luwu
-- conversion, bounds and sign
function integer<T>:cast<U>(to: Mode<U>): integer<U> ! IntegerErrors
function integer<T>:tonumber(): Result<number, number>
function integer<T>:clamp(min: integer<T>, max: integer<T>): integer<T>
function integer<T>:abs(): integer<T> ! Error.Overflow
function integer<T>:signum(): integer<T>

-- saturating and checked arithmetic
function integer<T>:sadd(other: integer<T>): integer<T>
function integer<T>:ssub(other: integer<T>): integer<T>
function integer<T>:smul(other: integer<T>): integer<T>
function integer<T>:cadd(other: integer<T>): integer<T> ! Error.Overflow | Error.Underflow
function integer<T>:csub(other: integer<T>): integer<T> ! Error.Overflow | Error.Underflow
function integer<T>:cmul(other: integer<T>): integer<T> ! Error.Overflow | Error.Underflow
function integer<T>:cdiv(other: integer<T>): integer<T> ! Error.Overflow | Error.DivisionByZero

-- powers, roots and logarithms
function integer<T>:pow(exp: integer): integer<T>
function integer<T>:spow(exp: integer): integer<T>
function integer<T>:cpow(exp: integer): integer<T> ! Error.Overflow | Error.Underflow
function integer<T>:sqrt(): integer<T>
function integer<T>:log2(): integer<T>
function integer<T>:log10(): integer<T>

-- bitwise operations
function integer<T>:band(...: integer<T>): integer<T>
function integer<T>:bor(...: integer<T>): integer<T>
function integer<T>:bxor(...: integer<T>): integer<T>
function integer<T>:bnot(): integer<T>

-- shifts and rotations
function integer<T>:lshift(by: integer): integer<T>
function integer<T>:rshift(by: integer): integer<T>
function integer<T>:lrotate(by: integer): integer<T>
function integer<T>:rrotate(by: integer): integer<T>

-- counting bits
function integer<T>:countlz(): integer<T>
function integer<T>:countrz(): integer<T>
function integer<T>:popcount(): integer<T>

-- bit fields and byte order
function integer<T>:extract(field: integer, width: integer?): integer<T>
function integer<T>:replace(value: integer<T>, field: integer, width: integer?): integer<T>
function integer<T>:btest(...: integer<T>): boolean
function integer<T>:bswap(): integer<T>
```

Each method is the library function of the same name with the integer as its first argument: `flags:bor(0b0100_u8)` is
`integer.bor(flags, 0b0100_u8)`.

`size:cast(integer.u32)` is `integer.cast(size, integer.u32)`, so a method can only cast an integer. Casting a string,
number or `__tointeger` value goes through `integer.cast` or a mode (`integer.u32(text)`). The method's mode is required:
`size:cast(integer.dynamic)` gives an `int`, and `size:cast()` is an error.

`integer.min` and `integer.max` aren't methods. People already write `math.min(a, b)` and `math.max(a, b)`, and
`a:min(b)` reads naturally mostly to people who know Rust.

#### Referenced types

```luwu
type IntegerErrors<E = Magic> = 
    | Error.CannotBeParsed
    | Error.Overflow
    | Error.Underflow
    | Error.UnexpectedFloat
    | Error.InvalidArgument
    | E -- inferred based on error variants of `__tointeger` on custom data structures
    | Error.Runtime -- `__tointeger` hit a regular RuntimeError

declare class Mode(private mode: Repr, min, max)
    public min: integer<Repr>, -- depending on whatever the primitive mode is so u8, i8, etc.
    public max: integer<Repr>,
    public __type = `integermode<{mode}>` -- all lowercase like typeofs for other builtins like VecDeque
    public function __call(from: integer | number): u8 ! IntegerErrors<u8> -- compiler special cases these they don't actually go thru __call
end

type integer = {
    Mode: typeof(Mode),
    dynamic: Mode<"int">,
    u8: Mode<"u8">,
    u16: Mode<"u16">,
    u32: Mode<"u32">,
    u64: Mode<"u64">,
    i8: Mode<"i8">,
    i16: Mode<"i16">,
    i32: Mode<"i32">,
    i64: Mode<"i64">,

    cast: <T, C>(from: C | integer | string | number, to: Mode<T>?) -> integer<T> ! IntegerErrors<ErrorsWhenCasting<C>>,
    tonumber: (i: integer) -> Result<number, number>,

    mode: <T>(i: integer<T>) -> Mode<T>,
    isbig: (i: int) -> boolean,
    suffixes: .("int", "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"),

    min: <T>(first: integer<T>, ...integer<T>) -> integer<T>,
    max: <T>(first: integer<T>, ...integer<T>) -> integer<T>,
    clamp: <T>(i: integer<T>, min: integer<T>, max: integer<T>) -> integer<T>,
    abs: <T>(i: integer<T>) -> integer<T> ! Error.Overflow,
    signum: <T>(i: integer<T>) -> integer<T>,

    sadd: <T>(a: integer<T>, b: integer<T>) -> integer<T>,
    ssub: <T>(a: integer<T>, b: integer<T>) -> integer<T>,
    smul: <T>(a: integer<T>, b: integer<T>) -> integer<T>,
    cadd: <T>(a: integer<T>, b: integer<T>) -> integer<T> ! Error.Overflow | Error.Underflow,
    csub: <T>(a: integer<T>, b: integer<T>) -> integer<T> ! Error.Overflow | Error.Underflow,
    cmul: <T>(a: integer<T>, b: integer<T>) -> integer<T> ! Error.Overflow | Error.Underflow,
    cdiv: <T>(a: integer<T>, b: integer<T>) -> integer<T> ! Error.Overflow | Error.DivisionByZero,

    pow: <T>(i: integer<T>, exp: integer) -> integer<T>,
    spow: <T>(i: integer<T>, exp: integer) -> integer<T>,
    cpow: <T>(i: integer<T>, exp: integer) -> integer<T> ! Error.Overflow | Error.Underflow,
    sqrt: <T>(i: integer<T>) -> integer<T>,
    log2: <T>(i: integer<T>) -> integer<T>,
    log10: <T>(i: integer<T>) -> integer<T>,

    band: <T>(first: integer<T>, ...integer<T>) -> integer<T>,
    bor: <T>(first: integer<T>, ...integer<T>) -> integer<T>,
    bxor: <T>(first: integer<T>, ...integer<T>) -> integer<T>,
    bnot: <T>(i: integer<T>) -> integer<T>,

    lshift: <T>(i: integer<T>, by: integer) -> integer<T>,
    rshift: <T>(i: integer<T>, by: integer) -> integer<T>,
    lrotate: <T>(i: integer<T>, by: integer) -> integer<T>,
    rrotate: <T>(i: integer<T>, by: integer) -> integer<T>,

    countlz: <T>(i: integer<T>) -> integer<T>,
    countrz: <T>(i: integer<T>) -> integer<T>,
    popcount: <T>(i: integer<T>) -> integer<T>,

    extract: <T>(i: integer<T>, field: integer, width: integer?) -> integer<T>,
    replace: <T>(i: integer<T>, value: integer<T>, field: integer, width: integer?) -> integer<T>,
    btest: <T>(first: integer<T>, ...integer<T>) -> boolean,
    bswap: <T>(i: integer<T>) -> integer<T>,
}
```

#### Vectors of small integers

`collections.Vec` and `collections.VecDeque` holding `u8`, `i8`, `u16` or `i16` integers store each element unboxed at
its width: one byte for `u8` and `i8`, two for `u16` and `i16`, instead of a 16-byte value per element in a table. The
first value fixes the mode, like the element type of any `Vec`, and pushing an integer of another mode raises.

```luwu
const .{ Vec } = collections

const bytes = Vec.from("héllo") -- 6 elements, since é takes two bytes
bytes:push(0x21_u8)
print(bytes:at(1)) -- 104
const text = bytes:tostring() -- "héllo!"
```

`Vec.from` and `VecDeque.from` have two overloads:

```luwu
function Vec.from<T>(t: { T }): Vec<T>
function Vec.from(bytes: string | buffer): Vec<integer.u8>
```

A table gives a `Vec` of its array part's values, which all need one type, like the values pushed one by one. A string
or a buffer gives a `Vec<integer.u8>` of its bytes. `VecDeque.from` works the same way. `v:tostring()` gives the bytes
of a `Vec` of `u8` back as a string. Converting either way is a single copy.

`v:tostring()` raises on a `Vec` of anything else, while `tostring(v)` keeps showing the `Vec` itself, as
`buffer.tostring(b)` and `tostring(b)` differ for buffers. `push`, `at` and `get` on these vectors are builtin fast
methods with native code lowering, like the integer methods.

A `buffer` is still the type for data with a layout of mixed widths, like a file header or a network packet. A list of
bytes that grows, shrinks and gets iterated is a `Vec<integer.u8>`.

#### Buffer reads and writes

```luwu
function buffer.readinteger<T>(b: buffer, offset: number, mode: Mode<T>): integer<T>
function buffer.writeinteger(b: buffer, offset: number, value: integer)
```

`buffer.readinteger` reads an integer of `mode` at `offset`, and `buffer.writeinteger` writes `value` in as many bytes
as its mode takes, little-endian like the rest of the `buffer` library. Writing a dynamically-sized integer raises an
error, since it has no fixed width. The existing `buffer.readi8` through `buffer.readu32` keep returning numbers.

```luwu
const header = buffer.readinteger(data, 0, integer.u64)
buffer.writeinteger(data, 8, 1u16)
```

### `__tointeger` metamethod

A table, object or userdata can define `__tointeger(self, mode: integer.Mode)` to convert itself to an integer.
`integer.cast(value, mode)` and calling a mode (`integer.u64(value)`) call it with the mode they convert to. Nothing else
calls it, so arithmetic never converts a value implicitly.

`__tointeger` can return an integer of any mode. `integer.cast` converts the result to `mode` with the same rules it uses
for an integer argument, so a type can return the integer that's natural for it and raise only when it can't produce one.
Use it for values a number can't hold exactly, like Discord snowflakes, tokens, byte counts and money:
`integer.u64(size)` then never goes through a `number` that would lose precision.

```luwu
const function is_same_i(lower: integer, upper: integer, pick: integer)
    const rep1 = typeof(lower)
    const rep2 = typeof(upper)
    return if rep1 == rep2 then pick else error(`integers are different representations ({rep1} vs {rep2})`)
end
const function in_bounds(value, lower, upper): integer
    return match is_same_i(lower, upper, value)
        when value >= lower and value <= upper then value
        else error("default is out of range")
end
class BoundedInteger(inner: integer, lower: integer, upper: integer)
    public lower = is_same_i(lower, upper, lower)
    public upper = is_same_i(lower, upper, upper)
    private inner = in_bounds(inner, lower, upper)

    public function __call(self, value: integer)
        ...
    end
    public function __tointeger(self, mode: integer.Mode): integer
        return self.inner -- integer.cast converts it to `mode`, raising if it doesn't fit
    end
end

const percent = BoundedInteger(50u8, 0u8, 100u8)
const wide = integer.cast(percent, integer.u64) -- 50u64
const same = integer.u8(percent) -- 50u8
const oops = integer.i8(BoundedInteger(200u8, 0u8, 255u8)) -- raises Error.Overflow
```

### Pattern matching

<!-- 
need opinions on this:?
Even though `u8` (the type of primitive `integer`s with mode `u8`) and `integer.u8` will both exist in patterns,
we ensure that `i is integer.u8` works the same as `i is u8` to mitigate confusion between the namespace and the library.
This can be done via simple parser transformation to the RHS of `is` expressions.
-->

To check if a value is an integer, you can use the `integer` primitive or an integer library object as a proxy for integer primitives
of the expected mode:

```luwu
if x is integer then
    if x is integer.u8 then
    elseif x is integer.u16 then
    end
end
```

For now, doing `x is u8` without pulling `u8` into scope with a binding is a runtime and type error.

Note this leads to some edge cases because of the library appearing like a class value but acting as a class object and returning
primitives instead of objects:

```luwu
const isu8 = 10u8 is integer.u8 -- true
const isinstance = class.isinstance(10u8, integer.u8) -- false (integer.u8 returns an integer which is a primitive not an object of integer.u8)
const called = integer.u8(10)
const iscalledu8 = called is integer.u8 -- true
const isinstance2 = class.isinstance(called, integer.u8) -- also false
const ismode = integer.u8 is integer.Mode -- true
```

In fairness to this design, `"foo" is string` also evaluates to `true` but `class.isinstance("foo", string)` does not
(it raises instead of silently succeeding).

Note that `class.isinstance(integer.u8, integer.u8)` returning false is not one of these edge cases because that behavior
holds upon the assumption that `integer.u8` is a class: `class.isinstance` returns `false` when a class value is compared against itself.

### Type system

We introduce one new type `int` into the global type scope.
Note that if we change this to allow referencing `u8`, `i8`, etc. without `type u8 = integer.u8` in analysis, we must also allow `x is u8`
without `local/const u8 = integer.u8`/`const .{u8, i8} = integer` at runtime for parity.

If you want to limit to only dynamic integers, use `int`:

```luwu
class Cat
    name: string
    age: int
end
const taz = Cat {
    name = "Taz",
    age = 12int
}
```

`int` is defined `type int = integer.dynamic` and is provided as an alias because it's what users usually want.

The primitive `integer` type is modified to accept a type parameter and to mean a supertype of all integer modes.
Specifically, it is typed like `type integer<T = integerlib.Mode> = Magic one of <integer.u8, integer.u16, ...>`
so that writing `integer` by itself accepts any integer mode.

This means you can pass `7u8` where any `integer` is accepted.

```luwu
class IntBox(inner: integer)
end

const boxyu8 = IntBox(10u8)
const boxyi64 = IntBox(12_215_312_i64)
```

Sometimes that isn't what you want. This a Snowflake class using Luau 0.730 integers that now accepts
integers much smaller than a Discord snowflake:

```luwu
class Snowflake(id: integer)
end

const snowflake = Snowflake(100000000000000001int) -- still works fine after you change suffix `i` to `int`
const badflake = Snowflake(12i8) -- this is too small, no type error.
```

Fix it by specifying what integer modes are allowed:

```luwu
class Snowflake(id: integer.u64)
end

const snowflake = Snowflake(100000000000000001u64) -- works fine
const badflake = Snowflake(12i8) -- type error
```

Of course you should probably be doing runtime checking:

```luwu
class Snowflake(id: integer.u64)
    id = do
        if id is not integer.u64 then
            error(`expected u64 got {typeof(id)}`)
        end
        if id < MINIMUM_SNOWFLAKE then
            error(`snowflakes aren't this small (got {id})`)
        end
        give id
end
const badflake = Snowflake(12i8) -- raises at runtime
```

Use `integer<T>` to constrain a generic parameter `T` to the same integer mode in all params/returns:
`function foo<T>(a: integer<T>, b: integer<T>): integer<T>` accepts `foo(1u8, 2u8)`, returns a `u8`,
and rejects `foo(1u8, 2i64)` with a type error.

## Implementation details

Two inline integers of the same mode take a fast path in each arithmetic instruction. A dynamically-sized integer adds
with an overflow check and only calls into the bigint code when the result doesn't fit in 64 bits. A sized integer
wraps with two shifts whose widths come from a table indexed by the mode, instead of branching on each of the eight
modes. New opcodes for integer arithmetic are fine wherever they make integers faster, as long as numbers and vectors
keep their speed.

Integer methods compile like string methods (`rfcs/string-extensions.md`): a method call with an integer method's name
tries the builtin first, with the method call as its fallback. On a table or object receiver the builtin misses. Native
code checks the receiver's tag before the builtin, so a class method named `replace` runs within 0.2 to 0.5 ns of one
with any other name, measured against a call of about 10 ns. Interpreted, the miss is a call into the builtin that
returns at once, about 1 ns on a 15 ns call. A name both strings and integers use adds one more tag check to the miss.

## Compatibility

This change breaks compatibility with Luau 0.730's `integer` feature and `integer` library. Even though
integers were always FFlagged, we consider this a compatibility break because the integer RFC was accepted by Luau 0.730,
integers were enabled by default in OSS Luau and mluau, and existing code uses the `integer` feature and library.

We have chosen to break compatibility to provide a better `integer` implementation and to make integers more versatile,
less footgun-prone, and easier to use.

We also believe that separating signed and unsigned integers and supporting integer arithmetic is critical to an integer
implementation and worth a compatibility break in itself.

Luau 0.730's `7i` syntax is no longer accepted. It will be caught in parser recovery, and we will provide an autofix
to change one or all usecases in a module to `i64` or the dynamically-sized `int`. Calls to `integer.create` will fail at
runtime and static analysis will direct users to `integer.cast` with a type error.

## Drawbacks

- **VM complexity:** every arithmetic and comparison instruction checks for integers on its slow path; this means integers
  will not be as fast as numbers outside arithmetic on literals.
- **Breaking runtime backwards compatibility** with Luau 0.730.
- **buffer.readi/u** methods are very confusing now... `buffer.readu8` returns `number` when we have an actual `u8` in the language.
  Breaking unflagged `buffer` library functions to return integers instead is much more breaking of a runtime breaking change than changing
  the `integer` feature and library which are less heavily relied-upon.

## Alternatives

- Two fixed-size types, `i64` and `u64`.
- Keep the existing `integer` library.
- Remove the `integer` library and rely on hosts to provide `i64`, `u64`, `integer`, implementations via native classes
  or userdata. These would all require heap allocations and would not be as first-class or performant as a primitive
  implementation.
