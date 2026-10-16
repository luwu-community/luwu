# Luna DSL

Luna is a DSL for simplifying luwu NCG and currently only handles IR lowering right now.

## Usage

```bash
make -C Luna

# or...
make luna-dsl
```

To find documentation for Luna's syntax forms, use `./build/debug/luwu-dsl-compiler --describe` or `./build/debug/luwu-dsl-compiler --describe <core/a64/x64>` (for just one specific section)

## Syntax & Lexical Conventions

- **Comments**: Begin with `;` (or `;;`) and continue until the end of the line.
- **Lists**: Enclosed in parentheses `(...)` or square brackets `[...]`. Brackets are semantically equivalent to parentheses and conventionally used for parameter signatures, binding pairs, and reuse candidate lists.
- **Keywords**: Identifiers prefixed with a colon `:` (e.g. `:reg`, `:lua_State.ci`, `:LUA_BHOST_IMMUTABLE`).
- **Symbols**: Standard identifiers consisting of alphanumeric characters and `_`, `-`, `!`, `?`, `%`, `.`.
  - Mutating operations end with `!`
  - Predicates usually end with `?`
  - Value-producing forms begin with `%` (`%produce-ptr`, `%produce-i32-reuse`).
- **Numbers**: Decimal integers (e.g. `0`, `42`).
- **Application Expressions**: `(op args...)`.

---

## Defining Rules

### `define-rule` vs `define-subrule`

1. **`define-rule`**: Declares a new top-level lowering rule. Compiles directly into a C++ `lower_<name>(...)` function in `LoweringRules<arch>.h`.
2. **`define-subrule`**: Declares a reusable macro template that is inlined at compile time. It does *not* generate a standalone C++ function.

```scheme
;; Macro Template:
(define-subrule (check-mem-nonzero-u8 [addr :any] [fallback :label])
  (target-case
    [:x64
     (x64:guard-nz! addr fallback)]
    [:a64
     (let ([tmp (alloc-temp :u32)])
       (load-u8! tmp addr)
       (a64:guard-nz! tmp fallback))]))

;; Top-Level Opcode Lowering:
(define-rule (check-readonly [table :reg] [fallback :label])
  (check-mem-nonzero-u8 (mem table :LuaTable.readonly :u8) fallback))
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

## Operations & Special Forms

### Value-Producing Special Forms
Rules that produce values for Luwu IR instructions wrap their body in `%produce-<type>` or `%produce-<type>-reuse`:
- **`(%produce-ptr <ident> <body>...)`**: Allocates a fresh 64-bit register (`regs.allocReg`), binds `<ident>`, and executes `<body>`.
- **`(%produce-i32 <ident> <body>...)`**, **`(%produce-u32 <ident> <body>...)`**: Allocates fresh 32-bit integer/unsigned register.
- **`(%produce-i64 <ident> <body>...)`**, **`(%produce-u64 <ident> <body>...)`**: Allocates fresh 64-bit integer/unsigned register.
- **`(%produce-f32 <ident> <body>...)`**, **`(%produce-f64 <ident> <body>...)`**: Allocates fresh 32-bit / 64-bit floating-point register.
- **`(%produce-vec <ident> <body>...)`**: Allocates fresh 128-bit SIMD vector register.
- **`(%produce-*-reuse <ident> <param> <body>...)`**: Allocates a result register reusing operand `<param>` when safe.
- **Multi-Operand Reuse `(%produce-*-reuse <ident> [<cand1> <cand2>] <body>...)`**:
  - Binary operations accept a list of candidate operands `[a b]` which enables Luna to reuse an operand register when it is safe to do so.

### Memory & State
- **`(mem <base-reg> <offset> [:<type>])`**
  - Specifies an effective address: `[base + offset]`.
  - `<base-reg>` can be a register symbol or a nested expression.
  - `<offset>` can be a struct member keyword `:Struct.member` (resolving to C++ `offsetof(Struct, member)`), an immediate integer, or an expression.
  - Examples: `(mem table :LuaTable.readonly)`, `(mem ci :CallInfo.savedpc)`.
- **`(sizeof :<type>)`**: Evaluates `sizeof(Type)` as a compile-time constant.

### Local Bindings & Temporary Registers
- **`(let ([<var> <expr>] ...) <body> ...)`**: Binds a local variable via normal lexical scoping rules. Within `%produce-` bodies with a single binding, destination registers are reused directly when possible to avoid temporary allocations.
- **`(alloc-temp <type>)`**: Allocates an explicit temporary register scoped to the enclosing `let` binding (`ScopedRegX64` on X64, `regs.allocTemp` on A64). Supported types include `:ptr`, `:i64`, `:u64`, `:i32`, `:u32`.
```scheme
(let ([tmp (alloc-temp :i64)])
  (x64:mov-imm-64! tmp b)
  (x64:add-reg-64! res tmp))
```

### Basic Control Flow 
- **`(target-case [:<arch> <body>...]...)`**: Selects branch based on active target (`:a64` vs `:x64`). Architecture-dependent operations (e.g. `x64:*`, `a64:*`) must reside inside their matching `target-case` branch.
- **`(cond [<pred> <body>...]... [else <body>...])`**: Multi-way conditional statement similar to Scheme's `cond` with an optional `else` clause that must be at the end of the `cond`
- **`(if <pred> <then> [<else>])`**: Single-condition branch, similar to Scheme's `if`. The `else` section is optional
- **`(begin <expr>...)`**: Evaluates expressions in sequence.
- **`(unsupported!)`**: Emits `CODEGEN_ASSERT(!"Unsupported instruction form");`.

### IR Condition Predicates

Can be used to check `IrCondition` operands:

- `(ir-cond-eq? <cond>)` / `(ir-cond-ne? <cond>)` (Equal)
- `(ir-cond-lt? <cond>)` / `(ir-cond-le? <cond>)` (Signed Less)
- `(ir-cond-gt? <cond>)` / `(ir-cond-ge? <cond>)` (Signed Greater)
- `(ir-cond-ult? <cond>)` / `(ir-cond-ule? <cond>)` (Unsigned Less)
- `(ir-cond-ugt? <cond>)` / `(ir-cond-uge? <cond>)` (Unsigned Greater)
