# RFC: 'Fat' C Closures (C Closures with Data)

## Summary

Add a new fat c closure feature to Luau to reduce the overhead of C->Luau boundary in mluau

## Motivation

Embedders like `mluau` currently need to use userdata (w/ a metatable etc.) and upvalues for every stateful closures. This has the downside of making C functions in luau a fair bit slower, increases GC pressure as the GC has to both handle the closure itself and the userdata allocation and less ergonomic for developers (involving needing upvalues etc.). `mluau` has a ton of infrastructure here regarding internal userdata with dtors which could be dropped while also increasing overall performance in general.

This RFC as such proposes the addition of 'fat' C closures (or C Closures with Data). A stateful closure that would otherwise require a full userdata + 1 upvalue + closure can now be directly done as a single closure reducing GC pressure (1 gc object vs 2) and improving memory layout + cacheability.

## Design

A new C API will be added to Luau for pushing c closures with data and getting out the data from the running closure:

```c
    typedef void (*lua_ClosureWithDataFree)(lua_State* L, void* data, size_t sz);

    // Push a new c closure with data returning a void* data pointer in which the raw function state can be 
    // directly saved to.
    LUA_API void* lua_pushcclosurewithdatak(
        lua_State *L, 
        lua_CFunction fn, 
        const char *debugname, 
        lua_Continuation cont, 
        size_t size, 
        lua_ClosureWithDataFree dtor
    );

    // Returns the void* data pointer of the currently executing c closure
    LUA_API void* lua_getcclosuredata(lua_State *L);
```

Like userdata, the data stored in a closure with data (herein called 'fat' C closures) are fully opaque and will not be scanned or traced by GC (embedders will need to make sure any references are stored in either the registry or a thread stack etc). Additionally, the data will be inline to the closure hence enabling for better memory layout(ing?)/caching.

## Drawbacks

Implementation complexity. While this shouldn't have any performance loss for existing C closures which don't use the feature, it is a new feature to optimize something that already exists.

## Alternatives

- Do nothing. Userdata (with dtors) and upvalues can already be used to implement this feature, just at the cost of more memory allocations and increased GC overhead.