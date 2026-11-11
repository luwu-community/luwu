// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace Luau
{

struct Scope;

// Luwu (helpful subtyping errors): making up names for types that have none, for anything that has to
// refer to such a type by name (`where type Animals = { ... }` under an error message).

// A binding or property name as a type name: `http_code`, `httpCode` and `HTTP_CODE` are `HttpCode`.
// Empty when the word has no letters to start one with.
std::string pascalCase(const std::string& word);

// The English singular and plural of a type name's last word (`CatNames` is `CatName`). A name with no
// other form comes back unchanged: `singularOf` of a singular, or `pluralOf` of an uncountable noun.
std::string singularOf(const std::string& name);
std::string pluralOf(const std::string& name);

// The name for a list of `element`s: its plural (`Animals`), or `<element>List` when the plural can't be
// told from the element (`DataList`) or the element's name is plural already (`AnimalsList`).
std::string listNameOf(const std::string& element);

// Names for a type with nothing to be named after. Fixed and in order, so one message always says the same
// thing.
const std::vector<std::string>& petNames();

// Hands out names that differ from each other, from types in scope, and from names the reader can already
// see.
class TypeNameAllocator
{
public:
    // `scope` is where a name is looked up to see if it's taken; null to check only what's reserved.
    explicit TypeNameAllocator(const Scope* scope);

    // A name in use where the new ones will be read (an alias the message prints), which none may reuse.
    void reserve(const std::string& name);

    // Takes `alias`, the reader's own name for a type, unless another type got it first. An alias is in
    // scope, so this doesn't look there.
    bool claimAlias(const std::string& alias);

    // The first of `preferred` that isn't taken, else the first pet name that isn't, else the first of
    // either with the lowest number after it that makes it free (`Animals2`).
    std::string allocate(const std::vector<std::string>& preferred);

private:
    bool isTaken(const std::string& name) const;

    const Scope* scope;
    std::unordered_set<std::string> used;
};

} // namespace Luau
