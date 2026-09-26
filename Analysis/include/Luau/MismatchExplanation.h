// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/NotNull.h"
#include "Luau/TypeFwd.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Luau
{

struct BuiltinTypes;
struct Scope;
struct Subtyping;

// Luwu (helpful subtyping errors): explains a failed `given <: expected` test by diffing the two types
// structurally, rather than narrating the subtyping test's own reasonings. Each bullet is a path from
// the given value (its variable, else its alias, else `given`) down to one place the types disagree,
// so the reader can find it in what they wrote. How similar the two types are decides how much is
// said: unrelated types get one line, because the two printed types already say the rest.
struct MismatchExplanation
{
    // Appended after the expected/got block. Starts with a line break; empty when there is nothing
    // to add to the two printed types.
    std::string reason;

    // `reason` is the whole message, with no leading line break: the two types add nothing to it (fields
    // missing from a table literal, which the squiggle is already on).
    bool standalone = false;

    // Which path notation `reason` uses, as `MismatchNotation` bits, so a legend can be printed once
    // for everything shown together instead of once per error.
    uint8_t notation = 0;

    // Printed in place of a type that has a name the reader wrote (a union alias has none of its own).
    std::optional<std::string> wantedName;
    std::optional<std::string> givenName;

    // The `= A | B` line under an aliased union, members in the order the `#n` numbers count them.
    std::optional<std::string> wantedExpansion;
    std::optional<std::string> givenExpansion;

    // Set when every difference is in a function's parameters, or every one is in its returns.
    std::optional<std::string> contextVerb;
    std::optional<std::string> contextWantedDisplay;
    std::optional<std::string> contextGivenDisplay;
};

// Returns nullopt when the types are of a kind the differ doesn't take apart (generics, type
// functions, classes), when no failing place can be found in two structured types, and when
// everything found involves an error type, so the caller can fall back to the subtyping test's own
// reasonings.
std::optional<MismatchExplanation> explainMismatch(
    TypeId given,
    TypeId expected,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    // The function the given value is an argument to, when it is one, so a sentence about a callback's
    // parameter can say who calls it.
    std::optional<std::string> callee = std::nullopt,
    // How the given value got here, for "The passed type is similar": "passed", "returned", or "given"
    // when it's neither.
    std::string_view arrival = "given",
    // The variable the given value was read from, when it's that simple: paths are rooted at its name
    // (`leaving.kind`) rather than at the generic `given`.
    std::optional<std::string> variable = std::nullopt
);

} // namespace Luau
