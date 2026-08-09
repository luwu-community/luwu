# RFC: Thread State Change Hook (`userthreadstatechange`)

## Summary

Add a new C-level callback to `lua_Callbacks` called `userthreadstatechange` to notify the host after a `lua_resume` giving its state of either yielded (`LUA_YIELD`), successful completion (`LUA_OK`), or errors enabling for low-overhead tracking of thread state changes (and intermediate return values) globally.

## Motivation

Luau is commonly paired custom task schedulers (like `mluau/scheduler`, other async impls). To track when a thread yields, finishes or errors, schedulers currently patch the global `coroutine.resume` function with a wrapper that intercepts the call, records the result in their scheduler, and forwards the return values.

This approach has several flaws:

- Every scheduler needs to manually patch `coroutine` library by hand to correctly track coroutine.resume thread states
- Threads resumed directly via the C API (`lua_resume`) will not trigger the scheduler's patched `coroutine.resume` func making the scheduler entirely blind to these manual thread resumes.
- Crossing the C/Rust to Luau boundary to intercept and unpack arguments/results via `coroutine.resume` incurs extra FFI overhead that the native std functions do not have.

By providing a native VM hook, schedulers can deterministically track thread state transitions with negligible performance penalty in the false case.

## Design

A new field `userthreadstatechange` is added to `lua_Callbacks`:

```c
void (*userthreadstatechange)(lua_State* L, int status); 
```

The callback receives:
- `L`: The thread that just suspended or finished.
- `status`: The status code of the thread (`LUA_YIELD`, `LUA_OK`, `LUA_ERRRUN`, etc.).

*Note:* When this callback fires, the yielded values, returned results, or the thrown error (if `status` is a `LUA_ERR*`) will be available on the top of the thread's stack `L`, and can be inspected or extracted directly from there.

## Drawbacks

There are no notable drawbacks. It adds a single pointer to `lua_Callbacks` and a single branch to the end of thread resumptions that should be negligible when the feature is not used.

## Alternatives

- Do nothing and force every scheduler to manually modify coroutine lib
- Schedulers could manually iterate over all known threads every frame to check `lua_status()` using thread creation/deletion events to track threads. This is wildly inefficient and scales poorly with the number of threads.
