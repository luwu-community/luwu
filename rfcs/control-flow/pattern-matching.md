# Pattern Matching

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans before it's accepted.

Status: Implemented (Flagged)

FFlag: DebugLuwuMatchExpressions (destructuring patterns also need LuwuDestructuring)

## Summary

The `is`, `is not` and `implements` operators test a value's type, class or trait, and can destructure it in the same test. A `match` expression picks
a value by testing one subject against a list of patterns, and a `match` statement runs the block of the first pattern that matches.

```luwu
if event is Click {x, y} then
    press(x, y)
elseif event is Key {code} then
    type_key(code)
end

const area = match const s = shape
    case is Point then 0
    case is Circle {radius} when radius > 0 then math.pi * radius ^ 2
    case is Rect {w, h as height} then w * height
    else 0
```

## Motivation

Dispatching on a value's type in Luau means `typeof(x) == "string"` or `class.isinstance(x, Point)` calls, and then reading the fields the branch
needs on separate lines. Choosing a value from several cases means a local assigned in every branch of an `if` chain, or a lookup table that can't
hold conditions.

Patterns written as operators read like what they check, and a pattern that binds the fields it needs keeps the binding next to the check that makes
it safe.

## Design

### `is`, `is not` and `implements`

These operators work in any expression and sit at the precedence of `==`:

```luwu
if x is string then ...            -- typeof(x) == "string"
if p is Point then ...             -- class.isinstance(p, Point)
if p is not Point then ...         -- not class.isinstance(p, Point)
if o implements Named then ...     -- class.implements(o, Named)
```

The right side of `is` is always a pattern, never an expression: `x is (Cat(name, age))` is an error, not a test against a `Cat` made on the
spot. A pattern is made of alternatives joined by `|`, and each alternative is one of:

- a primitive tag: `string`, `number`, `integer`, `boolean`, `buffer`, `vector`, `table`, `function`, `thread`, `userdata`, and Luwu's `object`
  and `class`. A tag is never looked up as a name: a local named `string` doesn't change what `is string` means;
- a builtin class's tag, which `typeof` gives its objects: `bundle`, `vec`, `vecdeque` and `orderedmap`, while their libraries are on;
- `nil` or `none`, compared with `==`;
- a literal string, number or boolean, compared with `==`;
- a class, given as a name or a dotted path such as `geometry.Point` (`class.isinstance`);
- a bundle pattern, `.(a, b)`.

`x is 1 | 2 | 3` means the `or` of the three tests, and narrows like them. `is not` negates the whole pattern. A tested value that isn't a local is read
once: `t.kind is "a" | "b"` doesn't read `t.kind` twice. Because `is not` is a fixed pair, `x is not y` never parses as `x is (not y)`.

`implements` takes a trait, given as a name or a dotted path like a class (`class.implements`). Like `is`, it takes alternatives
joined by `|`: `file implements NotFlac | Unreadable` is the same as `file implements NotFlac or file implements Unreadable`, and narrows
`file` to the types that implement either trait. `implements` has no `not` form.

### Destructuring in a test

A class test can also take the object apart. Put the fields to bind in braces after the class to bind them by name, or in parentheses
to bind them by position, the way the class's primary constructor takes them:

```luwu
if event is Click {x, y} then ... end
if cat is Cat(name, age) then ... end
if r is Ok(value) then ... end
```

`cat is Cat(name, age)` tests that `cat` is a `Cat` and binds `name` and `age` in the branch. A pattern can rename a field with `as` and test it with
`is`, and a pattern nested in a field can bind more names:

```luwu
if b is Box { inner as _ is Some(inner is string) } then
    print(inner)
end
```

A field taken apart without a test uses destructuring's `.{}` and `.()`, as in `is Branch {left.{value as left_value}, right}`. A binding's test
can be any pattern, so `Cat(name, age is not nil)` and `Event {kind is "join" | "leave"}` work too.

A vector's components are fields, so `is vector {x, y, z}` tests for a vector and binds them (and `w` where vectors have four).

### Bundle patterns

Bundles have no class to name, so a bundle pattern is written like the `.(1, 2, 3)` that builds one. `x is .(a, b)` matches a bundle of exactly
two values and binds them by position. Put `..` after the elements to match a bundle of at least that many values, and `.. as rest` to bind the
values after them as a bundle:

