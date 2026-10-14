// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/DenseHash.h"
#include "Luau/NotNull.h"
#include "Luau/TypeFwd.h"

#include <optional>
#include <string>
#include <vector>

namespace Luau
{

struct TypeGuard
{
    bool isTypeof;
    AstExpr* target;
    std::string type;
};

std::optional<TypeGuard> matchTypeGuard(AstExprBinary::Op op, AstExpr* left, AstExpr* right);

// Search through the expression 'expr' for typeArguments that are known to represent
// uniquely held references. Append these typeArguments to 'uniqueTypes'.
void findUniqueTypes(NotNull<DenseHashSet<TypeId>> uniqueTypes, AstExpr* expr, NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes);

void findUniqueTypes(
    NotNull<DenseHashSet<TypeId>> uniqueTypes,
    AstArray<AstExpr*> exprs,
    NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes
);
void findUniqueTypes(
    NotNull<DenseHashSet<TypeId>> uniqueTypes,
    const std::vector<AstExpr*>& exprs,
    NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes
);

// Luwu Traits (rfcs/classes/traits.md): the `self` of a method, written `function T:m()` or `function m(self)`
AstLocal* methodSelf(AstExprFunction* fn);

// Luwu Traits (rfcs/classes/traits.md): whether `fn`'s own `return`s (not those of functions nested in it) give back `self`,
// or a new object of self's class (`class.of(self)(...)`)
bool returnsSelf(AstExprFunction* fn, AstLocal* self);

// Luwu Traits (rfcs/classes/traits.md): whether every one of `fn`'s own `return`s gives back `self` or a new object of
// self's class, and its body ends in one (so it never returns nothing)
bool returnsOnlySelf(AstExprFunction* fn, AstLocal* self);

// Luwu Traits (rfcs/classes/traits.md): `class.of(self)` called, the `self` it is the class of
AstLocal* newOfClassOf(AstExprCall* call);

} // namespace Luau
