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
- **`(byteswap-32 <val>)`**, **`(byteswap-64 <val>)`**: Endian byte-swap (`rev` on A64, `bswap` on X64) with explicit 32-bit or 64-bit width.
- **`(add-32 <val1> <val2>)`**, **`(add-64 <val1> <val2>)`**: Integer addition (`add` on A64, `add` or `lea` on X64) with explicit 32-bit or 64-bit width.
- **`(sub-32 <val1> <val2>)`**, **`(sub-64 <val1> <val2>)`**: Integer subtraction (`sub` on A64, `sub` on X64) with explicit 32-bit or 64-bit width.
- **`(mul-64 <val1> <val2>)`**: Integer multiplication (`mul` on A64, `imul` on X64) with explicit width.

### Bitwise Operations
- **`(bitnot-32 <val>)`**, **`(bitnot-64 <val>)`**: Bitwise negation (`mvn_` on A64, `not_` on X64) with explicit width.
- **`(bitand-32 <val1> <val2>)`**, **`(bitand-64 <val1> <val2>)`**: Bitwise AND (`and_` on A64/X64).
- **`(bitor-32 <val1> <val2>)`**, **`(bitor-64 <val1> <val2>)`**: Bitwise OR (`orr` on A64, `or_` on X64).
- **`(bitxor-32 <val1> <val2>)`**, **`(bitxor-64 <val1> <val2>)`**: Bitwise XOR (`eor` on A64, `xor_` on X64).

### Comparisons
- **`(is-equal <val> <expected>)`**: Evaluates boolean equality, producing 1 or 0 (`cmp` + `cset` on A64, `xor` + `cmp` + `setcc` on X64).

### Value Producing Special Forms
Rules that produce values for Luwu IR instructions (`inst.regA64` / `inst.regX64`) wrap their body in `%produce-<type>` or `%produce-<type>-reuse`:
- **`(%produce-ptr <body>)`**: Allocates a fresh 64-bit register (`regs.allocReg`) and executes `<body>` targeting it.
- **`(%produce-ptr-reuse <param> <body>)`**: Allocates a 64-bit register reusing operand `<param>` (`regs.allocReuse` / `regs.allocRegOrReuse`) and executes `<body>`.
- **`(%produce-i32-reuse <param> <body>)`**: Allocates a 32-bit integer register reusing operand `<param>`.
- **`(%produce-u32-reuse <param> <body>)`**: Allocates a 32-bit unsigned register reusing operand `<param>`.
- **`(%produce-i64-reuse <param> <body>)`**: Allocates a 64-bit integer register reusing operand `<param>`.
- **`(%produce-u64-reuse <param> <body>)`**: Allocates a 64-bit unsigned register reusing operand `<param>`.
- **Multi-Operand Reuse `(%produce-*-reuse [<param1> <param2>] <body>)`**:
  - Binary operations accept a vector of candidate operands `[a b]`.
  - On **AArch64** (3-address: `op dst, src1, src2`), emits `{OP_A(inst), OP_B(inst)}`, allowing either operand register to be reused.
  - On **x86-64** (2-address: `op dst, src`), integer arithmetic restricts reuse strictly to `{OP_A(inst)}` so as not to clobber `src2`.

### Control Flow, Conditionals & Target Specialization
- **`(target-case [:x64 ...] [:a64 ...])`**: Conditionally compiles branches depending on target architecture (`a64` vs `x64`).
- **`(cond [<pred> <expr>...] ... [else <default>...])`**: Multi-way conditional branching mapping directly to C++ `if (...) { ... } else if (...) { ... } else { ... }`.
- **`(if <pred> <then-expr> [<else-expr>])`**: Single-condition branch.
- **`(begin <expr>...)`**: Evaluates expressions in sequence.
- **`(unsupported!)`**: Emits `CODEGEN_ASSERT(!"Unsupported instruction form");`.

#### Inspection Predicates
- **`(constant? <op>)`**: Tests `<op>.kind == IrOpKind::Constant`.
- **`(inst? <op>)`**: Tests `<op>.kind == IrOpKind::Inst`.
- **`(reused? <op>)`**: Tests `destReg == lowering.regOp(<op>)`.
- **`(not-reused? <op>)`**: Tests `destReg != lowering.regOp(<op>)`.
- **`(= (int-val <op>) <n>)`**: Tests `lowering.intOp(<op>) == <n>`.
- **`(= (int64-val <op>) <n>)`**: Tests `lowering.int64Op(<op>) == <n>`.
- **`(fits-i32? <op>)`**: Tests `lowering.int64Op(<op>) >= INT32_MIN && <= INT32_MAX`.
- **`(fits-imm-a64-32? <op>)`**, **`(fits-imm-a64-64? <op>)`**: Tests if immediate fits AArch64 12-bit range (`kMaxImmediate`).
- **`(and ...)`, `(or ...)`, `(not ...)`**: Standard boolean combinators.

### Fine-Grained Emission Builders
- **`(lea-reg-32! <base> <offset>)`**, **`(lea-reg-64! <base> <offset>)`**: Emits `build.lea(dest, addr[regOp(base) + regOp(offset)])`.
- **`(lea-imm-32! <base> <offset>)`**, **`(lea-imm-64! <base> <offset>)`**: Emits `build.lea(dest, addr[regOp(base) + imm])`.
- **`(lea-sub-32! <base> <offset>)`**, **`(lea-sub-64! <base> <offset>)`**: Emits `build.lea(dest, addr[regOp(base) - imm])`.
- **`(add-reg-32! <src>)`**, **`(add-reg-64! <src>)`**: Emits `build.add(dest, regOp(src))`.
- **`(add-imm-32! <src>)`**, **`(add-imm-64! <src>)`**: Emits `build.add(dest, imm)`.
- **`(sub-reg-32! <src>)`**, **`(sub-reg-64! <src>)`**: Emits `build.sub(dest, regOp(src))`.
- **`(sub-imm-32! <src>)`**, **`(sub-imm-64! <src>)`**: Emits `build.sub(dest, imm)`.
- **`(inc!)`**: Emits `build.inc(dest)`.
- **`(mov-reg! <src>)`**: Emits `build.mov(dest, regOp(src))`.
- **`(mov-imm-32! <src>)`**, **`(mov-imm-64! <src>)`**: Emits `build.mov` / `build.mov64`.
- **`(add-tmp-64! <imm>)`**, **`(sub-tmp-64! <imm>)`**: Allocates `ScopedRegX64 tmp`, moves 64-bit immediate, and adds/subs.

### Branch Guards
- **`(branch-if-not-zero! <val> <label>)`**: Guards `val != 0` (`cbnz` on A64; memory/reg `cmp` on X64).
- **`(branch-if-zero! <val> <label>)`**: Guards `val == 0` (`cbz` on A64; memory/reg `cmp` on X64).
- **`(branch-if-equal! <val> <expected> <label>)`**: Guards `val == expected` (`cmp` + `b.eq` on A64; memory/reg `cmp` on X64).
- **`(branch-if-not-equal! <val> <expected> <label>)`**: Guards `val != expected` (`cmp` + `b.ne` on A64; memory/reg `cmp` on X64).
- **`(branch-if-negative! <val> <label>)`**: Guards `val < 0` (`cmp` + `b.lt` on A64; `test` + `jcc(Less)` on X64).

### Local Bindings & Automatic Synthesis
- **`(let ([<var> <expr>] ...) <body> ...)`**: Introduces lexically scoped local variables. Within `%produce-` bodies with a single binding, destination registers are reused directly when possible to avoid temporary allocations.
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
