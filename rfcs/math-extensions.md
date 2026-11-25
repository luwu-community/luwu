# Math Extensions

Status: Proposed

FFlag: LuwuMathExtensions (not implemented yet)

## Summary

Add `math.inf`, `math.epsilon` and `math.exactlimit` to the math library, `math.isclose` to compare numbers that went
through rounding, and `math.ulp` and `vector.ulp` to find out how precise a value is at its size.

```luwu
const total = 0.1 + 0.2
print(total == 0.3) -- false
print(math.isclose(total, 0.3)) -- true
```

## Motivation

Luau's math library has `math.huge` for infinity, `math.isinf` and `math.isfinite` to test for it, and nothing that
describes the precision of a number.

- **Infinity has two names.** The constant is `math.huge`, from Lua 5.1, which took it from C's `HUGE_VAL`. Luau's
  newer test functions took C's `isinf` and `isfinite` instead, so code that tests `math.isinf(x)` compares against
  `math.huge`. People reach for `math.inf` and get `nil`.
- **Comparing results of arithmetic is a trap.** `0.1 + 0.2 == 0.3` is false, and every project ends up with a hand-written
  `abs(a - b) < 1e-9`. A fixed tolerance like that is wrong for large values: neighbouring numbers near `1e10` are
  about `2e-6` apart, so two results that differ only by rounding fail the test.
- **There's no name for the largest exact whole number.** Every whole number from `-(2^53 - 1)` to `2^53 - 1` is a
  `number` exactly. Past that, numbers skip whole values: `2^53 + 1` rounds to `2^53`. Code that stores IDs, counters
  or byte offsets in numbers needs this limit, and writes `9007199254740991` or `2^53` by hand.
- **Integers make the limit part of every conversion.** Luwu's integers (`rfcs/integers.md`) hold values a number
  can't, like Discord snowflakes and `u64` file sizes. Code moving an integer into a number, for `math` functions or for
  a JSON encoder that only writes numbers, needs to know which values survive the trip. `integer.tonumber(i)` reports
  it per value, but code that checks a range up front, or documents one, needs the limit as a constant. The same goes the
  other way: a number past the limit that came from text, like a snowflake decoded from JSON, may already have been
  rounded before `integer.cast` sees it.
