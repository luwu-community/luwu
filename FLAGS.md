# Fast flags

Luwu has 191 fast flags. This page lists every one of them with where it came from, what it does, and whether upstream
Luau still has it. Most are upstream's, inherited when Luwu last merged upstream at 0.731 or ported since. The rest are
Luwu's own.

A boolean `FFlag` defaults to off, as does a `DFFlag` whose row gives no default. An `FInt` or `DFInt` row gives the
number it defaults to. Luwu's command-line tools turn on every flag whose name starts with `Luau` or `Luwu`. Embedders
only get the fixes and speedups behind the flags they name.

The columns:

- **Area**: the part of Luwu that reads the flag.
- **Kind**: `Fix` corrects a wrong result, a crash or a memory safety bug. `Perf` makes something faster without changing
  what it does. `Feature` adds language, library or API behavior. `Analysis` changes what the type checker or linter
  reports. `Limit` is a tunable number. `Debug` is for testing.
- **Preset**: the first preset that turns the flag on (see [Presets](#presets)). Empty means no preset does.
- **Branch**: where the flag is defined. `master` means it has landed. A branch name means the flag only exists in that
  branch, and in `integration`, until the branch lands.

## Presets

An embedder can turn on a whole preset with `luau_setfflagpreset("<name>")` (in `lapix.h`) before it creates a state,
instead of naming each flag. It returns how many flags it turned on, or -1 for a name that isn't a preset. Each preset
holds every flag of the presets above it:

| Preset | Name | What it turns on |
| --- | --- | --- |
| Fixes | `fixes` | Correctness and safety fixes: upstream's that upstream has made permanent, and Luwu's own. |
| Recommended | `recommended` | Fixes, speedups and finished upstream changes, plus Luwu's small, stable improvements: error messages and lints, small library additions, `none`, and embedder APIs that only add functions. |
| New | `new` | The `Luwu` flags of Luwu's newer features, such as classes, traits and destructuring. Luwu treats them as on by default, but they may still change until Luwu 0.1.0. |
| WorkInProgress | `workinprogress` | The `DebugLuwu` flags the rest of the language needs, such as `match` and `try`. |

No preset has debug flags, integer limits, upstream flags upstream reverted, upstream's experimental type solver flags,
or the features still under heavy work: integers, builtin classes and bundles, the `ast` library, buffer comparison,
batched buffer reads and writes, and luwon. A flag that only exists on a branch joins its preset when the branch lands.
The Preset column below says which preset it will join.

## Luwu's flags (57)

Flags upstream Luau never had. A few have `Luau` names because they started as upstream pull requests or fixes that
upstream hasn't taken.

| Flag | Type | Area | Kind | Added by | Added | Preset | Branch | What turning it on does |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `DebugLuwuAllowNonNullTerminatedStrings` | FFlag | VM | Debug | Dev Chrysalis Dalal | 2026-09-13 |  | master | Skips the API check that external string data is null-terminated (used only by the test suite). |
| `DebugLuwuBufferBatched` | FFlag | VM | Feature | Crazyblox | 2026-10-06 |  | `batched-buffer` | Adds the batched `buffer.pack*` and `buffer.unpack*` functions, which read or write many values in one call, plus their type definitions. |
| `DebugLuwuBufferComparison` | FFlag | VM | Feature | Lolekic | 2026-11-17 |  | `buffer-memcmp` | Adds `buffer.order` and `buffer.equals` for comparing buffer contents, plus their type definitions (in progress, so a Debug flag). |
| `DebugLuwuBuiltinClasses` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-11-16 |  | `native-and-builtin-classes` | Opens the `collections` library (`OrderedMap`, `Vec`, `VecDeque`), written as builtin C++ classes, and declares its types (a prototype). |
| `DebugLuwuBundles` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-11-08 |  | `native-and-builtin-classes` | Opens the `bundle` library and enables `.(a, b)` bundle values, with the compiler's bundle fastcalls and unfolding (a prototype). |
| `DebugLuwuCompilerTrustsTypeAnnotations` | FFlag | Compiler | Debug | Dev Chrysalis Dalal | 2026-09-21 |  | master | Permits the `--!trust` directive, so in trusted files class type annotations pick the methods to inline at `-O2` (guarded by runtime class checks). |
| `DebugLuwuEncourageInline` | FFlag | Compiler | Feature | Dev Chrysalis Dalal | 2026-11-23 | WorkInProgress | `adjust-inlining-model` | Enables `@[inline(reason)]`, which weighs a function at a third of its size and makes a call that still can't be inlined a compile error. |
| `DebugLuwuIntegers` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-11-24 |  | `integers` | Enables integer literals such as `7int` and `255u8`, integer arithmetic and the `integer` library (in progress). |
| `DebugLuwuLuauClassCompat` | FFlag | Analysis | Feature | Dev Chrysalis Dalal | 2026-10-12 |  | `luau-class-compat` | Lets `.luau` modules use upstream Luau's class syntax and `class.classof`, with Luwu classes running underneath. |
| `DebugLuwuMatchExpressions` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-11-16 | WorkInProgress | `expressive-control-flow` | Enables `match` expressions and the `is`, `is not` and `implements` patterns, and emits WIP bytecode for their jumps (in progress). |
| `DebugLuwuPoperators` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-10 | WorkInProgress | `push-pop-operators` | Enables the push operator `target <+ values` and the pop operator `<-container`, with `__push` and `__pop` metamethods (in progress). |
| `DebugLuwuTryExpressions` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-30 | WorkInProgress | `try-expressions` | Enables `try` expressions, which give an `Ok` or an `Err`, and the `Ok`, `Err`, `Some` and `None` globals, using WIP bytecode protected ranges. |
| `DebugLuwuUserDefinedRefinements` | FFlag | Analysis | Feature | Dev Chrysalis Dalal | 2026-10-01 | WorkInProgress | `traits` | Enables `@[truthy(param, Type)]`, so a function that returns truthy refines that argument to `Type` at the call site (an MVP). |
| `LuauBetterMissingPropertiesTypeError` | FFlag | Analysis | Analysis | nnullcolumn | 2026-08-08 | Recommended | master | Rewords missing and extra property errors to list the required properties, then the found and expected types on separate lines. |
| `LuauBetterPackAndVariadicMismatchErrors` | FFlag | Analysis | Analysis | PhoenixWhitefire | 2026-07-04 | Recommended | master | Rewords the errors about a generic pack used as a type (and the reverse), and explains variadic-versus-generic-pack mismatches. |
| `LuauFunctionUnusedRecursiveLinting` | FFlag | Analysis | Analysis | nnullcolumn | 2026-08-08 |  | master | No effect now: only tests set it, and the `FunctionUnused` lint for functions used only inside their own body runs unconditionally. |
| `LuauIndexerModifierMismatchErrors` | FFlag | Analysis | Analysis | PhoenixWhitefire | 2026-08-01 | Recommended | master | Explains a read-only indexer used where a read-write one is expected as an access modifier violation, with a clearer message. |
| `LuauTruthyFalsy` | FFlag | Analysis | Analysis | Dev Chrysalis Dalal | 2026-08-07 | Recommended | master | Prints the falsy union and truthy intersections as `falsy` and `truthy`, and drops `truthy` from parts that can never be falsy. |
| `LuauUdtfReuseTypeUserdatas` | FFlag | Analysis | Analysis | gaymeowing | 2026-10-20 |  | `reuse-type-instance-userdatas` | Passes each type into user type functions as one shared userdata instead of a fresh frozen copy, so in-place mutations are visible everywhere the type appears. |
| `LuwuAttributesEverywhere` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-28 | New | `allow-attributes-above-identifiers` | Allows attributes on variables, type aliases, table fields, classes, class fields, parameters and assignments, instead of only on functions. |
| `LuwuBufferIsFrozen` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Adds `buffer.isfrozen` (with a fastcall and type definition), which reports whether a buffer is read-only, such as an external read-only buffer. |
| `LuwuClasses` | FFlag | Common | Feature | Dev Chrysalis Dalal | 2026-09-27 | New | master | Enables Luwu classes everywhere: class syntax, typing, and objects and class values in the VM and native code, with WIP bytecode version 200. |
| `LuwuCompileEncouragedInlineLimit` | FInt = 256 | Compiler | Limit | Dev Chrysalis Dalal | 2026-11-23 |  | `adjust-inlining-model` | Most instructions a function marked `@[inline(reason)]` may weigh at its call sites, in place of the cost model's threshold (default 256). |
| `LuwuCompileInlineBudget` | FInt = 4096 | Compiler | Limit | Dev Chrysalis Dalal | 2026-10-28 |  | `adjust-inlining-model` | Most instructions inlining may add to one function, regardless of thresholds, to keep bytecode size bounded (default 4096). |
| `LuwuCompileInlineCallCost` | FInt = 3 | Compiler | Limit | Dev Chrysalis Dalal | 2026-10-28 |  | `adjust-inlining-model` | Cost the inliner's model gives a call, which is the saving an inline weighs against the body's cost (default 3, as upstream). |
| `LuwuCompileInlineFewCallSites` | FInt = 6 | Compiler | Limit | Dev Chrysalis Dalal | 2026-10-28 |  | `adjust-inlining-model` | A loop-free function with fewer call sites than this gets its inline threshold scaled by this value over its call count, where 1 disables it (default 6). |
| `LuwuCompileInlineNativeCallCost` | FInt = 16 | Compiler | Limit | Dev Chrysalis Dalal | 2026-10-28 |  | `adjust-inlining-model` | Call cost used instead for functions compiled to native code, where a call costs more relative to the surrounding code (default 16). |
| `LuwuDeclareStatements` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-29 | New | `declare-statements` | Allows `declare` statements, including `export declare`, in ordinary source files instead of only in definition files. |
| `LuwuDefaultArguments` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-13 | New | master | Enables default values for function parameters (`function f(x = 1)`). |
| `LuwuDestructuring` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-29 | New | `destructuring` | Enables `.{x, y}` and `.(a, b)` destructuring patterns in Luwu modules and emits WIP bytecode for positional reads. |
| `LuwuDoExpr` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-12 | New | `expressive-control-flow` | Enables `do ... give` expressions and the `x or return`, `or break` and `or continue` shorthands, plus related lints. |
| `LuwuExportedClassIsNilWorkaround` | FFlag | Compiler | Fix | Dev Chrysalis Dalal | 2026-09-27 | Fixes | master | Workaround that assigns an exported class to the module's export table right after its declaration, so the class isn't `nil` inside its own methods. |
| `LuwuExternTypeGenericMethods` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-13 | New | master | Lets methods of declared extern types have their own generic type parameters. |
| `LuwuExternTypeUseDefinitionScope` | FFlag | Analysis | Fix | Dev Chrysalis Dalal | 2026-09-27 | Fixes | master | Resolves type references inside a declared extern type's body, such as a generic method's type parameters, in the declaration's scope. |
| `LuwuExternalString` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Enables `lua_pushexternalstring` and related APIs, so a Lua string can point at embedder-owned memory that is released through a free callback. |
| `LuwuExternallyManagedBuffers` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Enables `lua_newexternalbuffer`, so embedders can wrap memory they own as a buffer with a free callback and a read-only or read-write mode. |
| `LuwuFatCClosure` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Enables `lua_pushcclosurewithdatak` and `lua_getcclosuredata`, so a C closure carries an inline data block with an optional destructor. |
| `LuwuGenericNominals` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-13 | New | master | Lets classes and extern types take generic parameters with defaults, and makes Analysis track their instantiated type arguments. |
| `LuwuHelpfulSubtypingErrors` | FFlag | Analysis | Analysis | Dev Chrysalis Dalal | 2026-09-26 | Recommended | master | Gives structural explanations of type mismatches, typo suggestions for misspelled properties and type names, and `t.f` fix hints for failing `t:f()` calls. |
| `LuwuIfLocal` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-06 | New | `expressive-control-flow` | Enables `if local` and `if const` bindings with `when` chains in `if`, `elseif`, `while` and `for` headers, plus the `BindingSwallowsCondition` lint. |
| `LuwuInLuwuDocs` | FFlag | Analysis | Feature | Dev Chrysalis Dalal | 2026-10-10 | New | `doc-refs` | Parses references and bindings in doc comments, such as `[fs.file.read]`, and type checks them like code. |
| `LuwuLiteralTypes` | FFlag | Analysis | Feature | Dev Chrysalis Dalal | 2026-10-07 | New | `type-function-validators` | Enables `literal<T>` in parameter and local annotations, `literal` generics and `V<literal<T>>` validators (in progress). |
| `LuwuManagedReferences2` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Enables `lua_refpool`, `lua_getrefpool` and `lua_unrefpool`, which store references in a VM-managed array that is faster than registry refs. |
| `LuwuNoinlineAttribute` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-28 | New | `allow-attributes-above-identifiers` | Enables the `@noinline` attribute, which stops the compiler from inlining a function and leaves it unmarked for native inliners. |
| `LuwuNonePrimitive` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Adds the `none` primitive value (a symbol distinct from `nil`) and the APIs and library code that produce it, such as class reflection and Luwon. |
| `LuwuOctalLiterals` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-11-23 | New | `octal-literals` | Enables `0o755` octal number literals in Luwu modules instead of a "Malformed number" error. |
| `LuwuPcallMulti` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-09-13 | Recommended | master | Enables `lua_pcallmulti`, whose error handler may return several values that are all passed back to the caller. |
| `LuwuStringExtensions` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-10-22 | New | `string-extensions` | Adds `string.trim`, `trimstart`, `trimend`, `startswith`, `endswith`, `contains`, `replace` and `split`'s `maxpieces`, plus their fastcalls and fast string method calls. |
| `LuwuStringFormatFastcall` | FFlag | Compiler | Perf | Dev Chrysalis Dalal | 2026-11-23 | New | `fast-string-format` | Compiles `string.format` and interpolated strings to the `LBF_STRING_FORMAT` fastcall and emits WIP bytecode version 200. |
| `LuwuTableComprehensions` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-07 | New | `expressive-control-flow` | Enables table comprehensions (`{ for ... when ... give item }`) and emits WIP bytecode with `PRESIZETABLE` and `APPENDTABLE`. |
| `LuwuTableDiscard` | FFlag | VM | Feature | Dev Chrysalis Dalal | 2026-11-18 | Recommended | master | Adds `table.discard(t, value, count?)` to the table library and its type definition (Luwu's rename of `table.drop`). |
| `LuwuTableFunctionFields` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-10-08 | New | `expressive-control-flow` | Lets a table literal contain `function foo() end`, meaning `foo = function() end`. |
| `LuwuTableRemoveFootgunLint` | FFlag | Analysis | Analysis | Dev Chrysalis Dalal | 2026-09-19 | Recommended | master | Warns on `table.remove(t, i)` when `i` may be `nil`, such as a `table.find` result, since a `nil` index removes the last element. |
| `LuwuTraits` | FFlag | Parser | Feature | Dev Chrysalis Dalal | 2026-09-29 | New | `traits` | Enables `trait` declarations with `implements` and `needs`, `expect` and `final` members, and member placement modifiers (needs `LuwuClasses`). |
| `LuwuTypeFunctionMemoryLimitMb` | FInt = 256 | Analysis | Limit | Dev Chrysalis Dalal | 2026-10-19 |  | `helpful-type-functions` | Most memory, in MiB, that the user type function Lua state may allocate, with 0 meaning no limit (default 256). |
| `LuwuTypeFunctionTimeLimitMs` | FInt = 2000 | Analysis | Limit | Dev Chrysalis Dalal | 2026-10-19 |  | `helpful-type-functions` | Longest time, in milliseconds, one user type function evaluation may run before it errors, with 0 meaning no limit (default 2000). |
| `OptLuwuReflectUseAtoms` | FFlag | VM | Perf | cheesycod | 2026-08-27 |  | `ast` | Makes the `reflect` library install a `useratom` callback, so AST node field names resolve as atoms instead of by string comparison. |

## Upstream flags upstream still has (106)

| Flag | Type | Area | Kind | Upstream release | Preset | Branch | What turning it on does |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `CodegenHeuristicsBlockInstructionLimit` | FInt = 65'536 | CodeGen | Limit | 0.602 |  | master | Maximum IR instructions in one block before native compilation skips the function (default 65536). |
| `CodegenHeuristicsBlockLimit` | FInt = 32'768 | CodeGen | Limit | 0.602 |  | master | Maximum IR blocks in one function before native compilation skips it (default 32768). |
| `CodegenHeuristicsInstructionLimit` | FInt = 1'048'576 | CodeGen | Limit | 0.602 |  | master | Maximum total IR instructions that native compilation of one module may produce before it gives up (default 1048576). |
| `DebugCodegenChaosA64` | FFlag | CodeGen | Debug | 0.602 |  | master | Makes the arm64 register allocator pick registers from the low end, to provoke more conflicts in testing. |
| `DebugCodegenLimitRegs` | FFlag | CodeGen | Debug | 0.727 |  | master | Limits the native code register allocators to a few general-purpose and SIMD registers, to stress spilling in tests. |
| `DebugCodegenOptSize` | FFlag | CodeGen | Debug | 0.602 |  | master | Skips the native code optimization passes after the first constant propagation, to produce smaller code. |
| `DebugCodegenSkipNumbering` | FFlag | CodeGen | Other | 0.602 |  | master | No effect: the flag is defined, but nothing reads it in Luwu or upstream. |
| `DebugLogFragmentsFromAutocomplete` | FFlag | Analysis | Debug | 0.710 |  | master | Logs the source fragment that fragment autocomplete selected, and any internal errors it caught. |
| `DebugLuauAbortingChecks` | FFlag | CodeGen | Debug | 0.602 |  | master | Keeps native guards that constant propagation proved redundant as aborting checks, so a wrong proof crashes instead of running wrong code. |
| `DebugLuauAlwaysShowConstraintSolvingIncomplete` | FFlag | Analysis | Debug | 0.710 |  | master | Keeps the "constraint solving incomplete" error even when it is a module's only error. |
| `DebugLuauAssertOnForcedConstraint` | FFlag | Analysis | Debug | 0.710 |  | master | Asserts whenever the solver has to force-dispatch a constraint. |
| `DebugLuauCFG` | FFlag | Analysis | Debug | 0.728 |  | master | Builds an experimental control flow graph and type state for each module and feeds them to constraint generation. |
| `DebugLuauCheckNormalizeInvariant` | FFlag | Analysis | Debug | 0.602 |  | master | In assert builds, checks that every normalized type satisfies the normalizer's invariants. |
| `DebugLuauDumpCFGJson` | FFlag | Analysis | Debug | 0.724 |  | master | Prints each module's control flow graph as JSON before type checking. |
| `DebugLuauForbidInternalTypes` | FFlag | Analysis | Debug | 0.656 |  | master | Asserts that no internal types (free, blocked and similar) leak into a module's exported types. |
| `DebugLuauForceAllNewSolverTests` | FFlag | Analysis | Debug | 0.656 |  | master | Test harness only: runs tests marked as not passing the new solver under the new solver anyway. |
| `DebugLuauForceAllOldSolverTests` | FFlag | Analysis | Debug | 0.712 |  | master | Test harness only: runs tests marked as not passing the old solver under the old solver anyway. |
| `DebugLuauForceNonStrictMode` | FFlag | Analysis | Debug | 0.656 |  | master | Type checks every module in nonstrict mode, regardless of its directive or config. |
| `DebugLuauForceOldSolver` | FFlag | Analysis | Debug | 0.711 |  | master | Test harness only in Luwu: tests set it to run a case on the old solver, and no library code reads it. |
| `DebugLuauForceStrictMode` | FFlag | Analysis | Debug | 0.656 |  | master | Type checks every module in strict mode, regardless of its directive or config. |
| `DebugLuauFreezeArena` | FFlag | Analysis | Debug | 0.501 |  | master | Freezes type arenas (with memory protection) while they aren't being written, so stray writes to finished types crash. |
| `DebugLuauFreezeDuringUnification` | FFlag | Analysis | Debug | 0.521 |  | master | Freezes the module's type arena while the old solver unifies, to catch unifier code that allocates types. |
| `DebugLuauLogBindings` | FFlag | Analysis | Debug | 0.656 |  | master | Prints the scope bindings when constraint solving finishes. |
| `DebugLuauLogCFG` | FFlag | Analysis | Debug | 0.724 |  | master | Prints the control flow graph built for each module before type checking. |
| `DebugLuauLogSolver` | FFlag | Analysis | Debug | 0.535 |  | master | Prints the constraint solver's constraints and every dispatch step to stdout. |
| `DebugLuauLogSolverToJson` | FFlag | Analysis | Debug | 0.535 |  | master | Records a JSON log of constraint generation and solving for each checked module. |
| `DebugLuauLogSolverToJsonFile` | FFlag | Analysis | Debug | 0.656 |  | master | Writes the solver's JSON log to a `.log.json` file per module instead of stdout. |
| `DebugLuauLogTypeFamilies` | FFlag | Analysis | Debug | 0.656 |  | master | Prints each type function reduction, and why stuck reductions are stuck, to stdout. |
| `DebugLuauMagicTypes` | FFlag | Analysis | Debug | 0.501 |  | master | Recognizes the magic `_luau_ice` and `_luau_print` type names, which trigger an internal compiler error or print a type during checking. |
| `DebugLuauMagicVariableNames` | FFlag | Analysis | Debug | 0.710 |  | master | Makes autocomplete raise an internal error on the magic variable name `_luau_autocomplete_ice`. |
| `DebugLuauReportReturnTypeVariadicWithTypeSuffix` | FFlag | Parser | Debug | 0.710 |  | master | Sets a telemetry flag when the parser sees a variadic return type followed by a union or intersection suffix. |
| `DebugLuauSubtypingCheckPathValidity` | FFlag | Analysis | Debug | 0.656 |  | master | Asserts that every type path recorded in a subtyping failure reason can be traversed on the types it describes. |
| `DebugLuauTimeTracing` | FFlag | Common | Debug | 0.521 |  | master | Turns on time-trace event recording (the CLI's `--timetrace`) in builds compiled with `LUAU_ENABLE_TIME_TRACE`. |
| `DebugLuauToStringNoLexicalSort` | FFlag | Analysis | Debug | 0.602 |  | master | Stops `ToString` from sorting union and intersection members alphabetically. |
| `DebugLuauUserDefinedClasses` | FFlag | Common | Feature | 0.721 |  | `luau-class-compat` | In Luwu, only turns on the upstream class compatibility mode (the same as `DebugLuwuLuauClassCompat`). |
| `DebugLuauUserDefinedClassesRuntime` | FFlag | Common | Feature | 0.721 |  | `luau-class-compat` | In Luwu, only turns on the upstream class compatibility mode (the same as `DebugLuwuLuauClassCompat`). |
| `DebugLuauVerboseTypeNames` | FInt = 0 | Analysis | Debug | 0.521 |  | master | Verbosity of printed free and generic type names: 1 adds polarity, and higher levels add scope and other details (default 0). |
| `FixMathNoisePrecision` | FFlag | VM | Fix | 0.716 | Recommended | master | Wraps `math.noise` inputs modulo 256 before converting them to `float`, so large coordinates no longer collapse to almost always 0. |
| `LuauAddRecursionCounterToNonStrictTypeChecker` | FFlag | Analysis | Fix | 0.710 | Recommended | master | Adds a recursion counter to the nonstrict type checker, so deeply nested code stops before overflowing the stack. |
| `LuauAllowGlobalDeclarationToBeCalledClass` | FFlag | Parser | Fix | 0.722 | Recommended | master | Parses `declare class: T` as a global named `class` rather than as an extern class declaration, so a `class` library can be declared. |
| `LuauBytecodeFold` | FFlag | VM | Perf | 0.731 |  | master | Makes the JIT inliner constant-fold the combined caller graph before emitting the inlined proto. |
| `LuauCheckFunctionStatementTypes` | FFlag | Analysis | Fix | 0.710 | Recommended | master | Checks that a function defined with `function t.x()` matches the declared type of the field it is assigned to. |
| `LuauCheckRecursionLimit` | FInt = 300 | Analysis | Limit | 0.501 |  | master | Maximum nesting depth of statements and expressions the old solver checks before it reports "code too complex" (default 300). |
| `LuauCodeGenBlockSize` | FInt = 4 * 1024 * 1024 | CodeGen | Limit | 0.602 |  | master | Size of each executable memory block the native code allocator reserves (default 4 MiB). |
| `LuauCodeGenLiveSlotReuseLimit` | FInt = 8 | CodeGen | Limit | 0.710 |  | master | Maximum number of overlapping live reused slot lookups before constant propagation stops reusing them, to limit register pressure (default 8). |
| `LuauCodeGenMaxTotalSize` | FInt = 256 * 1024 * 1024 | CodeGen | Limit | 0.602 |  | master | Maximum total executable memory native code may use (default 256 MiB). |
| `LuauCodeGenMinLinearBlockPath` | FInt = 3 | CodeGen | Limit | 0.602 |  | master | Minimum length of a block chain that native constant propagation will linearize (default 3). |
| `LuauCodeGenReuseSlotLimit` | FInt = 64 | CodeGen | Limit | 0.602 |  | master | Maximum number of cached table and buffer address lookups constant propagation keeps for reuse per block (default 64). |
| `LuauCodeGenReuseUdataTagLimit` | FInt = 64 | CodeGen | Limit | 0.656 |  | master | Maximum number of cached userdata tag checks constant propagation keeps for reuse per block (default 64). |
| `LuauCodegenNopPadding` | FFlag | CodeGen | Other | 0.719 |  | master | No effect: the flag is defined, but nothing reads it in Luwu or upstream. |
| `LuauCompileInlineDepth` | FInt = 5 | Compiler | Limit | 0.535 |  | master | Maximum nesting depth of inlined calls inside inlined calls (default 5). |
| `LuauCompileInlineThreshold` | FInt = 25 | Compiler | Limit | 0.535 |  | master | Base cost threshold for inlining a function call at `-O2` (default 25). |
| `LuauCompileInlineThresholdMaxBoost` | FInt = 300 | Compiler | Limit | 0.535 |  | master | Maximum percentage by which the inlining threshold may be boosted when inlining saves cost (default 300). |
| `LuauCompileLoopUnrollThreshold` | FInt = 25 | Compiler | Limit | 0.535 |  | master | Base cost threshold for unrolling constant-range numeric `for` loops at `-O2` (default 25). |
| `LuauCompileLoopUnrollThresholdMaxBoost` | FInt = 300 | Compiler | Limit | 0.535 |  | master | Maximum percentage by which the loop unroll threshold may be boosted when unrolling enables constant folding (default 300). |
| `LuauConstraintGeneratorRecursionLimit` | FInt = 300 | Analysis | Limit | 0.710 |  | master | Recursion depth at which constraint generation reports "code too complex" (default 300). |
| `LuauCyclicRequireShortCircuit` | FFlag | Require | Feature | 0.725 |  | master | Passes a module placeholder into a required module, so a cyclic require gets the partially initialized module instead of an error. |
| `LuauDisallowRedefiningBuiltinTypes` | FFlag | Analysis | Fix | 0.710 | Recommended | master | Reports a duplicate type definition error when a type alias reuses a builtin type name such as `string`. |
| `LuauExportValueSyntax` | FFlag | Parser | Feature | 0.723 |  | master | Enables `export` on values and functions (`export x = v`, `export function f`) in the parser, printer and autocomplete. |
| `LuauExportValueTypecheck` | FFlag | Analysis | Feature | 0.723 |  | master | Synthesizes a module's return type from its exported values, so export value syntax type checks for requirers. |
| `LuauFixIndexerSubtypingOrdering` | FFlag | Analysis | Fix | 0.602 |  | master | Makes the old solver unify a table property against a supertype's indexer in the right direction (property <: indexer result). |
| `LuauGcTraceUdata` | FFlag | VM | Feature | 0.731 |  | master | Adds per-tag userdata mark callbacks and a weak registry, so userdata can tell the GC which Lua values they reference. |
| `LuauImproveUniqueTableWidthSubtyping` | FFlag | Analysis | Fix | 0.728 | Recommended | master | Checks properties missing from a fresh table literal against the expected type as `nil` with the right variance, improving width subtyping for literals. |
| `LuauIndentTypeMismatchMaxTypeLength` | FInt = 10 | Analysis | Limit | 0.602 |  | master | Type mismatch errors put the given and wanted types on separate indented lines when either is longer than this (default 10). |
| `LuauInlineHitsThreshold` | FInt = 32 | VM | Limit | 0.721 |  | master | Number of hits a call-feedback slot needs before the VM asks the JIT inliner to inline that call target (default 32). |
| `LuauInstantiateInSubtyping` | FFlag | Analysis | Analysis | 0.602 |  | master | Makes the old solver's unifier instantiate generic function subtypes before unifying them. |
| `LuauJitInlineSmallFunSize` | FInt = 128 | VM | Limit | 0.729 |  | master | Call targets with at most this many instructions skip the JIT inliner's cost model and are inlined directly (default 128). |
| `LuauJitInlineThreshold` | FInt = 25 | VM | Limit | 0.729 |  | master | Base cost threshold the JIT inliner allows for an inlined call target before scaling it by the expected profit (default 25). |
| `LuauJitInlineThresholdMaxBoost` | FInt = 300 | VM | Limit | 0.729 |  | master | Maximum percentage by which the JIT inliner may scale its inline threshold when inlining saves cost (default 300). |
| `LuauJitInlineTooLongFunSize` | FInt = 0xFFFF | VM | Limit | 0.729 |  | master | The JIT inliner refuses callers or targets with more instructions than this (default `0xFFFF`). |
| `LuauMaxCFGDataflowIterations` | FInt = 2 | Analysis | Limit | 0.728 |  | master | Number of passes the experimental CFG type state dataflow makes over a function (default 2). |
| `LuauNonStrictTypeCheckerRecursionLimit` | FInt = 300 | Analysis | Limit | 0.710 |  | master | Recursion limit for the nonstrict-mode type checker, with 0 meaning unlimited (default 300). |
| `LuauNormalizeCacheLimit` | FInt = 100000 | Analysis | Limit | 0.602 |  | master | Maximum entries in the normalizer's caches before they are cleared, with 0 meaning unlimited (default 100000). |
| `LuauNormalizerInitialFuel` | FInt = 3000 | Analysis | Limit | 0.710 |  | master | Work budget the normalizer starts with for one operation before giving up as too complex (default 3000). |
| `LuauParseErrorLimit` | FInt = 100 | Parser | Limit | 0.501 |  | master | Maximum number of parse errors reported before stopping with "Reached error limit", where 1 disables error recovery (default 100). |
| `LuauPrimitiveInferenceInTableLimit` | FInt = 500 | Analysis | Limit | 0.710 |  | master | Table literals with more items than this stop inferring singleton types for their primitive entries, with 0 meaning no limit (default 500). |
| `LuauPromoteProto` | FFlag | VM | Perf | 0.727 |  | master | Makes `getproto` switch a closure to its JIT-optimized `Proto` (following `Proto::optimized`) the next time the closure is called. |
| `LuauRecursionLimit` | FInt = 1000 | Parser | Limit | 0.501 |  | master | Maximum parser recursion depth before it reports "Exceeded allowed recursion depth" (default 1000). |
| `LuauSimplificationComplexityLimit` | FInt = 8 | Analysis | Limit | 0.656 |  | master | Maximum union or intersection size the type simplifier will distribute over before leaving the intersection unsimplified (default 8). |
| `LuauSolverConstraintLimit` | FInt = 1000 | Analysis | Limit | 0.710 |  | master | Maximum number of constraints the new solver dispatches per module before stopping, with 0 meaning unlimited (default 1000). |
| `LuauSolverRecursionLimit` | FInt = 500 | Analysis | Limit | 0.656 |  | master | Recursion limit for recursive solver steps such as `HasIndexer` dispatch (default 500). |
| `LuauSolverV2` | FFlag | Analysis | Analysis | 0.656 |  | master | Makes the `Frontend` use the new constraint-based type solver instead of the old one. |
| `LuauStackGuardThreshold` | FInt = 1024 | Analysis | Limit | 0.710 |  | master | Bytes of native stack that must remain before Analysis stops recursing and reports "too complex", with 0 or less disabling the guard (default 1024). |
| `LuauStepRefineRecursionLimit` | FInt = 64 | Analysis | Limit | 0.710 |  | master | Recursion limit for the `refine` type function's step refinement (default 64). |
| `LuauSubtypingIterationLimit` | FInt = 20000 | Analysis | Limit | 0.710 |  | master | Maximum number of subtyping steps per check before it reports "too complex", with 0 meaning unlimited (default 20000). |
| `LuauSubtypingReasoningLimit` | FInt = 100 | Analysis | Limit | 0.710 |  | master | Maximum number of failure reasons a subtyping result keeps (default 100). |
| `LuauSubtypingRecursionLimit` | FInt = 100 | Analysis | Limit | 0.710 |  | master | Maximum recursion depth of a subtyping check before it gives up as too complex (default 100). |
| `LuauSubtypingSkipUnreadReasoning` | FFlag | Analysis | Perf | 0.741 | Recommended | `upstream-perf` | Skips building failure reasons and type paths for successful subtyping results, since nothing reads them. |
| `LuauSuggestionDistance` | FInt = 4 | Analysis | Limit | 0.521 |  | master | Maximum edit distance for the linter's "did you mean" suggestions, with 0 disabling them (default 4). |
| `LuauTableTypeMaximumStringifierLength` | FInt = 0 | Analysis | Limit | 0.501 |  | master | Maximum length of a stringified table type before truncation, with 0 meaning no separate limit (default 0). |
| `LuauTarjanChildLimit` | FInt = 10000 | Analysis | Limit | 0.501 |  | master | Maximum number of child types a substitution may visit before giving up (default 10000). |
| `LuauTarjanPreallocationSize` | FInt = 256 | Analysis | Limit | 0.656 |  | master | Initial capacity reserved for the Tarjan substitution's internal tables (default 256). |
| `LuauTypeCloneIterationLimit` | FInt = 100'000 | Analysis | Limit | 0.602 |  | master | Maximum steps when cloning types between arenas, with 0 meaning unlimited (default 100000). |
| `LuauTypeFamilyApplicationCartesianProductLimit` | FInt = 5'000 | Analysis | Limit | 0.656 |  | master | Maximum size of the cartesian product when distributing a type function over union arguments before the reduction fails (default 5000). |
| `LuauTypeFamilyGraphReductionMaximumSteps` | FInt = 1'000'000 | Analysis | Limit | 0.602 |  | master | Maximum type function reduction steps before reporting `CodeTooComplex` (default 1000000). |
| `LuauTypeFamilyUseGuesserDepth` | FInt = -1 | Analysis | Limit | 0.656 |  | master | Nesting depth of type function instances past which the reducer guesses results, with -1 meaning never (default -1). |
| `LuauTypeFunctionSerdeIterationLimit` | FInt = 100'000 | Analysis | Limit | 0.656 |  | master | Maximum steps when serializing or deserializing types for user type functions (default 100000). |
| `LuauTypeInferIterationLimit` | FInt = 20000 | Analysis | Limit | 0.501 |  | master | Iteration limit for the unifier before it reports "code too complex" (default 20000). |
| `LuauTypeInferRecursionLimit` | FInt = 165 | Analysis | Limit | 0.501 |  | master | Recursion limit used by the old type inference and the unifier's recursion counters (default 165). |
| `LuauTypeInferTypePackLoopLimit` | FInt = 5000 | Analysis | Limit | 0.501 |  | master | Maximum iterations when the old solver unifies type packs before it reports "code too complex" (default 5000). |
| `LuauTypeLengthLimit` | FInt = 1000 | Parser | Limit | 0.602 |  | master | Maximum number of parts in a union or intersection type annotation before the parser reports an error (default 1000). |
| `LuauTypeMaximumStringifierLength` | FInt = 500 | Analysis | Limit | 0.501 |  | master | Maximum length of a stringified type before it is truncated (default 500). |
| `LuauTypePathMaximumTraverseSteps` | FInt = 100 | Analysis | Limit | 0.602 |  | master | Maximum steps when traversing a type path for error reporting (default 100). |
| `LuauTypeSimplificationIterationLimit` | FInt = 128 | Analysis | Other | 0.710 |  | master | No effect: the limit is defined, but nothing reads it (it is unused upstream too). |
| `LuauUnifierRecursionLimit` | FInt = 100 | Analysis | Limit | 0.710 |  | master | Recursion limit for the new solver's `Unifier2` (default 100). |
| `LuauVisitRecursionLimit` | FInt = 500 | Analysis | Limit | 0.535 |  | master | Recursion limit for the generic type visitor (default 500). |

## Upstream flags upstream has removed (28)

Upstream removed each of these flags in a later release. Luwu can remove them too:

- **Permanent**: upstream kept the flag-on code. Luwu can delete the flag and its flag-off code, which also turns the
  change on for every embedder.
- **Reverted**: upstream kept the flag-off code. Luwu can delete the flag and its flag-on code.
- **Renamed**: a successor flag replaced it. The description names the successor.

| Flag | Type | Area | Kind | Added upstream | Removed upstream | Outcome | Preset | Branch | What turning it on does |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `AddReturnExectargetCheck` | FFlag | CodeGen | Fix | 0.710 | 0.742 | Reverted |  | master | Makes native return paths check that the caller's proto still has native code (`exectarget`), and continue in the interpreter if it doesn't. |
| `DebugLuauCyclicRequireTypeInference` | FFlag | Analysis | Debug | 0.731 | 0.733 | Renamed |  | master | Makes the constraint solver read constraints and free types from the constraint graph for experimental cyclic require inference (upstream renamed it `LuauCyclicRequireTypeInference`). |
| `LuauAutocompleteExport` | FFlag | Analysis | Feature | 0.723 | 0.734 | Permanent | Recommended | master | Offers the `export` keyword in statement-start autocomplete when export value syntax is on. |
| `LuauBidirectionalInferenceVariadics` | FFlag | Analysis | Fix | 0.727 | 0.735 | Permanent | Fixes | master | Extends a variadic expected function type to the lambda's parameter count, so lambdas passed to variadic functions get the expected parameter types. |
| `LuauCIProto` | FFlag | VM | Other | 0.727 | 0.740 | Permanent | Recommended | master | Stores the running `Proto` in each `CallInfo`, so the VM, debugger and native code read it there instead of through the closure (likely groundwork for JIT-swapped protos). |
| `LuauCallFeedback` | FFlag | VM | Perf | 0.721 | 0.742 | Permanent | Recommended | master | Makes the VM and native code record call-target feedback on `CALLFB` instructions, so the JIT inliner can inline hot call targets. |
| `LuauCodegenBufferInteger` | FFlag | CodeGen | Other | 0.719 | 0.747 | Reverted |  | master | No effect in Luwu: nothing reads it (upstream used it to lower the `buffer.readinteger` and `buffer.writeinteger` fastcalls to native loads and stores). |
| `LuauCodegenDseRestoreHints` | FFlag | CodeGen | Perf | 0.723 | 0.738 | Permanent |  | master | Lets dead store elimination leave lazy restore hints, so a removed store can be written back on spill. |
| `LuauCompileStringInterpTargetTop` | FFlag | Compiler | Perf | 0.719 | 0.737 | Permanent | Recommended | master | Has an interpolated string's format call write its result straight into the target register when it is the top register, avoiding a `MOVE`. |
| `LuauCstAttr` | FFlag | Parser | Feature | 0.726 | 0.732 | Permanent | Recommended | master | Records concrete syntax positions for attributes and attribute lists, so they print back exactly as written. |
| `LuauDirectFieldGet` | FFlag | VM | Feature | 0.719 | 0.738 | Permanent | Recommended | master | Enables `lua_registeruserdatadirectfieldget`, so a userdata tag can register per-field C getters that `GETTABLEKS` calls directly without a Lua call frame. |
| `LuauDoNotExportBrokenTypeFunction` | FFlag | Analysis | Fix | 0.726 | 0.735 | Reverted |  | master | Replaces unsolved type function instances in a module's exported types with the error type, so requirers don't see broken types. |
| `LuauDropUnionSubtypeReasoning` | FFlag | Analysis | Perf | 0.726 | 0.737 | Permanent | Recommended | master | Clears the failure reasons from checking a type against each member of a union, since nothing reads them. |
| `LuauEmitCallFeedback` | FFlag | Compiler | Perf | 0.721 | 0.748 | Permanent | Recommended | master | Emits `CALLFB` instructions with call-target feedback slots and marks inlinable functions with a cost model, for the JIT inliner. |
| `LuauGenericCounterMaxSteps` | FInt = 1500 | Analysis | Limit | 0.710 | 0.748 | Reverted |  | master | Maximum steps the generalizer's generic-counting traversal takes before it stops as having hit its limits (default 1500). |
| `LuauHigherOrderGenericInference` | FFlag | Analysis | Analysis | 0.729 | 0.739 | Permanent | Recommended | master | Resolves generic substitutions before replacing them and unifies type packs element by element, for better inference of higher-order generic calls. |
| `LuauIndexingIntoErrorGivesError` | FFlag | Analysis | Fix | 0.729 | 0.737 | Permanent | Fixes | master | Makes indexing a property on an error type produce the error type silently instead of reporting more errors. |
| `LuauIntegerBufferFastcalls` | FFlag | Compiler | Other | 0.717 | 0.747 | Reverted |  | master | No effect in Luwu: nothing reads it (upstream used it to emit fastcalls for `buffer.readinteger` and `buffer.writeinteger`). |
| `LuauIntegerType2` | FFlag | Analysis | Feature | 0.723 | 0.747 | Reverted |  | master | Enables the `integer` primitive type in Analysis (globals, normalization, printing and type functions), which Luwu integers build on. |
| `LuauRelaxConstraintOrderingForFunctionCheck` | FFlag | Analysis | Perf | 0.732 | 0.740 | Permanent | Recommended | `upstream-perf` | Lets bidirectional table literal checking run on any checkable expression without first waiting for every blocked type in the literal, mainly to skip an expensive pre-scan. |
| `LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier` | FFlag | Analysis | Fix | 0.727 | 0.739 | Permanent | Fixes | master | Drops the primitive type constraint and instead binds free types from literals to their primitive or singleton type during generalization. |
| `LuauSubtypingMissingPropertiesAsNil` | FFlag | Analysis | Analysis | 0.710 | 0.743 | Permanent | Recommended | master | Treats a property missing from a subtype table as `nil`, so it can satisfy an optional property in the supertype. |
| `LuauTrackPrefixLocal` | FFlag | Parser | Fix | 0.729 | 0.738 | Permanent | Fixes | master | Records the local that a type reference's module prefix (`mod.Type`) resolves to, so tools can find it. |
| `LuauTypeFunctionSerializeArgNames` | FFlag | Analysis | Fix | 0.721 | 0.744 | Permanent | Fixes | master | Keeps function parameter names when types are serialized into or out of user type functions. |
| `LuauTypeFunctionStructuredErrors` | FFlag | Analysis | Other | 0.715 | 0.743 | Permanent | Recommended | master | Makes the type function runtime builder collect structured serialization errors instead of plain strings (an internal refactor). |
| `LuauTypeFunctionSupportsFrozen` | FFlag | Analysis | Fix | 0.710 | 0.743 | Permanent | Fixes | master | Freezes types passed into user type functions, so `setproperty` and other mutators raise an error that tells you to use `types.copy`. |
| `LuauUdataMetatablePinned` | FFlag | VM | Fix | 0.725 | 0.734 | Permanent | Fixes | master | Makes the GC mark metatables registered for userdata tags with `lua_setuserdatametatable`, so they can't be collected while still in use. |
| `LuauUdtfTypeUseTaggedMetatable` | FFlag | Analysis | Perf | 0.729 | 0.741 | Permanent | Recommended | master | Creates type userdata in type functions with `lua_newuserdatataggedwithmetatable` instead of setting the metatable on each object. |
