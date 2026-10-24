// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Error.h"
#include "Luau/NotNull.h"
#include "Luau/ToString.h"
#include "Luau/TypeFwd.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace Luau
{

struct BuiltinTypes;
struct Scope;
struct Subtyping;
struct TypeArena;

// Luwu (helpful subtyping errors): explains a failed `given <: expected` test by diffing the two types
// structurally, rather than narrating the subtyping test's own reasonings. Each bullet is a path from
// the given value (its variable, else its alias, else `given`) down to one place the types disagree,
// so the reader can find it in what they wrote. How similar the two types are decides how much is
// said: unrelated types get one line, because the two printed types already say the rest.
// A remark at a place in source that a bullet is about: where a mismatched property is declared, on either side.
struct MismatchNote
{
    Location location;
    std::string module;
    std::string message;
};

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

    // What follows `type X =` for a type the header prints by its alias: an aliased union's members, in
    // the order the `#n` numbers count them, or an aliased table spelled out.
    std::optional<std::string> wantedExpansion;
    std::optional<std::string> givenExpansion;
    // The variable `reason`'s paths are rooted at, which the message calls the given value by: `Expected 'settings' to be`.
    std::optional<std::string> givenVariable;
    // The `where` block for types the message would otherwise print several times. When it isn't empty,
    // `wantedName` and `givenName` are the two types printed with its names.
    std::vector<NamedTypeDeclaration> whereDeclarations;
    // The properties the reason's paths go through, per table, which a large table printed as a block shows
    // first (`ToStringOptions::propertyFocus`).
    PropertyFocus propertyFocus{nullptr};
    // Every bullet says both what was expected and what was given, so the two types are only context: a given
    // table prints only the properties the bullets point into.
    bool bulletsSayBothTypes = false;
    // The types the reason's bullets say were expected and given where a value doesn't fit, as printed
    // (`Method`), for declaring an alias one of them is.
    std::vector<std::string> bulletTypes;
    // When the one thing wrong is a misspelling, what was written and what was meant (`"LeftCtrl"`,
    // `"LeftControl"`; `leaving.reson`, `leaving.reason`): the error is a typo, not a type mismatch.
    std::optional<std::pair<std::string, std::string>> typo;
    // For a misspelled value inside the given table, the properties leading to it (`kind` of `{ kind = "leeve" }`).
    std::vector<std::string> typoProperties;
    // Where the bullets' mismatched properties are declared, said there.
    std::vector<MismatchNote> notes;


};

// Whether `written` is close enough to `meant` to be a misspelling of it: a few edits apart, and long enough that a
// few edits don't make it a different word.
bool looksLikeTypo(const std::string& written, const std::string& meant);

// The candidate `written` is most likely a misspelling of: the fewest edits away within a typo's limit, the first of
// those on a tie. Nullopt when none is that close. `written` itself is never one.
std::optional<std::string> closestSpelling(const std::string& written, const std::vector<std::string>& candidates);

// Luwu (helpful subtyping errors): the misspelled properties of a table literal whose expected type is `expectedType`.
// Each is a property the type lacks, named like one the literal leaves out. For an optional property upstream reports
// nothing: table types aren't exact, so the literal fits and the value is never read.
std::vector<std::pair<Location, Typo>> literalPropertyTypos(const AstExprTable* expr, TypeId expectedType);

// Luwu (helpful subtyping errors): `given`, the type of `literal`, with the strings written where `expected` wants a
// string singleton typed as written. The checker widens a table literal's strings to `string`, which hides a
// misspelled tag like `kind = "leeve"`.
TypeId withWrittenStrings(TypeId given, const AstExpr* literal, TypeId expected, TypeArena& arena);

// Where the value at `properties` is written inside the table literal `literal`, if it is.
std::optional<Location> literalValueLocation(const AstExpr* literal, const std::vector<std::string>& properties);

// Whether `a` and `b` are at most one edit apart (an insertion, deletion, substitution or swap of two neighbors).
bool withinOneEdit(const std::string& a, const std::string& b);

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
    std::optional<std::string> variable = std::nullopt,
    // Writes the explanation's helps; off, there are none.
    const Helpifier& helpifier = {}
);

} // namespace Luau
