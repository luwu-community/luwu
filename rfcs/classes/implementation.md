# Classes implementation notes

## Bytecode and IR

In order to stop future conflicts with Luwu and upstream Luau bytecode versions, we just made
our own bytecode constant: `LWBC_MAGIC = 0xFF` (`Common/include/Luau/Bytecode.h`).
Luau's `LBC` version is accepted until and including versin 12. An `LBC` of 255 means this is Luwu
bytecode and to use Luwu's bytecode constant instead.

We use Luwu bytecode version 200 to signify in progress features.

- For now, classes are emitted under Luwu's WIP bytecode version (200). We will make this Luwu bytecode
  version 1 during Luwu 0.1.0 release.
- Opcodes: `NEWCLASSMEMBER`, `NEWOBJECT`, `CHECKSELFCLASS` (check and raise in one opcode since
  `0d37a0d0`), `GETOBJECTMEMBER`/`SETOBJECTMEMBER`, `JUMPXISA`.
- `LBC_CONSTANT_CLASS_SHAPE`: member names, flags and constant defaults. Round-trips through the
  bytecode graph.
- Type tag `LBC_TYPE_OBJECT`.
- IR: `TRY_OBJECT_MEMBER_ADDR`, `OBJECT_MEMBER_ADDR`, `TRY_CLASS_MEMBER_ADDR`, `TRY_OBJECT_NAMECALL_ADDR`,
  `CHECK_OBJECT_CLASS`, `CHECK_CLASS_FIELDS_CONSTRUCTIBLE`, `NEW_OBJECT`, `FALLBACK_NEWOBJECT`.
- `NEWOBJECT` is what makes object construction in-module very fast by allowing us to completely skip `__init` in lightweight cases.
  - It has three forms (operand C):
    - DEFAULT (POD fallback: `Cat()` or a table the compiler can't split, like `Cat(t)`),
    - FIELDS (primary constructors and POD `Cat { ... }` literals: values in A+1 onwards, no call)
    - INIT (sets up a `CALL` of `__init` in A+1).
- Opcodes without machine code (`NEWCLASSMEMBER`, `NEWOBJECT` DEFAULT/INIT) use a C fallback
  (`FALLBACK_NEWOBJECT`), never a VM exit: an exit breaks dead-store elimination and throws rest of
  the function back to the interpreter.

## Optimizations

### Field access at a constant offset (in the same module only)

- `self.x` compiles to `GETOBJECTMEMBER` with a field offset, skipping any caching/slots and hash lookups.
- It **skips the private check**. That's sound because it's only emitted where the compiler has proven the
  receiver's class and is compiling that class's own code.
- Required a fix to give `self` the `LBC_TYPE_OBJECT` hint so it stops taking the `any` path (not to be confused
  with type system `any`). This costs us a bit of metatable oop performance in interpreted.
- `GETOBJECTMEMBER/SETOBJECTMEMBER` carry no native tag or shape guards; the compiler guarantees them.

### Inlining at O2

We inline method calls of objects whenever possible and profitable. This is the biggest speedup we can get
over tables since class shapes are known statically, `const`, and immutable, and we have methods available
that we can safely inline.

#### Speedups

This section assumes `--!trust` is disabled and users write `--!strict` code normally without optimizing for every single
method inline. In the section below, you'll see even better numbers that appear when users opt into trusting type annotations.

In the same module, compared to equivalent metatable OOP (these include call-site construction and
constant-offset fields, not just inlining):

- a math-heavy benchmark (vecphysics) is ~1.76x faster.
- a method-call-heavy benchmark (bank) is ~1.3x faster.

On a POD bench, POD classes branching on fused `class.isinstance(c, C)` measured around ~1.35x faster than enum-like tagged tables
branching on `t.kind`. This one isn't from inlining: it comes from fusing `class.isinstance` into `JUMPXISA` and
from the check proving the receiver, which turns its field reads into constant offsets.

Across modules, nothing inlines. A generic `List<T>` class used from another module is ~1.1x faster than
metatable OOP overall, and up to ~1.4x on individual operations.

We are extremely interested in cross-module inlining, which will take inlining improvements to other modules when we
can resolve and link them statically.

#### Speedups when trusting annotations

With `--!trust` mode enabled in code that uses type annotations normally, we see even better speedups in-module.
These improvements are particularly useful with composition, because people would never remember to do an `isinstance`
check to prove a local variable from a field their `self` holds.

In `--!trust` mode:

- a math-heavy benchmark (vecphysics) got 1.24x faster, going from 1.76x to 2.20x the speed of the equivalent
  metatable OOP.
- a method-call-heavy benchmark (bank) got ~1.14x faster.
- the same-module `List<T>` benchmark, with `local l: List<number> = List.with_capacity(n)`, inlines its method
  calls and is ~1.8x faster than metatable OOP (up to ~4.5x on small accessors).

#### What gets inlined

Receivers we inline on:

- Proven (compiler statically knows object is expected class; the inlined body skips `CHECKSELFCLASS`):
  - `self:method()` inside the same class
  - inside `if class.isinstance(x, C) then` if `x` isn't reassigned in that region or from a closure, with `C`
    in same module.
  - after `assert(class.isinstance(x, C))` if `x` isn't reassigned in that region or from a closure, with `C`
    in same module.
  - a never-reassigned local initialized by the constructor: `local/const cat = Cat(n)`
  - an inlined method's `self`, or an inlined function's parameter, when its argument was proven
- Trusted (only with `--!trust`; the inlined body keeps `CHECKSELFCLASS`, which throws a runtime error
  if incorrect `self`):
  - an annotated local or parameter: `p: Path`
  - an annotated field: `self.pos: Vec2`

Never inlined:

- `__init`: `const` fields can only be written from `__init` itself
- a private method called from outside its class: inlining would skip the private check
- from outside the class, a method whose body has private access that's checked at runtime (it would be
  checked against the caller):
  - using a private method or static
  - writing a private `const` field
  - constructing through a private constructor
  - `x[k]` on a possible object
