# Push and pop operators

Status: Proposed

FFlag: DebugLuwuPoperators

## Open questions

- **Pop syntax:** see [Pop](#pop).

## Summary

Adds a push statement operator `a <+ v` and a pop prefix operator (final syntax TBD but RFC implementation uses `<-a`).

The `arr <+ a, b` statement operator pushes one or more values into the container `arr`, which may be an array-like table or a value implementing
the new metamethod `__push`.

In `<-c`, the `<-` operator pops the last value of a container `c` and evaluates the expression to it. Like with push, the pop operator may be used
on an array-like table as well as any value that implements the new metamethod `__pop`.

## Motivation

Luwu is intended as an expressive language, especially for data processing. One thing I need to do all day at work is add things into arrays. Python
makes this just a method on the array. We obviously don't have methods on arrays (array-like tables), so although structured data is easier to
represent in Luau than in Python, actually manipulating it has quite a few ergonomic drawbacks (lack of table comps, etc.). Additionally, push
operations are used more frequently than length (boundary) operations, which get their own operator that similarly only works on array-like tables and
when `__len` is implemented. Push and pop work on tables (through `setmetatable`), userdata and objects. This allows all containers to share the same
push/pop interface regardless of their backing implementation, just like `__iter` and `__len` provide for lengthing and iterating.

Today, pushing is spelled one of two ways, and neither is pleasant to read or write hundreds of times a day:

```luwu
table.insert(results, row)
results[#results + 1] = row
```

Table comprehensions cover building an array in one go, but not adding to one across iterations of a loop, under conditions, or from several places.

## Design

### Push: `a <+ v`

`a <+ v` means push `v` into `a`. `a` must be a table (ideally array-like) or otherwise a data type that implements the new metamethod
`__push(self, ...)`. Must be a statement, like `+=`.

```luwu
local cats = {}
cats <+ "meow"
cats <+ "mrrp"
-- cats is { "meow", "mrrp" }

for _, row in rows do
    if row.valid then
        results <+ row
    end
end
```

Pushing and popping a `const` table binding are allowed. A previous version of this RFC disallowed it (requiring `local`) but this was rejected since
unlike Rust, `const` in Luwu refers solely to the immutableness of the binding itself rather than the value behind the binding.

The target can be any assignable expression, so `self.items <+ item` and `groups[key] <+ item` work (note `groups[key]` must already be a valid
container, otherwise we raise a runtime error for attempting to append to something that isn't a container).

#### Pushing several values

The right-hand side is an expression list, like the right-hand side of an assignment.

The pusherator allows its RHS to be multiple values. Regardless of how many values exist in the RHS, `__push` metamethod is invoked only once per
push statement.

```luwu
cats <+ "meow", "mrrp", "nya"
cats <+ get_more_cats()  -- pushes every value get_more_cats returns
cats <+ "meow", ...      -- "meow", then every vararg
```

#### Pushing `nil`

We treat pushing a `nil` as a noop so users don't need to `nil`check to push an element into a container. Pushing a `nil` will not create holes in an
array-like table.

```luwu
results <+ maybe_row              -- pushes only if maybe_row isn't nil
results <+ maybe_row or default   -- always pushes something
results <+ row_1, nil, row_3      -- pushes row_1 and row_3, doesn't create holes
```

#### Semantics

On array-like tables, `<+` reuses the existing semantics of `table.insert(a, v)`.

Values are written with a raw set (no `__newindex`), and pushing onto a readonly (frozen) table raises a runtime error.

That includes tables whose metatable has `__newindex` but no `__push`: the push is a raw write, exactly like `table.insert`, and skips
whatever the `__newindex` was written to do (a proxy, a change tracker, a readonly guard).

Checking for that at runtime would cost every push onto a table with a metatable, so the type checker reports it instead when it knows the table's
metatable (see [Type system](#type-system)). A table that should handle pushes itself defines `__push`.

In order, `a <+ v1, v2, ...`:

1. calls `__push` if `a` has a metatable with `__push` (see [Metamethods](#metamethods));
2. appends each non-`nil` value at `#a + 1` with a raw set, in order, if `a` is a table;
3. raises an error otherwise.

If the container is actually a map (or already mixed table), pushing sets the value at `#c + 1`, which is most likely unintended but is consistent
with `table.insert`. We will raise a TypeError for this in analysis but can't block it at runtime.

### Pop

The poperator removes the last element of container `c` and evaluates to that element. If the container is empty, it returns `nil`. For array-like
tables it is roughly equivalent to `table.remove(c)` except faster.

Popping is just an expression, so a popped value can be returned, drive a `while local` loop, etc.

> [!WARNING]
> **The syntax is not decided.** Requirements: it must not change the meaning of existing code (`a<-b` is a valid comparison in Luau), and it should
> be a prefix expression operator rather than a statement operator.

Candidates:

- **Prefix `<-`** (`const back = <-arr`): matches Go's channel receive (`v := <-ch`) and keeps the `+`/`-` pairing. It can't be a
  token (`a<-b` is a comparison), a line starting with `<-` silently continues the previous line (`x` then `<-arr` is `x < -arr`), and the arrow
  reads poorly in prefix position.
- **Prefix `->`** (`const back = ->arr`): `->` is already a token (function types) and can't start an expression today, so no new lexing rule. A
  line starting with `->` after an expression is a parse error rather than a silent continuation. Reads as "out of `arr`", but loses the `+`/`-`
  pairing.
- **No operator** (`const back = table.remove(arr)`): ship push alone. Pop is far less common than push, and `table.remove(arr)` is already short.

### Metamethods

- `__push(self, ...)` is called once per push statement with every pushed value, in order, `nil`s already skipped: it is a bulk
  append. `a <+ x, nil, y` calls `__push(a, x, y)` once. If every value is `nil`, `__push` isn't called.
- `__pop(self): T?` is called by pop. It must return exactly one value (not checked at runtime).

### Classes and traits

Classes may define `__push` and `__pop` for usage on their objects. Both metamethods may be inherited from any implemented traits just like
most other metamethods.

### Examples

A bounded history that drops its oldest entry when full:

```luwu
class History<T>(limit: number)
    entries: { T } = {}

    function __push(self, ...: T)
        self.entries <+ ...
        while #self.entries > self.limit do
            table.remove(self.entries, 1)
        end
    end

    function __pop(self): T?
        return table.remove(self.entries)
    end

    function __len(self): number
        return #self.entries
    end
end

const undo = History(100)
undo <+ edit -- never holds more than 100 edits
```

A set that ignores duplicates but remembers insertion order:

```luwu
class OrderedSet<T>()
    seen: { [T]: true } = {}
    order: { T } = {}

    function __push(self, ...: T)
        for _, item in { ... } do
            if self.seen[item] then
                continue
            end
            self.seen[item] = true
            self.order <+ item
        end
    end

    function __iter(self)
        return next, self.order
    end
end

const tags = OrderedSet()
tags <+ "cat", "dog", "cat" -- holds "cat", "dog"
```

A FIFO queue, where pop takes from the front instead of the back:

```luwu
class Queue<T>()
    items: { T } = {}
    head: number = 1

    function __push(self, ...: T)
        self.items <+ ...
    end

    function __pop(self): T?
        const item = self.items[self.head]
        if item ~= nil then
            self.items[self.head] = nil
            self.head += 1
        end
        return item
    end
end

const jobs = Queue() 
jobs <+ fetch_jobs() -- every job fetch_jobs returns
```

### Type system

- `a <+ v1, v2, ...` where `a: { T }` checks each RHS value against `T?`.
- Pop on `a: { T }` has type `T?`, since the array may be empty.
- For a type with `__push`/`__pop`, the element and result types are inferred from their respective metamethods' signatures.
- Pushing to or popping from anything else (a non-table, a map-like `{ [string]: T }`) is a type error.
- Pushing to or popping from a table whose metatable is known to have `__newindex` but no `__push`/`__pop` is a type error: at runtime it is a raw
  write that bypasses `__newindex` just like `table.insert`.

### Editor support

Minor additions needed to luwu-lsp's Luwu grammar, plus we might want to introduce lints and code actions to transform `table.insert(t, el)` into
`t <+ el`.

## Compatibility

`<+` breaks no existing code. Luau 0.730 has no unary `+`, so `a <+ b` is not valid anywhere today, and `<+` can be lexed as a single token.
Additionally, we will try to choose a final poperator that retains backwards compatibility.

## Drawbacks

- Pushing onto a table with `__newindex` and no `__push` silently bypasses `__newindex`, just like `table.insert` does. The type checker only
  catches it when it knows the table's metatable.
- Skipping `nil` silently means `arr <+ x` where `x` is unexpectedly `nil` does nothing instead of failing.
- Two more operators for readers to learn, where `table.insert` and `table.remove` already work.

## Alternatives

### Other syntax

We considered `a #= v`, because it can read as sugar for `a[#a + 1] = v`, and `a @= v`, since `@` looks like "a for append". We decided to go with
`<symb` because it's common in other languages and is more obvious for what the feature does.

The original design had pop as a statement, `b <- a` (and `const back <- arr`), for symmetry with `<+`. Unfortunately, `a<-b` is already a valid
comparison in Luau, so this would either break existing code or only work at the start of a line.

We considered using `<-` for push, which isn't ambiguous in the statement position but prevents us having a distinct poperator due to confusion.
It would match Go's `ch <- v`, but in other languages `<-` can be an assignment.

PHP's `a[] = v` doesn't need a new operator, but we consider the empty `[]` part noncongruent with how arrays are actually defined in the value or
type language, and I personally find it ugly.

### Other semantics

Pushing could take only the first value of `a <+ f()`, like assigning to a single variable. But pushing all of them lets you write `a <+ x, y, z`
and forward `...`, and you can still push just one with `a <+ (f())`.

`__push` could be called once per value, but then a container can't tell `a <+ x, y, z` apart from three separate pushes. One call is a bulk
append, so the container can reserve space or trim once.

Pushing `nil` could be an error, but then every optional push needs an `if` around it.

## Prior art

Some other languages in the same space as Luwu have dedicated append/push operators.

Uses `<<` to mean push (or an overload of `<<` on standard data types to mean push):
Ruby, Crystal, Groovy, V (vlang), C++ Qt library (which reimplements all of C++ data types).

Uses `+=`:
Kotlin, Scala, Swift, Bash, Zsh. Python's `+=` on a list extends it with another iterable (`lst += [v]`) rather than pushing one value.

Uses `~=`: D.

Go's channels use `ch <- v` to send a value and `<-ch` to receive one.

We can't use `<<` because that syntax opens explicit type arguments and may be confused with bitshift syntax, and we can't use `+=` because that
already does `x = x + 1`, using existing `__add` implementations.

## Implementation details

- `<+` compiles to `LOP_PUSH container firstValue count` (count 0 when a call or `...` last pushes up to the stack top), and pop to
  `LOP_POP target container`. Both are WIP bytecode (version 200) while `DebugLuwuPoperators` is on. A single pushed value is pushed from
  the register it's already in.
- Pushing one value onto a table without a metatable that has room in its array part, and popping one whose last element is in its array part,
  are inline in the interpreter and in native code. Everything else goes through `luaV_push`/`luaV_pop` (from native code, a fallback). Pushing
  measured 1.2-1.3x faster than `table.insert` and popping about 2.5x faster than `table.remove`, interpreted and native.
- Skipping `nil` is one tag check per value.
- `<-` is never a lexeme: the parser reads `<` followed directly by `-` as a pop only where an expression starts. `< <` opens explicit type
  arguments (`f<<T>>()`, also with a space between), except before a `-`: no type starts with one, so `x < <-t` compares with a pop.
- Code that exists only because pop is spelled `<-` has `` `<-` syntax `` in its comment, to find and strip if the syntax changes:
  `Parser::popFollows`, `Parser::parsePop`'s call in `parseExpr`, `Parser::typeArgumentsFollow`, `Parser::parseMistakenPushArrow`, the
  lexer's `Push` comment and the pretty printer's pop branch. Outside the code: the pop rule in luwu-lsp's `Luwu.tmLanguage.json` and its
  `pop_operator` hover, the `'<-'` in pop's messages (`TypeChecker2.cpp`'s `kPop`, `DocRefs.cpp`, the flag-off error in `Parser::parsePop`),
  and the tests.
- Type checking: a push adds an indexer to a table that's still being built (`const t = {}; t <+ 1` makes `t` a `{number}`), the same way
  `t[1] = 1` does, and pop's type comes from a `pop<T>` type function. `__push`/`__pop` on an instance of a generic class aren't checked against
  the class's type arguments: an instantiation shares its template's metatable, as it does for every other metamethod.
