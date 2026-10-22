# String Extensions

Status: Implemented (Flagged)

FFlag: LuwuStringExtensions

## Summary

Add `string.trim`, `string.trimstart`, `string.trimend`, `string.startswith`, `string.endswith`, `string.contains` and
`string.replace` to the string library, and an optional `maxpieces` argument to `string.split`. None of them use patterns.
Nearly every Luau project writes these by hand with patterns that are often subtly wrong.

## Motivation

Luau's string library has no literal (non-pattern) way to do the most common text operations. Each one ends up as a
pattern or a `find`/`sub` pair:

```luau
local trimmed = string.match(s, "^%s*(.-)%s*$")
local starts = string.sub(s, 1, #prefix) == prefix
local has = string.find(s, needle, 1, true) ~= nil
local replaced = string.gsub(s, old, new)
```

Each of these has a trap:

- **`gsub` treats `old` as a pattern and `new` as a replacement template.** `string.gsub("a.b", ".", "")` returns an
  empty string. A `%` in `new` is special. User input has to be escaped by hand before it can be searched for or
  inserted. Because `gsub` also returns the count as a second value, `table.insert(t, s:gsub(a, b))` passes three
  arguments and raises.
- **`find` without its fourth argument is a pattern search.** `string.find(s, ".")` finds the first character, not the
  first dot.
- **`%s` only matches ASCII whitespace.** A no-break space (U+00A0), an ideographic space (U+3000) or a byte order mark
  at the start of a file survives the usual trim pattern. Text pasted from the web or read from a Windows file often
  has all three.
- **Removing a suffix or a repeated token takes a loop** of `sub` calls, with off-by-one errors waiting at each end.

Every mainstream language has these functions, and people coming to Luwu expect them.

## Design

All of these are byte-based and literal: arguments are never patterns. They are also methods, so `s:trim()` and
`s:startswith("x")` work.

### `string.startswith`, `string.endswith`, `string.contains`

```luau
string.startswith(s: string, prefix: string, ...: string): boolean
string.endswith(s: string, suffix: string, ...: string): boolean
string.contains(s: string, needle: string, ...: string): boolean
```

Each returns whether `s` starts with, ends with or contains any of the given strings. The first one is required, and any
more are alternatives:

```luau
if path:endswith(".luau", ".luwu") then
    -- ...
end

if not line:startswith("#") and line:contains("=") then
    -- ...
end
```

An empty string matches anything, so `s:startswith("")` is `true`. This is what Python, JavaScript, Rust and Go do.

Comparing bytes is correct for UTF-8 text: a valid UTF-8 string can't match in the middle of a character.

### `string.trim`, `string.trimstart`, `string.trimend`

```luau
string.trim(s: string, ...: string): string
string.trimstart(s: string, ...: string): string
string.trimend(s: string, ...: string): string
```

`trim` removes from both ends of `s`, `trimstart` from the start only, and `trimend` from the end only.

**With no strings,** they remove whitespace: every character with the Unicode `White_Space` property, and U+FEFF (the
byte order mark). `White_Space` is tab, line feed, vertical tab, form feed, carriage return, space, U+0085, U+00A0,
U+1680, U+2000 to U+200A, U+2028, U+2029, U+202F, U+205F and U+3000. The byte order mark isn't whitespace, but it's
never wanted at either end of text.

```luau
print(("  hello  "):trim())          --> "hello"
print(("\u{FEFF}name,age\r\n"):trim()) --> "name,age"
print(("  hello  "):trimend())       --> "  hello"
```

**With strings,** they remove only those, as whole tokens, until none of them is left at the end being trimmed. At each
step the first given string that matches is removed:

```luau
print(("--title--"):trim("-"))          --> "title"
print(("/usr/local/"):trimend("/"))     --> "/usr/local"
print(("line\r\n\r\n"):trimend("\r\n")) --> "line"
print(("line\n"):trimend("\r\n"))       --> "line\n"
print(("line\r\n\n"):trimend("\r", "\n")) --> "line"
```

Giving strings replaces the default, so `s:trim("-")` doesn't remove whitespace. Empty strings are ignored.

Each string is a whole token, not a set of characters. To remove any of several characters, pass each one:
`trimend(s, "\r", "\n")`.

### `string.replace`

```luau
string.replace(s: string, old: string, new: string, max: number?): string
```

Replaces at most `max` occurrences of `old` in `s` with `new` (every occurrence when `max` is `nil`). It works left
to right without overlapping matches. Both `old` and `new` are literal:

```luau
print(("a.b.c"):replace(".", "/"))          --> "a/b/c"
print(("100%"):replace("%", " percent"))    --> "100 percent"
print(("aaaa"):replace("a", "b", 2))        --> "bbaa"
print(("aaa"):replace("aa", "b"))           --> "ba"
```

Because it returns only the new string, unlike `gsub`, `table.insert(t, s:replace(a, b))` works. An empty `old` is an
error: inserting `new` between every byte would split UTF-8 characters and is never what's meant. A negative `max` is
an error. A `max` of `0` returns `s` unchanged.

### `string.split`'s `maxpieces` argument

```luau
string.split(s: string, sep: string?, maxpieces: number?): { string }
```

With `maxpieces`, `split` returns at most that many pieces, and the last piece holds the rest of the string. Splitting once is
the common case:

```luau
const left, right = table.unpack(vals:split(" = ", 2))
```

It counts pieces, so it matches the number of names they're unpacked into. `maxpieces` must be at least `1`. Without it, `split` behaves as it does today.

### Type system

The functions are typed as shown above. `s:startswith()` is a type error as well as a runtime error, because the first
needle of `startswith`, `endswith` and `contains` is a required parameter. The trims accept no strings, so
`s:trim()` typechecks.

