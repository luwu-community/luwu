# Luna DSL

Luna is a declarative domain-specific language (DSL) for Luwu, designed to simplify and unify Luwu Native Code Generator (NCG) IR lowering. Instead of duplicating handwritten instruction selection, register allocation, offset math, and branch handling across both `IrLoweringA64.cpp` (AArch64) and `IrLoweringX64.cpp` (x86-64), lowering logic is expressed as high-level macro rules in [`rules.luna`](rules.luna).

The standalone C++17 metacompiler (`luwu-dsl-compiler`) compiles `.luna` rules into C++ inline lowering headers:
- `LoweringRulesA64.h`
- `LoweringRulesX64.h`

---

## Syntax & Lexical Conventions

- **Comments**: Begin with `;` (or `;;`) and continue until the end of the line.
- **Lists**: Enclosed in parentheses `(...)` or square brackets `[...]`. Brackets are semantically equivalent to parentheses and conventionally used for parameter signatures, binding pairs, and reuse candidate lists.
- **Keywords**: Identifiers prefixed with a colon `:` (e.g. `:reg`, `:lua_State.ci`, `:LUA_BHOST_IMMUTABLE`).
- **Symbols**: Standard identifiers consisting of alphanumeric characters and `_`, `-`, `!`, `?`, `%`, `.`.
  - Mutating operations end with `!` by convention (e.g. `store-ptr!`, `branch-if-zero!`).
  - Predicates end with `?` by convention.
  - Value-producing forms begin with `%` by convention (e.g. `%produce-ptr`).
- **Numbers**: Decimal integers (e.g. `0`, `42`).
- **Application Expressions**: `(op args...)`.

---

## Defining Rules

### `define-rule` vs `define-subrule`

1. **`define-rule`**: Declares a top-level `IrCmd` lowering rule. Compiles directly into a C++ `lower_<OPCODE>(...)` function in `LoweringRules*.h`.
2. **`define-subrule`**: Declares a reusable macro template that is inlined at compile time. It does **not** generate a standalone C++ function.

```scheme
;; Macro Template:
(define-subrule (check-nonzero [value :reg] [fallback :label])
  (branch-if-not-zero! value fallback))

;; Top-Level Opcode Lowering:
(define-rule (check-readonly [table :reg] [fallback :label])
  (check-nonzero (load-u8 (mem table :LuaTable.readonly)) fallback))
```

### Parameter Types

Luna rules map Luwu IR positional arguments (`OP_A(inst)`, `OP_B(inst)`, `OP_C(inst)`, `OP_D(inst)`) based on their declared type:

| Type | Allowed In | Semantics in Top-Level Opcode Lowering | Example |
| :--- | :---: | :--- | :--- |
| `:reg` | Both | Hardware register operand (`lowering.regOp(OP_*(inst))`) | `[table :reg]` |
| `:uint` | Both | Unsigned 32-bit integer operand (`lowering.uintOp(OP_*(inst))`) | `[pc-offset :uint]` |
| `:int` | Both | Signed 32-bit integer operand (`lowering.intOp(OP_*(inst))`) | `[offset :int]` |
| `:tag` | Both | Lua type tag operand (`lowering.tagOp(OP_*(inst))`) | `[tag :tag]` |
| `:label` / `:op` | Both | Raw `IrOp` handle for branch targets | `[fallback :label]` |
| `:any` | **`define-subrule` only** | Raw AST token (literal, keyword, macro constant) | `[expected :any]` |

---

## Operations & Primitives

### Memory & State
- **`(mem <base-reg> <offset>)`**
  - Specifies an effective address: `[base + offset]`.
  - `<base-reg>` can be a register symbol or a nested expression (e.g. `(load-ptr ...)`).
  - `<offset>` can be a struct member keyword `:Struct.member` (resolving to C++ `offsetof(Struct, member)`), an immediate integer, or a constant symbol.
  - Examples: `(mem table :LuaTable.readonly)`, `(mem ci :CallInfo.savedpc)`.
