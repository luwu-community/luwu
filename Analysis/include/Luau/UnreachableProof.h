// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/TypeFwd.h"

namespace Luau
{

struct SourceModule;

// Luwu: whether the code at `use` can't run, proven without the solver's refinements. The conditions that hold there
// (the branches it's in, the left of an `and`/`or` it's on the right of, earlier guards that leave and `assert`s) must
// rule out every member of `declared`, the local's declared type, by its tests on the local or one of its properties:
// truthiness, `==`/`~=` against a constant, and `type`/`typeof` against a type name. Tests on the same thing apply
// together (`not h.enc`, then `h.enc`). Nothing is proven when the function assigns to the local or its properties,
// which can make a condition stale. The solver can call a value `never` where none of that holds (a bug in a
// refinement), and then nothing it's used for is checked.
bool provesUnreachable(const SourceModule& source, const AstExprLocal* use, TypeId declared);

} // namespace Luau
