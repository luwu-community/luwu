// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
// whose format can still change.
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Luwu Traits (rfcs/classes/traits.md): enables `trait` declarations and `implements` (in progress). Needs LuwuClasses.
LUAU_FASTFLAGVARIABLE(LuwuTraits)

// Luwu collections: the `collections` library of builtin classes (`Pair`, `Triple`, `Quad`, `OrderedMap`, `Vec`,
// `VecDeque`), in the VM and in Analysis's builtin definitions. A prototype whose API can still change. Needs
// LuwuClasses.
LUAU_FASTFLAGVARIABLE(DebugLuwuCollections)

// Luwu try expressions: `try expr` gives an `Ok` or an `Err`, and the `Ok`, `Err`, `Some` and `None` globals exist. A
// Debug flag while it's in progress: a function containing `try` doesn't compile to native code yet. Needs LuwuClasses
// and LuwuNonePrimitive. The parser gates the syntax, and while it is on the compiler emits WIP bytecode (version 200),
// which holds the protected ranges.
LUAU_FASTFLAGVARIABLE(DebugLuwuTryExpressions)