## Compatibility

No new syntax or keywords. Luau code from <= 0.730 runs unchanged unless it passes `split` a third argument that
upstream ignores.

A polyfill that assigns its own `string.trim` (or similar) still replaces the builtin where the string table isn't
read-only. If its signature differs from the builtin's, it gets a type error.

These functions could be proposed upstream as they are.

## Drawbacks

- **Seven new names in the string library.** They appear in autocomplete and can't be used for something else later.
- **Token trimming differs from Python's `strip(chars)`.** `strip("\r\n")` in Python removes any `\r` or `\n`, while
  `trimend("\r\n")` removes only the pair. Someone coming from Python can get this wrong.
- **`split`'s `maxpieces` counts pieces, not splits.** Python's `split(sep, maxsplit)` and seal's `str.splitfront` count
  splits, so someone coming from either would write `split(" = ", 1)` to split once and get the whole string back as
  one piece.
- **Byte-based.** Because `contains` and the others compare bytes, equal-looking text in different Unicode encodings
  doesn't match. That belongs with Luwu's Unicode text handling work, not here.

## Alternatives

- **One `trim(s, mode)` function** with `"start"`, `"end"` and `"both"` instead of three functions. No mainstream
  language does this. It also can't combine with variadic trim strings, because a mode argument can't come after
  `...`.
- **`ltrim`/`rtrim` or `lstrip`/`rstrip`.** Short and Lua-like next to `gsub` and `gmatch`, but a one-letter difference
  is easy to miss when reading nested or tired code. `trimstart`/`trimend` also match `startswith`/`endswith`.
- **`trimfront`/`trimback`** (seal's names). No language uses them for strings.
- **Trim strings as a set of characters** (Python, Go, C#, PHP). A set can't express multi-character tokens like
  `"\r\n"` or `"//"`. Tokens can express a set as one single-character token per character.
- **Trim strings in a table** (`trim(s, { "\r", "\n" })`). A table literal allocates every time it's evaluated unless
  the caller hoists it. Variadic arguments never allocate.
- **A single needle for `startswith` and friends.** Simpler, but checking several suffixes is common (`.luau` or
  `.luwu`). The variadic form costs nothing when a call passes one needle.
- **`split` counting splits** (Python's `maxsplit`, seal's `splitfront`), so `split(sep, 1)` splits once. It reads
  well for the common case, but every other mainstream language counts pieces (see Prior art). A piece count also
  matches the names the result is unpacked into, and counting splits has caused off-by-one bugs with seal's
  `splitfront` and `splitback` that a piece limit would have avoided.
- **Naming it `limit`** (Java, Ruby) or `length`. `limit` doesn't say what's counted, and `length` reads like the length of
  each piece. `maxpieces` says it's a maximum and that it counts pieces, and it's the counterpart of Python's
  `maxsplit`.
- **Not adding them.** Every project keeps writing the patterns in Motivation, with their traps.

## Future work

- **Native lowering of the fastcalls.** Native code calls the same fastcall functions as the interpreter. A call with
  one constant needle could instead compile to a length check and a compare (or a `memchr`) with no call at all.
- **Fastcalls for method calls.** `s:startswith("x")` goes through the string metatable and never reaches the
  fastcall. Proving the receiver is a string would let it.
- **Lints** suggesting these functions over the patterns they replace: `find(s, x, 1, true)` to `contains`,
  `gsub` with a literal pattern to `replace`, `match(s, "^%s*(.-)%s*$")` to `trim`.
- **Removing exactly one prefix or suffix** (Python's `removeprefix`).

## Prior art

| | trim | starts / ends / contains | literal replace |
|---|---|---|---|
| Python | `strip`, `lstrip`, `rstrip` (optional set of characters) | `startswith`, `endswith` (a string or a tuple), `in` | `replace(old, new, count)` |
| JavaScript | `trim`, `trimStart`, `trimEnd` | `startsWith`, `endsWith`, `includes` | `replaceAll` |
| Rust | `trim`, `trim_start`, `trim_end`, `trim_matches` (patterns, including strings) | `starts_with`, `ends_with`, `contains` | `replace`, `replacen` |
| Go | `TrimSpace`, `Trim` (set of characters), `TrimPrefix`, `TrimSuffix` | `HasPrefix`, `HasSuffix`, `Contains` | `Replace(s, old, new, n)` |
| C# | `Trim`, `TrimStart`, `TrimEnd` (optional characters) | `StartsWith`, `EndsWith`, `Contains` | `Replace` |

Rust and JavaScript also use Unicode `White_Space` for their default trim. JavaScript also trims U+FEFF like this proposal.
`split`'s `maxpieces` counts pieces like Rust's `splitn`, Go's `SplitN`, Java's `split(regex, limit)`, C#'s
`Split(separator, count)`, Ruby's `split(pattern, limit)` and PHP's `explode`. Python's `maxsplit` counts splits
instead.

## Implementation details

- **`string.startswith`, `string.endswith` and `string.contains` are builtin fastcalls** (`LBF_STRING_STARTSWITH`,
  `LBF_STRING_ENDSWITH`, `LBF_STRING_CONTAINS`) when called through the `string` library. A needle that isn't a string
  falls back to the library function, which coerces numbers. Older runtimes without these builtins fall back to an
  ordinary call too, so the new builtin ids need no bytecode version.
- Functions that change nothing return `s` itself without allocating.
- Whitespace is matched as exact UTF-8 byte sequences at either end. That needs no decoding and no lookbehind, because
  the lead bytes of the multi-byte whitespace characters (`C2`, `E1`, `E2`, `E3`, `EF`) are never continuation bytes.
  An invalid byte at an end stops trimming.
