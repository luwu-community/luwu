# Table Comprehensions

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans before it's accepted.

Status: Implemented (Flagged)

FFlag: LuwuTableComprehensions

## Summary

A table comprehension builds a table from a loop written inside the table constructor: `{ for _, v in nums give v * 2 }`. `when` clauses filter and
bind values, several `for` clauses flatten nested loops, and a `[key] = value` item builds a map instead of an array.

```luwu
const doubled = { for _, v in nums give v * 2 }

const luwu_files = { for _, file in fs.listdir(dir) when file:endswith(".luwu") give file }

const extensions = {
    for _, file in files
    when const absolute = fs.path.absolute(file)
    give [absolute] = fs.path.extension(absolute)
}
```

## Motivation

Building a table from another is one of the most common things a program does. In Luau it takes a local, a loop and an insert, and the table's
contents are spread over several lines that don't say what the table is for:

```luwu
local doubled = {}
for _, v in nums do
    table.insert(doubled, v * 2)
end
```

The loop people usually write is also slower than it needs to be. A table that grows from `{}` rehashes about `log2(n)` times, and Luau has no way
to presize a hash part from Luau code at all. A comprehension knows its source and can size the table before the loop runs.

## Design

### Syntax

```
comprehension ::= '{' clause {clause} item '}'
clause ::= 'for' bindings ('=' range | 'in' exprlist) {'when' condition}
item ::= 'give' exp | 'give' '[' exp ']' '=' exp | 'give' '[' exp ']' compoundop exp | 'do' block
```

A comprehension is the only item of its table. `{ 1, for _, v in xs give v }` is an error.

### Clauses

Each `for` clause is a numeric or generic `for` loop head that takes the same bindings (destructuring included) and iterates like the loop
statement. A clause's `when` chain follows [If Local](if-local.md): a plain condition passes when it's truthy, and `when const x = e` binds `x` and
passes when `x` is truthy. A clause that fails skips the element. Bindings are visible to later clauses and to the item.

A second `for` clause runs inside the first to flatten nested loops:

```luwu
const filled = {
    for _, row in grid
    for _, cell in row when cell ~= 0
    give cell
}
```

### Items

- `give value` appends `value` to the array part. A `nil` value appends nothing: `give maybe(x)` never leaves a hole in the array, and `#` stays
  correct.
- `give [key] = value` writes `key` in a map. A `nil` value writes nothing. A `nil` key raises like any table assignment.
- `give [key] op= value` combines the value with what `key` already holds. The operator is `+=`, `-=`, `*=` or `..=`. An absent key takes the
  value as is (negated for `-=`). That matches starting from 0, 1 or `""` and doesn't assume the value is a number:

  ```luau
  const counts = { for _, word in words give [word] += 1 }
  ```

- `name = value` as an item is an error. It would write the same key on every iteration.

### `do` items

A `do` block in place of `give` runs statements for each element. It follows every rule of a [do expression](do-expressions.md): it has no `end`, it
ends at its `give`, `return`, `break` or `continue`, and `give` can appear in any branch. `continue` skips the element, `break` ends the
comprehension with what has been built so far, and `return` returns from the enclosing function. `give do ...` is the same as `do ...`.

```luwu
const records = {
    for _, line in lines when line ~= "" do
        const name, value = parse(line)
        validate(name, value)
        give Record(name, value)
}
```

A `do` item builds a map when its `give`s write keys. Every `give` directly in the item's block either writes a key (`give [k] = v` or
`give [k] op= v`) or gives a value, and mixing the two is a parse error. A `do` expression nested inside the item gives plain values only.

```luwu
const renamed = {
    for key, value in source do
        const new_key = normalize(key)
        if new_key == "" then
            continue
        end
        give [new_key] = transform(value)
}
```

### Evaluation

A comprehension runs exactly like the loop it's written as. Its bindings aren't visible after the comprehension. The item can't read the table while
it's built.

The compiler presizes the table from the source when it can. A numeric range gives the trip count, and a table source gives its capacity. A
filtering comprehension that kept fewer elements than it reserved shrinks its array part afterwards to at most twice what it uses.
`#{ for ... }` counts the elements without building the table. At `--!optimize 2`, an array comprehension over a constant range whose item and
conditions fold to constants compiles to the table literal it builds.

### Type system

An array comprehension of items of type `T` has type `{T}` with `nil` removed from `T`. A map comprehension's `{ [K]: V }` joins the key and value
types of every item that writes a key. `..=` adds `string` to the value type: an absent key stores the first value as is. When the comprehension
has an expected type (an annotation or a parameter), each item is checked against the expected indexer the way a table literal's items are.

## Compatibility

`{ for` isn't valid Luau. No existing code changes meaning. `give` is a keyword only after a comprehension's clauses, and a bare name can't
continue an expression there.

## Drawbacks

A nested comprehension reads inside out once it has more than two clauses, and a long chain of `when` clauses can be harder to follow than the loop it
replaces. Both are style questions rather than grammar.

Presizing a filtering comprehension reserves memory for elements that may be skipped, until the shrink at the end.

## Alternatives

- **Python's item-first order** (`{ v * 2 for _, v in t }`) makes nested comprehensions read inside out, and bindings in clauses would come after the
  expression that uses them.
- **`if` instead of `when` for filters** puts two `if` grammars side by side when the item is an if-else expression, one with `then`/`else` and one
  without.
- **A statement body with no keyword** was rejected. Statements in the item must be opened by `do`, and a comprehension without one is a single
  expression.
- **`[do ... give k] = do ... give v`** for map items that need statements runs the statements twice. Keyed `give`s replace it.

## Future work

- Loop expressions: `for` and `while` as lazy values usable outside table constructors.
- Typed containers (`vec`) built from comprehension syntax.

## Prior art

- Python's list, set and dict comprehensions, and nested comprehensions.
- Haskell list comprehensions with guards and multiple generators.
- Elixir's `for` with filters and `into:`.
- Iterator chains in Rust and C# (`map`, `filter`, `collect`). Comprehensions cover their common cases.
