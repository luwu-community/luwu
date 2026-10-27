# Batched Buffer Read/Write

Status: Implemented (Flagged)

FFlag: LuwuBufferBatched

## Summary

Extend the `buffer` library with batched byte-based read and write operations: `buffer.unpack*` functions that return
`...number` in a single call, and `buffer.pack*` functions that write `...number` in a single call. This reduces
verbosity, gives performance that scales over conventional `buffer.read*`/`buffer.write*` by minimizing Luwu/C
boundary crossings, reasons well under forward-compatible types that interoperate with `number`, and enables elegant
type conversion patterns while keeping the buffer's raw, unopinionated nature.

## Motivation

Working closely with buffers often requires verbose loops or repeated function calls, particularly when populating
vectors or other constructors that accept `...number`. Each individual call crosses the Luwu/C boundary, which incurs
significant overhead and limits performance gains in interpreted contexts. Batched operations drastically reduce these
crossings, offering low-hanging fruit for performance improvements across the language. Additionally, batched writes
enable concise type truncation and conversion (e.g. f64 to u16) without manual bit manipulation or intermediate
variables.

This builds on prior concepts, including [the Luau team's buffer/vector RFC](https://github.com/luau-lang/rfcs/pull/198),
but focuses strictly on `buffer` and `number` types to minimize the implementation surface and maximize ergonomic
benefits for existing Luwu APIs and user-provided functions that accept variadic numbers.

## Design

### Byte-Based Batched Reads

`buffer.unpack*` functions similarly to `buffer.read*`, gaining a third `count: number` parameter, accepting a minimum
`number` value of 1, guaranteeing an unambiguous return type. The type suffix indicates the byte size per value
(u8/i8=1, u16/i16=2, u32/i32/f32=4, f64=8). The index steps internally after each read, with the number of steps
provided by `count - 1`, and the number of bytes advanced per step inferred from the function's type suffix. Going OOB
errors the same way `buffer.read*` does, and prevents any values from being returned.

- **Current:** `buffer.read*(buffer: buffer, index: number) -> number`
- **Proposed:** `buffer.unpack*(buffer: buffer, index: number, count: number) -> ...number`

Providing a number literal to `count`, with 3 as an example, makes use of a magic function transforming
`-> ...number` to `-> (number, number, number)`.

Batched reads allow direct forwarding of return values into constructors or any Luwu function accepting `...number`.

Example: consolidating the need for type-specific buffer read/writes (the case that motivated
[the Luau team's RFC](https://github.com/luau-lang/rfcs/pull/198)):

*Current Luwu capabilities:*

```luau
local VECTOR_BYTES = 4 -- Default is f32, 4 bytes
local VECTOR_WIDTH = 3 -- Default is 3 components
local VECTOR_SIZEOF = VECTOR_WIDTH * VECTOR_BYTES

local function buffer_readvector(buf: buffer, i: number): (number, number, number)
  return
    buffer.readf32(buf, i),
    buffer.readf32(buf, i + 4),
    buffer.readf32(buf, i + 8)
end

local function doThing(buf: buffer)
  for i = 0, buffer.len(buf) - 1, VECTOR_SIZEOF do
    local v = vector.create(buffer_readvector(buf, i))
    -- ...
  end
end

doThing(buffer.create(128 * VECTOR_SIZEOF))
```

*Proposed batch approach, in `--!strict` mode:*

```luau
--!strict
local VECTOR_BYTES = 4 -- Default is f32, 4 bytes
local VECTOR_WIDTH = 3 -- Default is 3 components
local VECTOR_SIZEOF = VECTOR_WIDTH * VECTOR_BYTES

local function doThing(buf: buffer)
  for i = 0, buffer.len(buf) - 1, VECTOR_SIZEOF do
    local v = vector.create(buffer.unpackf32(buf, i, 3))
    -- ...
  end
end

doThing(buffer.create(128 * VECTOR_SIZEOF))
```

### Byte-Based Batched Writes

`buffer.pack*` functions similarly to `buffer.write*`, where `value: number` instead accepts `...number`, allowing
multiple numbers to be written in one call. The type suffix indicates the byte size per value (u8/i8=1, u16/i16=2,
u32/i32/f32=4, f64=8). The index steps internally after each write, with the number of steps provided by `count - 1`,
and the number of bytes advanced per step inferred from the function's type suffix. OOB is checked before writing to
the buffer and errors at runtime if bounds would be exceeded, preventing accidental partial writes.

- **Current:** `buffer.write*(buffer: buffer, index: number, value: number) -> ()`
- **Proposed:** `buffer.pack*(buffer: buffer, index: number, ...number) -> ()`

Because the batch size is implied by the trailing arguments, passing no trailing values is a valid empty batch that
writes nothing.

**Example: Convert buffer u32 numbers to buffer u8 numbers, with versatility on buffer choice and index**

*Current Luwu capabilities (`buffer.readu32`, `buffer.writeu8`):*

```luau
local function u32_to_u8(from_b: buffer, to_b: buffer, from_i: number, to_i: number, from_n: number)
  local to_cursor = to_i
  local from_cursor = from_i
  for i = 1, math.max(from_n or 1, 1) do
    buffer.writeu8(to_b, to_cursor, buffer.readu32(from_b, from_cursor))
    to_cursor += 1 -- to_buf is u8, need to increment by 1
    from_cursor += 4 -- from_buf is u32, need to increment by 4
  end
end
```

*Proposed batched approach (`buffer.unpacku32`, `buffer.packu8`):*

```luau
local function u32_to_u8(from_b: buffer, to_b: buffer, from_i: number, to_i: number, from_n: number)
  -- buffer.pack* accepts `...number`; inline unpacking is valid in `--!strict`
  buffer.packu8(to_b, to_i, buffer.unpacku32(from_b, from_i, from_n))
end
```

**Example: Write vector to buffer**

*Current Luwu capabilities (`buffer.writef32`):*

```luau
local VECTOR_BYTES = 4 -- Default is f32, 4 bytes
local VECTOR_WIDTH = 3 -- Default is 3 components
local VECTOR_SIZEOF = VECTOR_WIDTH * VECTOR_BYTES

local function buffer_writevector(buf: buffer, i: number, vX: number, vY: number, vZ: number)
  buffer.writef32(buf, i, vX)
  buffer.writef32(buf, i + 4, vY)
  buffer.writef32(buf, i + 8, vZ)
end

local b = buffer.create(VECTOR_SIZEOF)
local v = vector.one
buffer_writevector(b, 0, v.x, v.y, v.z)
```

*Proposed batch approach (`buffer.packf32`):*

```luau
local VECTOR_BYTES = 4 -- Default is f32, 4 bytes
local VECTOR_WIDTH = 3 -- Default is 3 components
local VECTOR_SIZEOF = VECTOR_WIDTH * VECTOR_BYTES

-- No 'buffer_writevector' helper function needed

local b = buffer.create(VECTOR_SIZEOF)
local v = vector.one
buffer.packf32(b, 0, v.x, v.y, v.z)
```

### Emergent Patterns

Combining extended read/write behaviors enables seamless pipelines, such as reading `...number` inputs as f64 and
immediately translating them to the valid type suffix before writing to the buffer at its given size. Small working
buffers may end up being created just to take advantage of this behavior without requiring more-verbose intermediate
variables or loops.

### Errors

Batched operations report the same errors as the equivalent single-value operations, so existing error handling keeps
working; the only new message is the batch size argument check:

| Situation                                                    | Error                                                       |
| ------------------------------------------------------------ | ----------------------------------------------------------- |
| `count` is missing or not a number                           | `missing argument #3 to 'unpacku8' (number expected)`       |
| `count` is less than 1 (zero or negative)                     | `invalid argument #3 to 'unpacku8' (count)`                 |
| the batch would read or write past the end of the buffer      | `buffer access out of bounds`                               |
| a `pack*` trailing value is not a number                      | `invalid argument #4 to 'packu8' (number expected, got string)` |
| a `pack*` target is a frozen buffer                           | `buffer is immutable`                                       |
| a `count` or a packed value is an `integer`, not a `number`    | `invalid argument #3 to 'unpacku8' (number expected, got integer)` |
| the batch needs more stack than the VM can give               | `too many values to unpack`                                 |

A batch is atomic with respect to all of the above: an error anywhere in a batch means the batch neither returns
partial values nor leaves a partially written buffer behind. In particular, `pack*` type checks every trailing
argument before performing its first write.

The `integer` row is worth calling out because Luwu ships a 64-bit integer type: batched operations are specified in
`number`s in both directions, so `buffer.unpackf32(b, 0, 3i)` is an argument error and `buffer.packu8(b, 0, 3i)` is
one too, even though `buffer.writeinteger` takes an `integer`. A batched `integer` variant is [future work](#future-work)
rather than a widening of these functions, because returning `...integer` is a type-system question, not a buffer
question.

### Type system

`unpack*` is declared as returning `...number` and `pack*` as taking `...number`, which is the honest declaration:
the batch size is a runtime value. An open type pack cannot, however, be handed to a builtin signature - with only
the declaration, `vector.create(buffer.unpackf32(b, i, 3))` is rejected as
`Expected this to be 'number, number, number?', but got '...number'`, which is exactly the case the feature exists to
serve. The declaration alone therefore cannot make use of a count that is written as a literal at the call site.

To close that gap, `unpack*` carries a magic function that reads the batch size off the call site. When the third
argument is a whole `number` literal in the range `1..4096`, the call's return pack is bound to exactly that many
`number`s, so it typechecks anywhere a fixed number of numbers is expected. Every other count - an expression or a
variable, zero, a negative or fractional value, an `integer` literal such as `3i` (which the runtime rejects for
`unpack*` because the values come back as `number`), or a count above 4096 - keeps the declared open pack, which
still flows into anything that accepts `...number`.

An exact pack is also better to report against: a batch passed where strings are expected is reported per value
(`Expected this to be 'string', but got 'number'`) instead of as one opaque pack mismatch, and exact packs satisfy
signatures with optional trailing parameters such as `vector.create`'s. Batched reads are usable as multiple
assignment sources (`local x, y, z = buffer.unpackf32(b, 0, 3)`), and the values they produce are ordinary `number`s
for every downstream rule - narrowing, generics, and type functions all see `number`, not a buffer-specific type.

Nothing in the type system special-cases `pack*`: it takes `...number`, so any mix of numbers that the checker can
see is accepted, and the values are truncated to the width named in the suffix at runtime exactly as `buffer.write*`
truncates a single value. The declarations are added to the analyzer's `buffer` table under the same flag as the VM
registration, so the analyzer never advertises a function the VM does not have, in either flag configuration.

No editor support is required beyond what the analyzer already provides; `unpack*`/`pack*` appear as ordinary members
of the `buffer` table for autocomplete and hover, and the magic only affects the resolved return pack of a call.

## Compatibility

This is a pure library addition: no new syntax, no new keywords, no new bytecode instructions or builtins, and no
change to the behavior of any existing function, so Luau 0.730 code keeps working unchanged and valid Luwu code stays
valid Luau code (the new functions simply do not exist upstream yet). The `buffer` table gains 16 fields when
`LuwuBufferBatched` is on, which is the only observable difference for an embedder.

The feature interacts with two other Luwu features and deliberately follows their rules rather than defining its own:

- **External Buffers** (`LuwuExternallyManagedBuffers`, `LuwuBufferIsFrozen`): `pack*` checks mutability the same way
  `write*` does, so packing into a frozen (`LUA_BHOST_IMMUTABLE`) buffer raises `buffer is immutable` and writes
  nothing, and `unpack*` reads host memory directly like `read*` does. The mutability check happens before the batch
  is sized, so even an empty batch on a frozen buffer is an error.
- **64-bit Integer Type** (`LuauIntegerType2`, `LuauIntegerLibrary`): `buffer.readinteger`/`buffer.writeinteger` have
  no batched counterpart in this RFC, because their values are `integer`s and `unpack*`/`pack*` are specified in terms
  of `number`. See [Future work](#future-work).

`LuwuBufferBatched` gates the whole feature - the VM registration and the type declarations together - so turning it
off restores upstream `buffer` exactly. Since the functions are additive and low risk, the flag is intended to be on
by default in standard builds of `luwu` and `luwu-lsp`.

This is a good candidate to propose upstream to `luau-lang/rfcs` once it settles in Luwu; it is not an inherited
upstream RFC and so is not listed in [UPSTREAM.md](./UPSTREAM.md).

## Drawbacks

- **Out-of-Bounds Risks:** incrementing the index per stepped read/write in a single call allows OOB runtime errors
  in cases where the user provides more data than the buffer is appropriately sized for. Under the initial type
  implementation and the later [stagnation of a prior RFC](https://github.com/luau-lang/rfcs/pull/155), the `buffer`
  is wholly seen as raw data without opinion. The drawback of this drawback is that it sounds like a motive.
- **Initial Fastcall Cost:** adding dedicated `pack*`/`unpack*` buffer library functions costs fastcall allocations
  (16 more entries on the `buffer` table, and 16 more C closures per VM state). Forward-compatibility based on future
  type APIs being interoperable with `number` needs to be considered to warrant this. More-immediate mitigations are
  explored in the alternatives section.
- **Sixteen More Library Functions:** this is the largest single addition to a standard library that Luwu has taken
  so far, and each width is a function that has to exist for the API to feel complete. A narrower suffix set would be
  easier to document and remember.
- **Two Kinds of "Batch Size":** `unpack*` takes its batch size from an explicit `count` argument, but `pack*` takes
  its size from the number of trailing values it was handed, which is an asymmetry users have to remember. It is the
  only shape that keeps both functions total (an `unpack*` that took a value list would have no way to ask for fewer
  values than it was handed, and a `pack*` that took a count would need a way to say "and here are `count` values"
  that the type system cannot express).

## Alternatives

- **Reduced number type suffix support:** the argument for this RFC is strongest regarding types f32 (`vector`
  components), u32/i32 (resultant `bit32` usage on `number`), and f64 (`number`). Implementing u8/i8/u16/i16 in
  scenarios where bitpacking multiple u8/i8/u16/i16s into a u32/i32 may be redundant. u32/i32/f32/f64 implementation
  would be an acceptable MVP.
- **Status Quo:** developers continue writing verbose loops or chaining single-value calls, accepting higher fastcall
  overhead, reduced ergonomics, and slower interpreted execution.
- **Incremental Type-Specific RFCs:** separate RFCs for improved type-to-buffer will continue to be created, some
  having a stronger argument for use than others, with each implementation accumulating towards exceeding what the
  fastcall allocation would be from implementing this RFC. Maintenance burden would be higher, allowing for more
  foot-in-the-door scenarios for discourse on RFCs based on type-to-buffer interoperability.
- **Manual Bit Manipulation:** continue relying on `bit32` and manual loops for type conversion and bitpacking, which
  is more verbose, error-prone, and less performant than native batched buffer operations.
- **A `count` argument on `pack*` too:** rejected because the argument count already says how many values there are,
  and a declared count would let a call lie about its own arity in a way `...number` cannot express.
- **Returning a table or a string from `unpack*`:** rejected because both allocate, both lose the ability to forward
  straight into `...number` parameters, and both are a worse fit for a library whose values are numbers.

## Future work

- **`buffer.unpackinteger` / `buffer.packinteger`:** the natural extension once Luwu's integer story settles, for
  batching `buffer.readinteger`/`buffer.writeinteger`. It needs `...integer` in the type system (or an exact-pack magic
  that produces `integer`s) rather than `...number`, and it needs a decision on whether a lossy `integer` to `number`
  conversion belongs in the buffer library at all, so it is intentionally out of scope here.
- **Signed/unsigned widths beyond 32 bits:** `unpack*`/`pack*` currently round-trip through `double`, like
  `buffer.read*`/`buffer.write*` do; a wider integer type would want exact-width returns.
- **Batched fastcall builtins:** `pack*`/`unpack*` are dispatched as ordinary by-name fastcalls. Adding `LBF_*`
  entries would let the compiler and native code generation recognize them, and a literal batch size would let the
  compiler emit a fixed return count for `unpack*`. This is deferred until profiling shows the generic path costing
  something; adding the entries later is not a breaking change.
- **Other libraries:** the same "minimize boundary crossings" argument applies to variadic table and string
  operations, but those have their own type-level complications and are not implied by this RFC.

## Prior art

Lua 5.4's `string.pack`/`string.unpack` batch several values per call, but they do it through a format string, which
is a runtime string that the type system cannot inspect and that returns byte offsets alongside values; `string.pack`
also allocates the string it builds. Python's `struct.pack`/`struct.unpack` has the same shape, and returns tuples,
which is one more allocation and one more indirection. Luwu already has `string.pack`/`string.unpack`/`string.packsize`
from Lua 5.3, and this RFC is deliberately *not* a re-take of them: it keeps one typed function per width so the batch
size is an argument the analyzer can read, and it hands back plain `number`s that forward into any `...number`
signature without an intermediate container. `buffer.create`/`buffer.write*`/`buffer.read*` from the buffer type RFC
are the direct precedent, and this RFC only extends their suffix family.

## Implementation details

The whole feature lives in three places, all tagged `// Luwu Batched Buffer Read/Write (rfcs/buffer-batched.md):`.

`VM/src/lbuflib.cpp` adds four templates - `buffer_unpackinteger`, `buffer_unpackfp`, `buffer_packinteger`,
`buffer_packfp` - instantiated for the eight widths, and a `bufferlib_batched` registration table that is applied with
`luaL_register(L, NULL, ...)` only when `LuwuBufferBatched` is on, so the functions do not exist at all when the flag
is off. The batch is bounds checked up front with an `isoutofboundsbatch(offset, len, count, accessize)` macro that
extends the existing `isoutofbounds` rule to `count` values; because offset and count are limited to 31 bits the
comparison is a single 64-bit comparison and cannot overflow. `unpack*` reserves room for all of its return values
with `lua_checkstack` before pushing any of them, which both avoids growing the stack per value and lets an
impossibly large batch fail before a partial batch is returned. `lua_checkstack` refuses any request above
`LUAI_MAXCSTACK` slots (8000 in the default build), so the ceiling on a batch is the stack limit rather than the
buffer size, and the refusal is the distinct `too many values to unpack` error rather than a silently shortened
batch; `tests/conformance/buffer_batched.luwu` pins that. `pack*` derives the batch size from `lua_gettop(L) - 2` and
type checks every trailing argument before the first `memcpy`, so a bad argument cannot leave a partially packed
buffer behind; like `write*` it obtains the buffer with `luaL_checkbuffermutable`, which is what makes frozen external
buffers refuse a batch. Byte order handling (including `LUAU_BIG_ENDIAN` reordering via a storage type for the
floating point variants) is identical to the single-value functions.

`Analysis/src/EmbeddedBuiltinDefinitions.cpp` appends the 16 declarations from `kBuiltinDefinitionBufferSrcBatched` to
the `buffer` table only when the flag is on, mirroring how `isfrozen` and `readinteger`/`writeinteger` are added, so
the analyzer's view of `buffer` matches the VM's in every flag configuration.

`Analysis/src/BuiltinDefinitions.cpp` adds `MagicUnpack`, one shared magic attached to all eight `unpack*` functions
(the batch size comes from the call site, not from the width being read). Its `preciseCount` helper only accepts a
literal count when the call has at least three arguments, the third one is an `AstExprConstantNumber`, the value is at
least 1 and at most `kMaxLiteralCount` (4096), and the value is integral; the range test is written so that the value
is truncated to an `int` only once it is known to be in range, and so that a value that is not a plain `number` at all
(`NaN`, an `integer` literal) cannot get through. `infer` then binds the call's return pack to that many
`builtinTypes->numberType`s. 4096 is a work limit, not a semantic one: the analyzer builds one type per value and then
re-walks that pack while resolving the enclosing call, and measured cost per call grows roughly linearly to about 16k
values (13ms) before turning superlinear and tripping the checker's own complexity limit at 32k; 4096 stays well under
that knee and is orders of magnitude past the widest all-number signature the standard library declares. There is no
equivalent limit at runtime - any batch that fits the buffer and the stack works.

The magic is attached in `registerBuiltinGlobals` behind the flag and only for properties that exist, so the same code
is correct with the flag on or off. `handleOldSolver` returns `std::nullopt`: the old solver is not a supported path
for this feature, and it leaves the declared `...number` in place.

No compiler, bytecode or native code generation changes are needed. `pack*`/`unpack*` are not `LBF_*` builtins, so the
compiler emits an ordinary by-name fastcall with a multiple-return call for `unpack*`, which is correct for every
batch size; `tests/conformance/buffer_batched.luwu` runs under native code generation as part of the normal
conformance suite to keep that path honest.
