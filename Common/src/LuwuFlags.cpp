// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
// whose format can still change.
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Luwu Traits (rfcs/classes/traits.md): enables `trait` declarations and `implements` (in progress). Needs LuwuClasses.
LUAU_FASTFLAGVARIABLE(LuwuTraits)

// Luwu builtin classes: standard library classes written in C++, in the VM and in Analysis's builtin definitions. So far
// that's the `collections` library (`OrderedMap`, `Vec`, `VecDeque`). A prototype whose API can still change. Needs
// LuwuClasses.
LUAU_FASTFLAGVARIABLE(DebugLuwuBuiltinClasses)

// Luwu try expressions: `try expr` gives an `Ok` or an `Err`, and the `Ok`, `Err`, `Some` and `None` globals exist. A
// Debug flag while it's in progress: a function containing `try` doesn't compile to native code yet. Needs LuwuClasses
// and LuwuNonePrimitive. The parser gates the syntax, and while it is on the compiler emits WIP bytecode (version 200),
// which holds the protected ranges.
LUAU_FASTFLAGVARIABLE(DebugLuwuTryExpressions)

// Luwu bundles: the `bundle` library, its classes, and the compiler's fastcalls and unfolding for it. A prototype whose
// API can still change. Needs LuwuClasses.
LUAU_FASTFLAGVARIABLE(DebugLuwuBundles)

// Luwu: the `@[inline(reason)]` attribute. The compiler weighs a marked function by a third of its size at its call sites,
// and a call it still can't inline is a compile error at the call. In progress, so a Debug flag.
LUAU_FASTFLAGVARIABLE(DebugLuwuEncourageInline)
