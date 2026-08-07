# External Strings

## Summary

Add support for externally managed/allocated strings to the Luau VM, allowing host applications to create Luau strings that wrap existing immutable memory without copying. These strings are fully interned into the string table and behave identically to normal strings from Lua's perspective.

## Motivation

Luau's `string` type represents immutable byte sequences. Currently, creating a string in Luau requires copying the bytes from the host application into a VM-managed allocation. In embedding scenarios, it is common for the host to already possess large string payloads (e.g. database query results, large JSON documents, or memory-mapped files). 

By introducing external strings, we allow embeddings to wrap these existing memory allocations without copying, while still allowing the Luau garbage collector to accurately track the memory footprint of externally allocated data. This follows the same pattern and motivation as the recently added External Buffers, but tailored for string interning semantics.

## Design

### C API Additions

The Luau C API is expanded with the following functions and types:

    typedef void (*lua_StringFree)(lua_State* L, const char* data, size_t sz, void* userdata);

    LUA_API const char* lua_newexternalstring(
        lua_State* L, 
        const char* data, 
        size_t len, 
        void* userdata, 
        lua_StringFree free_cb
    );

    LUA_API int lua_isstringexternal(lua_State* L, int idx);
    LUA_API void* lua_getstringexternaluserdata(lua_State* L, int idx);

* `lua_newexternalstring` creates a new string that wraps the `data` pointer of length `len`. 
* `userdata` is an opaque pointer that will be passed to `free_cb` alongside the string information.
* `free_cb` is an optional callback invoked when the string object is garbage collected.
* `lua_isstringexternal` returns `1` if the string at the given index is an external string, and `0` otherwise.
* `lua_getstringexternaluserdata` returns the opaque `userdata` pointer associated with the string, enabling operations like retrieving underlying host resource structures.

### Interning & Deduplication Semantics

Unlike buffers, Luau strings are always interned (deduplicated) in a global string table so that equality comparisons can be performed via fast pointer equality. Additionally, short strings may be assigned "atoms" for fast property lookups. External strings fully participate in this interning and atom process. 

Because of interning and atoms, garbage collection (and `free_cb`) may not happen when you expect it:

- If a matching string is already in the string table when you create an external string, your new allocation is deduplicated and the `free_cb` will be called prior to returning from `lua_newexternalstring`.

- Also, even if your Luau code loses all references to the external string, the string might be kept alive by the VM internals (as an atom etc.) for longer than anticipated, delaying the `free_cb`.

When `lua_newexternalstring` is called:

1. The external data is hashed and the string table is checked.

2. If an identical string (either inline or external) already exists in the VM, that existing string is returned.

3. **Crucially**, if a duplicate is found, the provided `free_cb` is invoked (before `lua_newexternalstring` returns) on the new external data, as the new allocation is not needed and the previous string is used instead (whether that be external or not).

From the perspective of Luau code, external strings are completely indistinguishable from normal strings. They can be concatenated, used as table keys, and queried with `string.sub` identically.

### Immutability and Undefined Behavior

Luau strings are strictly immutable. When an external string wraps host memory, the host **must** guarantee that the underlying bytes are never modified for the lifetime of the string object. 

Because strings are interned and hashed, modifying the underlying bytes of an active external string violates VM invariants. Doing so will corrupt the string table hash buckets, break string equality, and result in **Undefined Behavior (UB)**. If you need mutable shared memory, use External Buffers instead.

## Drawbacks

* **Memory Overhead for Normal Strings:** To ensure that accessing string data (via the internal `getstr()` macro) remains branchless and zero-cost for performance, we introduce a single pointer overhead (8 bytes on 64-bit platforms) to *all* string objects. Normal inline strings will use this pointer to point to their own inline memory, while external strings will use it to point to the host memory. This represents a minor memory increase across the board for string-heavy workloads.

## Alternatives

* **Userdata:** Expose host data through a `userdata`. This prevents the data from being used in standard string library functions (`string.sub`, `string.match`), prevents it from being easily concatenated, and prevents it from being used effectively as table keys (due to lack of interning).
