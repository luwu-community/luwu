# Generic parameters on extern types

FFlags: LuauGenericNominals, LuauExternTypeGenericMethods

Implementation-related upstream bugfix FFlags: LuauExternTypeUseDefinitionScope

## Summary

Allows nominal types (extern types and classes) to take generic parameters (like `<T>`).
This first implementation focuses only on extern types, with a followup implementation adding support for user-defined classes.

## Motivation

Currently, putting generic parameters on extern types (defined by the embedder) causes a syntax error.
The only way around it by using a generic parameter leakage hack...

Generic extern types allow for a better, richer typed experience in error handling and any embedder defined type
carrying data. They are also a LOT more readable than generic table types, and with the proposed implementation Rusty
concepts like `Result<List<string>, IoError<string>>` just work AND are correctly handled in hovers.

## Design

Parser is extended to allow for generic parameters in 2 new locations, method `function Foo<T>(meow: Param): Ret` within `extern type` definitions
as well as in extern type definitions themselves (`declare extern type Foo<T> with ... end`).

The type solver is modified to handle generic parameters on extern types:

- Generic parameters on extern types do not affect the runtime `typeof` of the extern type, and there's no way at runtime to recover what `T`
an extern type was instantiated with. This feature only exists to improve strictly typed semantics.
- Users may use `typeof` to refine a union of values including extern types with generic parameters to one or more extern types.
- Generic parameters on classes are planned but not implemented in the first iteration of this RFC. We want to wait for our version of classes to land before implementing generic parameters on them.

Example of using `typeof` to narrow:

```luau
--[[
-- globals.d.luau
declare extern type Exception<Data> with
    inner: Data
end
]]

const function can_fail(): string | Exception<{kind: "IoError"}> | Exception<"meow">
    return nil :: any
end

const ress = can_fail()
if typeof(ress) == "Exception" then
    -- here ress is narrowed to Exception<{kind: "IoError"}> | Exception<"meow">
    if typeof(ress.inner) == "string" then
        -- here ress is narrowed to Exception<"meow">
        const y = ress.inner
    else
        -- here ress is narrowed to Exception<{kind: "IoError"}>
        const x = ress.inner
    end
end
```

## Drawbacks

This is a major change to the type solver and will significantly affect our ability to merge patches from upstream.
Additionally, since generic parameters are not known at runtime one can say that implementing generic parameters on extern types is
technically incorrect. We feel that the benefits outweigh the drawbacks or technical correctness in this case.

## Alternatives

- the existing generics hack,
- waiting for classes to be fully implemented and then implementing them for both types of nominals
- only supporting generic parameters on methods inside extern types but not on the extern types themselves
- not supporting generic parameters on extern types