- **`(code-address <offset>)`**
  - Computes the absolute host memory address of the bytecode instruction at `<offset>`.
  - AArch64: `emitAddOffset(build, reg, rCode, offset * sizeof(Instruction))`.
  - x86-64: `build.mov(reg, sCode); build.add(reg, offset * sizeof(Instruction))`.

### Load Operations
- **`(load-ptr <mem>)`**: Loads a 64-bit pointer.
- **`(load-u8 <mem>)`**: Loads an unsigned 8-bit byte.
- **`(load-u32 <mem>)`**: Loads an unsigned 32-bit word.

### Store Operations (Effects)
- **`(store-ptr! <mem> <val>)`**: Stores a 64-bit pointer into memory.
- **`(store-u8! <mem> <val>)`**: Stores an 8-bit byte into memory.

### Arithmetic & Transformation Operations
- **`(sign-extend-8 <val>)`**: Sign-extends an 8-bit value to a 32-bit integer (`sbfx` on A64, `movsx ..., byteReg(...)` on X64).
- **`(sign-extend-16 <val>)`**: Sign-extends a 16-bit value to a 32-bit integer (`sbfx` on A64, `movsx ..., wordReg(...)` on X64).
- **`(truncate-uint <val>)`**: Truncates an unsigned integer to 32 bits (`ubfx ..., 0, 32` on A64, `mov` on X64).

### Comparisons
- **`(is-equal <val> <expected>)`**: Evaluates boolean equality, producing 1 or 0 (`cmp` + `cset` on A64, `xor` + `cmp` + `setcc` on X64).

### Value Producing Special Forms
Rules that produce values for Luwu IR instructions (`inst.regA64` / `inst.regX64`) wrap their body in `%produce-<type>` or `%produce-<type>-reuse`:
- **`(%produce-ptr <ident> <body>)`**: Allocates a fresh 64-bit register (`regs.allocReg`), binds `<ident>`, and executes `<body>`.
- **`(%produce-ptr-reuse <ident> <param> <body>)`**: Allocates a 64-bit register reusing operand `<param>` (`regs.allocReuse` / `regs.allocRegOrReuse`), binds `<ident>`, and executes `<body>`.
- **`(%produce-i32-reuse <ident> <param> <body>)`**: Allocates a 32-bit integer register reusing operand `<param>`, binds `<ident>`, and executes `<body>`.
- **`(%produce-u32-reuse <ident> <param> <body>)`**: Allocates a 32-bit unsigned register reusing operand `<param>`, binds `<ident>`, and executes `<body>`.
- **`(%produce-i64-reuse <ident> <param> <body>)`**: Allocates a 64-bit integer register reusing operand `<param>`, binds `<ident>`, and executes `<body>`.
- **`(%produce-u64-reuse <ident> <param> <body>)`**: Allocates a 64-bit unsigned register reusing operand `<param>`, binds `<ident>`, and executes `<body>`.
- **Multi-Operand Reuse `(%produce-*-reuse <ident> [<param1> <param2>] <body>)`**:
  - Binary operations accept a list of candidate operands `[a b]` which enables Luna to reuse  operand register to be reused when it is safe to do so.

### Control Flow, Conditionals & Target Specialization
- **`(target-case [:x64 ...] [:a64 ...])`**: Conditionally compiles branches depending on target architecture (`a64` vs `x64`). Architecture-dependent operations (e.g. `x64:*`, `a64:*`) must be within a `target-case` for the architecture.
- **`(cond [<pred> <expr>...] ... [else <default>...])`**: Multi-way conditional branching
- **`(if <pred> <then-expr> [<else-expr>])`**: Single-condition branch.
- **`(begin <expr>...)`**: Evaluates expressions in sequence.
- **`(unsupported!)`**: Emits `CODEGEN_ASSERT(!"Unsupported instruction form");`.

