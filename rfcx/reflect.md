# Reflect

Status: In-progress
FFlag: This RFC implements a new optional library and as such does not have any fflags right now.

## Summary
Implement a reflect library for luwu code to parse Luwu code into AST+CST build code transforms/documentation generators on top of Luwu

## Motivation
It is currently not possible for embedders to directly parse Luwu code without manually using the C++ API or bringing in a external parser (which will probably not support all Luwu functionality, uses a different parser from the Luwu parser and also tends to be a heavy external dependency in LoC)

## What this RFC is *not*:

Before discussing the design of `reflect`, there are a few things that this RFC explicitly does not guarantee with the `reflect` feature:

- **AST/CST stabilization**: Luwu AST and CST changes frequently with every release (and sometimes every other commit!). This RFC does not seek to change the status quo here in any way.
- **`reflect` typing stabilization**: Under this RFC, `reflect`'s typing is allowed to change between Luwu versions to account for both parser changes as well as potential optimizations (as and when found) to speed up parsing/usage *without* it being considered a breaking change to Luwu. A future RFC may override this however but this RFC *only seeks to add `reflect` as a perma-unstable library*
- As an extension of point 2, the `type`/`typeof` of any AST node is not guaranteed to remain constant. Embedders are *strongly encouraged* to always review changes to `reflect`'s `types.luau` (and global type defs when that is added) and make changes accordingly when updating Luwu tooling using `reflect`. This also means that `reflect` userdata types (seen via ``typeof``) may not strictly correspond to Luwu parser types when doing so is beneficial in some way. For example, `AstAux` in current `reflect` acts as a catch-all for multiple miscellaneous parser types to reduce the number of userdata tags the library needs to reserve. Additionally, `reflect` userdata's may be changed/converted to/from tables in the future (or vice versa) without said change being considered breaking in any way.
- Method result caching is undefined: `reflect` does *not* guarantee that any method call made will cache/'memoize' the returned output. That is to say: `AstDocument:comments()` may or may not return the same table reference as a second call to `AstDocument:comments()`. This constraint enables for the underlying `reflect` implementation to be completely stateless
  - Note that some nodes may provide an opaque `id` field based on the actual underlying Parser struct allocation which *does* remain fixed across multiple Luau userdata pushes (e.g. `AstDocument:comments().id` is referentially equal to another call to the `AstDocument:comments().id`) 

## Design

TODO: Write this section once design is finalized

## Prior art

- Lute runtime has a `syntax` library to eagerly build the full table for a given srccode file (including both AST+CST). This makes it slow for cases where you only need a few fields of a node (or even just a few nodes instead of the entire tree). `reflect` attempts to be as lazy as possible in building out the AST/CST tree and only materializing fields as they are actually indexed into. This also adds the benefit that unknown/not-yet-supported nodes in `reflect` can still fallback to a generic `AstNode` userdata instead of being omitted entirely.
