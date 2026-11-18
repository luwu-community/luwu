# Luna DSL

Luna is a DSL for simplifying luwu NCG and currently only handles IR lowering right now.

IR metadata lives in `defs/ir`: operand kinds, results, effects, and facts stay independent
of the machine architecture. Each command calls an ordinary Scheme procedure in
`defs/lowering`, with explicit A64 and X64 branches that build C++ using `gen:*`.
There is no second SSA graph, micro-op scheduler, fusion pass, or register ownership planner.

For example, a lowering can allocate its result and emit an instruction directly:

```scheme
(define (lower-load-env)
  (case (current-target)
    [:a64
     (gen:seq
       (gen:set! (result-reg)
         (gen:method cpp:regs :allocReg (gen:enum :KindA64 :x) cpp:index))
       (gen:build :ldr (result-reg)
         (a64:mem a64:rClosure (gen:offsetof :Closure.env))))]
    [:x64 ...]))
```

Instruction order and allocation are authored in those branches. Use the existing native
lowering helpers for operand materialization and common sequences. A64 scratch registers
normally live until the end of the IrCmd; calls spill explicitly and can claim their ABI
result with `takeReg`. X64 uses `ScopedRegX64` and `IrCallWrapperX64`. Reserve scratch
registers before branches that rejoin, so both paths observe the same spills. Move overlapping
call arguments together, and reload the VM base after helpers that can move the stack.

Scheme procedures and hygienic macros provide reuse without introducing another lowering
language. They run on Luna's register bytecode VM with lexical upvalues.

## Usage

```bash
make -C Luna

# or...
make luna-dsl
```

To find documentation for Luna's syntax forms, use `./build/debug/luwu-dsl-compiler --describe` or `./build/debug/luwu-dsl-compiler --describe <core/a64/x64>` (for just one specific section)
