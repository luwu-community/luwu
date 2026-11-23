# Do Expressions

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans before it's accepted.

Status: Implemented (Flagged)

FFlag: LuwuDoExpr

## Summary

A `do` expression runs a block of statements where a value goes and gives the block's result with `give`. It has no `end`: it ends at the `give`,
`return`, `break` or `continue` that ends its block. You can also leave from inside an expression: `t[k] or return nil` returns from the function when
`t[k]` is falsy, and gives `t[k]` otherwise.

```luwu
const temp = do
    if env.os == "Windows" then
        give Path(env.vars.TEMP)
    end
    give Path("/tmp")

const port = config.port or return nil
```

Statement-level guards (`return nil if #t == 0`) are a separate feature. See [Guards](guards.md).

## Motivation

Computing a value that needs a few statements means declaring the local first and assigning it in every branch, or moving the code into a function
called once. Both separate the value from its name. A `const` can't be computed this way at all, because it has to be initialized where it's declared.

Early exits from a lookup are the other common case. Luau needs three lines (or a one-line `if` that most style guides reject) to bail out when a
value is missing, before the code can use it:

```luwu
local port = config.port
if not port then
    return nil
end
```

## Design

### `do` expressions

`do` followed by a block is an expression wherever an expression can go. The block's last statement must be `give <value>`, `return`, `break` or
`continue`. A `do` expression has no `end`, and that last statement tells the parser where the expression ends.

```luwu
const contents = if reliable then do
        const result = fs.file.read(path)
        if typeof(result) == "error" then
            return result
        end
        give result :: string
    else fs.readfile(path)
```

- `give` exits the innermost `do` expression with one value. It can appear in nested blocks and loops of the `do` expression's block, but never
  crosses a function boundary. Like an if-else expression, a `do` expression gives exactly one value: a call in a `give` is truncated to its first
  result, and a call that returns nothing gives `nil`.
- `return` returns any number of values from the enclosing function. `break` and `continue` target the nearest enclosing loop. That loop can be
  outside the `do` expression.
- To make a `do` expression that always fails, end it with `give error(...)`. A bare `error(...)` call isn't a terminator, because the parser can't
  know that `error` never returns.
- An `if` whose every branch gives isn't a terminator. Put the last case after the `if` instead of in an `else`.
- In default values (function default arguments, class field defaults, constructor parameter defaults), a `do` expression can't `return`, and
  `break`/`continue` only work for a loop inside the `do` expression.

A statement that starts with `do` is still a `do ... end` block. To call or index a `do` expression in statement position, parenthesize it.

### Leaving from an expression

The right operand of `or` or `and` can be `return`, `break`, `continue`, or `give` inside a `do` expression. The expression then gives its left
operand when the left operand decides the result and leaves otherwise:

```luwu
const port = config.port or return nil
local name = tostring(entry.name or continue)
const v = do
    const raw = cache[key] or give default
    give decode(raw)
```

An if-else expression's arms can leave the same way (`if i < 5 then even else continue`).

A bare `return` in these positions takes one value: `local a, b = x or return 1, 2` reads `2` as the value of `b`. Parenthesize the leave to
return several values: `x or (return 1, 2)`. A `return` with no value must be parenthesized (`x or (return)`) or followed by `;`. Without that, the
next line would become its return value.

A statement can consist of one of these forms, such as `file or return nil`. [Guards](guards.md) describes those statements, the leave-first
`return nil if not file`, and the `ConfusingGuard` lint that suggests one or the other.

### Keywords

`give` and `continue` are hard keywords at the start of a statement inside a `do` expression. The parser decides on the token: `give (f)(x)` gives
a call's result instead of calling a function named `give`. They can't name a variable there, but positions that can't be a variable stay allowed:
`thing.give`, `thing:continue()`, `{ give = 1 }` and member names. Outside `do` expressions, `give` is an ordinary name and `continue` keeps Luau's
contextual rules.

### Type system

A `do` expression's type is the union of the types its `give`s give. A `do` expression that never gives (every path returns, breaks or continues)
has type `never`. `x or (do return nil)` therefore has the truthy part of `x`'s type. `x or return` narrows `x` to truthy for the rest of the block.

Assignments made inside the block are visible after the expression along every path that gives.

### Editor support

The `ReturnOnNextLine` lint reports a `return` inside a `do` expression whose first value starts on the next line. The return value
continues there. Autocomplete offers `do` wherever a value can start.

## Compatibility

`do` in expression position and `give` aren't valid Luau. No existing code changes meaning. `continue` after `or` is a valid Luau variable read in
a module that names a variable `continue`; in such a module it stays a read.

## Drawbacks

A `do` expression has no closing keyword. Code typed after an unfinished `do` expression is read into its block until a terminator. The editor
shows that as errors in the following code until the `give` is written.

Readers who expect a `return` inside a `do` expression to give the value can be surprised that it returns from the enclosing function.

## Alternatives

- **A closing `end`** would make `do ... give v end` look like a block statement and add a keyword to every use. Expressions in Luwu end on their last
  part, like if-else expressions.
- **Other keywords for `give`:** `in` reads poorly outside ML languages, `break <value>` conflicts with loop `break` inside the block, and `yield`
  reads as `coroutine.yield`.
- **Immediately-invoked functions** (`(function() ... end)()`) already compute values with statements, but they can't return from or continue the
  enclosing function or loop, and they allocate a closure.

## Future work

- `repeat ... until cond give v` as an expression, whose `give` could see the body's locals.
- Evaluating an expression with a leave in statement position: see [Guards](guards.md).

## Prior art

- Rust's block expressions and `let ... else`, and `break 'label value`.
- Kotlin's `run { }` and `?: return`.
- Ruby: every block is an expression, and `x or return` is idiomatic.
- Perl's `open(...) or die`.
