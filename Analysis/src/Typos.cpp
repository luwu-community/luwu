// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Typos.h"

#include "Luau/StringUtils.h"
#include "Luau/Type.h"
#include "Luau/TypeArena.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace Luau
{

namespace
{

// A misspelling is at most this many edits from what was meant
constexpr size_t kMaxTypoEdits = 2;
// A word this long or shorter gets one edit: two turn most short words into other words (`"Food"` and `"Tool"`)
constexpr size_t kShortWordLength = 4;
// A word this long or shorter isn't checked: it's a few edits from everything (`"t"` for `"s"`)
constexpr size_t kMaxUncheckedLength = 2;
// Strings longer than this aren't checked: the edit distance is quadratic in their length
constexpr size_t kMaxCheckedLength = 128;
// Members of an expected union a misspelled string is compared against
constexpr size_t kMaxSingletonCandidates = 256;

size_t typoEditLimit(const std::string& written)
{
    return written.size() <= kShortWordLength ? 1 : kMaxTypoEdits;
}

// The edit distance between `a` and `b` when it's at most `limit`. Strings whose lengths alone differ by more can't be
// that close, and long strings aren't compared at all.
std::optional<size_t> editDistanceWithin(const std::string& a, const std::string& b, size_t limit)
{
    size_t lengthGap = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
    if (lengthGap > limit || a.size() > kMaxCheckedLength || b.size() > kMaxCheckedLength)
        return std::nullopt;
    // Insertions, deletions, substitutions and swaps of two neighboring characters each count as one edit, so `"Bleu"`
    // is one edit from `"Blue"`
    size_t distance = editDistance(a, b);
    if (distance > limit)
        return std::nullopt;
    return distance;
}

// Whether `shorter` abbreviates `longer`: same first letter, its letters in order, and most of the length
bool isAbbreviation(const std::string& shorter, const std::string& longer)
{
    if (shorter.empty() || shorter.size() * 10 < longer.size() * 6 || shorter[0] != longer[0])
        return false;
    size_t at = 0;
    for (char c : longer)
    {
        if (at < shorter.size() && c == shorter[at])
            ++at;
    }
    return at == shorter.size();
}

// Whether `a` and `b` are the same apart from their digits
bool sameWithoutDigits(const std::string& a, const std::string& b)
{
    size_t i = 0;
    size_t j = 0;
    while (true)
    {
        while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i])))
            ++i;
        while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j])))
            ++j;
        if (i == a.size() || j == b.size())
            return i == a.size() && j == b.size();
        if (a[i] != b[j])
            return false;
        ++i;
        ++j;
    }
}

std::optional<size_t> typoDistanceWithin(const std::string& written, const std::string& meant, size_t limit)
{
    if (written.size() <= kMaxUncheckedLength || written == meant || sameWithoutDigits(written, meant))
        return std::nullopt;
    return editDistanceWithin(written, meant, std::min(limit, typoEditLimit(written)));
}

const StringSingleton* stringSingletonOf(TypeId ty)
{
    const SingletonType* singleton = get<SingletonType>(follow(ty));
    return singleton ? get<StringSingleton>(singleton) : nullptr;
}

// The tables a literal is checked against for `expected`: a table, or each table of an intersection, through a `T?`.
// Empty when one of them has an indexer (any name fits it) or isn't a table.
std::vector<const TableType*> expectedTables(TypeId expected)
{
    expected = follow(expected);
    if (const UnionType* optional = get<UnionType>(expected))
    {
        std::optional<TypeId> nonNil;
        for (TypeId option : optional->options)
        {
            if (isNil(follow(option)))
                continue;
            if (nonNil)
                return {};
            nonNil = follow(option);
        }
        if (!nonNil)
            return {};
        expected = *nonNil;
    }

    std::vector<TypeId> parts{expected};
    if (const IntersectionType* intersection = get<IntersectionType>(expected))
        parts.assign(intersection->parts.begin(), intersection->parts.end());

    std::vector<const TableType*> tables;
    for (TypeId part : parts)
    {
        const TableType* table = get<TableType>(follow(part));
        if (!table || table->indexer)
            return {};
        tables.push_back(table);
    }
    return tables;
}

