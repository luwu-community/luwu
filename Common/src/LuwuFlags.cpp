// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
// whose format can still change.
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Luwu: string.format and interpolated strings compile to a fastcall (LBF_STRING_FORMAT). While it is on, the compiler
// emits WIP bytecode (version 200), which a VM without the builtin can't run.
LUAU_FASTFLAGVARIABLE(LuwuStringFormatFastcall)

// Luwu: the `@[inline(reason)]` attribute. The compiler weighs a marked function by a third of its size at its call sites,
// and a call it still can't inline is a compile error at the call. In progress, so a Debug flag.
LUAU_FASTFLAGVARIABLE(DebugLuwuEncourageInline)
