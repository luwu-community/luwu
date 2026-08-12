# RFC: Faster Luau-managed references

## Summary

Add new `lua_refpool`, `lua_unrefpool` and `lua_getref` APIs to use a internal reference pool instead of the Luau registry (which is slow compared to thread stack / internal reference pool). This enables for embedders to drop hacks like thread stack etc. in favor of a native fast(er) reference API builtin to Luau and is one step towards improving the C API. The existing `lua_ref`, `lua_unref` and `lua_getref` are kept for backwards compatibility purposes.

## Motivation

Luau already has `lua_ref`, `lua_unref` and `lua_getref` as existing C APIs that merely work with the Luau registry (honestly should be `luaL_*` at that point?). Unfortunately, this may have performance drawbacks in certain degenerate table cases (table holes, interactions w/ the length operator for tables actually being boundary operator in Luau, performance etc.) leading to embedders like mluau choosing to instead hijack threads (or in some cases, handle multiple thread stacks) for the purpose of preventing GC.

This RFC proposes adding new Luau-specific `lua_refpool`, `lua_unrefpool` and `lua_getrefpool` to instead make use of a separate internal pool of references with all management of free lists etc. handled directly by the VM in a way that is as fast if not faster than hacks like abusing thread stacks. Embedders like `mluau` can then drop their internal hacks like auxiliary thread stacks etc. and just use `lua_refpool`, `lua_unrefpool` and `lua_getrefpool` directly without any fears of performance loss. Additionally, the ref pool is fully isolated from the registry making it easier for embedders like `mluau` to provide a safe registry API or even directly expose the registry as a normal table.

## Design

`lua_getrefpool` becomes an actual function instead of a macro. A reference pool is added consisting of an array of TValues and a free list (similar to what `mluau` rust bindings does in Rust but with the extra indirection of managing a thread stack that would be fully avoidable if implemented directly in the VM).

## Drawbacks

Increased implementation complexity

## Alternatives

- Do nothing. Threads can already be abused for fast Luau-managed references anyways