- a body with nested functions, across classes: the nested function's proto would get the caller's class

Private fields accessed through `self` don't block inlining: they compile to constant offsets.

**How it works:**

- `resolveReceiverClass` returns a `ReceiverClass`; its `proven` flag decides whether the inline site emits
  `CHECKSELFCLASS` (`selfIsAlreadyChecked`). The flag travels with the result so a new resolution path can't
  skip the check by accident.
- Entry points: `tryResolveMethodCall`, `tryCompileInlinedCall`, `classFromConstruction`,
  `matchAssertIsinstanceProof`.
- Privacy rules for inlining into outside code: `InlinedPrivateAccessVisitor`.
- `FFlag::DebugLuwuCompilerTrustsTypeAnnotations` makes `--!trust` available; a file is trusted only with the
  flag on and `--!trust` in the file.
- `return obj:method()` is multret and never inlines; tests have to assign to a local first.

### Method call slot cache (works across modules)

The bulk of our optimizations rely on in-module inlining, which is unavailable across modules. Since classes are
often going to be written one-class-per-file for organizational purposes, we can't park on cross-module inlining
to get classes faster than metatable OOP. One way we solve this is by caching method calls in slots, similar to
tables.

We reuse the existing `NAMECALL` caching Luau added to speed up `t:method()` calls on tables (metatable oop) by
allowing it to work on objects as well. This allows us to optimize methodcalls across module boundaries using
the existing mechanism, without needing a dedicated opcode for methods on objects.

At runtime, the first time `obj:method()` is called (when not inlined), it finds the offset slot of the method
alongside the call in `memberstooffset`. It then saves that offset into a cached slot so later calls from the
same call site can skip the lookup, as long as the receiver is the same class. Like upstream's, it only helps
code that runs more than once: a loop, or a function called repeatedly. Thankfully, hot code usually does,
making this a worthwhile optimization for speeding up hot loops and hot function calls.

When a method is not inlined and the slot lookup misses, we fallback to a regular table lookup.
The lookup table is a regular Luwu table created alongside a class definition's initialization. We've looked
into C++ native data structures but found that just keeping it a Luwu table is less complex and relatively fast
compared to using a C++ hashtable that we'd need to hash keys on (the Luwu strings are already interned), in
addition to extra testing coverage.

**How it works:**

- It reuses upstream's `NAMECALL` opcode, so no new opcode and no compiler work. For tables, operand C
  already remembers the hash slot the method was last found in; for objects it remembers the member's
  offset instead.
- The first call looks the method up by name and records the offset, and checks for private access.
  Every later call checks that the class's member at that offset has the right name, then loads it directly.
- It's all decided at runtime, so it needs no type information. A call site that sees both tables and
  objects stays correct: the check fails, the lookup runs again, and the cache is updated.
- A dedicated `METHODCALL` opcode was rejected: across modules the compiler can't know the receiver is an
  object, so it would still have to emit `NAMECALL`.
- The check is `offsettomember[C] == name`, in the interpreter and in native code
  (`TRY_OBJECT_NAMECALL_ADDR`).
