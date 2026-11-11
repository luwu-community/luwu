// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/DenseHash.h"
#include "Luau/Error.h"
#include "Luau/ToString.h"
#include "Luau/TypeFwd.h"

#include <optional>
#include <string>
#include <vector>

namespace Luau
{

struct Scope;

// Luwu (helpful subtyping errors): an expected/got message that would print the same long type several
// times prints it once, as `where type Animal = { ... }`, and by that name everywhere else, so nobody has
// to compare two copies of it by eye. The name is, best first:
// - an alias in scope with the same structure;
// - for a list or map of an aliased type, the alias's plural: `{Animal}` and `{ [string]: Animal }` are
//   `Animals`;
// - the binding or property holding it, capitalized (`animals` is `Animals`);
// - a pet name, for a type that sits in none of those.
struct RepeatedTypeNames
{
    // Every TypeId printed by a name, for `ToStringOptions::repeatedTypeNames`.
    DenseHashMap<TypeId, std::string> names{nullptr};
    // In the order the message first prints the types.
    std::vector<NamedTypeDeclaration> declarations;

    bool empty() const
    {
        return declarations.empty();
    }
};

// A type a message prints, and what the reader calls it there: the variable it was read from, when there
// is one.
struct PrintedType
{
    TypeId ty;
    std::optional<std::string> noun;
};

// Names for the long types reachable from `printed` that `message` prints at least twice. `printing` is
// how the message prints types, so they can be found in it. `scope`, when given, is where an alias with
// the same structure is looked for, and which names are already taken.
RepeatedTypeNames nameRepeatedTypes(
    const std::vector<PrintedType>& printed,
    const std::string& message,
    const ToStringOptions& printing,
    const Scope* scope
);

// How an expected/got message prints a type on lines of its own: a large table a property per line, and only the
// properties that matter of a very large one (`ToStringOptions::maxPropertiesShown`).
ToStringOptions blockPrinting(ToStringOptions options);

// The aliased types reachable from `roots` (`Address` inside `Customer`), by name, in the order a walk first
// meets them. Bounded like `nameRepeatedTypes`'s walk.
std::vector<std::pair<std::string, TypeId>> aliasesReachableFrom(const std::vector<TypeId>& roots);

// The types in `pack`, for `nameRepeatedTypes`.
void appendPrintedPack(std::vector<PrintedType>& printed, TypePackId pack);

// Prints the two types of an expected/got error that `explainMismatch` didn't explain (a `TypeMismatch` or
// `TypePackMismatch`) as `blockPrinting` does, with names for the types they repeat. Leaves any other error
// alone.
void nameRepeatedTypesInError(TypeErrorData& data, const Scope* scope);

// Whether `text` uses `name` as a whole word, for dropping a declaration nothing refers to.
bool mentionsName(const std::string& text, const std::string& name);

} // namespace Luau
