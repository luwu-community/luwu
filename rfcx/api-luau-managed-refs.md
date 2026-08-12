# RFC: Faster Luau-managed references

## Summary

Switch the existing `lua_ref`, `lua_unref` and `lua_getref` APIs to use a internal reference pool instead of the Luau registry (which is slow compared to thread stack / internal reference pool). This enables for embedders to drop hacks like thread stack etc. in favor of a native fast(er) reference API builtin to Luau and is one step towards improving the C API.

## Motivation

Luau already has `lua_ref`, `lua_unref` and `lua_getref` as existing C APIs that merely work with the Luau registry (honestly should be `luaL_*` at that point?). Unfortunately, this may have performance drawbacks in certain degenerate table cases (table holes, interactions w/ the length operator for tables actually being boundary operator in Luau, performance etc.) leading to embedders like mluau choosing to instead hijack threads (or in some cases, handle multiple thread stacks) for the purpose of preventing GC.

This RFC proposes changing the existing Luau-specific `lua_ref`, `lua_unref` and `lua_getref` to instead make use of a separate internal pool of references with all management of free lists etc. handled directly by the VM in a way that is as fast if not faster than hacks like abusing thread stacks. As this is merely a optimization of the existing Luau reference API, existing users should be unaffected (as long as they were using `lua_getref` for getting references instead of reading the Luau registry). Embedders like `mluau` can then drop their internal hacks like auxiliary thread stacks etc. and just use `lua_ref`, `lua_unref` and `lua_getref` directly without any fears of performance loss.

## Design

`lua_getref` becomes an actual function instead of a macro, a reference pool is added consisting of an array of TValues and a free list (similar to what `mluau` rust bindings does in Rust but with the extra indirection of managing a thread stack that would be fully avoidable if implemented directly in the VM). While this technically breaks the C API contract for users relying on `lua_ref` using the Luau registry, our fork of Luau doesn't promise full compat in the C API anyways and has already made (or is in the process of making) breaking changes anyways.

## Drawbacks

Minor breaking C API change for the benefit of making references in Luau more performant without needing to hijack threads for that purpose. References made with the Luau reference API will no longer be on the Luau registry table (which may be either a good or a bad thing for embedders)

## Alternatives

- Do nothing. Threads can already be abused for fast Luau-managed references anyways