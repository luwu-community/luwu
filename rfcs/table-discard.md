# table.discard (`table.discard` & `table.remove` lint improvement)

Author: @InfraredGodYT
FFlags: LuwuTableDiscard, LuwuTableRemoveFootgunLint

## Summary

Add the `table.discard` standard library method for removing table elements by value rather than index, along with a
linter improvement to prevent the `table.remove` footgun of accidental tail amputation.

## Motivation

To remove table elements by value rather than index, the following pattern is commonly used:

```luau
table.remove(t, table.find(t, element))
```

However, `table.find` returns `nil` when it cannot find the element specified in the second argument of the function
and `table.remove` defaults to removing the last element of a table when its second argument is either missing or
`nil`. This creates the footgun of unintentionally removing the last element of the table unless you use an `if`
statement or the global `assert` function.

## Design

First, I am proposing a standard library function for tables to remove elements by value rather than index:

```table.discard(t: {V}, value: V, count: number?): number```

`t` refers to the table you're using, `value` is the value you want removed, and `count` is an optional argument
specifying the limit of how many occurrences of `value` you want removed. If `count` is `nil`, every occurrence of
`value` is removed from the table. The function returns the number of removed elements.

For example:
```luau
local t = {"apple", "banana", "orange", "orange"}

table.discard(t, "orange") -- t is now {"apple", "banana"}
table.insert(t, "banana") -- t is now {"apple", "banana", "banana"}
table.discard(t, "banana", 1) -- t is now {"apple", "banana"}
```

Second, I'm also proposing the following linter rule for the `table.remove(t, foo)` footgun: if `foo` is an expression
or function result with `number?` as its type, the linter warns the user about the footgun.

```luau
local t = {"apple", "banana", "orange"}

table.remove(t, nil) -- OK
table.remove(t, table.find(t, "banana")) -- Warning: If this is nil, table.remove will remove the last element of the array.
                                          -- Consider using table.discard instead. If order is not important, use a
                                          -- key/value table for better performance.
```

### Implementation Details

`table.discard` iterates through the indices `1..#t`. The equality check uses the same `__eq` semantics as
`table.find`, respecting metamethods. The function errors on readonly tables or if `count` is <= 0.

The linter rule will warn the user when `table.remove(t, i)` is called and `i` has the `number?` type. Only `i` is
underlined. This limits false positives while catching the common footgun `table.remove(t, table.find(t, v))`.

## Drawbacks

While there are no compatibility concerns, this does add a new standard library function to the language.

## Alternatives

- Simply adding the linter rule would stop the footgun, but it would leave the verbose use of `if` statements as the
  only solution.
- Naming it `table.drop`, as this RFC first proposed, or `table.erase` or `table.remval`.
- Adding optional `start` and `finish` arguments to `table.discard` to specify search distance within the table.

## Prior art

- In most other languages, `drop(n)` gives a collection without its first n elements: Haskell, Kotlin, Scala, Elixir
  and Clojure all use it that way. A dataframe's `drop` removes columns by name. Readers coming from either would
  misread `table.drop`.
- `table.discard` behaves like Python's `set.discard`: it removes the value if it's there and never errors when it
  isn't.
- C++20's `std::erase(container, value)` removes every match and returns how many it removed, like `table.discard`
  without a `count`.
