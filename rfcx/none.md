# None Primitive

Status: Proposed
Author: @cheesycod
FFlag: `LuauNonePrimitive`

## Summary

Introduce `none` as a new first-class primitive value and built-in global in Luau, representing an intentional "no value" sentinel distinct from `nil`. Unlike `nil`, which denotes the absence of a value or an unassigned state, `none` is an explicit primitive of type `"none"`. It is falsy in boolean evaluation contexts, compares equal only to itself (`none == none` is `true` while `nil ~= none`), and does not delete table entries or create array holes when stored as a table value (`t[k] = none`).

## Motivation

In Luau and Lua, `nil` serves two conflicting roles: indicating the absence of a value (such as an uninitialized variable, omitted function argument, or non-existent table key) and acting as an intentional empty sentinel in data structures and APIs. Because assigning `t[k] = nil` removes key `k` from table `t`, developers cannot store an explicit empty sentinel in a table without resorting to workaround objects (such as `local NONE = {}` or unique userdata sentinels).

In standard Luau, assigning `nil` to an index within a sequential array creates a "hole" (e.g., `{ 1, nil, 3 }`). The `#` length operator on sparse tables is defined to return an arbitrary array boundary, making `#t` and sequential iteration via `ipairs` unpredictable when representing lists with missing or nullable entries. With the `none` primitive, array elements can be explicitly set to `none` (`local list = { 1, none, 3 }`). Because `none` is a stored value and does not leave holes, `#list` reliably evaluates to `3`, and sequential iteration visits every index without breaking sequence invariants.

### Why `none` instead of `null`?

The name `null` is too close to `nil`, which can cause visual confusion, typos, and ambiguity when reading scripts. Using `none` clearly distinguishes an explicit "none/no value" sentinel from the uninitialized or absent state represented by `nil`.

## Design

### Global Value and Type

When `LuauNonePrimitive` is enabled, `none` is exposed as a built-in global value representing the singleton value of the `none` primitive type.

1. **Primitive Type:**
   A new primitive value type `none` is introduced.
2. **Type Function:**
   The standard library `type(none)` returns the string value `"none"`. `typeof(none)` also returns `"none"`.

### Runtime Semantics and Comparison

1. **Falsiness:**
   In all boolean evaluation contexts (`if`, `elseif`, `while`, `until`, and logical operators `and`, `or`, `not`), `none` evaluates as **falsy**. The set of falsy values in Luau is exactly `false`, `nil`, and `none`. All other values are truthy.
   ```lua
   assert(not none == true)
   assert((none or "fallback") == "fallback")
   assert((none and "unreachable") == none)
   ```
2. **Equality (`==`, `~=`):**
   - `none == none` evaluates to `true`.
   - `none ~= nil` evaluates to `true` (`none == nil` is `false`).
   - `none` is not equal to any other value (`false`, `0`, `""`, `{}`).
3. **Relational Comparison (`<`, `<=`, `>`, `>=`):**
   Attempting to compare `none` with any value (including `none` itself) using relational operators raises a runtime error, consistent with relational comparisons on `nil` and `boolean`.

### Table Storage and No-Hole Semantics

Unlike `nil`, storing `none` in a table preserves the key-value pair:

1. **Dictionary Storage:**
   Assigning `t[k] = none` sets the value associated with `k` to `none`. It does not remove `k` from `t`.
   ```lua
   local dict = { a = 10, b = none }
   assert(dict.a == 10)
   assert(dict.b == none)
   assert(dict.c == nil)

   -- Assigning nil deletes the entry; assigning none overwrites the value
   dict.a = none
   assert(dict.a == none)
   ```
2. **Array Sequences and Length (`#`):**
   Arrays containing `none` values are contiguous sequences without holes. The `#` operator counts entries containing `none` as valid elements.
   ```lua
   local list = { "first", none, "third" }
   assert(#list == 3)
   assert(list[2] == none)

   -- Modifying an element to none does not truncate the sequence
   list[3] = none
   assert(#list == 3)

   -- Setting to nil deletes the element and truncates the sequence length
   list[3] = nil
   assert(#list == 2)
   ```
3. **Table Iteration:**
   - `pairs(t)` and `next(t, k)` visit all existing keys, including keys whose values are `none`.
   - `ipairs(t)` iterates sequentially from index `1` up to `#t`, yielding `(i, none)` for elements set to `none`, terminating only when an index evaluates to `nil`.

### C API Additions

The C API is expanded with the following definitions and functions:

```c
#define LUA_TSYMNONE 6

LUA_API int lua_issymnone(lua_State* L, int idx);
LUA_API void lua_pushsymnone(lua_State* L);
```

- `lua_type(L, idx)` returns `LUA_TSYMNONE` for `none` values.
- `lua_typename(L, LUA_TSYMNONE)` returns `"none"`.
- `lua_toboolean(L, idx)` returns `0` when the value at `idx` has type `LUA_TSYMNONE`.
- `lua_issymnone(L, idx)` returns `1` if the value at `idx` is `LUA_TSYMNONE`, and `0` otherwise.
*(Note: To accommodate `LUA_TSYMNONE` as tag 6, the API constant for an invalid/out-of-bounds stack index `-1` is kept as `LUA_TNONE`).*

### Standard Library

1. **Basic Library:**
   - `type(none)` and `typeof(none)` return `"none"`.
   - `tostring(none)` returns `"none"`.

## Drawbacks

- **Global Shadowing:**
   Because `none` is a built-in global value, code that explicitly declares a local variable or function parameter named `none` (`local none = ...`) will shadow the built-in `none` primitive within that scope.
- **Two Empty Primitives:**
   Having both `nil` and `none` introduces two concepts for emptiness, which may increase cognitive load. Developers must learn the distinction between absence/uninitialized state (`nil`) and explicit empty/sentinel state (`none`).

## Alternatives

- **Reserved Keyword:**
   Make `none` a reserved keyword in the language grammar rather than a global value. This was rejected because it would be a syntactic breaking change for existing scripts that use `none` as an identifier or table field name.
- **Do Nothing:**
   Doing nothing would force every embedder to make its own null/none type for JSON and other serialization formats (such as using a custom `lightuserdata` sentinel), which incurs performance costs and lacks falsiness in boolean evaluation contexts.
