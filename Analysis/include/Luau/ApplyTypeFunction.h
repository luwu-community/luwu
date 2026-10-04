// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Substitution.h"
#include "Luau/TypeArena.h"
#include "Luau/TxnLog.h"
#include "Luau/TypeFwd.h"

#include <mutex>

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

    // Made public here. A caller instantiating a nominal type maps the template onto the
    // instantiation (see ConstraintSolver::tryDispatch(InstantiateNominalPropConstraint)), and has to
    // mark the instantiation itself as not to be walked. `Substitution` declares `dontTraverseInto`
    // protected, because its own subclasses only call it from `clean()`.
    using Substitution::dontTraverseInto;

    std::unordered_map<TypeId, TypeId> typeArguments;
    std::unordered_map<TypePackId, TypePackId> typePackArguments;
    bool ignoreChildren(TypeId ty) override;
    bool ignoreChildren(TypePackId tp) override;
    bool isDirty(TypeId ty) override;
    bool isDirty(TypePackId tp) override;
    TypeId clean(TypeId ty) override;
    TypePackId clean(TypePackId tp) override;
};

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): the alias references `substitution` copied without
// expanding them (`Listable<T>` before `Listable` is declared). Nothing expands a copy unless the caller does.
std::vector<TypeId> copiedPendingExpansions(const Substitution& substitution);

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): whether `typeArguments` are `typeFun`'s own parameters
// (`List<T>` inside `List<T>`), so the instantiation is the type itself
bool isIdentityInstantiation(const TypeFun& typeFun, const std::vector<TypeId>& typeArguments, const std::vector<TypePackId>& packArguments);

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): `genericTemplate` instantiated with `typeArguments`. Its members are built from the
// template's on first read (ExternType::props); its metatable and implemented traits are substituted here, and the
// alias references that copied without expanding are added to `copiedReferences` for the caller to expand.
TypeId instantiateGenericNominal(
    TypeArena* arena,
    TypeId genericTemplate,
    std::vector<TypeId> typeArguments,
    std::vector<TypePackId> packArguments,
    std::vector<TypeId>& copiedReferences
);

// Luwu Traits (rfcs/classes/traits.md): the trait a `needs` list names, whether it lists the trait (`needs Base`) or an
// instantiation of it (`needs Base<T>`)
TypeId traitTemplateOf(TypeId trait);

// Luwu Traits (rfcs/classes/traits.md): the type arguments trait `needingTemplate`'s `needs` entry gives `needed`
// (`needs Base<T>`), with `needingArguments` substituted for `needingTemplate`'s own generics. Empty `needingArguments`
// leave them as written. nullopt when the entry gives no type arguments. The alias references that copied without
// expanding are added to `copiedReferences` for the caller to expand.
std::optional<std::vector<TypeId>> neededTraitTypeArguments(
    TypeArena* arena,
    const ExternType& needingTemplate,
    TypeId needed,
    const std::vector<TypeId>& needingArguments,
    std::vector<TypeId>& copiedReferences
);

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): the arena the instantiations a module exports build their
// members into. Modules importing them are checked on different threads, so a build holds `mutex` while it allocates. One
// arena per module rather than per instantiation: a block is 32KB for the handful of types one instantiation needs.
struct SharedNominalArena
{
    TypeArena arena;
    std::mutex mutex;
};

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): marks the members of `instantiation`, a copy of an
// instantiation, as not built. They are built into `sharedArena` when there is one (an exported instantiation), and
// into `arena` otherwise.
void resetNominalMembers(TypeId instantiation, TypeArena* arena, std::shared_ptr<SharedNominalArena> sharedArena);

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): what a read of member `name` of an instantiation has to wait for, while the template's
// member is still being solved
std::optional<TypeId> pendingNominalMember(const ExternType& instantiation, const Name& name);

} // namespace Luau
