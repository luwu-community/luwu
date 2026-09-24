# `@noinline` attribute

Status: Implemented (Flagged)

FFlag: LuwuNoinlineAttribute

## Summary

Adds a `@noinline` attribute that stops the compiler from inlining a function into its call sites at
`--!optimize 2`. It has no other effect: the function is compiled, called and typechecked exactly as
it would be without it.

## Motivation

Inlining is almost always what you want, which is why it happens automatically and there's no
`@inline` here. But it's not free, and there's currently no way to turn it off short of restructuring
your code until the cost model gives up:

- **Stack traces and `debug.info`.** An inlined call has no frame, so it doesn't appear in a
  traceback. When you're chasing a bug through a hot path this is exactly the frame you wanted.
- **Measuring.** Benchmarking a small function is awkward when the thing you're measuring gets
  inlined into the loop you're measuring it from. We hit this repeatedly while benchmarking classes.
- **Cost model disagreements.** The inline threshold is a heuristic. A function called from many
  sites can grow all of them, and occasionally you know better than the heuristic does.

We already had this as `@debugnoinline` behind `DebugLuauNoInline`, where nobody could use it. This
RFC promotes it to a real attribute with a real name.

## Design

`@noinline` is written like any other attribute, and is only allowed on functions that can actually
be inlined:

```luau
@noinline
local function expensive(x: number): number
    return x * 2
end

@noinline
const function also_fine(x: number): number
    return x * 3
end

local anonymous = @noinline function(x: number) return x * 4 end

class Cat
    @noinline
    public function meow(self): string
        return "meow"
    end
end
```

Everything above is resolved by the compiler at its call sites, so it's somewhere inlining can
happen. A **global** function is not: globals are never resolved statically, so they're never
inlined, and `@noinline` on one would silently do nothing. A `declare function` and a function type
have no body at all. Those are parse errors:

```luau
-- Attribute '@noinline' can only be applied to a local function, a const function,
-- a class method or a function expression
@noinline
function global_fn() end
```

We went with a parse error rather than a lint because it's the same mechanism every other attribute
already uses — each attribute declares the positions it allows, and the parser refuses it anywhere
else (see [attributes-for-types-variables-fields-classes.md](./attributes-for-types-variables-fields-classes.md)).
A lint would have been a second, weaker place to say the same thing.

Inlining only happens at `--!optimize 2`, so `@noinline` does nothing at O0 and O1. That's not a
special case in the implementation; there is simply nothing to turn off.

`@noinline` and `@native` are independent and can be written together: one decides whether the call
is inlined, the other whether the function is natively compiled.

`function t.m()` and `function t:m()` are rejected for the same reason a plain global is: the target
is a field of a global, so the call is never resolved statically. The `:` form also binds `self`,
which the compiler declines to inline regardless.

There are functions that can't be inlined for other reasons — variadics, functions using
`getfenv`/`setfenv`, and a function expression assigned to a local that is written somewhere:

```luau
-- inlined at O2
local a = function(x) return x + 1 end

-- not inlined: `b` is written, so the compiler won't resolve the call to this function
local b
b = function(x) return x + 1 end
```

`@noinline` is accepted in all of those and is simply redundant. We don't warn about it; the attribute
still correctly describes the intent, and a function can stop being variadic — or a local stop being
reassigned — later.

That second form is worth knowing about on its own: `local b; b = function ... end` is the shape that
already prevented inlining before this RFC, and it still does. `local a = function ... end` does not,
and never did.

### Type system

None. `@noinline` doesn't reach Analysis and doesn't affect any type.

### Editor support

`@noinline` is offered by autocomplete in the positions it allows and not in the others, which falls
out of the attribute registry without any editor-side change.

## Compatibility

Purely additive. `@noinline` was previously an invalid attribute name and a parse error, so no
program that works today changes meaning. Nothing is serialized into bytecode: the attribute is
consumed by the compiler when deciding `canInline`, and the resulting bytecode is bytecode that the
compiler could already produce. No version bump.

`@debugnoinline` and its `DebugLuauNoInline` flag are removed. It was a `Debug`-prefixed flag that was
off in every shipping build, so nothing that ran can have used it.

## Drawbacks

Inlining decisions are the compiler's job, and an attribute that overrides them can be wrong — a
`@noinline` written to work around one version's cost model may just be lost performance in the next.
It's also one more thing to explain about a function that reads fine without it.

Restricting it to inlinable positions means the set of allowed positions is tied to what the compiler
happens to resolve. If the compiler ever learns to inline something new, this RFC's list has to grow
with it, or `@noinline` will be rejected somewhere it would now be meaningful.

## Alternatives

**A lint instead of a parse error**, so `@noinline` could be written anywhere and merely warn where
it does nothing. Rejected: attributes already gate their own positions in the parser, and having one
attribute do it differently is worse than the slightly stricter rule.

**Do nothing**, and leave people to defeat inlining by making the function big enough that the cost
model refuses. This works and is what we did before, but it's a silly thing to ask of anyone.

**Also add `@inline`**, forcing inlining. Deliberately not proposed. Upstream Luau's
[No support for user inlining (yet)](https://github.com/luau-lang/rfcs/blob/master/docs/function-inlining.md)
decided against user-controlled inlining, and most of that reasoning is about being *told to inline*
— an `@inline` is a promise the compiler may not be able to keep, and it invites people to sprinkle
it around. Turning inlining off is different: it's always possible, always honoured, and its effect
is easy to describe.

## Prior art

Every compiled language with an inliner has this, and almost all of them spell it as a negative
annotation rather than a flag: GCC and Clang have `__attribute__((noinline))` / `[[gnu::noinline]]`,
MSVC has `__declspec(noinline)`, Rust has `#[inline(never)]`, and .NET has
`MethodImplOptions.NoInlining`. The motivations given are the ones above: debuggability, measurement,
and overriding a heuristic.
