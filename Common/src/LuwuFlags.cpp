// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
// whose format can still change.
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Luwu Push and Pop Operators (rfcs/push-pop-operators.md): `target <+ values` and `<-container` (in progress). While it
// is on, the compiler emits WIP bytecode (version 200), which has LOP_PUSH and LOP_POP.
LUAU_FASTFLAGVARIABLE(DebugLuwuPoperators)
