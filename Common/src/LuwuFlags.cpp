// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
// whose format can still change.
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Luwu Traits (rfcs/classes/traits.md): enables `trait` declarations and `implements` (in progress). Needs LuwuClasses.
LUAU_FASTFLAGVARIABLE(LuwuTraits)

// Luwu collections: the `collections` library of builtin classes (`pair`, `triple`, `quad`, `orderedmap`), in the VM
// and in Analysis's builtin definitions. A prototype whose API can still change. Needs LuwuClasses and
// LuwuNonePrimitive (`none` is the empty option).
LUAU_FASTFLAGVARIABLE(DebugLuwuCollections)
