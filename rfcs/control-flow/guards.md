# Guards

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans before it's accepted.

Status: Implemented (Flagged)

FFlag: LuwuDoExpr

## Summary

A guard is a leave statement followed by `if` and a condition. It leaves only when the condition is truthy:

```luwu
return nil if #t == 0
continue if item.hidden
break if attempts > MAX_ATTEMPTS
```

## Motivation

Early exits are the most common `if` statement in most code. Written as a full `if`, each one takes three lines for a single action, and a function
that checks four preconditions spends twelve lines on them. The one-line `if not x then return end` saves the lines, but it puts the action in the
middle of the line between the condition and `end`.

A guard is one line, and the reader sees what it does first. A line that starts with `return` or `continue` says "this is where the function or the
iteration ends" before the reader gets to the condition.

## Design

The guard statements are:

```luwu
continue if condition
break if condition
return if condition
return a, b, c if condition
give value if condition          -- inside a `do` expression
give [key] = value if condition  -- inside a table comprehension's `do` item
```

`<leave> if <condition>` means exactly `if <condition> then <leave> end`. The condition is evaluated first, and the values of a `return` or `give` are
evaluated only when the condition is truthy. A guard narrows like the `if` it stands for: after `return nil if user == nil`, the type checker knows
`user` isn't `nil` for the rest of the block.

The condition is any expression. It doesn't take `when` chains or bindings: a guard that needs a binding is an `if local` statement (see
[If Local](if-local.md)).

The guard's `if` must start on the line where the leave statement ends. A `return` whose values span lines is still guarded by an `if` after its last
value:

```luwu
return
    parse_header(bytes),
    parse_body(bytes) if #bytes > 0
```

An `if` on the next line is never a guard. After `break` or `continue` it's an error, as in Luau, and after a bare `return` it starts the
if-else expression being returned.

`return if c then a else b` still returns an if-else expression, as in Luau. `return if c` is a guard only when no `then` follows its condition.

Putting the leave first makes the guard's purpose obvious. The reader sees `return` or `continue` at the start of the line and knows the line is an
exit before reading the condition.

### Several values

A guard that returns several values is always an `if` statement on three lines:

```luwu
if not ok then
    return nil, err
end
```

`return nil, err if not ok` could be read as `return nil, (if not ok then err else nil)`, so the `ConfusingGuard` lint reports it and suggests the
`if` statement.

### Condition-first guards

The statement forms `<expr> or <leave>` and `<expr> and <leave>` stay valid. An `or` guard reads well when the code continues on the value it
checks, which is most early error handling: `file or return nil` says "have the file, or else return". The `ConfusingGuard` lint steers the rest:

| written | suggested |
|---|---|
| `done and break` | `break if done` |
| `x == 2 and return 1` | `return 1 if x == 2` |
| a long guard, `t.value or return types.error("...")` | `if not t.value then` with the `return` on the next line |
| a chained guard, `x or y and continue` | an `if` statement |
| one that leaves either way, `ok and continue or break` | an `if` statement |
| several values, `ok or (return nil, err)` | `if not ok then` with the `return` on the next line |

`<expr> and <leave>` stays valid syntax although the lint never suggests it. Someone who has used `x and return` inside an expression will
expect it to work as a statement too. Instead of a parse error that only says the statement is wrong, the lint points them at the form to
write.

Leaves in expression position are part of [Do Expressions](do-expressions.md): `const port = config.port or return nil` binds `port` or returns.

### Type system

The type checker and the linter see a guard as the `if` statement it stands for. A guard narrows and ends a block exactly like that `if`.

### Editor support

The `LuaGuard` lint reports a one-line `if not x then return end` in a Luwu file and suggests the equivalent guard (`return if not x`), or the
three-line `if` when it returns several values. `ConfusingGuard` reports the condition-first guards above. luwu-lsp has quick fixes for both, and a
refactor that rewrites a one-line `if` whose body is a single leave as a guard. The `UnflippedGuard` lint's
suggestion flips the guard's condition and keeps it leave-first.

## Compatibility

`return x if c`, `break if c` and `continue if c` aren't valid Luau, because a leave statement must end its block. No existing Luau code changes
meaning. `continue` stays contextual: `continue if c` is the keyword because a variable read can't be followed by `if`.

Code written with the `and`/`or` statement forms keeps working. The `ConfusingGuard` lint suggests the forms above.

## Drawbacks

A guard's condition comes after its action. A reader skimming only the start of each line sees exits without their conditions. Guards with long
conditions read worse than an `if` statement.

A guard can't bind a name. A check that needs a binding is still a three-line `if local`.

## Alternatives

- **`<expr> or <leave>` and `<expr> and <leave>` statements for every guard.** A prior implementation of this feature had only those
  (`#t > 0 or return nil`, `done and break`). In practice the `and` form and the longer guards gave code that was hard to read and hard to
  follow, and they brought back the readability problem of Lua's `a and b or c` idiom that the `LuaAndOr` lint exists for: the reader has to
  work out the truth table before knowing what the line does. A condition inverted to fit `or` (`x ~= "" or return`) made it worse. Putting the
  leave first makes the guard's purpose obvious. The short `or` form stays, because "have this, or else leave" is how early error handling reads.
- **`continue unless x > 3`**, Ruby's `unless`, was considered as the replacement for `x > 3 or continue` and rejected. Luwu has no `unless`
  statements or expressions, and inverted logic reads worse and takes more thinking to understand. Write `continue if x <= 3` or
  `continue if not (x > 3)` instead.
- **A `guard` keyword** (Swift's `guard x else return`) is as long as the one-line `if not x then return end`. That `if` already starts
  with a keyword.

## Future work

Luwu could allow expressions as statements in general, as Python and JavaScript do. `x or continue` would then be an ordinary expression
statement, and so would `f() + g()`. Lua forbids expression statements partly so a typo like `x == 1` for `x = 1` is an error, and both of those
languages lean on linters for that (pylint's `pointless-statement`, eslint's `no-unused-expressions`). Allowing them needs an error-level lint
for expressions that have no effect first. The `AmbiguousContinuation` lint already covers a line starting with `-` or a string that continues
the expression above it.

## Prior art

- Ruby and Perl statement modifiers: `return x if y`, `next if y` and `last if y` in Perl, `next if y` and `break if y` in Ruby.
- CoffeeScript's postfix `if` (`return if done`).
- Ruby's `unless` modifier, rejected above.
- Swift's `guard ... else`, rejected above.
