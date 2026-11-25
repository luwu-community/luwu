# Octal Literals

Status: Implemented (Flagged)

FFlag: LuwuOctalLiterals

## Summary

Number literals that start with `0o` or `0O` are written in base 8: `0o755` is the same as `493`. They follow the same rules as
binary and hexadecimal literals. Digits can be grouped with `_` (`0o7_5_5`), and literals too large for 64 bits get a lint warning.

## Motivation

Unix file permissions are written in octal. A mode like `755` has one octal digit for each of the owner, the group and everyone
else, and tools like `chmod` and `stat` print and read modes in that form. seal's archive entries take and return modes as numbers
(`ArchiveEntry:set_unix_mode(mode)`), and file permission APIs in its `fs` library would do the same. Without octal literals a script
has to spell `0o755` as `493` or `0x1ED`, and a reader can't check either one against `chmod 755` without converting it by hand.

## Design

`0o` or `0O` followed by the digits `0` to `7` is an octal literal:

```luau
const mode = 0o755 -- 493
const private_file = 0o600 -- 384
const umask = 0O022 -- 18
```

Like in other number literals, you can put `_` anywhere after the prefix to group digits. `0o7_5_5` and `0o_755` both mean `0o755`.

An octal literal is a `number` with the same value as its decimal form. The parser reports "Malformed number" for a digit `8` or `9`,
an exponent (`0o7e1`) and a prefix with no digits after it (`0o`).

Like binary and hexadecimal literals, octal literals past `2^64 - 1` are truncated to `2^64` and get the `IntegerParsing` lint
warning "Octal number literal exceeded available precision and was truncated to 2^64". Literals past `2^53` that a `number` can't
hold exactly get the same imprecision warning as decimal and hexadecimal literals.

Octal literals are a Luwu feature. Even with `LuwuOctalLiterals` on, a Luau module that uses one gets the parse error for Luwu-only
features.

## Compatibility

`0o755` is a "Malformed number" parse error in Luau and in Luwu without the flag. Valid programs never contain one and keep their
meaning. The feature reserves no keywords and doesn't change the bytecode: the compiler sees an ordinary number constant.

The Luau team has approved upstream's open RFC for the same feature, luau-lang/rfcs#133 "Octal integer literals", for a prototype
under a Debug flag. Luwu's design matches that RFC except for the capital prefix. Upstream accepts `0B` and `0X`, but the RFC
rejects `0O` as hard to read. Luwu accepts `0O` so all three prefixes follow one rule. Code written with `0O` won't parse in
upstream Luau if the RFC lands as written. Since rejecting `0O` later would break code that uses it, the choice needs settling
before the flag is removed.

## Drawbacks

Octal digits are also decimal digits. A reader who misses the `0o` prefix reads `0o755` as seven hundred and fifty-five. The `0O`
prefix makes this worse in fonts where `O` and `0` look alike.

## Alternatives

Scripts can keep writing modes in decimal or hexadecimal. Modes written as strings (`"755"` or `"rwxr-xr-x"`) in seal's APIs would
cover file permissions but no other use of octal.

C's leading-zero form (`0755`) is not an option: `0755` already means 755 in Luau.

## Prior art

Python 3, JavaScript (ES2015) and Rust write octal with `0o`. Python and JavaScript also accept `0O`. Rust accepts only `0o`. C and
C++ write octal with a leading zero (`0755`), and Lua has no octal literals.