```luwu
const label = match v
    case is .(name) then name
    case is .(name, count) when count > 0 then `{count} {name}`
    case is .(name, .. as rest) then `{name} and {#rest} more`
    else "not a bundle"
```

The size is exact on purpose, as in Ruby's, C#'s, Rust's and Python's sequence patterns. Destructuring is lenient instead:
`const .(x, y) = .(1, 2, 3)` ignores the third value. A table is never a bundle, so `{1, 2}` doesn't match `.(a, b)`.

The names are bound only when the whole condition holds and are visible in the branch it guards, like an `if local` binding. A bound field can be
falsy (`is Flag {on}` binds `on = false`). Destructuring is allowed only where a binding can be scoped: a whole `if`, `elseif` or `while` condition, a
`when` clause, or a `case` pattern. A guard such as `r is Ok(value) and return value` is an error. So is `x is not Cat(name)`: a test that has to fail
binds nothing.

Alternatives may bind different names. A name that an alternative doesn't bind is `nil` when that alternative matches, like a missing value in
Lua's multiple assignment, so `.(x, y) | .(x, y, z)` gives `z = nil` for a bundle of two. The first alternative that matches is the one that
binds:

```luwu
if shape is Circle(radius) | Square(side) then
    print(radius, side) -- one of them is nil
end

if event is Click {x, y} | Drag {x, y, button} then
    move_to(x, y)
end
```

### `match` expressions

```luwu
const which = match args[1] or error("missing 'patch' or 'unpatch'")
    case is "p" | "patch" then "patch"
    case is "u" | "unpatch" then "unpatch"
    case other then error(`expected patch or unpatch, got {other}`)

print(match foo()
    case name is string then `name: {name}`
    case is Cat {name, age} then `{name} is {age}`
    case other then error(`unexpected {other}`)
)
```

- The header is `match <subject>`, or `match const <name> = <subject>` (also `local`) to name the subject in every arm and guard.
- Each arm is `case [<name>] [is <pattern>] [when <condition> ...] then <value>`, or `when <condition> [when <condition> ...] then <value>`
  for an arm that only tests conditions. With a named subject, `when n > 10 then` reads better than `case n when n > 10 then`. The pattern is the pattern of `is` (and `is not` and `implements`),
  literals included: `case is "p" | "patch"`, `case is Point {x, y}`, `case implements Named`. A pattern always comes after `is`, as it does
  outside a `match`. A `when` chain after the pattern works as in [If Local](if-local.md).
- A name before `is` is always a new binding. It holds the subject's value, narrowed by the pattern, for the arm's guards and value:
  `case n is number when n > 10 then ...`. With no pattern, `case other` matches every value, `nil` and `false` included.
- A name that's already taken was meant as a test, which is Python's footgun of `case FOO:` binding a new `FOO`. The name of a builtin
  (`case string`, `case None`, `case Some`), a class, a trait or a `const` is a syntax error that asks for `case is ...`. A name that shadows a
  `local` gets the `LocalShadow` lint and one that shadows another builtin global gets `BuiltinShadow`, with the same suggestion.
- The last arm is `else <value>`, or a `case <name>` with no `when`, which matches everything. A `match` always gives a value. Because a nested
  `match` ends at its last arm, the arms after it belong to the outer `match`.
- An arm's value can leave instead, as an if-else expression's can: `case is nil then return`, `case is 0 then continue`.
- Names are introduced only by `case` names, class and bundle patterns, `when const` guards and the subject. A named subject is narrowed in each
  arm.
- A `match` expression has no `end`: it ends at its last arm's value.

The arms are tested in order, like the branches of an if-else chain, and the subject is evaluated once before the first arm.

### `match` statements

In statement position, `match` takes the same header and arms, but each arm's `then` (or `else`) is followed by a block, and the `match`
ends with `end`:

```luwu
match const event = next_event()
    case is Click {x, y} then
        press(x, y)
    case is Key {code} when code == ESCAPE then
        return
    when event == nil then
        print("no events")
    else
        log(event)
end
```

- A statement arm's block ends where the next `case`, `when` or `else` starts. Inside an arm's block, a statement can't start with `case`
  or `when` unless it assigns, indexes or calls a variable with that name.
- `else` is optional. When no arm matches, the statement runs nothing, like an `if` with no `else`.
- The arms are indented one level under `match`, and the `end` lines up with `match`.

### Type system

`is` and `implements` refine like the calls they stand for: `x is string` narrows `x` to `string` in the branch, and `p is Point` narrows `p` to
`Point`. In a `match`, the subject (named or a plain local) is narrowed in each arm. Destructured names get the field types of the tested class,
and a binding's test narrows the binding the way a test of a local does. A bundle pattern narrows the value to the bundles of its size, so on
`.(A, B) | .(C, D, E)` the `a` of `.(a, b)` is `A`; a pattern of more than six values, or one with `..`, narrows to every bundle. A name that
alternatives bind gets the union of the types they give it, and is optional when an alternative doesn't bind it: in
`Cat(name, lives) | Dog(name)`, `name` is a `string` and `lives` a `number?`. A pattern that no value of the matched type can match is a type
error at the pattern.

### Editor support

luwu-lsp highlights `match`, `case`, `is` and `implements` in the positions the parser accepts them, and indents `case` arms one level under `match`.
A tag after `is` or `is not` colors as part of the keyword, and in `is Cat(name)` the class colors as a type and the names as bindings.

## Compatibility

The operators and `match` are Luwu-only and parse only in Luwu files. In Luau files, `local a = x is = 1` and `local a = match b = 1` keep their
Luau meaning. In Luwu files `is`, `implements`, `case` and `match` stay valid names wherever the operator reading can't apply: `match` is a match
expression only when a `case` or `when` arm follows its subject, and `is` starts a pattern only before a name, `not`, a literal or
`function`. A statement that starts with `match` followed on the same line by anything but an assignment, an index or call arguments
is a `match` statement.

## Drawbacks

`is` with a class runs `class.isinstance` and checks the exact class. Luwu classes have no inheritance. A `match` over related kinds of
values has to list each class.

`match` arms are tested in order, one after another. A `match` with many string arms is a chain of comparisons rather than a jump table.

A pattern whose alternatives bind names tests the alternatives again for each name it binds, to find which one matched.

## Alternatives

- **Other layouts for the `match` statement.** Every layout has a cost: with the arms indented under `match`, the last arm hangs without its
  own `end`; an `end` per arm, or `end end`, is noise. The statement uses one `end` at the `match`'s indentation.
- **`or` between alternatives** (`case "p" or "patch"`) is already an expression that evaluates to `"p"`.
- **Patterns without `is`** (`case "p" | "patch"`, `case .(name)`). A bare `.(name)` reads as a bundle now that `.(a, b)` builds one, and
  patterns that only worked after `case` made `match` a second pattern language next to `is`.
- **A dot in class patterns** (`is Cat.(name, age)`, `is Pod.{x, y}`). Patterns used to be written like destructuring
  (`const Cat.(name) = c`), and the dot told a pattern apart from constructing a `Cat`. The dot was removed once every pattern moved behind `is`,
  where `Cat(name)` can't be a call. Bundle patterns keep it because `.(a, b)` is how a bundle is written everywhere.
- **Binding a bare name that's already taken** (Python's `case FOO:`) silently shadows `FOO`. A `case` name binds, and the names that are
  always meant as tests are errors.
- **`:` as a runtime check** in patterns was rejected. `:` is a static, erased annotation everywhere else.
- **An optional `else`** when the type checker proves the arms exhaustive is open. Requiring `else` keeps nested matches safe.

## Future work

- Exhaustiveness checking over unions of classes and singletons.
- Narrowing a bundle pattern of more than six values to the bundles of its size.
- Testing the alternatives of a pattern that binds once, instead of once for the test and again for each name.
- Computing `typeof` once per `match` instead of once per arm, and jump tables for many literal arms.

## Prior art

- A keyword on every arm: Ruby (`case ... when ... then`), Nim, Swift and Scala (`case`).
- `case is Type`: Swift. Other marked type tests: Kotlin's `is String` and F#'s `:? string`.
- `when` guards: Elixir, Erlang, OCaml and F#.
- Explicit bindings: Swift's `case let`, and Elixir's `^pin` for comparing instead of binding.
- Sequence patterns of an exact size with an explicit rest: Python's `[x, y, *rest]`, Rust's and C#'s `[x, y, ..]`, Ruby's `in [x, y, *]` and
  Scala's `Seq(x, y, _*)`. Ruby and JavaScript also make assignment lenient and patterns exact.
- A named subject: Kotlin's `when (val v = ...)`.
- A required final arm: OCaml has to parenthesize nested matches because a match without one takes the outer arms.