bool hasProperty(const std::vector<const TableType*>& tables, const std::string& name)
{
    return std::any_of(
        tables.begin(),
        tables.end(),
        [&](const TableType* table)
        {
            return table->props.count(name) != 0;
        }
    );
}

// The types `ty` can be: each member of a union, else `ty` itself
std::vector<TypeId> alternativesOf(TypeId ty)
{
    ty = follow(ty);
    const UnionType* u = get<UnionType>(ty);
    if (!u)
        return {ty};
    return std::vector<TypeId>(begin(u), end(u));
}

// The types the alternatives of `expected` give the property `name`
std::vector<TypeId> expectedProperty(const std::vector<TypeId>& expected, const std::string& name)
{
    std::vector<TypeId> properties;
    for (TypeId alternative : expected)
    {
        const TableType* table = get<TableType>(follow(alternative));
        if (!table)
            continue;
        auto it = table->props.find(name);
        if (it == table->props.end() || !it->second.readTy)
            continue;
        for (TypeId option : alternativesOf(*it->second.readTy))
            properties.push_back(option);
    }
    return properties;
}

bool wantsStringSingleton(const std::vector<TypeId>& wanted)
{
    return std::any_of(
        wanted.begin(),
        wanted.end(),
        [](TypeId ty)
        {
            return stringSingletonOf(ty) != nullptr;
        }
    );
}

TypeId withWrittenStrings(TypeId given, const AstExpr* literal, const std::vector<TypeId>& expected, TypeArena& arena)
{
    given = follow(given);
    const AstExprTable* expr = literal->as<AstExprTable>();
    const TableType* table = get<TableType>(given);
    if (!expr || !table)
        return given;

    std::optional<TableType> copy;
    for (const AstExprTable::Item& item : expr->items)
    {
        std::optional<std::string> name = literalPropertyName(item);
        auto it = name ? table->props.find(*name) : table->props.end();
        if (it == table->props.end() || !it->second.readTy)
            continue;

        TypeId checked = *it->second.readTy;
        TypeId written = checked;
        std::vector<TypeId> wanted = expectedProperty(expected, *name);
        const AstExprConstantString* string = item.value->as<AstExprConstantString>();
        if (string && isString(checked) && wantsStringSingleton(wanted))
            written = arena.addType(SingletonType{StringSingleton{std::string(string->value.data, string->value.size)}});
        else if (!string)
            written = withWrittenStrings(checked, item.value, wanted, arena);
        if (written == checked)
            continue;

        if (!copy)
            copy = *table;
        // Keeps where the property is declared, which the explanation's notes point at
        Property& property = copy->props[*name];
        property.readTy = written;
        if (property.writeTy)
            property.writeTy = written;
    }
    return copy ? arena.addType(std::move(*copy)) : given;
}

} // namespace

std::optional<std::string> literalPropertyName(const AstExprTable::Item& item)
{
    if (item.kind == AstExprTable::Item::Kind::List || !item.key)
        return std::nullopt;
    if (AstExprConstantString* key = item.key->as<AstExprConstantString>())
        return std::string(key->value.data, key->value.size);
    return std::nullopt;
}

bool withinOneEdit(const std::string& a, const std::string& b)
{
    return editDistanceWithin(a, b, 1).has_value();
}

std::optional<size_t> typoDistance(const std::string& written, const std::string& meant)
{
    return typoDistanceWithin(written, meant, kMaxTypoEdits);
}

bool looksLikeTypo(const std::string& written, const std::string& meant)
{
    return typoDistance(written, meant).has_value();
}

std::optional<std::string> closestSpelling(const std::string& written, const std::vector<std::string>& candidates)
{
    std::optional<std::string> best;
    size_t bestDistance = kMaxTypoEdits + 1;
    for (const std::string& candidate : candidates)
    {
        std::optional<size_t> distance = typoDistanceWithin(written, candidate, bestDistance - 1);
        if (distance)
        {
            best = candidate;
            bestDistance = *distance;
        }
    }
    return best;
}