#### Inspection Predicates
- **`(constant? <op>)`**: Tests `<op>.kind == IrOpKind::Constant`.
- **`(inst? <op>)`**: Tests `<op>.kind == IrOpKind::Inst`.
- **`(reused? <dest> <op>)`**: Tests `<dest> == lowering.regOp(<op>)`.
- **`(not-reused? <dest> <op>)`**: Tests `<dest> != lowering.regOp(<op>)`.
- **`(= <lhs> <rhs>)`**, **`(!= <lhs> <rhs>)`**: Compares two expressions, variables, or literals (`lhs == rhs`, `lhs != rhs`).
- **`(< <lhs> <rhs>)`**, **`(<= <lhs> <rhs>)`**, **`(> <lhs> <rhs>)`**, **`(>= <lhs> <rhs>)`**: Numerical relational comparisons.
- **`(int-val <op>)`**: Yields `lowering.intOp(<op>)`.
- **`(int64-val <op>)`**: Yields `lowering.int64Op(<op>)`.
- **`(fits-i32? <op>)`**: Tests `lowering.int64Op(<op>) >= INT32_MIN && <= INT32_MAX`.
- **`(and first rest ...)`:** Checks if `first` is falsy, if so evaluates to `#f`, otherwise evaluates to `(and rest ...)` with the base case of `(and a)` being `a`
- **`(or ...)`: Checks if `first` is truthy, if so evaluates to `first`, otherwise evaluate to `(or rest ...)` with the base case of `(or)` being `false`
- **`(not a)`**: Evaluates `!a`
- **`(fflag <flag>)`**, **`(dfflag <flag>)`**: Tests if a fast flag or dynamic fast flag is enabled (`FFlag::<flag>` / `DFFlag::<flag>`). Example: `(fflag :LuauCIProto)`

### Architecture-Specific Emission Builders
All architecture-dependent builders are prefixed with `<arch>:` and require an explicit `<dest>` operand (bound by `%produce-*` or `let`). They must appear inside a matching `target-case` branch.

#### x86-64 Builders (`x64:*`)
- **`(x64:inc! <dest>)`**: Emits `build.inc(<dest>)`.
- **`(x64:mov-reg! <dest> <src>)`**: Emits `build.mov(<dest>, regOp(<src>))`.
- **`(x64:mov-imm-32! <dest> <imm>)`**, **`(x64:mov-imm-64! <dest> <imm>)`**: Emits `build.mov` / `build.mov64`.
- **`(x64:mov-mem-reg-32! <dest> <src>)`**, **`(x64:mov-mem-reg-64! <dest> <src>)`**: Emits `build.mov(<dest>, memReg*Op(<src>))`.
- **`(x64:bswap-32! <dest>)`**, **`(x64:bswap-64! <dest>)`**: Emits `build.bswap(<dest>)`.
- **`(x64:not-32! <dest>)`**, **`(x64:not-64! <dest>)`**: Emits `build.not_(<dest>)`.
- **`(x64:and-32! <dest> <src>)`**, **`(x64:and-64! <dest> <src>)`**: Emits `build.and_(<dest>, memReg*Op(<src>))`.
- **`(x64:or-32! <dest> <src>)`**, **`(x64:or-64! <dest> <src>)`**: Emits `build.or_(<dest>, memReg*Op(<src>))`.
- **`(x64:xor-32! <dest> <src>)`**, **`(x64:xor-64! <dest> <src>)`**: Emits `build.xor_(<dest>, memReg*Op(<src>))`.
- **`(x64:imul-64! <dest> <src>)`**: Emits `build.imul(<dest>, memRegInt64Op(<src>))`.
- **`(x64:add-reg-32! <dest> <src>)`**, **`(x64:add-reg-64! <dest> <src>)`**: Emits `build.add(<dest>, regOp(<src>))`.
- **`(x64:add-imm-32! <dest> <imm>)`**, **`(x64:add-imm-64! <dest> <imm>)`**: Emits `build.add(<dest>, <imm>)`.
- **`(x64:sub-reg-32! <dest> <src>)`**, **`(x64:sub-reg-64! <dest> <src>)`**: Emits `build.sub(<dest>, regOp(<src>))`.
- **`(x64:sub-imm-32! <dest> <imm>)`**, **`(x64:sub-imm-64! <dest> <imm>)`**: Emits `build.sub(<dest>, <imm>)`.
- **`(x64:lea-reg-32! <dest> <base> <offset>)`**, **`(x64:lea-reg-64! <dest> <base> <offset>)`**: Emits `build.lea(<dest>, addr[regOp(<base>) + regOp(<offset>)])`.
- **`(x64:lea-imm-32! <dest> <base> <offset>)`**, **`(x64:lea-imm-64! <dest> <base> <offset>)`**: Emits `build.lea(<dest>, addr[regOp(<base>) + <offset>])`.
- **`(x64:lea-sub-32! <dest> <base> <offset>)`**, **`(x64:lea-sub-64! <dest> <base> <offset>)`**: Emits `build.lea(<dest>, addr[regOp(<base>) - <offset>])`.

