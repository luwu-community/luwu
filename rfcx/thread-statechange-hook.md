# RFC: Thread State Change Hook (`userthreadstatechange`)

## Summary

Add a new C-level callback to `lua_Callbacks` that fires precisely when a thread returns control to its resumer upon yields (`LUA_YIELD`), successful completion (`LUA_OK`), or errors enabling for low-overhead tracking of thread state changes globally.

## Motivation

Luau is commonly paired custom task schedulers (like `mluau/scheduler`, other async impls). To track when a thread yields, finishes or errors, developers typically have to patch the global `coroutine.resume` function with a wrapper that intercepts the call, records the result in their scheduler, and forwards the return values.

This approach has significant flaws:

1. **Bypass Risk**: If a user script captures the original `coroutine.resume` before it is patched, or uses an alternative mechanism to resume threads, the scheduler fails to track the state change.
2. **C-API Blindspot**: Threads resumed directly via the C API (`lua_resume`) will not trigger the scheduler's `coroutine.resume` patch, making the scheduler entirely blind to them w/o embedders handling every `lua_resume` manually.
3. **Overhead**: Crossing the C/Rust to Luau boundary to intercept and unpack arguments/results via `coroutine.resume` incurs unnecessary serialization overhead.

By providing a native VM hook, schedulers can deterministically track thread state transitions with negligible performance penalty in the false case.

## Design

A new field `userthreadstatechange` is added to `lua_Callbacks` in `lstate.h`:

```c
struct lua_Callbacks
{
    // ... existing callbacks ...

    // gets called when L returns from lua_resume (yielding, finishing, or erroring)
    void (*userthreadstatechange)(lua_State* L, int status); 
};
```

The callback receives:
- `L`: The thread that just suspended or finished.
- `status`: The integer status code of the thread (`LUA_YIELD`, `LUA_OK`, `LUA_ERRRUN`, etc.).

*Note:* When this callback fires, the yielded values, returned results, or the thrown error (if `status` is a `LUA_ERR*`) will be available on the top of the thread's stack `L`, and can be inspected or extracted directly from there.

## Drawbacks

There are no notable drawbacks. It adds a single pointer to `lua_Callbacks` and a single branch to the end of thread resumptions.

## Alternatives

- **Continue patching `coroutine.resume`**: Developers continue paying the FFI boundary cost and risking bypasses or C-API blindspots.
- **Polling / `lua_status` checks**: Schedulers could manually iterate over all known threads every frame to check `lua_status()`. This is wildly inefficient and scales poorly with the number of suspended threads.