std::optional<std::string> misspelledSingleton(TypeId sub, TypeId super, size_t& comparisonsLeft)
{
    const StringSingleton* givenString = stringSingletonOf(sub);
    if (!givenString || givenString->value.size() <= kMaxUncheckedLength)
        return std::nullopt;

    // `("Red" | "Blue")?` is a union holding a union, so collect members through nesting. A union can hold itself, so
    // each is opened once.
    std::vector<TypeId> candidates;
    std::unordered_set<TypeId> opened;
    std::vector<TypeId> pending{super};
    while (!pending.empty() && candidates.size() < kMaxSingletonCandidates)
    {
        TypeId ty = follow(pending.back());
        pending.pop_back();
        if (const UnionType* u = get<UnionType>(ty))
        {
            if (opened.insert(ty).second)
                pending.insert(pending.end(), u->options.rbegin(), u->options.rend());
        }
        else if (stringSingletonOf(ty))
            candidates.push_back(ty);
    }

    const std::string& written = givenString->value;
    std::optional<std::string> best;
    size_t bestDistance = kMaxTypoEdits + 1;
    for (TypeId candidate : candidates)
    {
        if (comparisonsLeft == 0)
            break;
        --comparisonsLeft;

        const std::string& want = stringSingletonOf(candidate)->value;
        std::optional<size_t> distance = typoDistanceWithin(written, want, bestDistance - 1);
        if (distance || (!best && written != want && isAbbreviation(written, want)))
        {
            best = "\"" + want + "\"";
            bestDistance = distance.value_or(bestDistance);
        }
    }
    return best;
}

std::vector<std::pair<Location, Typo>> literalPropertyTypos(const AstExprTable* expr, TypeId expectedType)
{
    std::vector<std::pair<Location, Typo>> typos;
    std::vector<const TableType*> tables = expectedTables(expectedType);
    if (tables.empty())
        return typos;

    // This runs for every table literal with an expected table type. Most write only properties the type has.
    bool writesUnknownProperty = std::any_of(
        expr->items.begin(),
        expr->items.end(),
        [&](const AstExprTable::Item& item)
        {
            std::optional<std::string> name = literalPropertyName(item);
            return name && !hasProperty(tables, *name);
        }
    );
    if (!writesUnknownProperty)
        return typos;

    std::unordered_set<std::string> written;
    for (const AstExprTable::Item& item : expr->items)
    {
        if (std::optional<std::string> name = literalPropertyName(item))
            written.insert(*name);
    }

    // The properties the literal leaves out, which a misspelled one is likely to be
    std::vector<std::string> unwritten;
    for (const TableType* table : tables)
    {
        for (const auto& [name, _] : table->props)
        {
            if (!written.count(name))
                unwritten.push_back(name);
        }
    }
    std::sort(unwritten.begin(), unwritten.end());

    for (const AstExprTable::Item& item : expr->items)
    {
        std::optional<std::string> name = literalPropertyName(item);
        if (!name || hasProperty(tables, *name))
            continue;
        if (std::optional<std::string> meant = closestSpelling(*name, unwritten))
            typos.emplace_back(item.key->location, Typo{*name, *meant});
    }
    return typos;
}

TypeId withWrittenStrings(TypeId given, const AstExpr* literal, TypeId expected, TypeArena& arena)
{
    return withWrittenStrings(given, literal, alternativesOf(expected), arena);
}

std::optional<Location> literalValueLocation(const AstExpr* literal, const std::vector<std::string>& properties)
{
    for (const std::string& property : properties)
    {
        const AstExprTable* expr = literal->as<AstExprTable>();
        if (!expr)
            return std::nullopt;
        auto found = std::find_if(
            expr->items.begin(),
            expr->items.end(),
            [&](const AstExprTable::Item& item)
            {
                return literalPropertyName(item) == property;
            }
        );
        if (found == expr->items.end())
            return std::nullopt;
        literal = found->value;
    }
    return literal->location;
}

} // namespace Luau
