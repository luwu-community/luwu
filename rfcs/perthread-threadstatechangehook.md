# RFC: Per-Thread Thread State Change Hook (`userthreadstatechange`)

## Summary

RFC "Thread State Change Hook (`userthreadstatechange`)" added a global lua_Callback for all thread state changes. This RFC changes that to be at a per-thread level for efficiency purposes. The existing behavior can anyways be achieved through this RFC with the `userthread` lua_Callback if desired.

Additionally, this rfc fixes a soundness hole that `lua_resetthread` does not call the thread state change cb even though it is in fact a thread state change. This enables for schedulers to clean up after themselves upon a `coroutine.close` call.

## Motivation

See "Thread State Change Hook (`userthreadstatechange`)". Same motivation applies. This RFC scopes it directly to a thread

## Design

2 new C API's to replace the global `userthreadstatechange` lua_Callback

```c
typedef void (*lua_ThreadStateChangeCb)(lua_State* L, int status);
lua_ThreadStateChangeCb lua_getthreadstatechangecb(lua_State* L); // gets called when L returns from lua_resume)
void lua_setthreadstatechangecb(lua_State* L, lua_ThreadStateChangeCb cb); // gets called when L returns from lua_resume)
```

The callback receives:
- `L`: The thread that just suspended or finished.
- `status`: The status code of the thread (`LUA_YIELD`, `LUA_OK`, `LUA_ERR*`).

*Note:* When this callback fires, the yielded values, returned results, or the thrown error (if `status` is a `LUA_ERR*`) will be available on the top of the thread's stack `L`, and can be inspected or extracted directly from there. Also note that this callback will also be fired on `lua_resetthread`. If so, this callback will see a status of `LUA_OK` and a gettop of `0`. `lua_isthreadreset` can also be used in such a case.

## Drawbacks

There are no notable drawbacks. It adds a single pointer to `lua_State` (a structure which already includes per-thread `userdata` etc.) and a single branch to the end of thread resumptions that should be negligible when the feature is not used.

## Alternatives

- Do nothing and force FFI overhead for all threads incl. those the scheduler does not want/need to track.