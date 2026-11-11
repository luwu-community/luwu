// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Disables getfenv/setfenv completely
//
// Required for multiple optimizations/features including:
// - proper tail calls.
// - SROA on classes (future opt.)
LUAU_FASTFLAGVARIABLE(LuwuNoFenv)

// proper tail calls: enables proper tail calls/tail call optimization
//
// Requires LuwuNoFenv.
LUAU_FASTFLAGVARIABLE(DebugLuwuProperTailCalls)