- **There's no way to ask how precise a value is.** The gap between neighbouring numbers grows with their size, and a
  `vector`'s components are 32-bit, so its gaps are about 500 million times wider than a number's. A game finds out
  when its characters start to jitter. In a space game with positions in metres, the gaps are:

  | Position | Distance | `vector` gap | `number` gap |
  | --- | --- | --- | --- |
  | ISS orbit (from Earth's centre) | 6,771 km | 0.5 m | 0.9 nm |
  | Geostationary orbit | 42,164 km | 4 m | 7.5 nm |
  | The Moon (from Earth's centre) | 384,400 km | 32 m | 60 nm |
  | Earth (from the Sun) | 1 AU | 16 km | 31 µm |
  | Mars | 1.5 AU | 16 km | 31 µm |
  | Jupiter | 5.2 AU | 66 km | 0.12 mm |
  | Neptune | 30 AU | 524 km | 1 mm |
  | Voyager 2 | ~140 AU | 2,100 km | 3.9 mm |
  | Oort cloud, inner edge | ~2,000 AU | 34,000 km | 6 cm |
  | 1 light year | | 1.07 million km | 2 m |
  | Oort cloud, outer edge | ~100,000 AU | 1.07 million km | 2 m |
  | Proxima Centauri | 4.24 ly | 4.3 million km | 8 m |

  An astronaut stored in a `vector` on the ISS can only stand at positions half a metre apart. Measured from the Sun,
  a whole city at Earth's distance is one position. A `number` keeps millimetre precision out to Neptune. Real space
  games (Kerbal Space Program, Outer Wilds, Elite) store positions in 64-bit coordinates, or move the origin as the
  player travels, and they decide when to do it from these gaps. Luau gives no way to compute them.

## Design

### `math.inf`

`math.inf` is the same value as `math.huge`. Both stay, and neither is deprecated.

### `math.epsilon`

`math.epsilon` is `2^-52` (about `2.22e-16`), the gap between `1` and the next number above it. Any arithmetic result
can be off from the exact answer by about `math.epsilon * math.abs(x)`. Use it to build a tolerance relative to the
values being compared. On its own it's far too small as a tolerance for anything but values close to 1.

### `math.exactlimit`

`math.exactlimit` is `2^53 - 1` (`9007199254740991`), the largest whole number `n` where `n` and `n + 1` are both exact.
`-math.exactlimit` is the smallest. JavaScript calls the same value `Number.MAX_SAFE_INTEGER`.

A number holds 53 significant bits, and that decides what's exact on each side of the limit:

- **Whole numbers up to `math.exactlimit`** are all exact. Adding, subtracting and multiplying them stays exact while
  every result stays in range. `integer.tonumber(i)` gives `Ok` for every integer from `-math.exactlimit` to
  `math.exactlimit`.
- **Past `math.exactlimit`,** numbers are spaced 2 apart, then 4, 8 and so on, doubling at each power of two. Only
  those multiples exist: `2^53 + 1` rounds to `2^53`. An integer there converts exactly only when it's one of them
  (`2^60` does, `2^60 + 1` doesn't), so `integer.tonumber` gives `Err` with the nearest number for most of them.
- **Fractions** are exact only when they're a sum of powers of two that fits in the 53 bits: `0.5`, `0.375` and
  `3.25` are exact, while `0.1` and `1 / 3` are the nearest number to the value written. The bits a number spends on
  its whole part are gone from its fraction, so fractions get coarser as the number grows. From `2^52` on there's no
  room for a fraction at all, and every number that large is a whole number.

```luwu
const function checked_id(id: number): number
    const exact = math.abs(id) <= math.exactlimit and math.floor(id) == id
    return if exact then id else error(`{id} can't be stored exactly as a number`)
end
```

### `math.ulp` and `vector.ulp`

```luwu
function math.ulp(x: number): number
function vector.ulp(v: vector): vector
```

`math.ulp(x)` is the gap between `x` and the next number after it, away from zero: the unit in the last place, which
Python and Java call `ulp` too. One rounded operation is off by at most half of it. `math.ulp(1)` is `math.epsilon`,
`math.ulp(-x)` is `math.ulp(x)`, and `math.ulp(0)` is the smallest positive number (`2^-1074`). `math.ulp(math.inf)` is
`math.inf`, and a NaN gives NaN.

`vector.ulp(v)` gives the gap of each component, at the width the build stores components in. That's 32 bits by
default. A build with `LUA_VECTOR_DOUBLE` stores 64-bit components, and `vector.ulp` then gives the same values as
`math.ulp`, so a check against `vector.ulp` can behave differently between embedders.

You can use `math.ulp` for a test tolerance that scales with the value, counted in rounding steps:

```luwu
const function assert_near(got: number, expected: number, steps: number)
    const within = math.abs(got - expected) <= steps * math.ulp(expected)
    assert(within, `{got} is more than {steps} rounding steps from {expected}`)
end

assert_near(0.1 + 0.2, 0.3, 1) -- 0.1 + 0.2 is one gap above 0.3
```

A step smaller than half the gap is lost, so `math.ulp` also tells you whether a change to a value still registers:

```luwu
const function advance(position: number, step: number): number
    const step_registers = step >= math.ulp(position) / 2
    return error(`a step of {step} is lost at {position}; rebase the position first`) if not step_registers

    return position + step
end

advance(1e9, 0.0000001) -- the gap at 1e9 is 1.2e-7
advance(1e17, 1) -- raises: the gap at 1e17 is 16, so 1e17 + 1 == 1e17
```

Put `vector.ulp` in front of the movement code to move the world's origin before positions get too coarse:

```luwu
const SMOOTH_MOTION = 0.001

const function needs_rebase(position: vector): boolean
    const gaps = vector.ulp(position)
    return math.max(gaps.x, gaps.y, gaps.z) > SMOOTH_MOTION
end

if needs_rebase(player.position) then
    world:shift_origin(-player.position)
end
```

### `math.isclose`

```luwu
function math.isclose(a: number, b: number, rel: number?, abs: number?): boolean
```

`math.isclose(a, b)` returns whether `a` and `b` are equal apart from rounding. It's true when
`math.abs(a - b) <= math.max(rel * math.max(math.abs(a), math.abs(b)), abs)`. This is Python's `math.isclose` from
PEP 485.

- `rel` is the tolerance relative to the larger of the two values. It defaults to `1e-9`, which asks for about 9
  matching significant digits out of the 15 to 17 a number holds.
- `abs` is a fixed tolerance that defaults to `0`. A relative tolerance shrinks to nothing near zero, so comparing a
  result against `0` needs an `abs`: `math.isclose(x, 0, nil, 1e-12)`.
- Equal values are always close, so `math.isclose(math.inf, math.inf)` is true. An infinity is close to nothing else,
  and a NaN is close to nothing, itself included.
- A negative `rel` or `abs` raises an error.

## Drawbacks

- Five more names in `math` and one in `vector`. Luau and Luwu code normally runs sandboxed, where the standard libraries are frozen, so
  scripts can't have added these names themselves. Embedders rarely add to `math` outside Roblox. Lua code running
  without a sandbox often assigns `math.inf = math.huge` itself, which keeps working since it's the same value.
- These are Luwu-only. The `LuwuOnlyApi` lint warns when a Luau or Lua file uses them.

## Alternatives

- **`math.isclose` with a single tolerance** (Roblox's `Vector3:FuzzyEq(other, epsilon)`). One absolute tolerance has
  the same problem as `abs(a - b) < 1e-9` for large values.
- **A default `rel` of `math.epsilon` or a small multiple of it.** Results that went through more than a few operations
  have more rounding error than that, so the default would reject values most people consider equal.
- **`math.maxsafeinteger`**, JavaScript's name. The value is a number and not an integer, "safe" doesn't say what
  the limit protects against, and the name is long.
- **`math.nextafter(x, toward)`** instead of `math.ulp`, as in C and Python. It's the more general primitive,
  but the gap is what code asks about, and `math.nextafter(x, math.inf) - x` is a roundabout way to write it.
- **A name starting with `max`** (`math.maxexact`). It sits next to `math.max` in autocomplete and reads like a function
  of the same family.
