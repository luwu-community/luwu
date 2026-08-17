# RFC: Faster Luwu-managed references

## Summary

Add new `lua_refpool`, `lua_unrefpool` and `lua_getrefpool` APIs to use an internal reference pool instead of the Luwu registry (which is slow compared to a thread stack or internal reference pool). This allows embedders to drop hacks like thread stacks in favor of a faster native reference API built into Luwu and is one step toward improving the C API. The existing `lua_ref`, `lua_unref` and `lua_getref` APIs are kept for backwards compatibility.

## Motivation

Luwu already has `lua_ref`, `lua_unref` and `lua_getref` as existing C APIs that merely work with the Luwu registry (honestly should be `luaL_*` at that point?). Unfortunately, this may have performance drawbacks in certain degenerate table cases (table holes, interactions w/ the length operator for tables actually being a boundary operator in Luwu, performance etc.), leading embedders like mluau to instead hijack threads (or, in some cases, handle multiple thread stacks) to prevent GC.

This RFC proposes adding the Luwu-specific `lua_refpool`, `lua_unrefpool` and `lua_getrefpool` APIs backed by a separate internal reference pool, with free-list management handled directly by the VM. This should be at least as fast as hacks like abusing thread stacks. Embedders like `mluau` can then drop auxiliary thread stacks and use `lua_refpool`, `lua_unrefpool` and `lua_getrefpool` directly without sacrificing performance. The reference pool is also fully isolated from the registry, making it easier for embedders like `mluau` to provide a safe registry API or expose the registry directly as a normal table.

## Design

`lua_getrefpool` becomes an actual function instead of a macro. A reference pool is added consisting of an array of TValues and a free list (similar to what the `mluau` Rust bindings do, but without the extra indirection of managing a thread stack).

## Drawbacks

Increased implementation complexity

## Alternatives

- Do nothing. Threads can already be abused for fast Luwu-managed references.
