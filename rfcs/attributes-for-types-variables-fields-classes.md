# Allow attributes to be used on types, variables, fields, and classes

Status: Implemented (Flagged)

FFlag: LuwuAttributesEverywhere

Adapted from [luau-lang/rfcs#147](https://github.com/luau-lang/rfcs/pull/147) by
[@gaymeowing](https://github.com/gaymeowing) (quaywinn), with her permission.

## Summary

Allows [Attributes](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions.md) to be applied to
types, variables, table fields, classes, class fields, and parameters in Luwu.

## Motivation

Currently attributes can only be applied to functions. This means types cannot be marked as
[`@deprecated`](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attribute-functions-deprecated.md), nor can
variables, table fields, classes, or class fields.

## Design

Note: with this RFC, each attribute states which bits of syntax it can be applied to, so one can't apply
[`@native`](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attribute-functions-native.md) to a type,
variable, class, or field and have it work. [Where each attribute is allowed](#where-each-attribute-is-allowed) lists
them.

The following sections propose how attributes attach to each bit of syntax in Luwu (except functions and comments).

### Attribute arguments

An attribute with arguments is always written in brackets, as
[Function Attribute Parameters](https://github.com/luau-lang/rfcs/blob/master/docs/syntax-attributes-functions-parameters.md)
already requires for functions:

```luau
@[deprecated { use = "dog" }]
local puppy = "whimper"
```

The original proposal wrote arguments bare (`@deprecated { use = "dog" }`), but the bare form is ambiguous in the new
positions (see [Alternatives](#alternatives)). Writing bare arguments anyway is a parse error that shows the bracketed
form:

```
Attribute arguments must be written as '@[deprecated ...]'; a bare '@deprecated' cannot take arguments
```

### Variables

This exists as it could be useful if, for instance, a constant is defined as a local variable and then exported:

```luau
@[deprecated { use = "dog" }]
local puppy = "whimper"

return table.freeze({
    puppy = puppy,
    dog = "woof",
})
```

This will cause the return in the module to be linted, but the lint won't be passed on to consumers of the module:

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

`const` declarations take attributes the same way as `local` ones.

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

If the value of a field has attributes, those will be merged with the attributes defined on the field, with the
attributes on the field having priority over the attributes on the value. For example, if both the value and the field
have a `@deprecated` attribute, the `@deprecated` attribute on the value will be ignored and the one on the field used
instead.

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

A list entry has no name, so attributes in front of a function in a list belong to the function:
`{ @native function() end }` compiles the function natively.

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
    -- Just like with tables, entries have their attributes merged with the value's attributes.
    @[deprecated { use = "dog" }] puppy: Puppy,
    dog: "bark",
    cat: "mrrp",
}
```

A deprecated type is reported wherever its name is written in the scope the type is visible in. A generic parameter or
a nearer type with the same name shadows it, and the type's definition can refer to itself without a report
(`@deprecated type Node = { next: Node? }`).

A deprecated field of a table type is reported where code reads the field from a value of that type. Libraries can use
this to deprecate one of their functions:

```luau
type FsLib = {
    @[deprecated { use = "readfile" }] read: (path: string) -> string,
    readfile: (path: string) -> string,
}

local fs = {} :: FsLib

-- DeprecatedApi: Member 'FsLib.read' is deprecated, use 'readfile' instead Luwu(22)
fs.read("config.toml")
```

Attributes go before a `read` or `write` modifier. Indexers take attributes too, and an attribute on an array type `{T}`
applies to its `[number]` indexer:

```luau
type Registry = {
    @deprecated read name: string,
    @deprecated [string]: number,
}
```

A `type function` can't take attributes yet, so `@deprecated type function f(t) ... end` is a parse error instead of an
attribute that does nothing.

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

Attributes usually go on the line above a member. On the same line they go in front of the access specifier, or after
it as methods already allow (`public @deprecated dog: string`), but not on both sides of one member. `const` counts as
part of the access specifier:

```luau
class PetSounds
    @deprecated private const cat: string = "mrrp"
    public dog: string
end
```

### Parameters

Function parameters take attributes before their name:

```luau
local function speak(@[deprecated { use = "sound" }] noise: string, sound: string)
    return if noise ~= nil then noise else sound
end
```

A parameter of a class's [primary constructor](./classes/classes.md) declares a field as well, so its attributes can go
on either side of the access specifier like a field's:

```luau
class SshKey(@deprecated public const key: string, @deprecated public const alg: string)
end
```

### Where each attribute is allowed

Each attribute lists the positions it can be written in. Writing it anywhere else is a parse error naming where it's
allowed:

| attribute | allowed on |
| --- | --- |
| `@checked`, `@native` | functions |
| [`@noinline`](./noinline-attribute.md) | functions the compiler can inline: local and const functions, class methods and function expressions |
| `@deprecated` | every position in this RFC |

```luau
-- Attribute '@native' can only be applied to functions
@native type X = number
```

A new attribute only has to list its positions. Autocomplete after `@` reads the same list, so it only offers
attributes the parser accepts at the cursor.

### Luau and Lua files

Upstream Luau only parses attributes on functions. Luau and Lua files write attributes in the new positions as a
`--@[...]` comment on the line above, so the file still runs on upstream Luau:

```luau
type Fs = {
    --- Reads a file
    --@[deprecated { use = "readfile" }]
    read: (path: string) -> string,
    readfile: (path: string) -> string,
}
```

A `--@[...]` comment means the same as the attribute written at the start of the next line, in every kind of file.
Text after the closing `]` is ignored. Only `--@[` starts one, so `-- @[deprecated]` and `---@deprecated` are ordinary
comments. Writing the attribute itself in a new position in a Luau or Lua file is a parse error that suggests the
comment.

Static analysis checks these comments like the attributes they stand for. The compiler skips one it can't read or
apply, since valid Luau has to keep compiling.

## Compatibility

This is purely additive. An attribute in any of these positions is a parse error in Luau 0.730, so no valid program
changes meaning. With `LuwuAttributesEverywhere` off, parsing and diagnostics are unchanged.

Existing Luau code can contain a `--@[...]` comment, which now reads as an attribute. It never stops a file compiling,
and only a `--@[native]` or `--@[noinline]` comment can change how a function compiles.

Attributes in the new positions aren't stored in bytecode, so the bytecode version doesn't change.

## Drawbacks

Allowing attributes on types, variables, table fields, classes, and class fields would add complexity to the
language. It would be more in line with what a user would expect or want, as it's odd from a user's perspective that
types currently can't be marked as deprecated.

The pretty printer has to reproduce attributes in every new position, or formatting drops them.

## Alternatives

**Bare attribute arguments** (`@deprecated { use = "dog" }`), as the original proposal writes them. Upstream chose
`@[]` for arguments "to facilitate syntax evolution without introducing parsing ambiguity", and the new positions are
where the bare form is ambiguous:

| bare form | could also mean |
| --- | --- |
| `@checked (number) -> number` | a function type's parameter list |
| `{ @deprecated {1, 2} }` | a list entry `{1, 2}` with an attribute |
| `{ @deprecated {string} }` | an array type `{string}` with an attribute |
| `{ @deprecated "hi" }` | a list entry `"hi"` with an attribute |

**Deprecation in comments** (`---@deprecated`, as some Lua tooling does). The parser can't check a comment, and it
would be a second spelling of what attributes already express.

## Future work

- Attributes right after a type alias's `=`, which the original proposal mentions without describing.
- Attributes on parameters in function types: `(@deprecated a: number) -> ()`.
- Attributes on a `type function`, reported where the type function is used.
- Reporting a deprecated parameter at the call sites that pass it.
- Reporting a deprecated assignment (`@deprecated module.dog = "woof"`), and deciding what happens when a field is
  assigned twice with different attributes.

## Prior art

Rust's `#[deprecated]` applies to functions, types, fields, variants and constants. C# attributes and Java annotations
apply to declarations in general, and each one declares the targets it allows (`AttributeTargets`, `@Target`) like the
list above. TypeScript's `@deprecated` is a JSDoc tag that editors show but the compiler can't check.

## Implementation details

Every position above parses. The `DeprecatedApi` lint reports variables, types, classes, class fields, table type
fields and table constructor entries. Parameters, assignments and indexers parse but aren't reported yet (see
[Future work](#future-work)). The new type solver marks deprecated table type fields, table constructor entries and
class fields in the type, and the old one marks table type fields only.
