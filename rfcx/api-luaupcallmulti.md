# RFC: Pcall error handler with multiple return values
FFlag: LuauPcallMulti

## Summary

Add a new `lua_pcallmulti` API to allow `lua_pcall` to return multiple values. It functions identically to `lua_pcall` but allows for embedder error handlers to return multiple values (error value + traceback for example) directly through the Luau stack w/o needing to allocate a new table or do bitpacking with refpool etc.

## Motivation

Mluau first-class error handlers currently has to do the following hack with lightuserdata to be able to get both the error value and the traceback at the same time from `lua_pcall`:

```rust
pub(crate) const FUNC_CALL_ERROR_TB_LUD: c_int = 123;
pub(crate) unsafe extern "C-unwind" fn func_call_error_traceback(state: *mut ffi::lua_State) -> c_int {
    // Luau calls error handler for memory allocation errors, skip it
    // See https://github.com/luau-lang/luau/issues/880
    if MemoryState::limit_reached(state) {
        return 0;
    }

    if ffi::lua_checkstack(state, 3) == 0 {
        // If we don't have enough stack space to even check the error type, do
        // nothing so we don't risk shadowing a rust panic.
        return 1;
    }

    let err_ref_id = ffi::lua_refpool(state, -1);
    if ffi::lua_checkstack(state, ffi::LUA_TRACEBACK_STACK) != 0 {
        ffi::luaL_traceback(state, state, std::ptr::null(), 0);
    } else {
        // Fallback if we can't allocate stack space
        ffi::lua_pushstring(state, cstr!(""));
    }
    let tb_ref_id = ffi::lua_refpool(state, -1);
    let packed_ref_ids = ((tb_ref_id as u64) << 32) | (err_ref_id as u32 as u64);
    ffi::lua_pushlightuserdatatagged(state, packed_ref_ids as *mut std::ffi::c_void, FUNC_CALL_ERROR_TB_LUD);
    1
}
```

While clever, this hack does end up using way more FFI calls (`lua_refpool` + getref/unref etc.) and being able to return both error value+tb (or multiple values in general) from a `pcall` is likely a limitation that others have experienced as well.

## Design

`lua_pcallmulti` is added with the same signature as `lua_pcall` but with support for returning multiple values in error handler. This allows for directly returning multiple error values from a `lua_pcallmulti` handler. The semantics of `lua_pcall` remains unchanged to avoid breaking existing code. 

## Drawbacks

Increased implementation complexity. Need to copy paste `pcall`'s implementation

## Alternatives

- Do nothing. Refpool w/ integer packing can be used to emulate this anyways at the cost of worse performance.