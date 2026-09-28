# Destructuring

Status: "Proposed"

FFlag: LuwuDestructuring

## Summary

Adds table, userdata, object, vector etc. destructuring as syntactical sugar.

The chosen syntax is:

```luwu
const .{x, y, z} = v
const .{zip, tar as .{gz as targz}} = require("@std/archive")
const fs.{readfile as rf, writefile as wf, path} = require("@std/fs")
```

## Motivation

We've wanted table destructuring for years. You currently have to write out every local binding multiple times,
and doing so is not fun. It also crowds up a lot of lines in file headers. Most languages have destructuring.

## Design

You can now take things out of bindings when you declare bindings. If there's an identifier to the left of the
`.` glyph then that becomes the variable name. This allows you to import smth, and then later on add things you
want to destructure just by using familar dot syntax but on the LHS of the binding instead of the RHS of the binding.

```luwu
const items = getitems() -- { Item }
-- 2 days later this got refactored into { store: string, items: { Item }, manager: Manager, sales: number.... }
const store_info.{store as store_name, items} = getitems()
```

We support this in `local` and `const` declarations.

## Compatibility

This is not compatible with Luau 0.730, but may be partially compatible with upstream Luau because it's based on VIG's RFC.

One thing we've added that they haven't added yet: you can qualify the top value as the identifier to the left of the `.`

## Drawbacks

- It's symbol soup
- May be confusing to read left to right, especially `as` with their own patterns.

## Alternatives

Use any of the previous syntaxes proposed in Luwu and Luau: `const x, y, z from t` (which is really clean),
`local { .x, .y } = t` which was accepted and then later reverted by Luau, etc.

## Future work (optional)

Support in for loops. Probably don't want to support in parameters because default values will be very wonky.
Also it would be very confusing.

## Prior art (required only for syntax and semantics changing RFCs)

JS, Zig, Rust, everyone except Lua.

## Implementation details (optional)

Parser only probably.
