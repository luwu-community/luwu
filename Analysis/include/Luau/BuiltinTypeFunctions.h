// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/TypeFunction.h"

namespace Luau
{

struct BuiltinTypeFunctions
{
    BuiltinTypeFunctions();
    BuiltinTypeFunctions(const BuiltinTypeFunctions&) = delete;
    void operator=(const BuiltinTypeFunctions&) = delete;

    TypeFunction userFunc;

    TypeFunction notFunc;
    TypeFunction lenFunc;
    TypeFunction unmFunc;

    TypeFunction addFunc;
    TypeFunction subFunc;
    TypeFunction mulFunc;
    TypeFunction divFunc;
    TypeFunction idivFunc;
    TypeFunction powFunc;
    TypeFunction modFunc;

    TypeFunction concatFunc;

    TypeFunction andFunc;
    TypeFunction orFunc;

    TypeFunction ltFunc;
    TypeFunction leFunc;

    TypeFunction refineFunc;
    TypeFunction singletonFunc;
    TypeFunction unionFunc;
    TypeFunction intersectFunc;

    TypeFunction keyofFunc;
    TypeFunction rawkeyofFunc;
    TypeFunction indexFunc;
    TypeFunction rawgetFunc;

    TypeFunction setmetatableFunc;
    TypeFunction getmetatableFunc;

    TypeFunction objectFunc;
    TypeFunction classFunc;

    TypeFunction weakoptionalFunc;
    // Luwu: `unlessnever<refined, fallback, tests...>` is `refined`, or `fallback` when `refined` is uninhabited
    // because tests on one property contradict each other (one of `tests` is uninhabited). See applyRefinements.
    TypeFunction unlessneverFunc;
    // Luwu Traits (rfcs/classes/traits.md): `qualified<V, "name">` types `obj:[expr].name` and `obj:[expr]:name()`, where
    // `expr` has type V: the member of the trait V is the value of, or `unknown` when V isn't a trait
    TypeFunction qualifiedFunc;

    // Luwu: `oneof<A, B, ...>`, the union of its arguments where each table member has the others' fields as `nil`
    TypeFunction oneofFunc;

    void addToScope(NotNull<TypeArena> arena, NotNull<Scope> scope) const;
};

const BuiltinTypeFunctions& builtinTypeFunctions_DEPRECATED();

// Luwu Classes (rfcs/classes): the object type of a class or trait value (what `object<C>` gives), or nullopt for any other type
std::optional<TypeId> objectTypeOfClassValue(NotNull<BuiltinTypes> builtinTypes, TypeId classValue);

// Luwu Traits (rfcs/classes/traits.md): the trait whose value has type `traitValue`, or nullptr when it's no trait's value
const ExternType* traitOfValue(TypeId traitValue);

// Luwu Traits (rfcs/classes/traits.md): the type of the function or field `name` scoped to the trait whose value has type
// `traitValue` (`scoped function name`, `scoped name = value`)
std::optional<TypeId> scopedMemberOf(TypeId traitValue, const std::string& name);

} // namespace Luau
