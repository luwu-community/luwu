// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/LuwuFlags.h"

#include "Luau/Common.h"

// Flags used by both Ast/Compiler/Analysis and VM/CodeGen. Those libraries don't link each other, but all of them
// link Common.

// Luwu Classes (rfcs/classes): enables classes. While it is on, the compiler emits WIP bytecode (version 200),
// whose format can still change.
LUAU_FASTFLAGVARIABLE(LuwuClasses)

// Luwu Traits (rfcs/classes/traits.md): enables `trait` declarations and `implements` (in progress). Needs LuwuClasses.
LUAU_FASTFLAGVARIABLE(LuwuTraits)

// Luwu Destructuring (rfcs/destructuring.md): `.{x, y}` and `.(x, y)` patterns. The parser gates the syntax, and while it
// is on the compiler emits WIP bytecode (version 200), which positional patterns need for LOP_GETPOSITIONAL.
LUAU_FASTFLAGVARIABLE(LuwuDestructuring)

// Luwu Table Comprehensions (rfcs/table-comprehensions.md): `{ for ... in ... when ... give item }` (in progress). While it
// is on, the compiler emits WIP bytecode (version 200), which has LOP_PRESIZETABLE.
LUAU_FASTFLAGVARIABLE(LuwuTableComprehensions)

// Luwu Push and Pop Operators (rfcs/push-pop-operators.md): `target <+ values` and `<-container` (in progress). While it
// is on, the compiler emits WIP bytecode (version 200), which has LOP_PUSH and LOP_POP.
LUAU_FASTFLAGVARIABLE(DebugLuwuPoperators)

// Luwu: Luau modules (a `.luau` file) accept upstream Luau's class syntax and see upstream's `class` library: members
// without an access specifier are public, `class.classof` instead of `class.of`. Luwu's classes run underneath, so it
// also needs LuwuClasses. Off by default: it is how far upstream classes and Luwu classes overlap, still being worked out.
LUAU_FASTFLAGVARIABLE(DebugLuwuLuauClassCompat)
// Upstream Luau's class flags. Luwu replaced them with LuwuClasses; an embedder that sets them asks for upstream
// classes, which turns on DebugLuwuLuauClassCompat (luauClassCompat).
LUAU_FASTFLAGVARIABLE(DebugLuauUserDefinedClasses)
LUAU_FASTFLAGVARIABLE(DebugLuauUserDefinedClassesRuntime)

// Luwu String Extensions (rfcs/string-extensions.md): trim, trimstart, trimend, startswith, endswith, contains, replace
// and split's maxpieces argument. Off, the string library is exactly upstream's. The compiler reads it to emit the
// startswith, endswith and contains fastcalls.
LUAU_FASTFLAGVARIABLE(LuwuStringExtensions)

namespace Luau
{

bool luauClassCompat()
{
    return FFlag::DebugLuwuLuauClassCompat || FFlag::DebugLuauUserDefinedClasses || FFlag::DebugLuauUserDefinedClassesRuntime;
}

} // namespace Luau

// Luwu collections: the `collections` library of builtin classes (`Pair`, `Triple`, `Quad`, `OrderedMap`, `Vec`,
// `VecDeque`), in the VM and in Analysis's builtin definitions. A prototype whose API can still change. Needs
// LuwuClasses.
LUAU_FASTFLAGVARIABLE(DebugLuwuCollections)

// Luwu try expressions: `try expr` gives an `Ok` or an `Err`, and the `Ok`, `Err`, `Some` and `None` globals exist. A
// Debug flag while it's in progress: a function containing `try` doesn't compile to native code yet. Needs LuwuClasses
// and LuwuNonePrimitive. The parser gates the syntax, and while it is on the compiler emits WIP bytecode (version 200),
// which holds the protected ranges.
LUAU_FASTFLAGVARIABLE(DebugLuwuTryExpressions)
