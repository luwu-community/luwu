# Trust Type Annotations (`--!trust`)

Status: WIP, partially implemented

FFlags:

- DebugLuwuCompilerTrustsTypeAnnotations

## Summary

Adds a new compiler directive (hot comment) to allow for type annotation-driven inlining. When a module contains the
`--!trust` hot comment, the Luwu compiler may optimize method calls to inline them when profitable, solely based upon
the (retrieved) type annotation. This allows for new behavior changes in O2 as long as incorrect runtime usage leads
to a runtime error, and never running incorrect code.

## Motivation

### Classes

Classes can currently take advantage of *proof* checks to allow inlining of methods of objects. This is a huge performance win, but requires
`assert(class.isinstance(obj, Class))` or `if class.isinstance(obj, Class)` to *prove* every branch. This may be feasible for functions that
take one or two objects of a class, but quickly becomes missable when users compose objects of other classes inside their classes.

### The evil string metatable

Right now every method call on a string is significantly slower than a call using the builtin `string` library method alternatives. Due to this, it's been a common piece of Luau advice to "never use the string metatable". Using the string library everywhere forces uglier, inside-out code that just wouldn't be needed at all if string methods were faster and users didn't avoid using them. The trust directive unlocks FASTCALL optimizations for functions that annotate parameters as strings.

## Design

The compiler classifies bindings thought to be of an expected type as *proven* or *trusted*.

A *proven* binding is statically *known* to be of the type expected, and we can skip all runtime checks gating optimizations for it.
For example, `typeof(s) == "string"` *proves* that `s` is a string until `s` is reassigned. Similarly, `class.isinstance(o, Class)` or `const o = Class()` *proves* that `o` is an object of the specified class until `o` is reassigned. Any optimizations taken for *proven* bindings apply regardless of the `--!trust` directive, since they can apply safely to any existing code that doesn't use `getfenv` or `setfenv` (in O2).

A *trusted* binding is statically known to have been annotated as a type we can optimize for. We expect Luwu code to be reasonably well-typed and for Luwu users to make significant use of type annotations, so we allow an option for users to put trust in their type annotations to make their code run faster. This allows static resolution of those annotations for inlining, redirecting to the correct method or library function, etc.

When the `--!trust` directive is enabled in a module, we will use annotations from the following locations to determine optimizations:

- Function or class parameters
- Function return types
- Local or const bindings
- Class field parameters
- Standard library functions and methods that return the expected type (or an option of it)

We will consider if the annotation is typed optionally (`Thing?` or `Thing | none`), and can mark the binding as *trusted* after a truthy-check.

If this is a user-facing feature that needs type system (`Analysis`) and/or editor (`luwu-lsp`) support, also describe the relevant type system and editor design in subsections named `### Type system` and/or `### Editor support` or similar.

## Compatibility

When enabled, runtime behavior *will* change between O1 and O2 for incorrectly-typed programs. Those programs will now throw runtime errors.

Additionally, this would be the first new hot comment added to Luwu that doesn't exist in upstream Luau, meaning that using this feature with Luau Language Server without the luwu-lsp binary will cause lints.

## Drawbacks

Why should we *not* do this? These should note the drawbacks of the current design/implementation, and not overlap with Alternatives.

## Alternatives

What other designs have been considered? What is the impact of not doing this?

## Future work (optional)

How can we build upon this feature in the future? Will this feature be expanded in a future RFC with more semantics? How will this interact with other wanted but not yet implemented features?

## Prior art (required only for syntax and semantics changing RFCs)

How has this feature been influenced by other programming languages, theory, and practical use?

## Implementation details (optional)

Any relevant implementation details the team should know about the proposed implementation, including optimizations, edge cases, and drawbacks. If the implementation details are all very important and need to be mentioned, consider putting the RFC specification in its own directory in `rfcs/` with an `implementation.md`.