#### AArch64 Builders (`a64:*`)
- **`(a64:mov-imm-32! <dest> <imm>)`**: Emits `build.mov(<dest>, intOp(<imm>))`.
- **`(a64:mov-imm-64! <dest> <imm>)`**: Emits `emitLoadInt64(build, <dest>, int64Op(<imm>))`.
- **`(a64:rev-32! <dest> <src>)`**, **`(a64:rev-64! <dest> <src>)`**: Emits `build.rev(<dest>, temp*(<src>))`.
- **`(a64:mvn-32! <dest> <src>)`**, **`(a64:mvn-64! <dest> <src>)`**: Emits `build.mvn_(<dest>, temp*(<src>))`.
- **`(a64:and-reg-32! <dest> <src1> <src2>)`**, **`(a64:and-reg-64! <dest> <src1> <src2>)`**: Emits `build.and_(<dest>, temp1, temp2)`.
- **`(a64:and-imm-32! <dest> <src> <imm>)`**: Emits `build.and_(<dest>, regOp(<src>), unsigned(intOp(<imm>)))`.
- **`(a64:orr-reg-32! <dest> <src1> <src2>)`**, **`(a64:orr-reg-64! <dest> <src1> <src2>)`**: Emits `build.orr(<dest>, temp1, temp2)`.
- **`(a64:orr-imm-32! <dest> <src> <imm>)`**: Emits `build.orr(<dest>, regOp(<src>), unsigned(intOp(<imm>)))`.
- **`(a64:eor-reg-32! <dest> <src1> <src2>)`**, **`(a64:eor-reg-64! <dest> <src1> <src2>)`**: Emits `build.eor(<dest>, temp1, temp2)`.
- **`(a64:eor-imm-32! <dest> <src> <imm>)`**: Emits `build.eor(<dest>, regOp(<src>), unsigned(intOp(<imm>)))`.
- **`(a64:mul-reg-64! <dest> <src1> <src2>)`**: Emits `build.mul(<dest>, temp1, temp2)`.
- **`(a64:add-reg-32! <dest> <src1> <src2>)`**, **`(a64:add-reg-64! <dest> <src1> <src2>)`**: Emits `build.add(<dest>, regOp(<src1>), regOp(<src2>))`.
- **`(a64:add-imm-32! <dest> <src> <imm>)`**, **`(a64:add-imm-64! <dest> <src> <imm>)`**: Emits `build.add(<dest>, regOp(<src>), <imm>)`.
- **`(a64:sub-reg-32! <dest> <src1> <src2>)`**, **`(a64:sub-reg-64! <dest> <src1> <src2>)`**: Emits `build.sub(<dest>, temp1, temp2)`.
- **`(a64:sub-imm-32! <dest> <src> <imm>)`**, **`(a64:sub-imm-64! <dest> <src> <imm>)`**: Emits `build.sub(<dest>, regOp(<src>), <imm>)`.
- **`(a64:fits-imm-32? <op>)`**, **`(a64:fits-imm-64? <op>)`**: Tests if immediate fits AArch64 12-bit range (`kMaxImmediate`).
- **`(a64:fits-mask-32? <op>)`**, **`(a64:fits-mask? <op>)`**: Tests if immediate matches an AArch64 logical bitmask (`AssemblyBuilderA64::isMaskSupported`).


