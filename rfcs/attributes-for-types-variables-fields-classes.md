# Allow attributes to be used on types, variables, fields, and classes

Status: Implemented (Flagged)

FFlag: LuwuAttributesEverywhere

Adapted from [luau-lang/rfcs#147](https://github.com/luau-lang/rfcs/pull/147) by
[@gaymeowing](https://github.com/gaymeowing) (quaywinn), with her permission.

## Summary

Allows [Attributes](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions.md) to be applied to
types, variables, table fields, classes, class fields, and parameters in Luwu.

## Motivation

Currently attributes can only be applied to functions, this means types cannot be marked as
[`@deprecated`](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attribute-functions-deprecated.md).
Nor can variables, table fields, classes, or class fields be marked as `@deprecated`.

## Design

Note: With this RFC the internals for attributes will be able to state they can be applied to only certain bits of
syntax, so one can't apply [`@native`](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attribute-functions-native.md)
to a type, variable, class, or field and have it work. How each attribute states this is described in
[Where each attribute is allowed](#where-each-attribute-is-allowed).

The following list proposes how attributes should be attached to each bit of syntax in Luwu (except functions and
comments).

### Attribute arguments

An attribute that takes arguments is written in brackets, `@[deprecated { use = "dog" }]`, in every position, as
[Function Attribute Parameters](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions-parameters.md)
already requires for functions. A bare `@name` never takes arguments; [Alternatives](#alternatives) explains why the
bare form can't be allowed in the positions this RFC adds.

Writing the arguments bare is the likeliest mistake, so it gets its own diagnostic instead of whatever the next token
then fails to be:

```
Attribute arguments must be written as '@[deprecated ...]'; a bare '@deprecated' cannot take arguments
```

The arguments are still attached to the attribute, so it means what was intended and that is the only error. Inside a
table, value or type, an entry may itself be a table or a string, so there `{ @deprecated {1, 2} }` is an attributed
entry whose value is `{1, 2}`, and nothing is reported.

### Variables

This exists as it could be useful for if for instance a constant is defined as a local variable and then exported:

```luau
@[deprecated { use = "dog" }]
local puppy = "whimper"

return table.freeze({
    puppy = puppy,
    dog = "woof",
})
```

Although this will cause the return in the module to be linted, but this lint won't be passed on to consumers of the
module:

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

`const` bindings take attributes the same way.

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
For example: if both the value and the field have a `@deprecated` attribute, the `@deprecated` attribute on the value
will be ignored.
With the `@deprecated` attribute on the field being used instead.

```luau
@[deprecated { reason = "cat is a more modern API" }]
local function get_cat_sound()
    return "meow"
end

-- DeprecatedApi: Function 'get_cat_sound' is deprecated. cat is a more modern API Luwu(22)
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

A list entry has no name to attach an attribute to, so attributes directly in front of `function` in a list entry
belong to that function, as they do wherever else a function expression is written. `{ @native function() end }`
compiles the function natively.

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

A deprecated type is reported wherever its name is written: in an annotation, and inside another type's definition,
as `CanineSounds` shows. It is scoped like the type itself. It is reported throughout the block that declares it,
including above the declaration, where the type is also visible. A generic parameter or a nearer type with the same
name shadows it, and a reference inside its own definition (`@deprecated type Node = { next: Node? }`) is not
reported.

A deprecated field of a table type is reported where the field is used through a value of that type, never at the
definition. This is how a library's type marks one of its functions deprecated:

```luau
type FsLib = {
    @[deprecated { use = "readfile" }] read: (path: string) -> string,
    readfile: (path: string) -> string,
}

local fs = {} :: FsLib

-- DeprecatedApi: Member 'FsLib.read' is deprecated, use 'readfile' instead Luwu(22)
fs.read("config.toml")
```

An entry's attributes come before its `read` or `write` modifier, so the entry reads in the order it is written. An
indexer takes attributes too, and since an array-like `{T}` is shorthand for `{[number]: T}`, an attribute on one
attaches to that indexer:

```luau
type Registry = {
    @deprecated read name: string,
    @deprecated [string]: number,
}
```

A `type function` takes no attributes. `@deprecated type function f(t) ... end` is a parse error, "Attributes cannot be
applied to a type function", rather than an attribute that is silently dropped.

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

Class fields work the same as [Table Fields](#table-fields). Attributes placed before a `public` field declaration are
attached to that class member:

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

-- DeprecatedApi: Member 'PetSounds.puppy' is deprecated, use 'dog' instead Luwu(22)
print(sounds.puppy)
```

An attribute is best written on the line above the member it annotates, as it is everywhere else. Written inline, it
goes in front of the access specifier, since an attribute is not a modifier keyword:

```luau
class PetSounds
    @deprecated private const cat: string = "mrrp"
    public dog: string
end
```

After the access specifier is accepted too (`public @deprecated dog: string`), since methods already allowed it, but
not on both sides at once: that would leave a reader looking in two places for them. `const` counts as part of the
specifier. An attribute never changes what the keywords next to it mean: a `@deprecated private const` field is still
private and still const.

### Parameters

A function parameter takes attributes, written before its name:

```luau
local function speak(@[deprecated { use = "sound" }] noise: string, sound: string)
    return if noise ~= nil then noise else sound
end
```

A parameter of a class's [primary constructor](./classes/classes.md) also declares a field, so its attributes may sit
on either side of the access specifier, as a field's may:

```luau
class SshKey(@deprecated public const key: string, @deprecated public const alg: string)
end
```

### Where each attribute is allowed

Each attribute states the positions it may be written on, in one place: the parser's attribute registry. Writing an
attribute anywhere else is a parse error that says where it is allowed:

| attribute | allowed on |
| --- | --- |
| `@checked`, `@native` | functions, since they describe how a function is typechecked or compiled |
| [`@noinline`](./noinline-attribute.md) | the functions the compiler can inline: a local function, a const function, a class method or a function expression |
| `@deprecated` | every position in this RFC, since any API can be one callers should stop using |

```luau
-- Attribute '@native' can only be applied to functions
@native type X = number
```

A new attribute states its positions the same way, and no position needs code of its own for it. Some positions are
not known yet when their attributes are parsed: a statement could still turn out to be a function, a local, a type, a
class or an assignment, and a class member could be a method or a field. There the check happens once the parser knows
which it is, so exactly one error is reported.

### Type system

`@deprecated` on a table type's field, a table constructor's entry or a class field marks the property deprecated in
the type, which is what the `DeprecatedApi` lint reads for `Member '...' is deprecated`. The old type solver marks
table type fields only.

### Editor support

Autocomplete after `@` offers only the attributes allowed in the position being written, taken from the same registry
the parser checks against, so it never suggests one the parser would then reject.

## Compatibility

This is purely additive: an attribute in any of these positions is a parse error in Luau 0.730, so no program that is
valid without the feature changes meaning with it. With `LuwuAttributesEverywhere` off, parsing and every diagnostic
are the same as without this feature.

Attributes in the new positions have no runtime representation and are not serialized into bytecode, so there is no
bytecode version bump.

This could be proposed back upstream: it is upstream's own proposal, with its attribute arguments in the bracketed
form and parameters added.

## Drawbacks

Allowing attributes to be on types, variables, table fields, classes, and class fields, would be added complexity to the
language. Although would be more inline with what a user would expect/want, as its odd from the perspective of a user
that currently types can't be marked as deprecated.

Every new position is also one the pretty printer has to reproduce attributes in exactly, or formatting drops or
mangles them. And an attribute added to the registry without a considered set of positions will be accepted somewhere
it means nothing.

## Alternatives

**Bare attribute arguments** (`@deprecated { use = "dog" }`). The upstream proposal writes them this way, but upstream
chose `@[]` for arguments "to facilitate syntax evolution without introducing parsing ambiguity", and the positions
this RFC adds are exactly where the bare form is ambiguous:

| bare form | ambiguous with |
| --- | --- |
| `@checked (number) -> number` | the attribute's arguments vs. the function type's parameter list, which is existing syntax |
| `{ @deprecated {1, 2} }` | an arguments table vs. an attributed list entry whose value is `{1, 2}` |
| `{ @deprecated {string} }` | an arguments table vs. an attributed array-like table type |
| `{ @deprecated "hi" }` | string-call arguments vs. an attributed list entry |

**Comment-driven deprecation** (`---@deprecated`, as some Lua tooling uses). The parser can't see it, so it can't be
checked for validity, and it would be a second, weaker spelling of something attributes already express.

## Future work

- **Attributes directly after a type alias's `=`.** The upstream proposal mentions these without describing them; they
  would need rules for how they relate to an attribute on the alias itself.
- **Parameters in function types**, `(@deprecated a: number) -> ()`.
- **Attributes on a `type function`**, `@deprecated` above all, reported where the type function is used.
- **Linting a deprecated parameter**: reporting at the call sites that pass an argument for it, rather than at uses
  inside the function, which needs a way to carry the deprecation per argument.
- **Linting a deprecated assignment** (`@deprecated module.dog = "woof"`), including what happens when the same field is
  assigned twice with different attributes.

## Prior art

Rust's `#[deprecated]` applies to nearly every item: functions, types, fields, variants and constants, which is the
shape this RFC is reaching for. C# attributes and Java annotations also apply to declarations generally rather than only
methods, and both limit each attribute to a declared set of targets (`AttributeTargets`, `@Target`), the same mechanism
as the registry above. TypeScript spells deprecation as the `@deprecated` JSDoc tag, which editors show but the compiler
can't check, which is the weakness of keeping it in comments. Upstream Luau's own
[Attributes](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions.md) and
[Function Attribute Parameters](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions-parameters.md)
RFCs are what this one extends.

## Implementation details

All positions above parse and keep their attributes. The `DeprecatedApi` lint reports variables, types, classes, class
fields, table type fields and table constructor entries, including the rule that a field's attribute takes priority
over its value's. Three positions are parsed but not yet reported, for the reasons in [Future work](#future-work):
parameters, assignments, and indexers (which have no member name to report against).
