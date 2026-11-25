// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/Error.h"
#include "Luau/TypeFwd.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Luau
{

struct TypeArena;

// Luwu helpful subtyping errors: telling a misspelled name or string from a different word, for the typo errors on
// properties, type names, variables and string singletons.

// How many edits `written` is from `meant` when it looks like a misspelling of it, else nullopt. It doesn't when it's
// too short to tell (`"t"` is a few edits from everything), when it's more edits away than a word its length allows,
// and when the two differ only in digits: those are siblings (`Part8`, `Part0`), not misspellings.
std::optional<size_t> typoDistance(const std::string& written, const std::string& meant);

// Whether `written` is close enough to `meant` to be a misspelling of it (`typoDistance`)
bool looksLikeTypo(const std::string& written, const std::string& meant);

// The candidate `written` is most likely a misspelling of: the fewest edits away, the first of those on a tie. Nullopt
// when none looks like one.
std::optional<std::string> closestSpelling(const std::string& written, const std::vector<std::string>& candidates);

// The string singleton in `super` that `sub`, a string singleton, most likely misspells: one `typoDistance` allows, or
// one it abbreviates (`"LeftCtrl"` for `"LeftControl"`). Each string compared spends one of `comparisonsLeft`.
std::optional<std::string> misspelledSingleton(TypeId sub, TypeId super, size_t& comparisonsLeft);

// The misspelled properties of a table literal whose expected type is `expectedType`. Each is a property the type
// lacks, named like one the literal leaves out. For an optional property upstream reports nothing: table types aren't
// exact, so the literal fits and the value is never read.
std::vector<std::pair<Location, Typo>> literalPropertyTypos(const AstExprTable* expr, TypeId expectedType);

// `given`, the type of `literal`, with the strings written where `expected` wants a string singleton typed as written.
// The checker widens a table literal's strings to `string`, which hides a misspelled tag like `kind = "leeve"`.
TypeId withWrittenStrings(TypeId given, const AstExpr* literal, TypeId expected, TypeArena& arena);

// Where the value at `properties` is written inside the table literal `literal`, if it is
std::optional<Location> literalValueLocation(const AstExpr* literal, const std::vector<std::string>& properties);

// The name of a literal's property, when it's written as one (`name = v` or `["name"] = v`)
std::optional<std::string> literalPropertyName(const AstExprTable::Item& item);

// Whether `a` and `b` are at most one edit apart (an insertion, deletion, substitution or swap of two neighbors)
bool withinOneEdit(const std::string& a, const std::string& b);

} // namespace Luau
