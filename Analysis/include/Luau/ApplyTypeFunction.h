// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Substitution.h"
#include "Luau/TxnLog.h"
#include "Luau/TypeFwd.h"

namespace Luau
{

// A substitution which replaces the type parameters of a type function by arguments
struct ApplyTypeFunction : Substitution
{
    ApplyTypeFunction(TypeArena* arena)
        : Substitution(TxnLog::empty(), arena)
        , encounteredForwardedType(false)
    {
    }

    // Never set under deferred constraint resolution.
    bool encounteredForwardedType;

    // When instantiating a nominal type with generic parameters (see LuauGenericNominals),
    // this is the TypeId of the template ExternType currently being expanded, which is
    // `tf->type` in ConstraintSolver::tryDispatch(TypeAliasExpansionConstraint). ExternTypes
    // are normally opaque leaves to this substitution, so their contents are left alone and
    // their nominal identity is preserved. The template being instantiated right now is the
    // one exception: its contents need to be visited so its generics can actually be replaced
    // with the supplied arguments. Every other ExternType this substitution runs into elsewhere
    // in the type graph (an unrelated class reached through some field, a non-generic
    // superclass, etc.) stays fully opaque, same as before.
    TypeId genericNominalRoot = nullptr;

    std::unordered_map<TypeId, TypeId> typeArguments;
    std::unordered_map<TypePackId, TypePackId> typePackArguments;
    bool ignoreChildren(TypeId ty) override;
    bool ignoreChildren(TypePackId tp) override;
    bool isDirty(TypeId ty) override;
    bool isDirty(TypePackId tp) override;
    TypeId clean(TypeId ty) override;
    TypePackId clean(TypePackId tp) override;
};

} // namespace Luau
