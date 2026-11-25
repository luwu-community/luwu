// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

namespace Luau
{

// Luwu: whether Luau modules accept upstream Luau's class syntax and see upstream's `class` library
// (DebugLuwuLuauClassCompat). Turning on upstream's own class flags, DebugLuauUserDefinedClasses or
// DebugLuauUserDefinedClassesRuntime, turns it on too, so an embedder that enables classes the upstream way gets it.
bool luauClassCompat();

} // namespace Luau
