# Inherited Luau RFCs

Luwu accepts valid Luau code from upstream Luau (luau-lang/luau) up to and including **0.730**. As a result we inherit all RFCs accepted into [luau-lang/rfcs](https://github.com/luau-lang/rfcs) by 2026-07-17.

RFCs accepted upstream after 0.730 are **not** covered by Luwu's compatibility guarantee.

This page lists which RFCs from Luau we implement, don't implement, and ones we modify or replace. This list snapshots luau-lang/rfcs commit [`c03475e`](https://github.com/luau-lang/rfcs/tree/c03475e9e3ca53ceb3449296882a4edb75a7ca38), the last commit before Luau 0.730 was published.

Dates are when each RFC was merged into luau-lang/rfcs. RFCs from before that repository existed keep the dates of their original commits in luau-lang/luau.

## Replaced in Luwu (2)

| RFC | Accepted | Notes |
| --- | --- | --- |
| [Classes](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-classes.md) | 2026-04-27 | Luwu builds upon this original `Classes!` RFC but significantly diverges from upstream's planned implementation that includes inheritance and inheritance constructor semantics. Our classes RFC lives at [classes.md](./classes.md). |
| [An official mascot for Luau](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/luau-mascot.md) | 2024-06-04 | Luwu's mascot is Nyla; her design will be proposed in a future RFC. |

## To be replaced in Luwu (4)

We plan on improving, adapting, or superseding these RFCs with our own design.

| RFC | Accepted | Notes |
| --- | --- | --- |
| [Export by Value](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/export-keyword.md) | 2026-04-27 | Implemented in Luwu; will need optimization fixes and redesign around `import` and cyclic imports. |
| [64-bit Integer Type](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/type-long-integer.md) | 2026-02-12 | This feature is in limbo upstream and according to the Luau team, since it was never enabled in Roblox (only enabled by default in the OSS release of Luau), it was never 'released' and not subject to their backwards-compatibility promise. Luwu is working on an alternative implementation with an actual distinction between signed/unsigned, arithmetic operators, bigints, and more. |
| [Key destructuring (`local {.a, .b} = t`)](https://github.com/luau-lang/rfcs/blob/364425c5185166a2f0fc1b97e1e7b6924d0c241d/docs/syntax-key-destructuring.md) | 2024-05-20 | Reverted; never implemented by Luau. |
| [Safe navigation postfix operator `?`](https://github.com/luau-lang/rfcs/blob/1ddb4755189f4a7399a293f009e80ad7c978e714/docs/syntax-safe-navigation-operator.md) | 2021-12-20 | Reverted; never implemented by Luau. |

## Accepted but not implemented in Luwu (3)

No `Status: Implemented` marker upstream and no implementation found in our source code.

| RFC | Accepted | Notes |
| --- | --- | --- |
| [Relax the recursive type restriction](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/relax-recursive-type-restriction.md) | 2025-03-05 | |
| [Negation types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/negation-types.md) | 2025-02-24 | Negation types exist inside the type solver, but there is no user-facing negation type syntax. |
| [Expanded Subtyping for Generic Function Types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/generic-function-subtyping.md) | 2022-09-19 | |

## Reverted by Luau (5)

RFCs Luau accepted and later withdrew. We may replace some of these with our own (see [To be replaced in Luwu](#to-be-replaced-in-luwu-4)).

| RFC | Accepted | Reverted | Notes |
| --- | --- | --- | --- |
| [Key destructuring (`local {.a, .b} = t`)](https://github.com/luau-lang/rfcs/blob/364425c5185166a2f0fc1b97e1e7b6924d0c241d/docs/syntax-key-destructuring.md) | 2024-05-20 | 2024-06-14 | Unaccepted and removed from luau-lang/rfcs ([`7ad9975`](https://github.com/luau-lang/rfcs/commit/7ad9975637246de77a11c66270b144aa94bf79ec)). |
| [Shared self types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/shared-self-types.md) | 2024-01-23 | 2026-04-08 | Still listed at the snapshot, but withdrawn on the unmerged `no-shared-self-types` branch ([`6f2e60d`](https://github.com/luau-lang/rfcs/commit/6f2e60d): "We will not be implementing shared self types"). |
| [Safe navigation postfix operator `?`](https://github.com/luau-lang/rfcs/blob/1ddb4755189f4a7399a293f009e80ad7c978e714/docs/syntax-safe-navigation-operator.md) | 2021-12-20 | 2022-06-09 | Withdrawn by "Do not implement safe navigation operator" ([`ea9bd25`](https://github.com/luau-lang/rfcs/commit/ea9bd25c5367d4516dd4a3a5403d3b6637d4f8ad)). |
| [nil-forgiving postfix operator `!`](https://github.com/luau-lang/rfcs/blob/492d422346393ecf990b75855ac5d6288f3d3363/docs/syntax-nil-forgiving-operator.md) | 2021-06-23 | 2022-03-24 | Withdrawn by "Do not implement non-nil postfix `!` operator" ([`26971fb`](https://github.com/luau-lang/rfcs/commit/26971fb08e5a77e950fc4262a1c7a358baf0d504)). |
| [Allow method call on string literals](https://github.com/luau-lang/rfcs/blob/f10b39b00b41d49bf00af928b647ab6ea06bf49d/docs/syntax-method-call-on-string-literals.md) | 2021-05-17 | 2021-11-12 | Withdrawn by "Do not allow method call on string literals" ([`a607a9f`](https://github.com/luau-lang/rfcs/commit/a607a9f98dfbd67ba32269d95046612773e5ea01)). |

## Policy, non-feature and abandoned (4)

| RFC | Accepted | Notes |
| --- | --- | --- |
| [No support for user inlining (yet)](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-inlining.md) | 2024-08-06 | Decision *not* to support user-controlled inlining. |
| [Reserve dollar sign (`$`) to be forever unused](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/reserve-dollar-sign.md) | 2024-06-25 | Reserves `$`; nothing to implement. |
| [Disallow `name T` and `name(T)` in future syntactic extensions for type annotations](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/disallow-proposals-leading-to-ambiguity-in-grammar.md) | 2022-07-28 | Grammar policy for future syntax. |
| [Lower Bounds Calculation](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/lower-bounds-calculation.md) | 2022-03-29 | Marked upstream as abandoned in favor of local type inference. |

## Implemented in Luwu (79)

Rows without notes are marked `**Status**: Implemented` in luau-lang/rfcs as of the snapshot. Rows with notes had no such marker upstream, usually because the feature was still behind a flag at 0.730; the note says where the implementation lives here. Flags named below are Luau fast flags and default to off.

| RFC | Accepted | Notes |
| --- | --- | --- |
| [Support Cyclic Imports](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/support-for-cyclic-requires.md) | 2026-06-02 | Behind `LuauCyclicRequireShortCircuit`. |
| [Export by Value](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/export-keyword.md) | 2026-04-27 | Behind `LuauExportValueSyntax` (parsing) and `LuauExportValueTypecheck` (type checking). |
| [Const Keyword](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/const-keyword.md) | 2026-02-24 | `const` locals parse unconditionally. |
| [64-bit Integer Type](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/type-long-integer.md) | 2026-02-12 | Behind `LuauIntegerType2`. |
| [Math constants](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/math-constants.md) | 2026-02-11 | `math.e`, `math.phi`, `math.sqrt2`, `math.tau` and `math.nan` are always registered. |
| [math.isnan, math.isinf and math.isfinite for Math Library](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/math-isnan-isfinite-isinf.md) | 2025-11-03 | |
| [Support Luau-syntax configuration files](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/config-luauconfig.md) | 2025-10-22 | |
| [`extern` tag in User-Defined Type Functions.](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/udtf-is-extern.md) | 2025-08-21 | The UDTF tag for extern types is `"extern"`. |
| [vector.lerp](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-vector-lerp.md) | 2025-08-14 | |
| [types.optional](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/types-library-optional.md) | 2025-04-25 | |
| [Abstract module paths and `init.luau`](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/abstract-module-paths-and-init-dot-luau.md) | 2025-04-08 | |
| [type:issubtypeof](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/method-type-issubtypeof.md) | 2025-04-02 | Behind `LuauUdtfTypeIsSubtypeOf`. |
| [Explicit type parameter instantiation](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/explicit-type-parameter-instantiation.md) | 2025-02-21 | |
| [Metatable Type Functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/metatable-type-functions.md) | 2025-02-04 | |
| [Support for Generic Types and Packs in User-Defined Type Functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/support-for-generic-function-types-in-user-defined-type-functions.md) | 2025-01-08 | |
| [math.lerp](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-math-lerp.md) | 2025-01-07 | |
| [buffer.readbits/writebits](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-buffer-bits.md) | 2024-12-13 | |
| [2-component vector constructor](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/vector-library-vector2-constructor.md) | 2024-12-09 | |
| [Support for thread and buffer Types in User-Defined Type Functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/support-for-thread-and-buffer-types-in-user-defined-type-functions.md) | 2024-12-03 | |
| [math.map](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-math-map.md) | 2024-10-16 | |
| [Amended Require Syntax and Resolution Semantics](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/amended-require-resolution.md) | 2024-09-25 | |
| [Vector library](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/vector-library.md) | 2024-09-11 | |
| [User-Defined Type Functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/user-defined-type-functions.md) | 2024-08-28 | |
| [Deprecated Attribute for Functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-attribute-functions-deprecated.md) | 2024-08-06 | |
| [`rawget` type function](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/rawget-type-operator.md) | 2024-06-20 | |
| [`index` type function](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/index-type-operator.md) | 2024-06-10 | |
| [Native Attribute for Functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-attribute-functions-native.md) | 2024-06-10 | |
| [Function Attribute Parameters](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-attributes-functions-parameters.md) | 2024-05-30 | |
| [Attributes (for Functions)](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-attributes-functions.md) | 2024-04-11 | |
| [Improved type checking rules of cast operator](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/type-ascription-by-inhabitance.md) | 2024-03-18 | Implemented long ago; upstream never added the status marker. |
| [Leading `\|` and `&` in types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-leading-bar-and-ampersand.md) | 2024-03-11 | |
| [`keyof` and `rawkeyof` type functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/keyof-type-operator.md) | 2024-01-16 | |
| [Syntax for table property access modifiers](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-property-access-modifiers.md) | 2024-01-12 | |
| [Require by String with Aliases](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/require-by-string-aliases.md) | 2023-11-30 | |
| [Require by String with Relative Paths](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/new-require-by-string-semantics.md) | 2023-11-16 | |
| [Stricter utf8 library validation](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/behavior-stricter-utf8-library.md) | 2023-11-08 | |
| [Byte buffer type](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/type-byte-buffer.md) | 2023-10-19 | |
| [bit32.byteswap](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-bit32-byteswap.md) | 2023-10-16 | |
| [New non-strict mode](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/new-nonstrict.md) | 2023-10-09 | Part of the new type solver (`NonStrictTypeChecker`). |
| [Local Type Inference](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/local-type-inference.md) | 2023-08-23 | Part of the new type solver. |
| [Floor division operator](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-floor-division-operator.md) | 2023-03-21 | |
| [Type Error Suppression](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/type-error-suppression.md) | 2023-03-13 | |
| [Deprecate table.getn/foreach/foreachi](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/deprecate-table-getn-foreach.md) | 2023-02-14 | |
| [Support `__len` metamethod for tables and `rawlen` function](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/len-metamethod-rawlen.md) | 2022-06-28 | |
| [never and unknown types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/never-and-unknown-types.md) | 2022-06-22 | |
| [table.clone](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-table-clone.md) | 2022-02-28 | |
| [Generalized iteration](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/generalized-iteration.md) | 2022-02-14 | |
| [Unsealed table literals](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/unsealed-table-literals.md) | 2022-01-06 | |
| [Only strip optional properties from unsealed tables during subtyping](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/unsealed-table-subtyping-strips-optional-properties.md) | 2022-01-06 | |
| [String interpolation](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-string-interpolation.md) | 2021-11-22 | |
| [coroutine.close](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-coroutine-close.md) | 2021-11-17 | |
| [bit32.countlz/countrz](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-bit32-countlz-countrz.md) | 2021-11-09 | |
| [Write-only properties](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/property-writeonly.md) | 2021-10-27 | Superseded by *Syntax for table property access modifiers* (`read`/`write`), which is implemented. |
| [Type alias type packs](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-type-alias-type-packs.md) | 2021-10-27 | |
| [Read-only properties](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/property-readonly.md) | 2021-10-11 | Superseded by *Syntax for table property access modifiers* (`read`/`write`), which is implemented. |
| [Configure analysis via .luaurc](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/config-luaurc.md) | 2021-10-07 | |
| [Unsealed table assignment creates an optional property](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/unsealed-table-assign-optional-property.md) | 2021-10-05 | |
| [Recursive type restriction](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/recursive-type-restriction.md) | 2021-09-27 | |
| [Relaxing type assertions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-type-ascription-bidi.md) | 2021-09-23 | |
| [Default type alias type parameters](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-default-type-alias-type-parameters.md) | 2021-08-20 | |
| [Deprecate getfenv/setfenv](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/deprecate-getfenv-setfenv.md) | 2021-06-24 | |
| [Sealed table subtyping](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/sealed-table-subtyping.md) | 2021-05-31 | |
| [Singleton types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-singleton-types.md) | 2021-05-28 | |
| [Named function type arguments](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-named-function-type-args.md) | 2021-05-13 | |
| [Generic functions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/generic-functions.md) | 2021-05-12 | |
| [table.freeze](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-table-freeze.md) | 2021-05-04 | |
| [if-then-else expression](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-if-expression.md) | 2021-05-04 | |
| [Always call `__eq` when comparing for equality](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/behavior-eq-metamethod.md) | 2021-05-03 | |
| [Change \_VERSION global to "Luau"](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/change-global-version.md) | 2021-05-03 | |
| [debug.info](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-debug-info.md) | 2021-05-03 | |
| [string.pack/unpack/packsize from Lua 5.3](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-string-pack-unpack.md) | 2021-05-03 | |
| [table.clear](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-table-clear.md) | 2021-05-03 | |
| [table.create and table.find](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/function-table-create-find.md) | 2021-05-03 | |
| [Array-like table types](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-array-like-table-types.md) | 2021-05-03 | |
| [Compound assignment using `op=` syntax](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-compound-assignment.md) | 2021-05-03 | |
| [continue statement](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-continue-statement.md) | 2021-05-03 | |
| [Extended numeric literal syntax](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-number-literals.md) | 2021-05-03 | |
| [Type ascriptions](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-type-ascription.md) | 2021-05-03 | |
| [Typed variadics](https://github.com/luau-lang/rfcs/blob/c03475e9e3ca53ceb3449296882a4edb75a7ca38/docs/syntax-typed-variadics.md) | 2021-05-03 | |