- Native code only reads the cache. The interpreter writes it, and under native code only the fallbacks
  in `CodeGenUtils.cpp` (`executeNAMECALL` etc.) do. Any new object fast path needs a fallback that
  patches C, or it misses forever.
- Offsets above `LUAR_MAX_CACHED_MEMBER_SLOT` (255) aren't cached.

## Type system

A class is an `ExternType`, the same representation embedder userdata uses. Each class declaration makes two
of them from one `AstStatClass`: the class value and the object type. Most of the type system work is
teaching the shared `ExternType` machinery to tell those apart from each other and from userdata.

### Nominal roots

Upstream has one nominal top type. Luwu has four parentless ones, one per nominal family:

- `userdata`: embedder types
- `class`: every class value
- `object`: every instance
- `vector`: reuses the `ExternType` representation to get fields `x`, `y`, and `z` for free.

Pointers:

- `BuiltinTypes::nominalRoots()` (`Type.h`), used where code reasons about all nominal types at once
  (`Normalize.cpp`).
- `ExternType::root` says which family a type belongs to. Nominal identity checks compare it
  (`isSameGenericNominalInstantiation`, `isBareGenericNominalRoot` in `Type.cpp`); without it, a class value
  and its own object type were interchangeable.
- The class value and object type link to each other through the `Obj` and `Klass` relations (`Type.h`).

### Generics on classes

Upstream extern types can't take type parameters at all, so this touched most of the solver.
We added full support for generics on nominal types as part of LuwuGenericNominals, and fixed/improved/hooked
into that for classes.

- **`Box<number>` is expanded like a generic type alias**, except the result is a new nominal type instead
  of a table (`ConstraintSolver.cpp`, type alias expansion).
- **Each instantiation remembers its type arguments**, so hovers and errors show `Box<number>`, not `Box`.
  Every part of the solver that walks types had to learn to look at them.
- **`Box<number>` is the same type wherever it's written.** Upstream compared nominal types by pointer;
  we compare their arguments, invariantly (`Type.cpp`, `Subtyping.cpp`).
- **A class can refer to itself** (`self`, a method returning `Box<T>`, a class declared later) before its
  own members are solved. That needed a new constraint, `InstantiateNominalPropConstraint`.
- **Recursive generics:** a class can refer to itself with different type arguments, which upstream always
  refuses. It's only refused when that would expand forever (`A<{T}>`).
- **Construction infers type arguments from the expected type**, so `local e: Exception<number> =
  Exception("hi")` is an error instead of widening.
- Defaults work like type alias defaults: `class Box<T = string>`.

**Known issue:** a table literal passed as a generic class's `T` is often inferred on its own
(`{ kind: string }`) instead of against the expected type (`{ kind: "InvalidInput" }`), so
`Exception<{ kind: string }>` is rejected where `Exception<Info>` was expected.

- Works: `local e: Exception<Info> = Exception({ kind = "InvalidInput" })`, and `return Exception(...)` from
  a function annotated `: Exception<Info>`.
- Broken: passing `Exception({ ... })` as an argument, putting it in a table field, or reassigning a
  variable with it.

Workaround: annotate the table first (`local info: Info = { ... }`) and pass `info`.

### Naming classes

- `class<T>` gets the class value of object type `T`; `objectof` goes the other way and is only created by
  refinements. Both are in `BuiltinTypeFunctions.cpp` (`classFunc`, `objectofFunc`).
- `class<...>` is routed to the type function in `resolveReferenceType` (`ConstraintGenerator.cpp`), so bare
  `class` keeps its binding as the top type.
- `typeof(Cat)` is an error suggesting `class<Cat>`, since both print as `Cat` otherwise.
- Same-name type mismatches say `class<Item>` vs `Item` instead of repeating the module
  (`nominalDisplayName`, `Error.cpp`).

### Refinement and the `class` library

- `class.isinstance(x, C)` refines `x` to `objectof<C>`: `matchIsInstanceGuard` (`ConstraintGenerator.cpp`).
  It has to be recognized by the checker; the solver can't refine on `class.of(x) == C`.
- `class.name` and `class.fields` have precise return types: `MagicClassName`, `MagicClassFields`
  (`BuiltinDefinitions.cpp`).

### Checker rules

These mirror the runtime rules, so the checker and the VM agree:

- private members, `const` fields (`isInitWritingItsSelf`), and private constructors, in `TypeChecker2.cpp`.
- reading `__init` by name: `ConstructorReadByName`.
- a private constructor nothing in the class calls: `UninstantiableClass`.
