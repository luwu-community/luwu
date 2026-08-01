# Null Primitive

Status: Proposed
Author: @cheesycod
FFlag: `LuauNullPrimitive`

## Summary

Introduce `null` as a new first-class primitive value and built-in global in Luau, representing an intentional "no value" sentinel distinct from `nil`. Unlike `nil`, which denotes the absence of a value or an unassigned state, `null` is an explicit primitive of type `"null"`. It is falsy in boolean evaluation contexts, compares equal only to itself (`null == null` is `true` while `nil ~= null`), and does not delete table entries or create array holes when stored as a table value (`t[k] = null`).

## Motivation

In Luau and Lua, `nil` serves two conflicting roles: indicating the absence of a value (such as an uninitialized variable, omitted function argument, or non-existent table key) and acting as an intentional empty sentinel in data structures and APIs. Because assigning `t[k] = nil` removes key `k` from table `t`, developers cannot store an explicit null sentinel in a table without resorting to workaround objects (such as `local NULL = {}` or unique userdata sentinels).

In standard Luau, assigning `nil` to an index within a sequential array creates a "hole" (e.g., `{ 1, nil, 3 }`). The `#` length operator on sparse tables is defined to return an arbitrary array boundary, making `#t` and sequential iteration via `ipairs` unpredictable when representing lists with missing or nullable entries. With the `null` primitive, array elements can be explicitly set to `null` (`local list = { 1, null, 3 }`). Because `null` is a stored value and does not leave holes, `#list` reliably evaluates to `3`, and sequential iteration visits every index without breaking sequence invariants.

## Design

### Global Value and Type

When `LuauNullPrimitive` is enabled, `null` is exposed as a built-in global value representing the singleton value of the `null` primitive type.

1. **Primitive Type:**
   A new primitive value type `null` is introduced.
2. **Type Function:**
   The standard library `type(null)` returns the string value `"null"`. `typeof(null)` also returns `"null"`.

### Runtime Semantics and Comparison

1. **Falsiness:**
   In all boolean evaluation contexts (`if`, `elseif`, `while`, `until`, and logical operators `and`, `or`, `not`), `null` evaluates as **falsy**. The set of falsy values in Luau is exactly `false`, `nil`, and `null`. All other values are truthy.
   ```lua
   assert(not null == true)
   assert((null or "fallback") == "fallback")
   assert((null and "unreachable") == null)
   ```
2. **Equality (`==`, `~=`):**
   - `null == null` evaluates to `true`.
   - `null ~= nil` evaluates to `true` (`null == nil` is `false`).
   - `null` is not equal to any other value (`false`, `0`, `""`, `{}`).
3. **Relational Comparison (`<`, `<=`, `>`, `>=`):**
   Attempting to compare `null` with any value (including `null` itself) using relational operators raises a runtime error, consistent with relational comparisons on `nil` and `boolean`.

### Table Storage and No-Hole Semantics

Unlike `nil`, storing `null` in a table preserves the key-value pair:

1. **Dictionary Storage:**
   Assigning `t[k] = null` sets the value associated with `k` to `null`. It does not remove `k` from `t`.
   ```lua
   local dict = { a = 10, b = null }
   assert(dict.a == 10)
   assert(dict.b == null)
   assert(dict.c == nil)

   -- Assigning nil deletes the entry; assigning null overwrites the value
   dict.a = null
   assert(dict.a == null)
   ```
2. **Array Sequences and Length (`#`):**
   Arrays containing `null` values are contiguous sequences without holes. The `#` operator counts entries containing `null` as valid elements.
   ```lua
   local list = { "first", null, "third" }
   assert(#list == 3)
   assert(list[2] == null)

   -- Modifying an element to null does not truncate the sequence
   list[3] = null
   assert(#list == 3)

   -- Setting to nil deletes the element and truncates the sequence length
   list[3] = nil
   assert(#list == 2)
   ```
3. **Table Iteration:**
   - `pairs(t)` and `next(t, k)` visit all existing keys, including keys whose values are `null`.
   - `ipairs(t)` iterates sequentially from index `1` up to `#t`, yielding `(i, null)` for elements set to `null`, terminating only when an index evaluates to `nil`.

### C API Additions

The C API is expanded with the following definitions and functions:

```c
#define LUA_TNULL 10

LUA_API int lua_isnull(lua_State* L, int idx);
LUA_API void lua_pushnull(lua_State* L);
```

- `lua_type(L, idx)` returns `LUA_TNULL` for `null` values.
- `lua_typename(L, LUA_TNULL)` returns `"null"`.
- `lua_toboolean(L, idx)` returns `0` when the value at `idx` has type `LUA_TNULL`.
- `lua_isnull(L, idx)` returns `1` if the value at `idx` is `LUA_TNULL`, and `0` otherwise.

### Standard Library

1. **Basic Library:**
   - `type(null)` and `typeof(null)` return `"null"`.
   - `tostring(null)` returns `"null"`.

## Drawbacks

- **Global Shadowing:**
   Because `null` is a built-in global value, code that explicitly declares a local variable or function parameter named `null` (`local null = ...`) will shadow the built-in `null` primitive within that scope.
- **Two Empty Primitives:**
   Having both `nil` and `null` introduces two concepts for emptiness, which may increase cognitive load. Developers must learn the distinction between absence/uninitialized state (`nil`) and explicit empty/sentinel state (`null`).

## Alternatives

- **Reserved Keyword:**
   Make `null` a reserved keyword in the language grammar rather than a global value. This was rejected because it would be a syntactic breaking change for existing scripts that use `null` as an identifier or table field name.
- **Do Nothing:**
   Doing nothing would force every embedder to make its own null type for JSON and other serialization formats (such as using a custom `lightuserdata` sentinel), which incurs performance costs and lacks falsiness in boolean evaluation contexts.

