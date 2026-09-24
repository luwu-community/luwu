# Allow attributes to be used on types, variables, fields, and classes

Status: Implemented (Flagged)

FFlag: DebugLuwuBetterAttributes

> Adapted, with permission, from [luau-lang/rfcs#147](https://github.com/luau-lang/rfcs/pull/147) by
> [@gaymeowing](https://github.com/gaymeowing) (quaywinn). Her text is kept as written except where
> this fork needs something different; each such change is called out below. The upstream PR is
> unmerged, so per [rfcs/README.md](./README.md) this document records that we implemented it and who
> wrote it.
>
> **Implementation status.** Implemented behind `DebugLuwuBetterAttributes`: the syntax, the
> per-position rules, the parse-time diagnostics, and the `DeprecatedApi` lint for variables, type
> aliases, classes, class fields, table type fields and table constructor entries -- including the
> rule that a field's attribute takes priority over the value's.
>
> Two positions parse and store their attributes but are not linted yet. An attribute on a
> **parameter** has no agreed meaning to report on: deprecating a parameter is a message for callers,
> and there is no per-argument channel to deliver it through, so inventing one was left for a follow-up
> rather than guessed at. An attribute on an **assignment** (`@deprecated m.dog = "woof"`) is likewise
> unreported; the field it names usually belongs to a table whose type is inferred, and marking it
> needs a decision about which declaration wins when the same field is assigned more than once.
> Indexers take attributes for consistency but have no member name to report against.
>
> The old type solver is covered for table *type* fields only; table constructor entries and class
> fields are marked in the new solver, which is the one in use.

## Summary

Allows [Attributes](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions.md)
to be applied to types, variables, table fields, classes, class fields, and parameters in Luwu.

## Motivation

Currently attributes can only be applied to functions, this means types cannot be marked as
[`@deprecated`](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attribute-functions-deprecated.md).
Nor can variables, table fields, classes, or class fields be marked as `@deprecated`.

## Design

Note: With this RFC the internals for attributes will be able to state they can be applied to only
certain bits of syntax, so one can't apply
[`@native`](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attribute-functions-native.md)
to a type, variable, class, or field and have it work.

*(Amendment: the upstream RFC leaves the mechanism unspecified. In Luwu each attribute declares the
set of positions it allows as one value in the parser's attribute registry, and the parser refuses it
anywhere else — see [Where each attribute is allowed](#where-each-attribute-is-allowed).)*

### Attribute parameters use the bracketed form

*(Amendment. The upstream draft writes parameterized attributes bare, as `@deprecated { use = "dog" }`.
That spelling cannot be parsed, and upstream ruled it out deliberately in
[Function Attribute Parameters](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions-parameters.md):
`@[]` exists "to facilitate syntax evolution without introducing parsing ambiguity". Every example
below therefore uses `@[deprecated { ... }]`. The ambiguity is concrete for exactly the positions this
RFC adds; see [Alternatives](#alternatives).)*

A bare `@name` never takes arguments. `@[name arg]` is how arguments are passed, in every position.

Because writing them bare is the likeliest mistake -- it is how other languages spell it, and how the
upstream draft was written -- it gets a diagnostic of its own rather than surfacing as whatever the
next token failed to be:

```
Attribute arguments must be written as '@[deprecated ...]'; a bare '@deprecated' cannot take arguments
```

The arguments are still parsed and attached, so the attribute means what was intended and that is the
only error. This applies wherever the attributed thing must start with a keyword or a name; inside a
table, value or type, an entry may legitimately *be* a table or a string, so `{ @deprecated {1, 2} }`
is an attributed entry and nothing is reported. A `(` is never read as arguments anywhere, since an
attributed function type is existing syntax and the two would be ambiguous -- which is the reason
arguments are bracketed in the first place.

Editor completion offers the attributes that are legal in the position being written, taken from the
same registry that validates them, so it never suggests one the parser will then reject.

### Variables

This exists as it could be useful for if for instance a constant is defined as a local variable and
then exported:

```luau
@[deprecated { use = "dog" }]
local puppy = "whimper"

return table.freeze({
    puppy = puppy,
    dog = "woof",
})
```

Although this will cause the return in the module to be linted, but this lint won't be passed on to
consumers of the module:

Module:

```luau
-- DeprecatedApi: Variable 'puppy' is deprecated, use 'dog' instead Luwu(22)
return table.freeze({
    puppy = puppy,
    dog = "woof",
})
```

Consumer:

```luau
-- No lint occurs on the import
local module = require("@module")

-- DeprecatedApi: Member 'puppy' is deprecated, use 'dog' instead Luwu(22)
print(module.puppy)
```

### Tables

```luau
local pet_sounds = {
    @deprecated cat = "meow"
}

@deprecated module.dog = "woof"

@deprecated
module.parrot = "cracker, now"

return table.freeze(pet_sounds)
```

#### Table Fields

If the value of a field has attributes, those will be merged with the attributes defined on the field.
With the attributes on the field having priority over the attributes on the value.
For example: if both the value and the field have a `@deprecated` attribute, the `@deprecated`
attribute on the value will be ignored.
With the `@deprecated` attribute on the field being used instead.

```luau
@[deprecated { reason = "cat is a more modern API" }]
local function get_cat_sound()
    return "meow"
end

-- DeprecatedApi: Function 'get_cat_sound' is deprecated, cat is a more modern API Luwu(22)
local bad_module = table.freeze({
    get_cat_sound = get_cat_sound,
})

-- No lint occurs, because the @deprecated attribute of the 'get_cat_sound' function has been overridden
-- by the @deprecated attribute of the field 'get_cat_sound'.
local module = table.freeze({
    @[deprecated { use = "cat" }] get_cat_sound = get_cat_sound,
    cat = "meow"
})

-- DeprecatedApi: Member 'get_cat_sound' is deprecated, use 'cat' instead Luwu(22)
module.get_cat_sound()
```

### Types

Types work similarly to [Variables](#variables) and [Tables](#tables), except being types.

```luau
@deprecated
type Puppy = "whimper"

-- DeprecatedApi: Type 'Puppy' is deprecated Luwu(22)
type CanineSounds = {
    puppy: Puppy,
    dog: "woof",
}

-- No lint occurs, because the @deprecated attribute of the type 'Puppy' has been overridden
-- by the @deprecated attribute of the field 'puppy'.
type PetSounds = {
    -- Just like with tables, entries have their attributes merged with the values attributes.
    @[deprecated { use = "dog" }] puppy: Puppy,
    dog: "bark",
    cat: "mrrp",
}
```

*(Amendment: an attribute written before a table type's entry attaches to that entry, and comes before
a `read`/`write` modifier, so it reads in the order it is written. An indexer takes them too, and
since an array-like `{T}` desugars to `{[number]: T}`, an attribute on one attaches to that indexer.)*

```luau
type Registry = {
    @deprecated read name: string,
    @deprecated [string]: number,
}
```

### Classes

```luau
@[deprecated { use = "Dog" }]
class Puppy
    public sound: string
end

class Dog
    public sound: string
end

-- DeprecatedApi: Class 'Puppy' is deprecated, use 'Dog' instead Luwu(22)
local puppy = Puppy {
    sound = "whimper",
}
```

Class fields work the same as [Table Fields](#table-fields). Attributes placed before a `public` field
declaration are attached to that class member:

```luau
class PetSounds
    @[deprecated { use = "dog" }]
    public puppy: string

    public dog: string
end

local sounds = PetSounds {
    puppy = "whimper",
    dog = "woof",
}

-- DeprecatedApi: Member 'puppy' is deprecated, use 'dog' instead Luwu(22)
print(sounds.puppy)
```

*(Amendment: an attribute goes on the line above the member it annotates, which is how they are written
everywhere else in the language. When one is written inline it goes in front of the access specifier,
not after it -- an attribute is not a modifier keyword and should not be dressed up as one.)*

```luau
class PetSounds
    @deprecated
    public puppy: string

    @deprecated private const cat: string = "mrrp"
end
```

Luwu also accepts an attribute after the access specifier (`public @deprecated dog: string`), since a
method already allowed it, but not on both sides at once -- that would leave a reader scanning in two
places for them. `const` counts as part of the specifier group. An attribute never changes what the
qualifiers next to it mean: a `@deprecated private const` field is still private and still const.

### Parameters

*(New in Luwu; not part of the upstream RFC.)*

A function parameter takes attributes, written before its name:

```luau
local function speak(@[deprecated { use = "sound" }] noise: string, sound: string)
    return if noise ~= nil then noise else sound
end
```

A class's [primary constructor](./classes/classes.md) parameters take them too, and because such a
parameter also declares a field, its attributes may sit on either side of the access specifier exactly
as a field's may:

```luau
class SshKey(@deprecated public const key: string, @deprecated public const alg: string)
end
```

Parameters in function *types* (`(@deprecated a: number) -> ()`) are not covered; see
[Future work](#future-work).

### Where each attribute is allowed

*(New in Luwu: the mechanism the upstream RFC calls for but leaves unspecified.)*

Each attribute declares the set of positions it may be written on, in one place -- the parser's
attribute registry. `@checked`, `@native` and `@debugnoinline` describe how a function is compiled or
typechecked, so they allow only functions; `@deprecated` marks an API callers should stop using, which
every position can have. Writing one somewhere it does not allow is a parse error naming the position:

```luau
-- Attribute '@native' cannot be applied to a type alias
@native type X = number
```

A future attribute declares its positions the same way and needs no per-position code. Where the
position is not yet known when the attributes are parsed -- a statement could still turn out to be a
function, a local, a type, a class or an assignment; a class member could be a method or a field -- the
check happens once the parser knows which it is, so exactly one error is reported and it names the real
position.

## Compatibility

This is purely additive: `@` in each of these positions is a parse error in Luau 0.730 and in Luwu
today, so no program that is valid without the feature changes meaning with it. With
`DebugLuwuBetterAttributes` off, parsing and every diagnostic are byte-identical to before.

Attributes in the new positions have no runtime representation and are not serialized into bytecode, so
this needs no bytecode version bump and does not affect bytecode compatibility.

This could be proposed back upstream; it is upstream's own RFC, with the `@[]` correction and
parameters added.

## Drawbacks

Allowing attributes to be on types, variables, table fields, classes, and class fields, would be added
complexity to the language. Although would be more inline with what a user would expect/want, as its
odd from the perspective of a user that currently types can't be marked as deprecated.

*(Amendment: two more. Every new position is a place the pretty printer has to reproduce attributes
exactly, or formatting silently drops or corrupts them. And the per-position registry is a new
invariant: an attribute added without a considered position set will be accepted somewhere it means
nothing.)*

## Alternatives

**Bare parameterized attributes** (`@deprecated { use = "dog" }`), as the upstream draft writes them.
Rejected: it is ambiguous in exactly the positions this RFC adds, which is why upstream chose `@[]` in
the first place.

| bare form | ambiguous with |
| --- | --- |
| `@checked (number) -> number` | the attribute's arguments vs. the function type's parameter list -- and this is syntax Luwu already accepts |
| `{ @deprecated {1, 2} }` | an arguments table vs. an attributed list entry whose value is `{1, 2}` |
| `{ @deprecated {string} }` | an arguments table vs. an attributed array-like table type |
| `{ @deprecated "hi" }` | string-call arguments vs. an attributed list entry |

**Comment-driven deprecation** (`---@deprecated`, as some Lua tooling uses). Rejected: it is invisible
to the parser, cannot be checked for validity, and would give a second, weaker spelling for something
attributes already express.

## Future work

- **Linting a deprecated parameter**, which means reporting at call sites that pass an argument for
  it rather than at uses inside the body, and so needs a way to carry the deprecation per argument.
- **Linting a deprecated assignment**, `@deprecated m.dog = "woof"`, including what happens when the
  same field is assigned twice with different attributes.
- **Parameters in function types**, `(@deprecated a: number) -> ()`. Deferred because a function type's
  parameter names are stored as a bare pair with nowhere to hang attributes, and the list has a
  sparse-fill invariant that any parallel array has to respect.
- **Attributes after a type alias's `=`.** The upstream draft gestures at this ("type declarations can
  also have attributes directly after the `=`") without specifying it; it is not implemented here, and
  would need its own rules about how it relates to an attribute on the alias itself.

## Prior art

Rust's `#[deprecated]` applies to nearly every item form -- functions, types, fields, variants,
constants -- which is the shape this RFC is reaching for. C# attributes and Java annotations likewise
target declarations generally rather than methods only, and both restrict each attribute to a declared
set of targets (`AttributeTargets`, `@Target`), which is the same mechanism as the registry described
above. TypeScript expresses deprecation through the `@deprecated` JSDoc tag, which editors surface but
the compiler cannot validate -- the weakness that motivates keeping this in the grammar rather than in
comments. Upstream Luau's own
[Attributes](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions.md) and
[Function Attribute Parameters](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions-parameters.md)
RFCs are what this one extends.
