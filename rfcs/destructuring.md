# Destructuring

Status: "Proposed"

FFlag: LuwuDestructuring

## Summary

Adds table, userdata, object, vector etc. destructuring as syntactical sugar.

The chosen syntax is:

```luwu
const .{x, y, z} = v
const .{zip, tar as .{gz as targz}} = require("@std/archive")
const fs.{readfile as rf, writefile as wf, path} = require("@std/fs")
```

## Motivation

We've wanted table destructuring for years. You currently have to write out every local binding multiple times,
and doing so is not fun. It also crowds up a lot of lines in file headers. Most languages have destructuring.

## Design

You can now take things out of bindings when you declare bindings. If there's an identifier to the left of the
`.` glyph then that becomes the variable name. This allows you to import smth, and then later on add things you
want to destructure just by using familar dot syntax but on the LHS of the binding instead of the RHS of the binding.

```luwu
const items = getitems() -- { Item }
-- 2 days later this got refactored into { store: string, items: { Item }, manager: Manager, sales: number.... }
const store_info.{store as store_name, items} = getitems()
```

We support destructuring in `local`, `const`, and `type` bindings, as well as `export type`, `export local` and
`export const`, as well as in loop variables of `for ... in` loops. `export .{x, y} = v` means `export const .{x, y} = v`,
and exports every name it binds. Note that `type` bindings *should* destructure a runtime binding from a `require`
call that magically contains types visible to the type system. We report a TypeError if a user incorrectly destructures
a runtime binding that does not contain types. Supplying a top level variable in the destructure
(`type Smth.{Ta, Tb} = smth`) is a syntax error, since such a type cannot exist.

A `local` or `const` bound to a class a required module exports also brings in the class's type under the same name,
so `const geometry.{Point} = require("./geometry")`, `local Point = geometry.Point` and
`local Point = require("./geometry").Point` all make `Point` usable as a type without a separate
`type .{Point} = geometry`. A type already declared under that name in the same scope wins. An exported binding
(`export const .{Point} = require("./geometry")`) exports the type too.

A module exports a class's type whether it writes `export class Point`, `return { Point = Point }` or
`return .{Point}`: a class in the table a module returns is exported under the table's key.

A required module a module exports as a value (`export t = require("@self/t")`, or in the table it returns) still has its
types: `const .{Cat} = hub.t`, `hub.t.Cat(4)`, `const m = hub.t` then `m.Age`, and `type .{Age} = hub.t` all see
`t`'s types.

<!-- WIP: agent-written draft of `.{...}` construction. Audit the wording before it leaves WIP. -->

### Building tables with `.{...}`

`.{...}` also works on the right-hand side, where it builds a table instead of taking one apart. A bare name becomes a
field of the same name holding that name's value, and `key = value` works like in any table:

```luwu
local name, email = "ann", "ann@example.com"
local user = .{name, email, created = os.time()}
-- local user = { name = name, email = email, created = os.time() }

return .{new, parse, from_string} -- module exports without writing every name twice
```

There are no list items in `.{...}`: a bare name is always a field, so `.{x}` can never quietly become an array the way
`{x}` would. Use `{...}` for list items and `[key]` fields. `as` only renames when destructuring; to build a field
under another name, write `key = value`.

Like `{...}` and strings, `.{...}` can be a function's only argument without parentheses, so a POD class constructs
straight from locals named after its fields:

```luwu
class Color
    r: number
    g: number
    b: number
end

local r, g, b = 255, 128, 0
local orange = Color.{r, g, b} -- Color { r = r, g = g, b = b }
```

This compiles exactly like `Color { r = r, g = g, b = b }`. It's for classes without a primary constructor, which take
their fields as a table; a primary constructor takes positional arguments.

The same `.{x, y}` means the same fields on both sides: `local .{x, y} = p` takes them out of `p`, and `Point.{x, y}`
puts them into a new `Point`. Since `name.{...}` on the right-hand side is a call, `t.{x, y}` can't also mean "a new
table with `t`'s `x` and `y`".

<!-- /WIP -->

<!-- WIP: agent-written draft of positional patterns. Audit the wording before it leaves WIP. -->

### Positional patterns with `.(...)`

`.(a, b)` destructures by position instead of by name. An object of a class that can be made as `Cat(name, age)` can be
destructured in the same shape:

```luwu
class Cat(name: string, age: number) end

const .(taz_name, taz_age) = Cat("Taz", 12)
```

A positional pattern on an object reads the fields its class's primary constructor takes, in the constructor's order.
That holds even when the class body restates the parameters in another order. Fewer names than parameters is fine. A
class without a primary constructor (a POD class, or one with `__init`) can't be destructured positionally, and neither
can a position past the constructor's last parameter: both raise at runtime, the way reading a field an object doesn't
have does, and both are type errors. A private field is read with the same rules as reading it by name.

Anything else is destructured like unpacking an array: element `i` is `value[i]`, so a missing one is `nil`.

```luwu
const .(a, b, c) = { "Hi", "there" } -- a = "Hi", b = "there", c = nil
```

Positional patterns go anywhere `.{...}` patterns do, nest in each other, and take a name and annotations the same way:
`local pair.(first: number, .(x, y), entry.{kind}) = t`, `const .{pos as .(x, y)} = u`,
`for _, .(key, value) in entries do`. An element is a binding already, so there's no `as`.

In a type test, a positional pattern binds the values for the branch the test guards, like `.{...}` does:

```luwu
if fs.read(p) is Ok.(content) then
    print(content)
end

const data = match fs.read(p)
    case is Ok.(content) then do
        print(content)
        give content
    case is Err.(err) then error(err)
    else error("unreachable")
```

The `.` isn't needed to tell a pattern apart in this position, but we require it for consistency with every other
pattern, and so `Ok.(content)` can't be read as calling `Ok`. A type test written as a call whose arguments are all
names nothing in scope has (`is Ok(content)`) is a syntax error that suggests the `.`.

#### Building bundles with `.(...)`

Where a value goes, `.(a, b)` builds a `bundle.Pair`, the way `.{x, y}` builds a table. Three values make a
`bundle.Triple`, four a `bundle.Quad`, and five to twelve the bundle of that size (`bundle.pack`). More than twelve is a
syntax error that suggests a table or a function returning multiple values instead.

```luwu
const .(name, age) = do
    -- ... a bunch of logic
    give .(got_name, got_age) -- a bundle.Pair
```

A pair that's destructured right where it's built, like this one, is never created. The compiler keeps each value in a
register of its own, and it costs nothing at runtime.

<!-- /WIP -->

## Compatibility

This is not compatible with Luau 0.730, but may be partially compatible with upstream Luau because it's based on VIG's RFC.

One thing we've added that they haven't added yet: you can qualify the top value as the identifier to the left of the `.`

## Drawbacks

- It's symbol soup
- May be confusing to read left to right, especially `as` with their own patterns.

## Alternatives

Use any of the previous syntaxes proposed in Luwu and Luau: `const x, y, z from t` (which is really clean),
`local { .x, .y } = t` which was accepted and then later reverted by Luau, etc.

## Future work (optional)

Probably don't want to support in parameters because default values will be very wonky.
Also it would be very confusing.

## Prior art (required only for syntax and semantics changing RFCs)

JS, Zig, Rust, everyone except Lua.

## Implementation details (optional)

Parser only probably.