### Branch Guards
- **`(branch-if-not-zero! <val> <label>)`**: Guards `val != 0` (`cbnz` on A64; memory/reg `cmp` on X64).
- **`(branch-if-zero! <val> <label>)`**: Guards `val == 0` (`cbz` on A64; memory/reg `cmp` on X64).
- **`(branch-if-equal! <val> <expected> <label>)`**: Guards `val == expected` (`cmp` + `b.eq` on A64; memory/reg `cmp` on X64).
- **`(branch-if-not-equal! <val> <expected> <label>)`**: Guards `val != expected` (`cmp` + `b.ne` on A64; memory/reg `cmp` on X64).
- **`(branch-if-negative! <val> <label>)`**: Guards `val < 0` (`cmp` + `b.lt` on A64; `test` + `jcc(Less)` on X64).

### Local Bindings & Temporary Registers
- **`(let ([<var> <expr>] ...) <body> ...)`**: Introduces lexically scoped local variables. Within `%produce-` bodies with a single binding, destination registers are reused directly when possible to avoid temporary allocations.
- **Explicit Temporary Registers `(alloc-temp <type>)`**: Allocates an explicit temporary register scoped to the enclosing `let` binding (`ScopedRegX64` on X64, `regs.allocTemp` on A64). Supported types include `:i64`, `:u64`, `:ptr`, `:i32`, `:u32`.
```scheme
(let ([tmp (alloc-temp :i64)])
  (x64:mov-imm-64! tmp b)
  (x64:add-reg-64! res tmp))
```
- **Automatic `let` Synthesis**: When a sub-rule takes a `:reg` parameter and the caller supplies a compound memory load, the expander automatically wraps the inlined body in a synthetic `let`:

```scheme
;; Source:
(define-rule (check-nonzero [value :reg] [fallback :label])
  (branch-if-not-zero! value fallback))

(define-rule (check-readonly [table :reg] [fallback :label])
  (check-nonzero
    (load-u8 (mem table :LuaTable.readonly))
    fallback))

;; Expands to:
(define-rule (check-readonly [table :reg] [fallback :label])
  (let ([value (load-u8 (mem table :LuaTable.readonly))])
    (branch-if-not-zero! value fallback)))
```

---

## Metacompiler Architecture

The `luwu-dsl-compiler` frontend and code generation pipeline is built around three core architectural pillars:

### 1. Source-Located Diagnostics (`CompileError`)
All compilation errors (syntax errors, arity mismatches, invalid types, bare architecture ops outside `target-case`) throw `CompileError` carrying a `SourceLocation` (`file`, `line`, `col`). Errors are formatted matching standard Clang/GCC conventions:
```
rules.luna:150:12: error: sign-extend-8 takes 1 argument
```

### 2. Structured Code Writer (`CodeWriter`)
Manual string formatting and raw indent string passing have been eliminated in favor of [`CodeWriter`](CodeWriter.h):
- **RAII Indentation**: `block(header, lambda)` automatically indents and formats braces `{ ... }`.
- **Declaration Hoisting**: Declarations (such as `ScopedRegX64`, `regs.allocTemp`, `Label fresh;`) are collected via `addDecl` and emitted at the top of the generated C++ functions.
- **Buffering**: `StringCodeWriter` enables isolated body evaluation before emitting hoisted declarations.

### 3. Lexical Scoping (`Scope` & `ScopeGuard`)
Environment management replaces $O(N)$ hash map cloning with linked symbol tables:
- **Linked Structure**: Each nested `let` or `%produce-*` allocates a lightweight [`Scope`](Scope.h) pointing to its parent scope.
- **RAII Guards**: `ScopeGuard` pushes and pops scopes in $O(1)$ without heap reallocation, ensuring exception-safe lexical scope unwinding.

