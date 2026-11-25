# If Local

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans before it's accepted.

Status: Implemented (Flagged)

FFlag: LuwuIfLocal

## Summary

You can bind a value in the condition of an `if`, `elseif` or `while`, test it, and use it in the branch it guards. The condition is a `when` chain:
clauses joined by `when`. Each clause is a plain condition or a `local`/`const` binding that passes when the bound value is truthy. The same
chains work in if-else expressions, and `for` loops take them as a filter on each iteration.

```luwu
if const user = find_user(id) when user.active then
    greet(user)
end

while local line = reader:read() when line ~= "" do
    handle(line)
end

for _, item in items when item.visible do
    draw(item)
end
```

## Motivation

Code that looks something up and acts on it only when the lookup succeeds is everywhere. In Luau the binding has to go above the `if`. It then stays in
scope long after the branch that needed it:

```luwu
local user = find_user(id)
if user and user.active then
    greet(user)
end
-- `user` is still in scope here, possibly nil
```

Each extra lookup that depends on the previous one adds a level of nesting or another local that outlives its use. `while` loops that read until a
value runs out need the read written twice (before the loop and at the end of the body) or a `while true` with a `break`. `for` loops that skip
items wrap the whole body in an `if` or start it with a `continue`.

A `when` chain keeps each binding next to the test that uses it and ends its scope with the branch.

## Design

### `when` chains

A condition in an `if`, `elseif` or `while` head, or in an if-else expression, is a chain of clauses separated by `when`. There are three kinds of
clause:

- A binding (`local name[: Type] = expr` or `const name[: Type] = expr`) passes when the bound value is truthy: not `nil`, `false` or `none`.
- A destructuring binding (`local .{a, b} = expr` or `const .{a, b} = expr`) passes when every name it binds is truthy.
- A plain expression passes when it's truthy.

Clauses run left to right and stop at the first one that fails. Clauses can come in any order. A chain with no binding at all is allowed and means
the same as joining its clauses with `and`:

```luwu
if const config = load_config()
when const port = config.port
when port > 1024
then
    listen(port)
end

const label = if const name = user.nickname when name ~= "" then name else user.login
```

A binding is visible to the clauses after it and to the body of its branch. Its initializer, later `elseif` and `else` branches and the code after
the statement can't see it. A `const` binding can't be reassigned in the branch.

A clause binds one name. `if local ok, result = pcall(f)` is a parse error that suggests writing a second clause with `when`. Writing `and` where
`when` was meant (`if local x = f() and local y = g(x)`) is one error that says to use `when`.

### Loops

A `when` chain can follow any loop head, and the head's keyword ends it.

`while` takes the full chain grammar. When the chain fails, the loop ends:

```luwu
while ready when const job = queue:pop() do
    run(job)
end
```

Each pass evaluates the chain again with fresh bindings. A binding is never the previous pass's value, and a closure created in the body captures
that pass's binding. `continue` in the body runs the chain again.

In a `for` loop, every clause starts with `when`. When the chain fails, that iteration is skipped and the loop goes on:

```luwu
for i = 1, n when i % 2 == 0 when local square = i * i do
    print(square)
end

for _, .{name, age} in people when age > 18 do
    print(name)
end
```

A `for` with a `when` chain behaves exactly like the same loop with an `if` around its body, and compiles to the same bytecode.

### Type system

A binding is typed by its initializer and narrowed to its truthy part in the branch, as `if x then` narrows `x`. A plain clause refines the
names it tests for the clauses after it and for the branch, as the same condition joined with `and` would.

The `BindingSwallowsCondition` lint reports a binding whose value is an unparenthesized `and`/`or` that was probably meant as two clauses:
`if local x = f() and x > 1 then` binds `x` to the whole `and`. The `x` on the right of the `and` reads an outer `x`.

### Editor support

Autocomplete offers `when` and `do` after a loop head or one of its clauses, and `local`/`const` after `while`. Hover and go-to-definition work on
bindings like on any local.

## Compatibility

`when` is a keyword only right after a clause in a condition. A bare name can't continue a Luau expression there. `if local` and `while local` aren't
valid Luau. No existing code changes meaning.

Upstream Luau added a different `if local` (statements only, one binding, no chains) after 0.730. Luwu doesn't accept upstream's later syntax, and this
design covers it.

## Drawbacks

A long chain on one line is hard to read. Writing one clause per line once a chain has more than two clauses is a formatting convention rather than a
rule.

A binding tests truthiness rather than `nil`. `if local flag = read_flag()` skips the branch when the flag is `false`. Code that needs to see
`false` has to bind it outside the condition.

## Alternatives

- **Luau RFC 110** (`if local x = f() in x > 3`) stacks `local` clauses with `in` and was proposed upstream by the same author. Luwu doesn't follow it:
  `in` collides with `for ... in` and reads poorly in a chain, and its clauses can't mix plain conditions with bindings in any order.
- **Several bindings in one clause** (`if local a, b = f(), g()`) was rejected because it hides which value failed and invites `pcall` unpacking,
  which this feature isn't meant for.
- **`if` instead of `when` in loop heads** reads like a nested statement with no `then` or `end`.

## Future work

- `repeat ... until` chains.
- Patterns in clauses beyond destructuring are covered by the Pattern Matching RFC (`if x is Point {x, y} then`).

## Prior art

- Rust's let-chains (`if let Some(x) = f() && x > 3`) mix bindings and conditions in any order.
- Swift's `if let x = f(), x > 3` and `while let`.
- Go's `if x := f(); x > 3` scopes the binding to the `if`.
- C++17's `if (auto x = f(); x > 3)`.
- `when` as a guard keyword: Elixir, Erlang, OCaml and F# match guards.
