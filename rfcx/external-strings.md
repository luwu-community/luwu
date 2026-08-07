# External Strings

## Summary

Add support for externally managed/allocated strings to the Luau VM, allowing embedders to create zero-copy Luau strings that fully participate in all existing Luau features (like interning etc.) and behave identically to normal strings to the user.

## Motivation

Luau's `string` type represents immutable byte sequences. Currently, creating a string in Luau requires copying the bytes from the host application into a VM-managed allocation. In embedding scenarios, it is common for the host to already possess large strings (such as errors w/ stack traces, data from a JSON file etc.). 

While external buffers do exist in Luau now, buffers cannot be manipulated by the `string` library (and other string-related operations/infrastructure) nor can they easily be used as table keys etc. Furthermore, it is expected/idiomatic for certain things in Luau to be a `string` and not a `buffer` (error tracebacks, strings in a json etc.). 

External strings (which also have existing precedence in Lua 5.5) allows embedders to wrap these existing string allocations without copying while maintaining full access to existing string infrastructure (`string` library, tables w/ string keys etc.)

## Design

### C API Additions

The Luau C API is expanded with the following functions and types (similar to external buffers except w/o the mode flag as Luau strings are *always* immutable):

    typedef void (*lua_StringFree)(lua_State* L, const char* data, size_t sz, void* userdata);

    LUA_API const char* lua_pushexternalstring(
        lua_State* L, 
        const char* data, 
        size_t len, 
        void* userdata, 
        lua_StringFree free_cb
    );

    LUA_API int lua_isstringexternal(lua_State* L, int idx);
    LUA_API void* lua_getstringexternaluserdata(lua_State* L, int idx);

* `lua_pushexternalstring` creates a new string that wraps the `data` pointer of length `len`. Like Lua 5.5's design of external strings, the string provided must be null-terminated (i.e. `data[len] == '\0'`), even though `len` does not include the null terminator. This is required to maintain compatibility with C APIs that use `lua_tostring`.
* `userdata` is an opaque pointer that will be passed to `free_cb` alongside the string information.
* `free_cb` is an optional callback invoked when the string object is garbage collected.
* `lua_isstringexternal` returns `1` if the string at the given index is an external string, and `0` otherwise.
* `lua_getstringexternaluserdata` returns the opaque `userdata` pointer associated with the string, enabling operations like retrieving underlying host resource structures.

### Interning & Deduplication Semantics

Unlike buffers, Luau strings are always interned (deduplicated) in a global string table so that equality comparisons can be performed via fast pointer equality. Additionally, short strings may be assigned "atoms" for fast property lookups. External strings fully participate in this interning and atom process. 

Because of interning and atoms, garbage collection (and `free_cb`) may not happen when you expect it:

- If a matching string is already in the string table when you create an external string, your new allocation is deduplicated and the `free_cb` will be called prior to returning from `lua_pushexternalstring`.

- Also, even if your Luau code loses all references to the external string, the string might be kept alive by the VM internals (as an atom etc.) for longer than anticipated, delaying the `free_cb`.

When `lua_pushexternalstring` is called:

1. The external data is hashed and the string table is checked.

2. If an identical string (either inline or external) already exists in the VM, that existing string is returned.

3. **Crucially**, if a duplicate is found, the provided `free_cb` is invoked (before `lua_pushexternalstring` returns) on the new external data, as the new allocation is not needed and the previous string is used instead (whether that be external or not).

From the perspective of Luau code, external strings are completely indistinguishable from normal strings. They can be concatenated, used as table keys, and queried with `string.sub` identically.

### Immutability and Undefined Behavior

Luau strings are strictly immutable. When an external string wraps host memory, the host **must** guarantee that the underlying bytes are never modified for the lifetime of the string object. 

Because strings are interned and hashed, modifying the underlying bytes of an active external string violates VM invariants. Doing so will result in **Undefined Behavior (UB)**. If you need mutable shared memory, use External Buffers instead.

## Drawbacks

* **Performance Overhead for Normal Strings:** To support external strings without increasing the memory footprint of normal strings, `getstr()` requires a conditional check (`is_external`) to determine whether the data pointer is inline or external. While this introduces a minor branch penalty to string lookups, benchmarks have showed that this results in a negligible performance change in practice.

## Alternatives

* **Userdata:** Expose host data through a `userdata`. This prevents the data from being used in standard string library functions (`string.sub`, `string.match`), prevents it from being easily concatenated, and prevents it from being used effectively as table keys (due to lack of interning).
