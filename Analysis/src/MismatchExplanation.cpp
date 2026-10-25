// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/MismatchExplanation.h"

#include "Luau/Error.h"
#include "Luau/MismatchWording.h"
#include "Luau/RepeatedTypeNames.h"
#include "Luau/Scope.h"
#include "Luau/StringUtils.h"
#include "Luau/Subtyping.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"
#include "Luau/TypePack.h"
#include "Luau/TypeUtils.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

// Luwu (helpful subtyping errors): decides what `explainMismatch` reports about a failed subtyping
// test. How each report reads is `MismatchWording`'s.

namespace Luau
{

using namespace MismatchWording;

namespace
{

// Subtyping tests one whole message may run. Every node compared costs one, and choosing between union members diffs every candidate, so without a bound a
// pathological type could make an error message the slowest thing in the check.
constexpr int kMessageBudget = 400;
// How much of the message's budget one candidate may spend when picking the closest member of an
// expected union.
constexpr int kCandidateBudget = 60;
// How deep a diff descends before reporting the two types at that point as they are.
constexpr int kMaxDiffDepth = 8;
// How deep a helper that walks a type's structure (printing, property lookup through intersections)
// goes. Types can be cyclic, so every such walk stops here.
constexpr int kMaxWalkDepth = 16;
// Failing members of a given union explained path by path; the rest are only named.
constexpr size_t kDetailedMembers = 3;
// Failing members of a given union named after those, one line each; the rest are counted.
constexpr size_t kNamedMembers = 5;
// An aliased union or intersection after `type X =` shows the members the message refers to in place when that fits
// in this many characters; a longer one lists them by number.
constexpr size_t kMaxSpelledOutWidth = 240;
// Members of an expected union a misspelled string is compared against.
constexpr size_t kMaxSpellingCandidates = 256;
// Strings longer than this aren't checked for typos: the edit distance is quadratic in their length.
constexpr size_t kMaxSpellingLength = 128;
// Aliases a message mentions without showing them (`Address` in `type Customer = { billing: Address }`) that
// it declares, past the ones it names itself: the rest of a long chain is more than a reader needs.
constexpr size_t kMaxMentionedAliases = 4;
// A mentioned alias longer than this isn't declared: a type that large is one the reader knows by its name,
// and its declaration would bury the message.
constexpr size_t kMaxMentionedAliasWidth = 240;
// A header type's alias whose table prints on one line in at most this many characters is spelled out in place,
// `'Vector2: { x: number, y: number }'`, rather than declared in the `where` block.
constexpr size_t kMaxInPlaceAliasWidth = 60;
// How long a printed type may get before `ToString` truncates it.
constexpr size_t kTypeWidth = 120;
constexpr size_t kFunctionTypeWidth = 160;

// The coarse shape of a type, for deciding whether two types are related at all.
enum class Shape
{
    Nil,
    Scalar,
    Table,
    Function,
    Union,
    Intersection,
    Other,
};

Shape shapeOf(TypeId ty)
{
    ty = follow(ty);
    if (isNil(ty))
        return Shape::Nil;
    if (get<PrimitiveType>(ty) || get<SingletonType>(ty))
        return Shape::Scalar;
    if (get<TableType>(ty) || get<MetatableType>(ty))
        return Shape::Table;
    if (get<FunctionType>(ty))
        return Shape::Function;
    if (get<UnionType>(ty))
        return Shape::Union;
    if (get<IntersectionType>(ty))
        return Shape::Intersection;
    return Shape::Other;
}

// A table or a function: something a diff can walk into and find a place in.
bool isStructured(Shape shape)
{
    return shape == Shape::Table || shape == Shape::Function;
}

bool anyStructured(const std::vector<TypeId>& types)
{
    return std::any_of(
        types.begin(),
        types.end(),
        [](TypeId ty)
        {
            return isStructured(shapeOf(ty));
        }
    );
}

// Whether choosing the closest member of an expected union can lead anywhere: only a table, function
// or intersection has places to compare against a structured member. Anything else is just not one
// of the members, which the two printed types already say.
bool canChooseMember(TypeId sub, const std::vector<TypeId>& members)
{
    Shape subShape = shapeOf(sub);
    bool subHasPlaces = isStructured(subShape) || subShape == Shape::Intersection;
    return subHasPlaces && anyStructured(members);
}

const TableType* tableOf(TypeId ty)
{
    ty = follow(ty);
    if (const TableType* table = get<TableType>(ty))
        return table;
    if (const MetatableType* metatable = get<MetatableType>(ty))
        return get<TableType>(follow(metatable->table));
    return nullptr;
}

// The name the reader wrote for this type, if it has one.
std::optional<std::string> aliasName(TypeId ty)
{
    ty = follow(ty);
    if (const TableType* table = get<TableType>(ty))
    {
        // An instantiated generic (`Box<string>`) would lose its arguments if printed by name alone. A
        // `syntheticName` is the name of the local a table literal was assigned to, not a type name.
        if (table->name && table->instantiatedTypeParams.empty() && table->instantiatedTypePackParams.empty())
            return table->name;
        return std::nullopt;
    }
    // A metatable type's alias name can only be stored as its `syntheticName`.
    if (const MetatableType* metatable = get<MetatableType>(ty))
        return metatable->syntheticName;
    if (const UnionType* u = get<UnionType>(ty))
        return u->name;
    if (const IntersectionType* i = get<IntersectionType>(ty))
        return i->name;
    return std::nullopt;
}

// A table literal held in a local, which prints as the local's name: its `syntheticName` isn't a type.
bool printsAsLocalName(TypeId ty)
{
    const TableType* table = get<TableType>(follow(ty));
    return table && table->syntheticName && !table->name;
}

bool isIdentifier(const std::string& s)
{
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    for (char c : s)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    }
    return true;
}

// Members of a union, without `nil`, in the order `#n` counts them: the order they're printed in
// with `sortUnionMembers` off, where a `nil` moves to the trailing `?`.
std::vector<TypeId> numberedMembers(const std::vector<TypeId>& options)
{
    std::vector<TypeId> members;
    for (TypeId option : options)
    {
        if (!isNil(follow(option)))
            members.push_back(option);
    }
    return members;
}

// The `T` of a `T?`; any other type as it is.
TypeId withoutNil(TypeId ty)
{
    ty = follow(ty);
    if (const UnionType* u = get<UnionType>(ty))
    {
        std::vector<TypeId> members = numberedMembers(u->options);
        if (members.size() == 1)
            return follow(members[0]);
    }
    return ty;
}

ToStringOptions printOptions(size_t width)
{
    ToStringOptions options;
    options.sortUnionMembers = false;
    options.maxTypeLength = width;
    options.classAndTraitValuesInTypeForm = true;
    return options;
}

// The edit distance between `a` and `b` when it's at most `limit`. Strings whose lengths alone differ by
// more can't be that close, and long strings aren't compared at all.
std::optional<size_t> editDistanceWithin(const std::string& a, const std::string& b, size_t limit)
{
    size_t lengthGap = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
    if (lengthGap > limit || a.size() > kMaxSpellingLength || b.size() > kMaxSpellingLength)
        return std::nullopt;
    // insertions, deletions, substitutions and swaps of two neighboring characters each count as one edit, so `"Bleu"`
    // is one edit from `"Blue"`
    size_t distance = editDistance(a, b);
    if (distance > limit)
        return std::nullopt;
    return distance;
}

// Whether `shorter` abbreviates `longer`: same first letter, its letters in order, and most of the length.
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

// A misspelling is at most this many edits from what was meant, and longer than this: a string that short is a few
// edits from everything (`"t"` for `"s"`).
constexpr size_t kMaxTypoEdits = 2;
// A word this long or shorter gets one edit: two turn most short words into other words (`"Food"` and `"Tool"`).
constexpr size_t kShortWordLength = 4;

size_t typoEditLimit(const std::string& written)
{
    return written.size() <= kShortWordLength ? 1 : kMaxTypoEdits;
}

// The expected string a given string singleton was most likely a misspelling of: within two edits,
// or an abbreviation of it (`"LeftCtrl"` for `"LeftControl"`).
std::optional<std::string> misspelledSingleton(TypeId sub, TypeId super)
{
    const SingletonType* given = get<SingletonType>(follow(sub));
    const StringSingleton* givenString = given ? get<StringSingleton>(given) : nullptr;
    if (!givenString || givenString->value.size() <= kMaxTypoEdits)
        return std::nullopt;

    // `("Red" | "Blue")?` is a union holding a union, so collect members through nesting. A union can
    // hold itself, so each is opened once.
    std::vector<TypeId> candidates;
    std::unordered_set<TypeId> opened;
    std::vector<TypeId> pending{super};
    while (!pending.empty() && candidates.size() < kMaxSpellingCandidates)
    {
        TypeId ty = follow(pending.back());
        pending.pop_back();
        if (const UnionType* u = get<UnionType>(ty))
        {
            if (opened.insert(ty).second)
                pending.insert(pending.end(), u->options.rbegin(), u->options.rend());
        }
        else
            candidates.push_back(ty);
    }

    std::optional<std::string> best;
    size_t bestDistance = typoEditLimit(givenString->value) + 1;
    for (TypeId candidate : candidates)
    {
        const SingletonType* singleton = get<SingletonType>(follow(candidate));
        const StringSingleton* candidateString = singleton ? get<StringSingleton>(singleton) : nullptr;
        if (!candidateString)
            continue;

        const std::string& want = candidateString->value;
        size_t distance = editDistanceWithin(givenString->value, want, bestDistance - 1).value_or(bestDistance);
        if (distance < bestDistance || (!best && isAbbreviation(givenString->value, want)))
        {
            best = "\"" + want + "\"";
            bestDistance = std::min(distance, bestDistance);
        }
    }
    return best;
}

// Whether `written` is a misspelling of the string `meant`.
bool isMisspelledString(const SingletonType& written, const SingletonType& meant)
{
    const StringSingleton* writtenString = get<StringSingleton>(&written);
    const StringSingleton* meantString = get<StringSingleton>(&meant);
    return writtenString && meantString && writtenString->value != meantString->value && looksLikeTypo(writtenString->value, meantString->value);
}

// The singleton field that picked a member of an expected union: `kind` is `"leave"`.
struct Tag
{
    std::string field;
    std::string value;
};

// Subtyping tests left for one message, shared by every `Differ` working on it.
struct Budget
{
    int remaining = kMessageBudget;
};

struct Differ
{
    NotNull<Subtyping> subtyping;
    NotNull<Scope> scope;
    NotNull<BuiltinTypes> builtinTypes;
    NotNull<Budget> budget;

    // What this differ may spend of `budget`, for a trial that must leave some for the others.
    int allowance = std::numeric_limits<int>::max();
    int spent = 0;

    // Pairs on the current descent, so a recursive type (`Json`) doesn't descend forever.
    std::vector<std::pair<TypeId, TypeId>> descending;

    // The functions the current descent is inside, innermost last.
    std::vector<Anchor> anchors;

    std::vector<Difference> differences;
    // How many compared parts agreed, for judging how close the two types are.
    int matched = 0;

    std::vector<std::string> helps;
    Helpifier helpifier;

    // Types the message prints by a name rather than spelled out (`where type Animal = ...`).
    const DenseHashMap<TypeId, std::string>* names = nullptr;
    // Every type displayed, for finding the ones the message repeats.
    std::vector<PrintedType>* printed = nullptr;

    Differ(NotNull<Subtyping> subtyping, NotNull<Scope> scope, NotNull<BuiltinTypes> builtinTypes, NotNull<Budget> budget)
        : subtyping(subtyping)
        , scope(scope)
        , builtinTypes(builtinTypes)
        , budget(budget)
    {
    }

    // A differ for trying one candidate, spending from the same budget.
    Differ trial(int trialAllowance = std::numeric_limits<int>::max()) const
    {
        Differ d{subtyping, scope, builtinTypes, budget};
        d.allowance = trialAllowance;
        d.names = names;
        d.printed = printed;
        d.helpifier = helpifier;
        return d;
    }

    ToStringOptions printing(size_t width) const
    {
        ToStringOptions options = printOptions(width);
        options.repeatedTypeNames = names;
        return options;
    }

    // The whole message is out of budget: stop comparing anything more.
    bool outOfBudget() const
    {
        return budget->remaining <= 0;
    }

    bool canDescend() const
    {
        return !outOfBudget() && spent < allowance;
    }

    bool fits(TypeId sub, TypeId super)
    {
        --budget->remaining;
        ++spent;
        SubtypingResult result = subtyping->isSubtype(sub, super, scope);
        return result.isSubtype || result.isErrorSuppressing;
    }

    // `ToString` doesn't print a union's alias name inside a larger type, so `{Json}` would come out as
    // a `t1 where ...` cycle. The simple containers are spelled out here, where names are known. A
    // type that reaches itself without passing a name, or is nested too deep, is printed by `ToString`
    // whole, which prints cycles.
    std::string display(TypeId ty)
    {
        if (printed)
            printed->push_back(PrintedType{ty, std::nullopt});
        std::vector<TypeId> walking;
        std::optional<std::string> shown = displayWalk(ty, walking);
        return shown ? *shown : toString(ty, printing(kTypeWidth));
    }

    // Members joined by `separator` in the order `#n` counts them, a `nil` member as a trailing `?`. Past
    // `maxWidth` characters the rest are cut off, as `ToString` cuts off a long type.
    std::string expansion(const std::vector<TypeId>& members, const char* separator, size_t maxWidth = std::string::npos)
    {
        std::optional<std::string> shown = joinMembers(
            members,
            separator,
            [&](TypeId member)
            {
                return std::optional<std::string>{display(member)};
            },
            maxWidth,
            true
        );
        LUAU_ASSERT(shown);
        return shown.value_or("");
    }

    // `members` as `expansion` prints them, each by `show`. Nullopt when `show` gives up on one, or when
    // they pass `maxWidth` characters and `truncate` is off.
    template<typename Show>
    static std::optional<std::string> joinMembers(const std::vector<TypeId>& members, const char* separator, Show show, size_t maxWidth, bool truncate)
    {
        // A `nil` anywhere is printed as the trailing `?`.
        bool hasNil = std::any_of(
            members.begin(),
            members.end(),
            [](TypeId member)
            {
                return isNil(follow(member));
            }
        );
        std::vector<std::string> parts;
        bool truncated = false;
        size_t width = 0;
        for (TypeId member : members)
        {
            if (isNil(follow(member)))
                continue;
            if (width > maxWidth)
            {
                if (!truncate)
                    return std::nullopt;
                truncated = true;
                break;
            }

            std::optional<std::string> shown = show(member);
            if (!shown)
                return std::nullopt;
            // A function or a nested intersection inside a union needs parentheses to read as one member.
            bool needsParentheses = get<FunctionType>(follow(member)) || (get<IntersectionType>(follow(member)) && !aliasName(member));
            parts.push_back(needsParentheses ? "(" + *shown + ")" : *shown);
            width += parts.back().size() + strlen(separator);
        }

        if (parts.empty())
            return hasNil ? "nil" : "never";
        if (truncated)
            parts.push_back("... *TRUNCATED*");
        std::string joinedParts = joined(parts, separator);
        if (!hasNil)
            return joinedParts;
        return parts.size() > 1 ? "(" + joinedParts + ")?" : joinedParts + "?";
    }

    // `display`, or nullopt when `ty` is already being displayed further up `walking`.
    std::optional<std::string> displayWalk(TypeId ty, std::vector<TypeId>& walking)
    {
        ty = follow(ty);
        if (std::optional<std::string> name = aliasName(ty))
            return name;
        if (const std::string* name = names ? names->find(ty) : nullptr)
            return *name;

        bool cycles = std::find(walking.begin(), walking.end(), ty) != walking.end();
        if (cycles || walking.size() >= size_t(kMaxWalkDepth))
            return std::nullopt;

        walking.push_back(ty);
        std::optional<std::string> shown;
        if (const TableType* table = get<TableType>(ty); table && table->props.empty() && table->indexer)
        {
            TypeId key = follow(table->indexer->indexType);
            std::optional<std::string> value = displayWalk(table->indexer->indexResultType, walking);
            const char* access = table->indexer->isReadOnly ? "read " : "";
            if (key == follow(builtinTypes->numberType))
                shown = value ? std::optional<std::string>{"{" + std::string(access) + *value + "}"} : std::nullopt;
            else if (std::optional<std::string> keyShown = value ? displayWalk(key, walking) : std::nullopt)
                shown = "{ " + std::string(access) + "[" + *keyShown + "]: " + *value + " }";
        }
        else if (const UnionType* u = get<UnionType>(ty))
            shown = joinMembers(
                u->options,
                " | ",
                [&](TypeId member)
                {
                    return displayWalk(member, walking);
                },
                kTypeWidth,
                false
            );
        else
            shown = toString(ty, printing(kTypeWidth));
        walking.pop_back();
        return shown;
    }

    std::string display(TypePackId tp)
    {
        if (printed)
            appendPrintedPack(*printed, tp);
        std::string s = toString(tp, printing(kTypeWidth));
        return s.empty() ? "()" : s;
    }

    // A function's type with its parameter names, when it still has them: the names are what a bullet
    // about one of its parameters refers to.
    std::string displayFunction(TypeId ty)
    {
        if (printed)
            printed->push_back(PrintedType{ty, std::nullopt});
        ToStringOptions options = printing(kFunctionTypeWidth);
        options.functionTypeArguments = true;
        return toString(ty, options);
    }

    // Records `d` inside the current function, noting whether either type suppresses errors.
    void push(Difference d, TypeId a, std::optional<TypeId> b = std::nullopt)
    {
        d.suppressing = shouldSuppressErrors(subtyping->normalizer, a) == ErrorSuppression::Suppress ||
                        (b && shouldSuppressErrors(subtyping->normalizer, *b) == ErrorSuppression::Suppress);
        if (!anchors.empty())
            d.anchor = anchors.back();
        differences.push_back(std::move(d));
    }

    void differ(Difference::Kind kind, const DiffPath& path, TypeId sub, TypeId super)
    {
        std::optional<std::string> suggestion = kind == Difference::Kind::Mismatch ? misspelledSingleton(sub, super) : std::nullopt;
        Difference d{kind, path, display(sub), display(super), suggestion};
        if (const UnionType* u = get<UnionType>(follow(sub)); u && kind == Difference::Kind::CouldBeNil && aliasName(sub))
            d.subMeaning = expansion(u->options, " | ", kTypeWidth);
        push(std::move(d), sub, super);
    }

    // The element type of a plain array `{T}`: number keys, no named fields.
    std::optional<TypeId> arrayElement(TypeId ty)
    {
        const TableType* table = get<TableType>(follow(ty));
        bool plainArray = table && table->props.empty() && table->indexer && follow(table->indexer->indexType) == follow(builtinTypes->numberType);
        if (!plainArray)
            return std::nullopt;
        return table->indexer->indexResultType;
    }

    // The key type of a plain map `{ [K]: V }` keyed by anything but numbers.
    std::optional<TypeId> mapKey(TypeId ty)
    {
        const TableType* table = get<TableType>(follow(ty));
        bool plainMap = table && table->props.empty() && table->indexer && follow(table->indexer->indexType) != follow(builtinTypes->numberType);
        if (!plainMap)
            return std::nullopt;
        return table->indexer->indexType;
    }

    // A plain map spelled out, `{ [string]: boolean }`, even when it has an alias: the alias doesn't say it's a
    // map.
    std::string mapDisplay(TypeId ty)
    {
        const TableType* table = get<TableType>(follow(ty));
        LUAU_ASSERT(table && table->indexer);
        if (!table || !table->indexer)
            return display(ty);
        return "{ [" + display(table->indexer->indexType) + "]: " + display(table->indexer->indexResultType) + " }";
    }

    // `sub` is an array of what `super` expects, or one element of the array `super` expects: the
    // reader passed a list where one value goes, or the reverse. Nullopt for anything else, and for an
    // `any` or `unknown` element, which fits everything and so says nothing about intent.
    std::optional<std::pair<Difference::Kind, TypeId>> arrayNesting(TypeId sub, TypeId super)
    {
        auto isTop = [](TypeId ty)
        {
            ty = follow(ty);
            return get<AnyType>(ty) || get<UnknownType>(ty);
        };

        if (std::optional<TypeId> element = arrayElement(sub); element && !isTop(*element) && fits(*element, super))
            return std::make_pair(Difference::Kind::ArrayOfExpected, *element);
        if (std::optional<TypeId> element = arrayElement(super); element && !isTop(*element) && fits(sub, *element))
            return std::make_pair(Difference::Kind::ElementOfExpected, *element);
        return std::nullopt;
    }

    // A property of `ty` as a reader of it sees it, following the table part of a metatable and each
    // part of an intersection. A string-keyed indexer answers for any name.
    std::optional<TypeId> readProperty(TypeId ty, const std::string& name, int depth = 0)
    {
        ty = follow(ty);
        if (const IntersectionType* intersection = get<IntersectionType>(ty))
        {
            if (depth >= kMaxWalkDepth)
                return std::nullopt;
            for (TypeId part : intersection->parts)
            {
                if (std::optional<TypeId> found = readProperty(part, name, depth + 1))
                    return found;
            }
            return std::nullopt;
        }

        const TableType* table = tableOf(ty);
        if (!table)
            return std::nullopt;

        if (auto it = table->props.find(name); it != table->props.end())
            return it->second.readTy;

        if (table->indexer && fits(builtinTypes->stringType, table->indexer->indexType))
            return table->indexer->indexResultType;

        return std::nullopt;
    }

    void collectPropertyNames(TypeId ty, std::vector<std::string>& names, int depth)
    {
        ty = follow(ty);
        if (const IntersectionType* intersection = get<IntersectionType>(ty))
        {
            if (depth >= kMaxWalkDepth)
                return;
            for (TypeId part : intersection->parts)
                collectPropertyNames(part, names, depth + 1);
        }
        else if (const TableType* table = tableOf(ty))
        {
            for (const auto& [name, prop] : table->props)
                names.push_back(name);
        }
    }

    std::vector<std::string> propertyNames(TypeId ty)
    {
        std::vector<std::string> names;
        collectPropertyNames(ty, names, 0);
        return names;
    }

    bool hasStringIndexer(TypeId ty)
    {
        const TableType* table = tableOf(ty);
        return table && table->indexer && fits(builtinTypes->stringType, table->indexer->indexType);
    }

    // True when two types have nothing to compare: different kinds of value, or two tables where
    // not one property the expected table needs is present on the given one.
    bool unrelated(TypeId sub, TypeId super, int depth = 0)
    {
        sub = follow(sub);
        super = follow(super);

        // An intersection is related to whatever any of its parts is related to.
        if (const IntersectionType* intersection = get<IntersectionType>(super))
        {
            if (depth >= kMaxWalkDepth)
                return false;
            for (TypeId part : intersection->parts)
            {
                if (!unrelated(sub, part, depth + 1))
                    return false;
            }
            return true;
        }

        Shape subShape = shapeOf(sub);
        Shape superShape = shapeOf(super);

        if (subShape == Shape::Intersection && superShape == Shape::Table)
            subShape = Shape::Table;

        if (subShape != superShape)
            return true;

        if (superShape != Shape::Table)
            return false;

        const TableType* superTable = tableOf(super);
        if (!superTable)
            return false;

        // A record against an array (or any map not keyed by strings): its fields are nothing the
        // indexer could hold. Two maps whose keys can't overlap (an array and a string map) likewise.
        if (superTable->props.empty())
        {
            const TableType* subTable = tableOf(sub);
            bool bothPureMaps = subTable && subTable->props.empty() && subTable->indexer && superTable->indexer;
            if (bothPureMaps)
            {
                TypeId subKey = subTable->indexer->indexType;
                TypeId superKey = superTable->indexer->indexType;
                return !fits(subKey, superKey) && !fits(superKey, subKey);
            }

            bool subIsRecord = !propertyNames(sub).empty() && !(subTable && subTable->indexer);
            return subIsRecord && superTable->indexer && !fits(builtinTypes->stringType, superTable->indexer->indexType);
        }

        if (hasStringIndexer(sub))
            return false;

        std::vector<std::string> names = propertyNames(sub);
        std::unordered_set<std::string> subNames{names.begin(), names.end()};
        for (const auto& [name, prop] : superTable->props)
        {
            if (subNames.count(name))
                return false;
        }
        return true;
    }

    // Properties in the order they were written, which is the order a reader scans them in; `Props`
    // is sorted by name.
    static std::vector<std::pair<std::string, const Property*>> propertiesInSourceOrder(const TableType* table)
    {
        std::vector<std::pair<std::string, const Property*>> props;
        for (const auto& [name, prop] : table->props)
            props.emplace_back(name, &prop);

        auto locationOf = [](const Property* prop) -> std::optional<Location>
        {
            if (prop->typeLocation)
                return prop->typeLocation;
            return prop->location;
        };

        std::stable_sort(
            props.begin(),
            props.end(),
            [&](const auto& a, const auto& b)
            {
                std::optional<Location> la = locationOf(a.second);
                std::optional<Location> lb = locationOf(b.second);
                if (la && lb)
                    return la->begin < lb->begin;
                return la.has_value() && !lb.has_value();
            }
        );
        return props;
    }

    // An indexer's key as a path shows it: `[string]`, and `[i]` for an array, which is how the type
    // is printed right above.
    Segment indexerSegment(TypeId key)
    {
        if (follow(key) == follow(builtinTypes->numberType))
            return Segment{Segment::Kind::Indexer, "i"};
        return Segment{Segment::Kind::Indexer, display(key)};
    }

    void diff(TypeId sub, TypeId super, const DiffPath& path, int depth)
    {
        sub = follow(sub);
        super = follow(super);

        if (fits(sub, super))
        {
            ++matched;
            return;
        }

        bool alreadyDescending = std::find(descending.begin(), descending.end(), std::make_pair(sub, super)) != descending.end();
        if (!canDescend() || depth > kMaxDiffDepth || alreadyDescending)
        {
            differ(Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        descending.emplace_back(sub, super);
        diffUnlessEqual(sub, super, path, depth);
        descending.pop_back();
    }

    void diffUnlessEqual(TypeId sub, TypeId super, const DiffPath& path, int depth)
    {
        // Comparing an array with one of its elements field by field would list every field of the
        // element as a difference; what's wrong is the braces around it.
        if (std::optional<std::pair<Difference::Kind, TypeId>> nesting = arrayNesting(sub, super))
        {
            Difference d{nesting->first, path, display(sub), display(super)};
            if (nesting->first == Difference::Kind::ElementOfExpected)
                d.element = display(nesting->second);
            push(std::move(d), sub, super);
            return;
        }

        if (const UnionType* subUnion = get<UnionType>(sub))
        {
            diffGivenUnion(sub, subUnion, super, path, depth);
            return;
        }

        if (const UnionType* superUnion = get<UnionType>(super))
        {
            std::vector<TypeId> members = numberedMembers(superUnion->options);
            if (members.size() == 1)
            {
                // Only worth unwrapping `T?` to walk into a `T`; a leaf has to show the `?` it expected.
                bool walkIntoMember = isStructured(shapeOf(members[0])) && !unrelated(sub, members[0]);
                if (walkIntoMember)
                    diff(sub, members[0], path, depth + 1);
                else
                    differ(Difference::Kind::Mismatch, path, sub, super);
                return;
            }

            diffExpectedUnion(sub, members, super, path, depth);
            return;
        }

        if (const IntersectionType* superIntersection = get<IntersectionType>(super))
        {
            // Overloads are told apart by number. The parts of `Request & { user: Player }` are one
            // table to the reader, so a property path through them needs no number.
            bool overloads = std::all_of(
                superIntersection->parts.begin(),
                superIntersection->parts.end(),
                [](TypeId part)
                {
                    return shapeOf(part) == Shape::Function;
                }
            );

            size_t before = differences.size();
            for (size_t i = 0; i < superIntersection->parts.size() && canDescend(); ++i)
            {
                TypeId part = superIntersection->parts[i];
                if (!fits(sub, part))
                    diff(sub, part, overloads ? with(path, Segment{Segment::Kind::Member, "", i + 1, false, true}) : path, depth + 1);
            }
            if (differences.size() == before)
                differ(Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        Shape subShape = shapeOf(sub);
        Shape superShape = shapeOf(super);

        // A metatable whose table part isn't a table has no properties to compare.
        bool comparableTables =
            superShape == Shape::Table && tableOf(super) && (subShape == Shape::Table || subShape == Shape::Intersection) && !unrelated(sub, super);
        if (comparableTables)
        {
            diffTables(sub, super, path, depth);
            return;
        }

        if (superShape == Shape::Function && subShape == Shape::Function)
        {
            diffFunctions(sub, super, path, depth);
            return;
        }

        differ(Difference::Kind::Mismatch, path, sub, super);
    }

    void diffGivenUnion(TypeId sub, const UnionType* subUnion, TypeId super, const DiffPath& path, int depth)
    {
        bool nilFails = false;
        std::vector<TypeId> failing;
        for (TypeId option : subUnion->options)
        {
            // Which members fail decides what is said, so without the budget to find out, say only
            // that the union doesn't fit.
            if (outOfBudget())
            {
                differ(Difference::Kind::Mismatch, path, sub, super);
                return;
            }
            if (fits(option, super))
                continue;
            if (isNil(follow(option)))
                nilFails = true;
            else
                failing.push_back(option);
        }

        if (failing.empty())
        {
            differ(nilFails ? Difference::Kind::CouldBeNil : Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        // One member of a `T?` that fails structurally is worth walking into, with the `nil` said
        // separately; anything else is one line saying what could arrive here.
        bool walkIntoMember = failing.size() == 1 && isStructured(shapeOf(failing[0])) && !unrelated(failing[0], super);
        if (walkIntoMember)
        {
            if (nilFails)
                differ(Difference::Kind::CouldBeNil, path, sub, super);
            diff(failing[0], super, path, depth + 1);
            return;
        }

        // Walking into each failing member would print one set of paths per member, which is the
        // blowup this is here to avoid. A named union that mostly fails is shown by its name.
        std::vector<TypeId> nonNil = numberedMembers(subUnion->options);
        bool mostlyFailingAlias = aliasName(sub) && failing.size() * 2 >= nonNil.size();
        std::string got;
        if (failing.size() == nonNil.size() || mostlyFailingAlias)
        {
            got = display(sub);
        }
        else
        {
            if (nilFails)
                failing.push_back(builtinTypes->nilType);
            got = expansion(failing, " | ", kTypeWidth);
        }
        Difference d{Difference::Kind::Mismatch, path, got, display(super)};
        if (std::string whole = display(sub); whole != got)
            d.subInFull = std::move(whole);
        push(std::move(d), sub, super);
    }

    // The member of an expected union whose singleton tag the given table carries, when exactly one
    // member has it. `misspelled` looks for the one member whose string tag the given one is a typo of instead.
    std::optional<size_t> taggedMember(TypeId sub, const std::vector<TypeId>& members, std::optional<Tag>* pickedBy, bool misspelled)
    {
        const TableType* subTable = tableOf(sub);
        if (!subTable)
            return std::nullopt;

        for (const auto& [name, prop] : subTable->props)
        {
            if (!prop.readTy)
                continue;
            const SingletonType* tag = get<SingletonType>(follow(*prop.readTy));
            if (!tag)
                continue;

            std::optional<size_t> onlyMatch;
            size_t matches = 0;
            for (size_t i = 0; i < members.size() && matches < 2 && !outOfBudget(); ++i)
            {
                std::optional<TypeId> memberTag = readProperty(members[i], name);
                const SingletonType* memberSingleton = memberTag ? get<SingletonType>(follow(*memberTag)) : nullptr;
                bool sameTag = memberSingleton && (misspelled ? isMisspelledString(*tag, *memberSingleton) : *memberSingleton == *tag);
                if (sameTag)
                {
                    onlyMatch = i;
                    ++matches;
                }
            }

            if (matches == 1)
            {
                if (pickedBy && !misspelled)
                    *pickedBy = Tag{name, display(*prop.readTy)};
                return onlyMatch;
            }
        }
        return std::nullopt;
    }

    // The member of an expected union the given value most likely meant to be: one whose singleton
    // tag it carries, or else the one it differs from least.
    std::optional<size_t> closestMember(TypeId sub, const std::vector<TypeId>& members, std::optional<Tag>* pickedBy)
    {
        if (std::optional<size_t> tagged = taggedMember(sub, members, pickedBy, false))
            return tagged;
        // A tag one typo from a member's (`kind = "leeve"`) still says which member was meant.
        if (std::optional<size_t> tagged = taggedMember(sub, members, nullptr, true))
            return tagged;

        std::optional<size_t> best;
        size_t bestDifferences = 0;
        int bestMatched = 0;
        bool bestUnrelated = true;
        // Choosing may spend half of what's left, so the member chosen can still be diffed. A candidate
        // the budget cut off stopped comparing early and would look closer than the ones before it, so
        // it doesn't count.
        int reserve = budget->remaining / 2;
        for (size_t i = 0; i < members.size() && (!best || budget->remaining > reserve); ++i)
        {
            bool isUnrelated = unrelated(sub, members[i]);

            Differ candidate = trial(kCandidateBudget);
            candidate.diff(sub, members[i], DiffPath{}, 0);
            if (best && outOfBudget())
                break;

            bool closerKind = bestUnrelated && !isUnrelated;
            bool sameKind = bestUnrelated == isUnrelated;
            bool fewerDifferences = candidate.differences.size() < bestDifferences;
            bool moreAgreeing = candidate.differences.size() == bestDifferences && candidate.matched > bestMatched;
            if (!best || closerKind || (sameKind && (fewerDifferences || moreAgreeing)))
            {
                best = i;
                bestDifferences = candidate.differences.size();
                bestMatched = candidate.matched;
                bestUnrelated = isUnrelated;
            }
        }
        return best;
    }

    void diffExpectedUnion(TypeId sub, const std::vector<TypeId>& members, TypeId super, const DiffPath& path, int depth)
    {
        if (!canChooseMember(sub, members))
        {
            differ(Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        std::optional<size_t> closest = closestMember(sub, members, nullptr);
        if (!closest || unrelated(sub, members[*closest]))
        {
            differ(Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        diff(sub, members[*closest], with(path, Segment{Segment::Kind::Member, "", *closest + 1}), depth + 1);
    }

    // A property the given table has under a name close to `name`, which the expected table doesn't
    // use itself.
    static std::optional<std::string> similarlyNamed(const std::string& name, const std::vector<std::string>& candidates, const TableType* expected)
    {
        for (const std::string& candidate : candidates)
        {
            bool unusedByExpected = expected->props.count(candidate) == 0;
            if (unusedByExpected && looksLikeTypo(candidate, name))
                return candidate;
        }
        return std::nullopt;
    }

    // Where a table is written, when it's written in source.
    static std::optional<PropertySite> tableSite(const TableType* table)
    {
        if (!table || table->definitionModuleName.empty() || table->definitionLocation == Location())
            return std::nullopt;
        return PropertySite{table->definitionLocation, table->definitionModuleName};
    }

    // Marks the differences from `from` on that are under the `kind` segment `i` or `j` right after `at` segments.
    void markSwapped(size_t from, size_t at, Segment::Kind kind, size_t i, size_t j)
    {
        for (size_t k = from; k < differences.size(); ++k)
        {
            Difference& d = differences[k];
            bool underSwapped = d.path.size() > at && d.path[at].kind == kind && (d.path[at].index == i || d.path[at].index == j);
            if (underSwapped)
                d.explainedBySwap = true;
        }
    }

    static std::optional<PropertySite> propertySite(const TableType* table, const std::string& name)
    {
        if (!table || table->definitionModuleName.empty())
            return std::nullopt;
        auto it = table->props.find(name);
        if (it == table->props.end())
            return std::nullopt;
        std::optional<Location> at = it->second.typeLocation ? it->second.typeLocation : it->second.location;
        if (!at)
            return std::nullopt;
        return PropertySite{*at, table->definitionModuleName};
    }

    // Where the two tables declare `name`, on the differences from `from` on that are at the property itself
    // (`pathLength` segments long) rather than inside it.
    void notePropertySites(size_t from, size_t pathLength, const TableType* given, const TableType* expected, const std::string& name)
    {
        if (from == differences.size())
            return;
        std::optional<PropertySite> givenSite = propertySite(given, name);
        std::optional<PropertySite> expectedSite = propertySite(expected, name);
        for (size_t i = from; i < differences.size(); ++i)
        {
            Difference& d = differences[i];
            if (d.path.size() != pathLength)
                continue;
            if (!d.givenSite)
                d.givenSite = givenSite;
            if (!d.expectedSite)
                d.expectedSite = expectedSite;
        }
    }

    // `leafIfClean`: when nothing more specific is found, whether to report the two tables themselves.
    void diffTables(TypeId sub, TypeId super, const DiffPath& path, int depth, bool leafIfClean = true)
    {
        const TableType* superTable = tableOf(super);
        if (!superTable)
        {
            if (leafIfClean)
                differ(Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        size_t before = differences.size();
        std::vector<std::string> subNames = propertyNames(sub);

        // Subtyping never accepts a sealed table with no indexer where one with an indexer is expected,
        // whatever its fields hold, so pointing at a field would send the reader to fix the wrong thing.
        const TableType* givenTable = tableOf(sub);
        bool expectsPureMap = superTable->props.empty() && superTable->indexer;
        bool givenSealedRecord = givenTable && !givenTable->indexer && givenTable->state == TableState::Sealed;
        if (expectsPureMap && givenSealedRecord)
        {
            differ(Difference::Kind::NotAMap, path, sub, super);
            return;
        }

        std::vector<std::pair<std::string, const Property*>> expectedProps = propertiesInSourceOrder(superTable);
        for (const auto& [name, prop] : expectedProps)
        {
            if (!canDescend())
                break;
            if (!prop->readTy)
                continue;

            TypeId wanted = *prop->readTy;
            DiffPath propertyPath = with(path, Segment{Segment::Kind::Property, name});
            std::optional<TypeId> have = readProperty(sub, name);
            if (!have)
            {
                if (fits(builtinTypes->nilType, wanted))
                {
                    ++matched;
                    continue;
                }

                Difference missing{Difference::Kind::Missing, propertyPath, "", display(wanted)};
                missing.similarName = similarlyNamed(name, subNames, superTable);
                missing.givenSite = tableSite(tableOf(sub));
                missing.expectedSite = propertySite(superTable, name);
                push(std::move(missing), wanted);
                continue;
            }

            size_t found = differences.size();
            diff(*have, wanted, propertyPath, depth + 1);
            notePropertySites(found, propertyPath.size(), tableOf(sub), superTable, name);
        }

        if (superTable->indexer && canDescend())
        {
            const TableIndexer& wantedIndexer = *superTable->indexer;
            DiffPath indexPath = with(path, indexerSegment(wantedIndexer.indexType));

            if (givenTable && givenTable->indexer)
                diff(givenTable->indexer->indexResultType, wantedIndexer.indexResultType, indexPath, depth + 1);

            // Each property the expected table doesn't name is a value its indexer would hold. A literal
            // inferred against a map gets the map's indexer *and* keeps its own properties, so this
            // applies whether or not the given table has an indexer.
            if (givenTable && fits(builtinTypes->stringType, wantedIndexer.indexType))
            {
                for (const auto& [name, prop] : propertiesInSourceOrder(givenTable))
                {
                    if (!canDescend())
                        break;
                    if (prop->readTy && superTable->props.count(name) == 0)
                        diff(*prop->readTy, wantedIndexer.indexResultType, with(path, Segment{Segment::Kind::Property, name}), depth + 1);
                }
            }
        }

        if (differences.size() != before)
            return;

        // Every part fits when read, so it's a part that can also be written: the expected type
        // could store any of its own type there, which the given one doesn't allow.
        for (const auto& [name, prop] : expectedProps)
        {
            if (!canDescend())
                break;
            if (!prop->readTy || !prop->writeTy || !givenTable)
                continue;

            auto it = givenTable->props.find(name);
            if (it == givenTable->props.end() || !it->second.readTy || !it->second.writeTy)
                continue;

            if (fits(*prop->writeTy, *it->second.writeTy))
                continue;

            // Subtyping reads a table-valued property covariantly without checking properties
            // against the other table's indexer, so a `{ quality = true }` put in a
            // `{ [string]: number }` only fails as "not exactly". Look inside for the real cause.
            DiffPath propertyPath = with(path, Segment{Segment::Kind::Property, name});
            size_t beforeProperty = differences.size();
            TypeId wantedTable = withoutNil(*prop->readTy);
            bool bothTables = tableOf(*it->second.readTy) && tableOf(wantedTable);
            if (bothTables && depth < kMaxDiffDepth)
                diffTables(follow(*it->second.readTy), wantedTable, propertyPath, depth + 1, false);

            if (differences.size() == beforeProperty)
                differ(Difference::Kind::MismatchExactly, propertyPath, *it->second.readTy, *prop->readTy);
        }

        bool bothWritableIndexers =
            superTable->indexer && givenTable && givenTable->indexer && !superTable->indexer->isReadOnly && !givenTable->indexer->isReadOnly;
        if (bothWritableIndexers && !fits(superTable->indexer->indexResultType, givenTable->indexer->indexResultType))
            differ(
                Difference::Kind::MismatchExactly,
                with(path, indexerSegment(superTable->indexer->indexType)),
                givenTable->indexer->indexResultType,
                superTable->indexer->indexResultType
            );

        if (differences.size() == before && leafIfClean)
            differ(Difference::Kind::Mismatch, path, sub, super);
    }

    // The parameter's name as the given function spells it: that's the function a bullet prints, so a
    // name from the other one would refer to nothing on screen. Empty when it wasn't kept.
    static std::string parameterName(const FunctionType* given, size_t index)
    {
        if (index < given->argNames.size() && given->argNames[index] && !given->argNames[index]->name.empty())
            return given->argNames[index]->name;
        return "";
    }

    static std::optional<TypeId> variadicOf(std::optional<TypePackId> tail)
    {
        if (!tail)
            return std::nullopt;
        if (const VariadicTypePack* variadic = get<VariadicTypePack>(follow(*tail)))
            return variadic->ty;
        return std::nullopt;
    }

    void diffFunctions(TypeId sub, TypeId super, const DiffPath& path, int depth)
    {
        const FunctionType* given = get<FunctionType>(sub);
        const FunctionType* expected = get<FunctionType>(super);
        LUAU_ASSERT(given && expected);
        size_t before = differences.size();

        // Which parameter a generic stands for depends on how it's instantiated, which a structural
        // walk can't see.
        bool generic = !given->generics.empty() || !given->genericPacks.empty() || !expected->generics.empty() || !expected->genericPacks.empty();
        if (generic)
        {
            differ(Difference::Kind::Mismatch, path, sub, super);
            return;
        }

        auto [expectedArgs, expectedArgTail] = flatten(expected->argTypes);
        auto [givenArgs, givenArgTail] = flatten(given->argTypes);

        // Every difference below is about this function, so it's what a bullet shows first.
        anchors.push_back(Anchor{displayFunction(sub), given->name, path.size()});

        std::vector<size_t> mismatchedParameters;
        size_t parameterCount = std::max(expectedArgs.size(), givenArgs.size());
        for (size_t i = 0; i < parameterCount && canDescend(); ++i)
        {
            DiffPath parameterPath = with(path, Segment{Segment::Kind::Parameter, parameterName(given, i), i});

            std::optional<TypeId> accepted = i < givenArgs.size() ? std::optional<TypeId>{givenArgs[i]} : variadicOf(givenArgTail);
            if (!accepted)
            {
                // Only a function with no `...` of its own has nowhere to put this argument.
                if (i < expectedArgs.size() && !givenArgTail)
                {
                    Difference extra{Difference::Kind::ExtraPassed, parameterPath, display(expectedArgs[i]), ""};
                    extra.parameterCount = givenArgs.size();
                    push(std::move(extra), expectedArgs[i]);
                }
                continue;
            }

            std::optional<TypeId> passed = i < expectedArgs.size() ? std::optional<TypeId>{expectedArgs[i]} : variadicOf(expectedArgTail);
            if (!passed)
            {
                if (fits(builtinTypes->nilType, *accepted))
                    ++matched;
                else
                {
                    Difference nothing{Difference::Kind::NothingPassed, parameterPath, "", display(*accepted)};
                    nothing.parameterCount = expectedArgs.size();
                    push(std::move(nothing), *accepted);
                }
                continue;
            }

            size_t beforeParameter = differences.size();
            diff(*passed, *accepted, parameterPath, depth + 1);
            bool declaredOnBothSides = i < expectedArgs.size() && i < givenArgs.size();
            if (differences.size() != beforeParameter && declaredOnBothSides)
                mismatchedParameters.push_back(i);
        }

        // Two parameters that each fit the other's place were almost certainly written in the wrong
        // order. `nil` is judged separately, so a `Player?` in the other's place still reads as swapped.
        for (size_t a = 0; a + 1 < mismatchedParameters.size() && canDescend(); ++a)
        {
            size_t i = mismatchedParameters[a];
            size_t j = mismatchedParameters[a + 1];
            if (fits(withoutNil(expectedArgs[i]), givenArgs[j]) && fits(withoutNil(expectedArgs[j]), givenArgs[i]))
            {
                helps.push_back(swappedParametersHelp(parameterName(given, i), parameterName(given, j), i, j, helpifier));
                markSwapped(before, path.size(), Segment::Kind::Parameter, i, j);
                break;
            }
        }

        auto [expectedRets, expectedRetTail] = flatten(expected->retTypes);
        auto [givenRets, givenRetTail] = flatten(given->retTypes);
        std::vector<size_t> mismatchedReturns;
        for (size_t i = 0; i < expectedRets.size() && canDescend(); ++i)
        {
            DiffPath returnPath = with(path, Segment{Segment::Kind::Return, "", i, expectedRets.size() == 1});
            std::optional<TypeId> returned = i < givenRets.size() ? std::optional<TypeId>{givenRets[i]} : variadicOf(givenRetTail);
            if (!returned)
            {
                if (fits(builtinTypes->nilType, expectedRets[i]))
                    ++matched;
                else
                    push(Difference{Difference::Kind::NothingReturned, returnPath, "", display(expectedRets[i])}, expectedRets[i]);
                continue;
            }

            size_t beforeReturn = differences.size();
            diff(*returned, expectedRets[i], returnPath, depth + 1);
            if (differences.size() != beforeReturn && i < givenRets.size())
                mismatchedReturns.push_back(i);
        }

        // Two return values that each fit the other's place were returned in the wrong order.
        for (size_t a = 0; a + 1 < mismatchedReturns.size() && canDescend(); ++a)
        {
            size_t i = mismatchedReturns[a];
            size_t j = mismatchedReturns[a + 1];
            if (fits(givenRets[i], expectedRets[j]) && fits(givenRets[j], expectedRets[i]))
            {
                helps.push_back(swappedReturnsHelp(i, j, helpifier));
                markSwapped(before, path.size(), Segment::Kind::Return, i, j);
                break;
            }
        }

        anchors.pop_back();

        if (differences.size() == before)
            differ(Difference::Kind::Mismatch, path, sub, super);
    }
};

// A parameter that doesn't take the `nil` the expected type can pass it. Parameters are where a
// narrower type is wrong, and a value is where it's right.
// Whether `d`'s bullet says both what was expected and what was given (`expected 'number', got 'string'`,
// a missing property with its type), rather than leaving one of them to the two types above it.
bool saysBothTypes(const Difference& d)
{
    switch (d.kind)
    {
    case Difference::Kind::ArrayOfExpected:
    case Difference::Kind::ElementOfExpected:
        return false;
    case Difference::Kind::Mismatch:
    case Difference::Kind::MismatchExactly:
    case Difference::Kind::Missing:
    case Difference::Kind::CouldBeNil:
    case Difference::Kind::NothingPassed:
    case Difference::Kind::NothingReturned:
    case Difference::Kind::ExtraPassed:
    case Difference::Kind::NotAMap:
        return true;
    }
    return true;
}

bool isNarrowedParameter(const Difference& d)
{
    bool inFunction = d.anchor && d.anchor->at < d.path.size();
    return d.kind == Difference::Kind::CouldBeNil && inFunction && d.path[d.anchor->at].kind == Segment::Kind::Parameter;
}

// A misspelled string at a property path of the given table is a typo the caller can report at the string, when it
// finds where the string is written.
void misspelledValueTypo(const Difference& d, MismatchExplanation& explanation)
{
    std::vector<std::string> properties;
    for (const Segment& segment : onGivenSide(d.path))
    {
        if (segment.kind != Segment::Kind::Property)
            return;
        properties.push_back(segment.name);
    }
    if (properties.empty() || !d.similarName)
        return;
    explanation.typo = {d.sub, *d.similarName};
    explanation.typoProperties = std::move(properties);
}

// A difference only names a place when it has a path; one at the root restates the two printed types.
// One involving an unresolved type is dropped, and `suppressedAny` notes that it happened. So is one at a swapped
// pair of parameters or return values, which a help says, unless that's all there is.
std::vector<Difference> locatedDifferences(const std::vector<Difference>& differences, bool& suppressedAny)
{
    std::vector<Difference> located;
    bool somethingElse = false;
    for (const Difference& d : differences)
    {
        if (d.suppressing)
            suppressedAny = true;
        else if (!d.path.empty())
        {
            located.push_back(d);
            somethingElse |= !d.explainedBySwap;
        }
    }
    if (somethingElse)
    {
        auto swapped = [](const Difference& d)
        {
            return d.explainedBySwap;
        };
        located.erase(std::remove_if(located.begin(), located.end(), swapped), located.end());
    }
    return located;
}

// The table a `Missing` difference is about, on the given side.
DiffPath missingParent(const Difference& d)
{
    LUAU_ASSERT(!d.path.empty());
    return onGivenSide(DiffPath{d.path.begin(), d.path.end() - 1});
}

// Bullets for `differences`, at most `limit` and a count of the rest: those inside one function
// grouped under that function's type, and `kGroupedMissingFields` or more properties missing from
// one table folded into one.
std::vector<Line> bullets(const std::vector<Difference>& differences, Rendering& rendering, const std::optional<std::string>& callee, size_t limit)
{
    // Which group each difference is in, found once per difference. A group is shown where its first
    // member is.
    std::vector<std::vector<size_t>> groups;
    std::vector<std::optional<size_t>> groupOf(differences.size());
    std::unordered_map<std::string, size_t> groupByKey;
    for (size_t i = 0; i < differences.size(); ++i)
    {
        const Difference& d = differences[i];
        std::string key;
        if (d.anchor)
            key = "function\n" + d.anchor->display + "\n" + anchorLabel(d, rendering.givenRoot);
        else if (d.kind == Difference::Kind::Missing && !d.similarName)
        {
            DiffPath parent = missingParent(d);
            key = "missing\n" + renderPath("", parent, 0, parent.size());
        }
        else
            continue;

        auto [it, inserted] = groupByKey.try_emplace(key, groups.size());
        if (inserted)
            groups.emplace_back();
        groups[it->second].push_back(i);
        groupOf[i] = it->second;
    }

    // Missing fields fold only past a few; fewer each get a bullet of their own, where they are.
    auto foldsIntoOne = [&](size_t group)
    {
        const Difference& first = differences[groups[group].front()];
        return first.anchor || groups[group].size() >= kGroupedMissingFields;
    };

    std::vector<Line> lines;
    std::vector<bool> shownGroup(groups.size(), false);
    size_t total = 0;
    for (size_t i = 0; i < differences.size(); ++i)
    {
        std::optional<size_t> group = groupOf[i];
        bool folded = group && foldsIntoOne(*group);
        if (folded && shownGroup[*group])
            continue;
        if (folded)
            shownGroup[*group] = true;

        ++total;
        if (lines.size() == limit)
            continue;

        const Difference& d = differences[i];
        if (!folded)
        {
            lines.push_back(Line{true, {describe(d, rendering)}});
            continue;
        }

        std::vector<const Difference*> members;
        for (size_t j : groups[*group])
            members.push_back(&differences[j]);

        if (d.anchor)
        {
            // The caller is only known for the function passed to it, not for one nested inside.
            std::optional<std::string> caller = d.anchor->at == 0 ? callee : std::nullopt;
            lines.push_back(functionBullet(members, caller, rendering));
        }
        else
            lines.push_back(missingFieldsBullet(missingParent(d), members, rendering));
    }

    if (total > lines.size())
        lines.push_back(andMore(total - lines.size(), true));
    return lines;
}


// Close enough that the reader most likely meant it: a few differences, or most of it agreeing.
bool isNear(size_t differences, int matched)
{
    constexpr size_t kFewDifferences = 3;
    constexpr int kAgreeingPercent = 75;
    int compared = matched + int(differences);
    return differences <= kFewDifferences || matched * 100 >= compared * kAgreeingPercent;
}

struct MemberDiff
{
    size_t number;
    TypeId type;
    std::vector<Difference> differences;
    int matched;
    bool unrelated;
};

// An empty table where one of several tables is expected (`local e: GameEvent = {}`, typically while it's
// still being written): nothing that's there is wrong, it's what isn't there yet, so this lists what to
// add rather than calling the two unrelated. The fields every member needs come first, then what each
// member needs on top of them, named by its tag when the members have one (`'kind = "chat"' also needs
// ...`). Returns false when some member isn't a plain table or needs no fields at all, since then the
// empty table isn't failing for want of fields.
bool describeEmptyTableForUnion(
    Differ& differ,
    const std::vector<TypeId>& members,
    const std::string& subject,
    Rendering& rendering,
    std::string& header,
    std::vector<Line>& lines
)
{
    if (members.empty())
        return false;

    // Each member's required fields (those that can't be left out as `nil`), in source order.
    using Fields = std::vector<std::pair<std::string, TypeId>>;
    std::vector<Fields> required;
    for (TypeId member : members)
    {
        const TableType* table = get<TableType>(follow(member));
        if (!table || table->indexer)
            return false;

        Fields fields;
        for (const auto& [name, prop] : Differ::propertiesInSourceOrder(table))
        {
            // Which fields are required decides every line, so without the budget to find out, say nothing.
            if (differ.outOfBudget())
                return false;
            if (prop->readTy && !differ.fits(differ.builtinTypes->nilType, *prop->readTy))
                fields.emplace_back(name, *prop->readTy);
        }
        if (fields.empty())
            return false;
        required.push_back(std::move(fields));
    }

    auto typeIn = [](const Fields& fields, const std::string& name) -> std::optional<TypeId>
    {
        for (const auto& [fieldName, ty] : fields)
        {
            if (fieldName == name)
                return ty;
        }
        return std::nullopt;
    };

    // Fields every member requires, each shown with the types the members give it, deduplicated
    // (`kind: "join" | "leave" | "chat"`, `player: Player`).
    std::vector<std::string> common;
    std::vector<Line> commonLines;
    std::optional<std::string> tag;
    for (const auto& [name, firstType] : required.front())
    {
        std::vector<std::string> shown;
        bool everywhere = true;
        bool singletons = true;
        for (const Fields& fields : required)
        {
            std::optional<TypeId> ty = typeIn(fields, name);
            if (!ty)
            {
                everywhere = false;
                break;
            }
            singletons &= get<SingletonType>(follow(*ty)) != nullptr;
            std::string s = differ.display(*ty);
            if (std::find(shown.begin(), shown.end(), s) == shown.end())
                shown.push_back(s);
        }
        if (!everywhere)
            continue;

        // A singleton that tells every member apart is what the reader will pick a member by.
        if (!tag && singletons && shown.size() == members.size())
            tag = name;

        common.push_back(name);
        commonLines.push_back(fieldBullet(name, joined(shown, " | ")));
    }

    std::string target = rendering.expectedTypeName();
    if (common.empty())
        header = missingEveryMembersFieldsHeader(subject, target);
    else
    {
        header = missingCommonFieldsHeader(subject, common.size(), target);
        size_t shownCommon = std::min(commonLines.size(), kMaxBullets);
        lines.insert(lines.end(), commonLines.begin(), commonLines.begin() + shownCommon);
        if (commonLines.size() > shownCommon)
            lines.push_back(andMore(commonLines.size() - shownCommon, true, "properties"));
    }

    auto isCommon = [&](const std::string& name)
    {
        return std::find(common.begin(), common.end(), name) != common.end();
    };

    size_t shownMembers = 0;
    size_t hiddenMembers = 0;
    for (size_t i = 0; i < members.size(); ++i)
    {
        bool needsMore = std::any_of(
            required[i].begin(),
            required[i].end(),
            [&](const auto& field)
            {
                return !isCommon(field.first);
            }
        );
        if (!needsMore)
            continue;
        if (shownMembers == kMaxBullets)
        {
            ++hiddenMembers;
            continue;
        }

        std::vector<std::string> extra;
        for (const auto& [name, ty] : required[i])
        {
            if (!isCommon(name))
                extra.push_back(quoted(name + ": " + differ.display(ty)));
        }

        // Under the shared fields, these are more than the header counted, so they get a line of their own.
        if (!common.empty() && shownMembers == 0)
            lines.push_back(someMembersNeedMoreLine());

        std::optional<TypeId> tagType = tag ? typeIn(required[i], *tag) : std::nullopt;
        Line line = memberNeedsBullet(tagType ? tagLabel(*tag, differ.display(*tagType)) : rendering.expectedMember(i + 1), !common.empty(), extra);
        if (!tagType)
            line.expectedMembers.push_back(i + 1);
        lines.push_back(std::move(line));
        ++shownMembers;
    }
    if (hiddenMembers > 0)
        lines.push_back(andMore(hiddenMembers, true, "members"));
    return true;
}

// What follows `type X =` for an aliased union or intersection whose members the message counts: the `shown` ones
// in place, with the rest left out (`… | { kind: "leave", ... } | …`), else, when that's still long, only the `shown`
// ones by number.
std::string declaredExpansion(Differ& differ, TypeId ty, const std::set<size_t>& shown)
{
    const UnionType* u = get<UnionType>(ty);
    const std::vector<TypeId>& options = u ? u->options : get<IntersectionType>(ty)->parts;
    const char* separator = u ? " | " : " & ";

    std::vector<TypeId> members = numberedMembers(options);
    std::string inPlace;
    size_t hidden = 0;
    auto append = [&](const std::string& part)
    {
        inPlace += (inPlace.empty() ? "" : separator) + part;
    };
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (!shown.count(i + 1))
        {
            ++hidden;
            continue;
        }
        if (hidden > 0)
            append(hiddenMembers(hidden));
        hidden = 0;
        append(differ.display(members[i]));
    }
    if (hidden > 0)
        append(hiddenMembers(hidden));
    if (members.size() != options.size())
        inPlace = "(" + inPlace + ")?";
    if (inPlace.size() <= kMaxSpelledOutWidth || !u)
        return inPlace;

    std::vector<std::pair<size_t, std::string>> listed;
    for (size_t number : shown)
    {
        if (number >= 1 && number <= members.size())
            listed.emplace_back(number, differ.display(members[number - 1]));
    }
    bool optional = members.size() != options.size();
    return unionSummary(members.size(), optional, listed);
}

// What follows `type X =` for a table the header prints by its alias alone: what the paths below
// point into, so the reader doesn't have to go find the declaration to follow them. Nullopt for anything
// else.
std::optional<std::string> aliasedTableExpansion(const Differ& differ, TypeId ty, const PropertyFocus* focus)
{
    ty = follow(ty);
    if (!get<TableType>(ty) || !aliasName(ty))
        return std::nullopt;
    ToStringOptions options = blockPrinting(differ.printing(kMaxSpelledOutWidth));
    options.alwaysExpandRootAlias = true;
    options.propertyFocus = focus;
    return toString(ty, options);
}

// The table `ToString` prints for `ty`: the table part of a metatable type, the `T` of a `T?`.
std::optional<TypeId> printedTable(TypeId ty)
{
    ty = follow(withoutNil(ty));
    if (const MetatableType* metatable = get<MetatableType>(ty))
        ty = follow(metatable->table);
    if (get<TableType>(ty))
        return ty;
    return std::nullopt;
}

// For each table on the way to a place the reason points at, on both sides, the properties the way goes
// through: what a large table printed as a block shows first.
PropertyFocus focusOf(const std::vector<Difference>& differences, TypeId given, TypeId expected)
{
    PropertyFocus focus{nullptr};
    auto note = [&](std::optional<TypeId> table, const std::string& name)
    {
        if (!table)
            return;
        std::vector<std::string>& names = focus[*table];
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    };
    auto property = [](std::optional<TypeId> table, const std::string& name) -> std::optional<TypeId>
    {
        if (!table)
            return std::nullopt;
        const TableType* t = get<TableType>(*table);
        auto it = t->props.find(name);
        if (it == t->props.end())
            return std::nullopt;
        return it->second.readTy;
    };
    auto element = [](std::optional<TypeId> table) -> std::optional<TypeId>
    {
        const TableType* t = table ? get<TableType>(*table) : nullptr;
        if (!t || !t->indexer)
            return std::nullopt;
        return t->indexer->indexResultType;
    };
    auto member = [](std::optional<TypeId> ty, size_t number) -> std::optional<TypeId>
    {
        if (!ty)
            return std::nullopt;
        TypeId followed = follow(*ty);
        std::vector<TypeId> members;
        if (const UnionType* u = get<UnionType>(followed))
            members = numberedMembers(u->options);
        else if (const IntersectionType* intersection = get<IntersectionType>(followed))
            members = intersection->parts;
        if (number == 0 || number > members.size())
            return std::nullopt;
        return members[number - 1];
    };

    for (const Difference& d : differences)
    {
        std::optional<TypeId> g = given;
        std::optional<TypeId> e = expected;
        for (const Segment& segment : d.path)
        {
            bool intoFunction = segment.kind == Segment::Kind::Parameter || segment.kind == Segment::Kind::Return;
            if (intoFunction)
                break;
            // `#2` picks a member of the expected union; the given side stays where it is.
            if (segment.kind == Segment::Kind::Member)
            {
                e = member(e, segment.index);
                continue;
            }
            std::optional<TypeId> givenTable = g ? printedTable(*g) : std::nullopt;
            std::optional<TypeId> expectedTable = e ? printedTable(*e) : std::nullopt;
            // A property missing under a similar name: the misspelled one is what the help points at.
            bool lastSegment = &segment == &d.path.back();
            if (lastSegment && d.kind == Difference::Kind::Missing && d.similarName)
                note(givenTable, *d.similarName);
            if (segment.kind == Segment::Kind::Property)
            {
                note(givenTable, segment.name);
                note(expectedTable, segment.name);
                g = property(givenTable, segment.name);
                e = property(expectedTable, segment.name);
            }
            else
            {
                g = element(givenTable);
                e = element(expectedTable);
            }
        }
    }
    return focus;
}

// `explainMismatch`, printing the types in `names` by name, and noting every type it prints in `printed`
// when given one.
std::optional<MismatchExplanation> explainOnce(
    TypeId given,
    TypeId expected,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    const std::optional<std::string>& callee,
    std::string_view arrival,
    const std::optional<std::string>& variable,
    const DenseHashMap<TypeId, std::string>* names,
    std::vector<PrintedType>* printed,
    const Helpifier& helpifier
)
{
    given = follow(given);
    expected = follow(expected);

    // Generics, `any`, type functions and classes are compared in ways a structural walk can't see.
    if (shapeOf(given) == Shape::Other || shapeOf(expected) == Shape::Other)
        return std::nullopt;

    MismatchExplanation explanation;

    // `Recipe?` is labelled by the `Recipe` in it: a table isn't going to be the `nil`.
    TypeId expectedCore = withoutNil(expected);

    // Paths are rooted at a name the reader wrote: the expected type's alias, and on the given side the
    // variable or property it was read from, else its type's alias, else `given`. The variable wins: it's
    // what the reader goes looking for in their code. An aliased union is still printed as
    // `type Drop = ...` above, so `lastDrop#2` counts against the members shown.
    Rendering rendering;
    std::optional<std::string> givenAlias;
    if (std::optional<std::string> name = aliasName(expectedCore); name && isIdentifier(*name))
    {
        rendering.expectedRoot = *name;
        rendering.expectedIsPlaceholder = false;
    }
    if (std::optional<std::string> name = aliasName(given); name && isIdentifier(*name))
        givenAlias = name;

    if (variable || givenAlias)
    {
        rendering.givenRoot = variable ? *variable : *givenAlias;
        rendering.givenIsPlaceholder = false;
    }
    if (rendering.expectedRoot == rendering.givenRoot)
    {
        rendering = Rendering{};
        givenAlias.reset();
    }
    rendering.helpifier = helpifier;

    // A union or intersection has no name of its own when printed, so an aliased one is shown by its
    // name. Its members are spelled out too if a line counts them (`Drop#2`), further down.
    auto isComposite = [](TypeId ty)
    {
        return get<UnionType>(ty) || get<IntersectionType>(ty);
    };
    bool givenNamed = isComposite(given) && givenAlias.has_value();
    bool expectedNamed = isComposite(expected) && !rendering.expectedIsPlaceholder && expectedCore == expected;
    // The type's own name, not the root label: a `type Drop = ...` declaration is about the alias even
    // when paths are rooted at the variable holding it.
    if (givenNamed)
        explanation.givenName = *givenAlias;
    if (expectedNamed)
        explanation.wantedName = rendering.expectedRoot;

    Budget budget;
    Differ differ{subtyping, scope, builtinTypes, NotNull{&budget}};
    differ.names = names;
    differ.printed = printed;
    differ.helpifier = helpifier;

    // Makes the header's two types something the reason's paths can be followed into: the variable the paths are
    // rooted at names the value (`Expected 'settings' to be`), and, when `declareAliases`, an aliased table is spelled
    // out, in place when it's short and in the `where` block under the two types otherwise. Nothing is spelled out
    // when both sides have the same name, which the header has to tell apart some other way.
    auto nameHeaderTypes = [&](bool declareAliases)
    {
        if (explanation.standalone)
            return;
        if (variable && rendering.givenRoot == *variable)
            explanation.givenVariable = variable;

        if (printsAsLocalName(given) && !explanation.givenName)
        {
            ToStringOptions options = differ.printing(kTypeWidth);
            options.ignoreSyntheticName = true;
            explanation.givenName = toString(given, options);
        }

        std::optional<std::string> givenTableAlias = aliasName(given);
        if (!declareAliases || (givenTableAlias && givenTableAlias == aliasName(expected)))
            return;
        // `Recipe?` is declared as its `Recipe`, in the `where` block: in place, the `?` would sit between the name and
        // what it stands for.
        auto spellOut = [&](TypeId side, std::optional<std::string>& printedAs)
        {
            TypeId ty = follow(withoutNil(side));
            std::optional<std::string> expansion = aliasedTableExpansion(differ, ty, &explanation.propertyFocus);
            if (!expansion)
                return;
            bool fitsInPlace = expansion->find('\n') == std::string::npos && expansion->size() <= kMaxInPlaceAliasWidth;
            if (fitsInPlace && ty == follow(side) && !printedAs)
                printedAs = *aliasName(ty) + ": " + *expansion;
            else
                explanation.whereDeclarations.push_back(NamedTypeDeclaration{*aliasName(ty), *expansion});
        };
        spellOut(expected, explanation.wantedName);
        spellOut(given, explanation.givenName);
    };

    // A string that is one typo away from one of the strings expected needs nothing but the correction.
    if (std::optional<std::string> suggestion = misspelledSingleton(given, expected))
    {
        explanation.reason = paragraph(misspelledStringHelp(*suggestion, helpifier));
        explanation.typo = {differ.display(given), *suggestion};
        return explanation;
    }

    // The whole value is an array of what's expected, or one element of the expected array: a path
    // would be empty, so this is one sentence.
    if (std::optional<std::pair<Difference::Kind, TypeId>> nesting = differ.arrayNesting(given, expectedCore))
    {
        std::string subject = variable ? quoted(*variable) : "The given value";
        bool givenIsArray = nesting->first == Difference::Kind::ArrayOfExpected;
        explanation.reason = paragraph(arrayNestingSentence(subject, givenIsArray, variable));
        nameHeaderTypes(true);
        return explanation;
    }

    std::vector<Line> lines;
    std::string header;
    bool suppressedAny = false;
    // The header is one sentence that says everything, so the message is just it.
    bool sentenceSaysAll = false;

    // What "could be 'nil'" is about: the given type as printed above when it's short (`'Drop?'`),
    // else the label that stands for it.
    std::string printedGiven = differ.display(given);
    auto nilSubject = [&]
    {
        bool nameThePrintedType = rendering.givenIsPlaceholder && printedGiven.size() <= kShortTypeWidth;
        return nameThePrintedType ? printedGiven : rendering.givenName();
    };
    std::vector<Difference> shownDifferences;

    const UnionType* givenUnion = get<UnionType>(given);
    std::vector<TypeId> givenMembers = givenUnion ? numberedMembers(givenUnion->options) : std::vector<TypeId>{};

    const UnionType* expectedUnion = get<UnionType>(expected);
    std::vector<TypeId> expectedMembers = expectedUnion ? numberedMembers(expectedUnion->options) : std::vector<TypeId>{};

    // A given `T?` is compared as its `T`, with the `nil` said separately when it matters.
    TypeId givenCore = withoutNil(given);
    bool givenNilFails = givenMembers.size() == 1 && !differ.fits(builtinTypes->nilType, expected);

    // An empty table where a union of tables is expected is described by what to add to it, not by how
    // far it is from each member.
    const TableType* givenTable = get<TableType>(givenCore);
    bool givenIsEmpty = givenTable && givenTable->props.empty() && !givenTable->indexer;
    const UnionType* expectedCoreUnion = get<UnionType>(expectedCore);
    // A given union is never an empty table, so this never takes the place of the union branch below.
    bool emptyForUnion = givenIsEmpty && expectedCoreUnion;
    std::string emptySubject = variable ? quoted(*variable) : "The given table";
    bool describedEmpty =
        emptyForUnion && describeEmptyTableForUnion(differ, numberedMembers(expectedCoreUnion->options), emptySubject, rendering, header, lines);

    if (givenMembers.size() >= 2)
    {
        bool nilFails = false;
        for (TypeId option : givenUnion->options)
        {
            if (isNil(follow(option)) && !nilFails && !differ.fits(option, expected))
                nilFails = true;
        }

        std::vector<MemberDiff> failing;
        // Members the budget ran out before examining or explaining; some may fit.
        size_t unexplained = 0;
        for (size_t i = 0; i < givenMembers.size(); ++i)
        {
            if (differ.outOfBudget())
            {
                ++unexplained;
                continue;
            }
            if (differ.fits(givenMembers[i], expected))
                continue;

            Differ member = differ.trial();
            member.diff(givenMembers[i], expected, DiffPath{}, 0);
            // Cut off partway, it would look closer or more unrelated than it is.
            if (differ.outOfBudget())
            {
                ++unexplained;
                continue;
            }
            std::vector<Difference> located = locatedDifferences(member.differences, suppressedAny);
            bool isUnrelated = differ.unrelated(givenMembers[i], expectedCore) || located.empty();
            failing.push_back(MemberDiff{i + 1, givenMembers[i], std::move(located), member.matched, isUnrelated});
            differ.helps.insert(differ.helps.end(), member.helps.begin(), member.helps.end());
        }

        // Closest first: the member the reader most likely has to fix is the one to read first.
        std::stable_sort(
            failing.begin(),
            failing.end(),
            [](const MemberDiff& a, const MemberDiff& b)
            {
                if (a.unrelated != b.unrelated)
                    return !a.unrelated;
                if (a.unrelated)
                    return false;
                if (a.differences.size() != b.differences.size())
                    return a.differences.size() < b.differences.size();
                return a.matched > b.matched;
            }
        );

        if (failing.empty() && nilFails)
            header = couldBeNilHeader(nilSubject());
        else if (!failing.empty())
        {
            // A value of a union type can be any of its members, so every one that doesn't fit is a
            // problem of its own, and each is listed with what's wrong with it.
            // Unexamined members may fit, so "no member" is only said when every one was examined.
            bool noneFit = unexplained == 0 && failing.size() == givenMembers.size();

            // Nothing fits and even the closest member agrees on nothing: the reader most likely has
            // the wrong value entirely, and a page of paths only makes that look worse than it is.
            const MemberDiff& closest = failing.front();
            bool wrongValueEntirely = noneFit && (closest.unrelated || closest.matched == 0);
            if (wrongValueEntirely)
            {
                explanation.reason = paragraph(unrelatedSentence(givenAlias, printedGiven, differ.display(expectedCore)));
                return explanation;
            }

            // Scalars against a scalar need no labels or paths, just which ones aren't it: `'string' isn't
            // a 'number'.` A `nil` has its own sentence, so that case keeps the list.
            bool scalarsOnly = shapeOf(expectedCore) == Shape::Scalar && !nilFails && unexplained == 0 &&
                               std::all_of(
                                   failing.begin(),
                                   failing.end(),
                                   [](const MemberDiff& member)
                                   {
                                       return shapeOf(member.type) == Shape::Scalar;
                                   }
                               );
            if (scalarsOnly)
            {
                std::vector<std::string> names;
                for (const MemberDiff& member : failing)
                    names.push_back(differ.display(member.type));
                header = scalarsHeader(names, differ.display(expectedCore), get<PrimitiveType>(expectedCore) != nullptr);
            }
            else
            {
                std::string unionName = givenAlias ? quoted(*givenAlias) : "the union";
                header = unionMembersHeader(noneFit, unionName, rendering.expectedTypeName());

                Shape expectedShape = shapeOf(expectedCore);
                std::vector<std::string> otherKinds;
                std::vector<size_t> otherKindNumbers;
                size_t detailed = 0;
                size_t named = 0;
                // Members past the ones detailed and named, and those the budget didn't reach.
                size_t unmentioned = unexplained;

                // With a member that's almost there, the ones with nothing in common are only a
                // distraction from the fix the reader is after.
                bool closestIsNear = !closest.unrelated && isNear(closest.differences.size(), closest.matched);

                for (const MemberDiff& member : failing)
                {
                    if (member.unrelated && closestIsNear)
                        continue;

                    bool bothTables = shapeOf(member.type) == Shape::Table && expectedShape == Shape::Table;
                    bool otherKind = member.unrelated && !bothTables;
                    bool detailedHere = !member.unrelated && detailed < kDetailedMembers;
                    bool namedHere = !otherKind && !detailedHere;
                    if (namedHere && named == kNamedMembers)
                    {
                        ++unmentioned;
                        continue;
                    }

                    std::string label = rendering.givenMember(member.number);
                    std::string type = differ.display(member.type);

                    if (otherKind)
                    {
                        otherKinds.push_back(memberWithType(label, type));
                        otherKindNumbers.push_back(member.number);
                        continue;
                    }

                    Line line;
                    if (member.unrelated)
                        line = memberNothingInCommonLine(label, type, rendering);
                    // Past a few, the rest only get named; their paths would bury the ones above.
                    else if (!detailedHere)
                        line = memberAlsoHasLine(label, type, member.differences.size());
                    else
                        line = memberDetailLine(label, type, isNear(member.differences.size(), member.matched), member.differences.size());
                    line.givenMembers.push_back(member.number);
                    lines.push_back(std::move(line));

                    if (namedHere)
                    {
                        ++named;
                        continue;
                    }

                    for (Line& bullet : bullets(member.differences, rendering, callee, kMaxBullets))
                        lines.push_back(std::move(bullet));
                    shownDifferences.insert(shownDifferences.end(), member.differences.begin(), member.differences.end());
                    ++detailed;
                }

                if (!otherKinds.empty())
                {
                    std::optional<std::string> expectedKinds;
                    if (expectedShape == Shape::Table)
                        expectedKinds = "tables";
                    else if (expectedShape == Shape::Function)
                        expectedKinds = "functions";
                    Line line = otherKindsLine(otherKinds, expectedKinds);
                    line.givenMembers = otherKindNumbers;
                    lines.push_back(std::move(line));
                }

                if (unmentioned > 0)
                    lines.push_back(andMore(unmentioned, false, "members"));
            }

            if (nilFails)
                lines.push_back(canAlsoBeNilLine(rendering));
        }
    }
    else if (describedEmpty)
    {
        if (givenNilFails)
            lines.push_back(couldBeNilBullet(rendering));
    }
    else if (expectedMembers.size() >= 2 && !get<UnionType>(givenCore))
    {
        std::optional<Tag> tag;
        std::optional<size_t> closest = canChooseMember(givenCore, expectedMembers) ? differ.closestMember(givenCore, expectedMembers, &tag) : std::nullopt;
        if (closest && !differ.unrelated(givenCore, expectedMembers[*closest]))
        {
            differ.diff(givenCore, expectedMembers[*closest], DiffPath{Segment{Segment::Kind::Member, "", *closest + 1}}, 0);

            std::vector<Difference> located = locatedDifferences(differ.differences, suppressedAny);
            if (!located.empty())
            {
                std::string label = rendering.expectedMember(*closest + 1);
                // A named union is printed above with this member spelled out (`type Event = … | { ... }`).
                std::optional<std::string> memberType;
                if (!expectedNamed)
                    memberType = differ.display(expectedMembers[*closest]);
                if (tag)
                    header = taggedMatchHeader(tag->field, tag->value, label, memberType, rendering);
                else
                    header = closestMatchHeader(label, memberType);
                lines = bullets(located, rendering, callee, kMaxBullets);
                shownDifferences = std::move(located);
            }
        }

        if (givenNilFails && header.empty())
            header = couldBeNilHeader(nilSubject());
        else if (givenNilFails)
            lines.push_back(couldBeNilBullet(rendering));
    }
    // Related types are diffed whole. Relatedness is judged on the `T` of a given `T?`, so `number?`
    // isn't unrelated to `number`; the `nil` is its own sentence.
    else if (!differ.unrelated(givenCore, expectedCore))
    {
        differ.diff(given, expected, DiffPath{}, 0);

        std::vector<Difference> located = locatedDifferences(differ.differences, suppressedAny);
        bool onlyNil = differ.differences.size() == 1 && differ.differences[0].kind == Difference::Kind::CouldBeNil && located.empty();

        Shape givenShape = shapeOf(given);
        Shape expectedShape = shapeOf(expected);
        bool involvesStructure = isStructured(givenShape) || givenShape == Shape::Intersection || isStructured(expectedShape) ||
                                 expectedShape == Shape::Intersection;
        // Similarity is about tables; two functions (or a function and its overloads) are compared
        // slot by slot, which the bullets already say.
        bool comparedSlotBySlot = givenShape == Shape::Function || expectedShape == Shape::Function || expectedShape == Shape::Intersection;

        // Everything wrong is under the given function's own parameters and return values, which the header prints
        // with their names: the reason lists them without printing the function again.
        auto underGivenFunction = [](const Difference& d)
        {
            if (d.path.empty())
                return false;
            Segment::Kind first = d.path.front().kind;
            return first == Segment::Kind::Parameter || first == Segment::Kind::Return;
        };
        bool inGivenFunction =
            givenShape == Shape::Function && !located.empty() && std::all_of(located.begin(), located.end(), underGivenFunction);

        if (onlyNil)
            header = couldBeNilHeader(nilSubject());
        else if (located.empty() && involvesStructure && !suppressedAny)
            return std::nullopt;
        else if (inGivenFunction)
        {
            // The function's own slots are items; a function inside one (a callback it takes) gets bullets of its own.
            std::vector<const Difference*> group;
            std::vector<Difference> nestedArguments;
            std::vector<Difference> nestedReturns;
            for (const Difference& d : located)
            {
                if (d.anchor && d.anchor->at == 0)
                    group.push_back(&d);
                else if (d.path.front().kind == Segment::Kind::Parameter)
                    nestedArguments.push_back(d);
                else
                    nestedReturns.push_back(d);
            }
            FunctionReason reason = givenFunctionReason(
                group,
                bullets(nestedArguments, rendering, std::nullopt, kMaxFunctionItems),
                bullets(nestedReturns, rendering, std::nullopt, kMaxFunctionItems),
                rendering
            );
            header = std::move(reason.header);
            lines = std::move(reason.lines);
            sentenceSaysAll = reason.standalone;
            shownDifferences = std::move(located);
        }
        else if (!located.empty())
        {
            // Two tables only disagree about properties; how many and how near is what the bullets show.
            bool bothTables = shapeOf(givenCore) == Shape::Table && shapeOf(expectedCore) == Shape::Table;
            if (comparedSlotBySlot)
                header = becauseHeader();
            else if (bothTables)
                header = propertiesHeader();
            else if (isNear(located.size(), differ.matched))
                header = similarHeader(arrival, located.size());
            else
                header = pathsHeader(arrival, located.size());
            lines = bullets(located, rendering, callee, kMaxBullets);
            shownDifferences = std::move(located);
        }
    }

    // Two tables or functions with nothing in common get one calm line rather than silence: the reader
    // most likely has the wrong value, not a nearly right one. Scalars say this already by being printed.
    if (header.empty() && !get<UnionType>(given) && differ.unrelated(givenCore, expectedCore))
    {
        bool involvesStructure = isStructured(shapeOf(givenCore)) || isStructured(shapeOf(expectedCore));
        if (involvesStructure)
        {
            // An array where a map is expected, or the reverse, is said as just that: the two printed types
            // differ only in their brackets, which is easy to miss.
            std::optional<TypeId> expectedKey = differ.mapKey(expectedCore);
            std::optional<TypeId> givenKey = differ.mapKey(givenCore);
            bool arrayForMap = differ.arrayElement(givenCore) && expectedKey;
            bool mapForArray = givenKey && differ.arrayElement(expectedCore);
            if (arrayForMap || mapForArray)
            {
                TypeId mapType = arrayForMap ? expectedCore : givenCore;
                TypeId arrayType = arrayForMap ? givenCore : expectedCore;
                std::string element = differ.display(*differ.arrayElement(arrayType));
                std::string map = differ.mapDisplay(mapType);
                explanation.reason = paragraph(arrayAndMapSentence(variable, arrayForMap, map, element));
            }
            else
                explanation.reason = paragraph(unrelatedSentence(givenAlias, printedGiven, differ.display(expectedCore)));
            // The array-and-map sentence spells both types out itself.
            nameHeaderTypes(!(arrayForMap || mapForArray));
            return explanation;
        }
    }

    // Everything found involved an unresolved type, whose error has already been reported: the
    // subtyping test's own reasonings decide whether there is anything to add.
    if (header.empty() && suppressedAny)
        return std::nullopt;

    // Every problem is a typo: the correction says it all, so the message is the two types and the help, and nothing
    // the reason would have counted or spelled out is shown.
    bool typosOnly = !shownDifferences.empty() && std::all_of(
                                                      shownDifferences.begin(),
                                                      shownDifferences.end(),
                                                      [](const Difference& d)
                                                      {
                                                          return d.similarName && !d.anchor;
                                                      }
                                                  );
    if (typosOnly)
    {
        // One misspelled property name is only a typo. A misspelled string in a table is said at the table, so the
        // help names the place it's in.
        const Difference& only = shownDifferences.front();
        if (shownDifferences.size() == 1 && only.kind == Difference::Kind::Missing)
            explanation.typo = misspelledFieldNames(only, rendering);
        else if (shownDifferences.size() == 1 && only.kind == Difference::Kind::Mismatch)
            misspelledValueTypo(only, explanation);
    }

    // Every problem is two parameters or return values in the wrong order, which the help says.
    bool swapsOnly = !shownDifferences.empty() && std::all_of(
                                                      shownDifferences.begin(),
                                                      shownDifferences.end(),
                                                      [](const Difference& d)
                                                      {
                                                          return d.explainedBySwap;
                                                      }
                                                  );
    bool helpsSayAll = typosOnly || swapsOnly;
    if (helpsSayAll)
    {
        header.clear();
        lines.clear();
        rendering.notation = 0;
        rendering.givenMembersShown.clear();
        rendering.expectedMembersShown.clear();
    }

    if (!header.empty() || helpsSayAll)
    {
        // A missing field with a similarly named one there, or a string one typo from an expected one, is
        // almost certainly a misspelling: the correction goes first, as a help, not buried in a bullet.
        for (const Difference& d : shownDifferences)
        {
            if (!d.similarName || d.anchor)
                continue;
            if (d.kind == Difference::Kind::Missing)
                differ.helps.push_back(misspelledFieldHelp(d, rendering));
            else if (d.kind == Difference::Kind::Mismatch)
                differ.helps.push_back(misspelledValueHelp(d, rendering));
        }

        std::vector<const Difference*> narrowed;
        for (const Difference& d : shownDifferences)
        {
            if (isNarrowedParameter(d))
                narrowed.push_back(&d);
        }

        explanation.reason = composeReason(differ.helps, header, lines, narrowedParameterHelp(narrowed, rendering), rendering);
    }

    // An aliased union's members are spelled out under its name only if something above counts them.
    if (givenNamed && !rendering.givenMembersShown.empty())
        explanation.givenExpansion = declaredExpansion(differ, given, rendering.givenMembersShown);
    if (expectedNamed && !rendering.expectedMembersShown.empty())
        explanation.wantedExpansion = declaredExpansion(differ, expected, rendering.expectedMembersShown);

    // Missing fields named by tag or shared by every member need nothing more, nor does a function whose only problem
    // is extra parameters. A `Shape#2` needs the union printed above to mean anything, so that keeps the types.
    if ((describedEmpty && rendering.expectedMembersShown.empty() && !givenNilFails) || sentenceSaysAll)
    {
        LUAU_ASSERT(explanation.reason.rfind("\n\n", 0) == 0);
        explanation.reason.erase(0, 2);
        explanation.standalone = true;
    }

    // A bullet that says both what was expected and what was given needs neither type spelled out to be
    // read: spelling them out would say it all again. One that says only one side (`is an array where a
    // single 'T' is expected`), or a reason with no bullets, needs them.
    bool everyBulletSaysBoth = !shownDifferences.empty() && std::all_of(shownDifferences.begin(), shownDifferences.end(), saysBothTypes);
    explanation.propertyFocus = focusOf(shownDifferences, given, expected);
    for (const Difference& d : shownDifferences)
    {
        if (d.path.empty() || d.path.back().kind != Segment::Kind::Property)
            continue;
        // A typo's help already says where the property is
        if (d.kind == Difference::Kind::Missing && !d.similarName)
        {
            const std::string& name = d.path.back().name;
            if (d.expectedSite)
                explanation.notes.push_back(MismatchNote{d.expectedSite->location, d.expectedSite->module, expectedPropertyNote(name, d.super)});
            if (d.givenSite)
                explanation.notes.push_back(MismatchNote{d.givenSite->location, d.givenSite->module, missingPropertyNote(d, rendering)});
            continue;
        }

        bool doesNotFit = d.kind == Difference::Kind::Mismatch || d.kind == Difference::Kind::MismatchExactly;
        if (!doesNotFit)
            continue;
        // One declaration on both sides (a generic's `value: T`, instantiated twice) says nothing about the mismatch
        bool sameDeclaration = d.expectedSite && d.givenSite && d.expectedSite->location == d.givenSite->location &&
                               d.expectedSite->module == d.givenSite->module;
        if (sameDeclaration)
            continue;
        const std::string& name = d.path.back().name;
        if (d.expectedSite)
            explanation.notes.push_back(MismatchNote{d.expectedSite->location, d.expectedSite->module, expectedPropertyNote(name, d.super)});
        if (d.givenSite)
            explanation.notes.push_back(MismatchNote{d.givenSite->location, d.givenSite->module, givenPropertyNote(name, d.sub, d.super)});
    }
    for (const Difference& d : shownDifferences)
    {
        // What an alias allows only matters where a value doesn't fit it. A `nil` that could arrive, or an
        // array where one value goes, is wrong whatever the alias holds.
        bool doesNotFit = d.kind == Difference::Kind::Mismatch || d.kind == Difference::Kind::MismatchExactly;
        if (!doesNotFit)
            continue;
        explanation.bulletTypes.push_back(d.sub);
        explanation.bulletTypes.push_back(d.super);
    }
    explanation.bulletsSayBothTypes = everyBulletSaysBoth;
    if (typosOnly)
        explanation.bulletTypes.clear();
    if (!explanation.reason.empty())
        nameHeaderTypes(!everyBulletSaysBoth && !typosOnly);

    explanation.notation = rendering.notation;
    return explanation;
}

// Whether the given type prints as only the context for the bullets: a table they say everything about, with no
// alias to print it by. It's printed on one line, cut down to what they point into (`givenContextPrinting`).
bool givenIsContext(const MismatchExplanation& explanation, TypeId given)
{
    return explanation.bulletsSayBothTypes && printedTable(given) && !aliasName(withoutNil(given));
}

// How a given table that's only context prints: `config: { tls: { port: string, … }, … }`. Without the names of
// repeated types: the places themselves, not names declared in full below them.
ToStringOptions givenContextPrinting(const MismatchExplanation& explanation, ToStringOptions header)
{
    header.repeatedTypeNames = nullptr;
    header.propertyFocus = &explanation.propertyFocus;
    header.onlyFocusedProperties = true;
    header.identifyByConventionalNames = !explanation.givenVariable;
    return header;
}

// Everything the message prints, as `ErrorConverter` will print it, for finding what it repeats.
std::string printedMessage(const MismatchExplanation& explanation, TypeId given, TypeId expected, ToStringOptions header)
{
    std::string text;
    auto add = [&](const std::optional<std::string>& part)
    {
        if (part)
            text += *part + "\n";
    };
    add(explanation.wantedName ? explanation.wantedName : toString(expected, header));
    // The given table as `printHeader` will print it: cut down to what the bullets point into when they say
    // both types, and a table literal held in a local in full, not as the explanation's own `givenName` for it,
    // which is cut off where the reason would cut it and can hide a repeat.
    if (givenIsContext(explanation, given))
    {
        ToStringOptions trimmed = givenContextPrinting(explanation, header);
        trimmed.ignoreSyntheticName = printsAsLocalName(given);
        add(toString(given, trimmed));
    }
    else if (printsAsLocalName(given))
    {
        ToStringOptions literal = header;
        literal.ignoreSyntheticName = true;
        add(toString(given, literal));
    }
    else
        add(explanation.givenName ? explanation.givenName : toString(given, header));
    // An aliased union's members are listed under it by number (`#1 = ...`) so the `Drop#1` lines in the
    // reason can be matched to them. Each is printed twice by design, which isn't a repeat to name away.
    if (!get<UnionType>(expected))
        add(explanation.wantedExpansion);
    if (!get<UnionType>(given))
        add(explanation.givenExpansion);
    for (const NamedTypeDeclaration& declaration : explanation.whereDeclarations)
        add(declaration.expansion);
    text += explanation.reason;
    return text;
}

} // namespace

bool looksLikeTypo(const std::string& written, const std::string& meant)
{
    return written.size() > kMaxTypoEdits && editDistanceWithin(written, meant, typoEditLimit(written)).has_value();
}

static std::string withoutDigits(const std::string& name)
{
    std::string letters;
    for (char c : name)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            letters += c;
    }
    return letters;
}

std::optional<std::string> closestSpelling(const std::string& written, const std::vector<std::string>& candidates)
{
    if (written.size() <= kMaxTypoEdits)
        return std::nullopt;

    std::optional<std::string> best;
    size_t bestDistance = typoEditLimit(written) + 1;
    const std::string writtenLetters = withoutDigits(written);
    for (const std::string& candidate : candidates)
    {
        // Names that differ only in digits are siblings (`Part8`, `Part0`), not misspellings of each other
        if (candidate == written || withoutDigits(candidate) == writtenLetters)
            continue;
        std::optional<size_t> distance = editDistanceWithin(written, candidate, bestDistance - 1);
        if (distance)
        {
            best = candidate;
            bestDistance = *distance;
        }
    }
    return best;
}

// The name of a literal's property, when it's written as one (`name = v` or `["name"] = v`).
static std::optional<std::string> literalPropertyName(const AstExprTable::Item& item)
{
    if (item.kind == AstExprTable::Item::Kind::List || !item.key)
        return std::nullopt;
    if (AstExprConstantString* key = item.key->as<AstExprConstantString>())
        return std::string(key->value.data, key->value.size);
    return std::nullopt;
}

std::vector<std::pair<Location, Typo>> literalPropertyTypos(const AstExprTable* expr, TypeId expectedType)
{
    std::vector<std::pair<Location, Typo>> typos;

    // `Config?` is checked as its `Config`.
    TypeId expected = follow(expectedType);
    if (const UnionType* optional = get<UnionType>(expected))
    {
        std::vector<TypeId> nonNil;
        for (TypeId option : optional->options)
        {
            if (!isNil(follow(option)))
                nonNil.push_back(option);
        }
        if (nonNil.size() != 1)
            return typos;
        expected = follow(nonNil.front());
    }

    const TableType* table = get<TableType>(expected);
    if (!table || table->indexer)
        return typos;

    // This runs for every table literal with an expected table type. Most write only properties the type has.
    bool writesUnknownProperty = false;
    for (const AstExprTable::Item& item : expr->items)
    {
        std::optional<std::string> name = literalPropertyName(item);
        if (name && !table->props.count(*name))
        {
            writesUnknownProperty = true;
            break;
        }
    }

    if (!writesUnknownProperty)
        return typos;

    std::unordered_set<std::string> written;
    for (const AstExprTable::Item& item : expr->items)
    {
        if (std::optional<std::string> name = literalPropertyName(item))
            written.insert(*name);
    }

    for (const AstExprTable::Item& item : expr->items)
    {
        std::optional<std::string> name = literalPropertyName(item);
        if (!name || table->props.count(*name))
            continue;

        for (const auto& [meant, property] : table->props)
        {
            if (!written.count(meant) && looksLikeTypo(*name, meant))
            {
                typos.emplace_back(item.key->location, Typo{*name, meant});
                break;
            }
        }
    }
    return typos;
}

// The types `ty` can be: each member of a union, else `ty` itself.
static std::vector<TypeId> alternativesOf(TypeId ty)
{
    ty = follow(ty);
    const UnionType* u = get<UnionType>(ty);
    if (!u)
        return {ty};
    return std::vector<TypeId>(begin(u), end(u));
}

// The types the alternatives of `expected` give the property `name`.
static std::vector<TypeId> expectedProperty(const std::vector<TypeId>& expected, const std::string& name)
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

static TypeId withWrittenStrings(TypeId given, const AstExpr* literal, const std::vector<TypeId>& expected, TypeArena& arena)
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
        if (const AstExprConstantString* string = item.value->as<AstExprConstantString>())
        {
            bool wantsSingleton = std::any_of(
                wanted.begin(),
                wanted.end(),
                [](TypeId ty)
                {
                    const SingletonType* singleton = get<SingletonType>(follow(ty));
                    return singleton && get<StringSingleton>(singleton);
                }
            );
            if (wantsSingleton && isString(checked))
                written = arena.addType(SingletonType{StringSingleton{std::string(string->value.data, string->value.size)}});
        }
        else
            written = withWrittenStrings(checked, item.value, wanted, arena);
        if (written == checked)
            continue;

        if (!copy)
            copy = *table;
        copy->props[*name] = Property::rw(written);
    }
    return copy ? arena.addType(std::move(*copy)) : given;
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

std::optional<MismatchExplanation> explainMismatch(
    TypeId given,
    TypeId expected,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    std::optional<std::string> callee,
    std::string_view arrival,
    std::optional<std::string> variable,
    const Helpifier& helpifier
)
{
    given = follow(given);
    expected = follow(expected);

    std::vector<PrintedType> printed{{expected, std::nullopt}, {given, variable}};
    std::optional<MismatchExplanation> plain =
        explainOnce(given, expected, subtyping, scope, builtinTypes, callee, arrival, variable, nullptr, &printed, helpifier);
    // A standalone reason prints neither type, so it has no `where` block to put names in.
    if (!plain || plain->standalone)
        return plain;

    // How `ErrorConverter` prints the types of an explained mismatch.
    ToStringOptions header;
    header.sortUnionMembers = false;
    header.functionTypeArguments = true;
    header.classAndTraitValuesInTypeForm = true;

    // The two types as the message shows them: a large table a field per line, and a repeated type by its
    // name.
    auto printHeader = [&](MismatchExplanation& explanation, const RepeatedTypeNames* names)
    {
        ToStringOptions block = blockPrinting(header);
        block.propertyFocus = &explanation.propertyFocus;
        if (names)
            block.repeatedTypeNames = &names->names;
        // Set only where it differs from how `ErrorConverter` prints the type anyway: errors are compared by
        // these fields, and an error that says the same thing should compare equal.
        if (std::string wanted = toString(expected, block); !explanation.wantedName && wanted != toString(expected, header))
            explanation.wantedName = std::move(wanted);
        // A given table the bullets already say everything about is printed on one line, cut down to what they
        // point into, as a way to find those places in it.
        ToStringOptions givenOptions = givenIsContext(explanation, given) ? givenContextPrinting(explanation, header) : block;
        if (printsAsLocalName(given))
        {
            givenOptions.ignoreSyntheticName = true;
            explanation.givenName = toString(given, givenOptions);
        }
        else if (std::string shown = toString(given, givenOptions); !explanation.givenName && shown != toString(given, header))
            explanation.givenName = std::move(shown);
    };

    // Declares the short aliases a bullet says were expected or given (`expected 'Method', got '"PATCH"'`),
    // which say nothing about what's wrong until the reader knows what they stand for. Not an alias that only
    // appears inside a larger type (`Stack` in `(string) -> Stack?`): what's wrong there is elsewhere. Only
    // tables, unions and intersections, whose names don't say what they stand for.
    auto declareMentionedAliases = [&](MismatchExplanation& explanation)
    {
        std::unordered_set<std::string> declared;
        for (const NamedTypeDeclaration& declaration : explanation.whereDeclarations)
            declared.insert(declaration.name);

        // The two types' own aliases (`Recipe` of a `Recipe?`) are spelled out, or not, by `nameHeaderTypes`.
        std::optional<std::string> givenAlias = aliasName(withoutNil(given));
        std::optional<std::string> expectedAlias = aliasName(withoutNil(expected));
        if (givenAlias)
            declared.insert(*givenAlias);
        if (expectedAlias)
            declared.insert(*expectedAlias);

        ToStringOptions expansionOptions = blockPrinting(header);
        expansionOptions.propertyFocus = &explanation.propertyFocus;
        expansionOptions.alwaysExpandRootAlias = true;
        ToStringOptions oneLine = header;
        oneLine.alwaysExpandRootAlias = true;
        size_t added = 0;
        for (const auto& alias : aliasesReachableFrom({expected, given}))
        {
            const std::string& name = alias.first;
            TypeId ty = alias.second;
            if (added == kMaxMentionedAliases)
                break;
            TypeId aliased = follow(ty);
            const TableType* table = get<TableType>(aliased);
            bool declarable = (table && table->instantiatedTypeParams.empty() && table->instantiatedTypePackParams.empty()) ||
                              get<UnionType>(aliased) || get<IntersectionType>(aliased);
            bool isABulletType = std::any_of(
                explanation.bulletTypes.begin(),
                explanation.bulletTypes.end(),
                [&](const std::string& shown)
                {
                    return shown == name || shown == name + "?";
                }
            );
            if (!declarable || declared.count(name) || !isABulletType)
                continue;
            if (toString(aliased, oneLine).size() > kMaxMentionedAliasWidth)
                continue;
            explanation.whereDeclarations.push_back(NamedTypeDeclaration{name, toString(aliased, expansionOptions)});
            declared.insert(name);
            ++added;
        }
    };

    // Two types that print alike are told apart by where they're from; one name for both would hide that.
    if (toString(given, header) == toString(expected, header))
    {
        printHeader(*plain, nullptr);
        declareMentionedAliases(*plain);
        return plain;
    }

    std::string message = printedMessage(*plain, given, expected, header);
    ToStringOptions focused = header;
    focused.propertyFocus = &plain->propertyFocus;
    RepeatedTypeNames names = nameRepeatedTypes(printed, message, focused, scope.get());
    std::optional<MismatchExplanation> named;
    if (!names.empty())
        named = explainOnce(given, expected, subtyping, scope, builtinTypes, callee, arrival, variable, &names.names, nullptr, helpifier);
    if (!named)
    {
        printHeader(*plain, nullptr);
        declareMentionedAliases(*plain);
        return plain;
    }

    printHeader(*named, &names);
    header.repeatedTypeNames = &names.names;

    // A name whose type the message prints nowhere would declare something it never refers to: a type cut off
    // in one place and not another can be counted twice and printed once, and a given table cut down to what
    // the bullets point into leaves out the rest.
    std::string namedMessage = named->wantedName ? *named->wantedName : toString(expected, header);
    namedMessage += "\n" + (named->givenName ? *named->givenName : toString(given, header)) + "\n" + named->reason + "\n";
    namedMessage += named->wantedExpansion.value_or("") + "\n" + named->givenExpansion.value_or("") + "\n";
    for (const NamedTypeDeclaration& declaration : named->whereDeclarations)
        namedMessage += declaration.expansion + "\n";
    for (const NamedTypeDeclaration& declaration : names.declarations)
        namedMessage += declaration.expansion + "\n";
    for (NamedTypeDeclaration& declaration : names.declarations)
    {
        if (mentionsName(namedMessage, declaration.name))
            named->whereDeclarations.push_back(std::move(declaration));
    }
    declareMentionedAliases(*named);
    return named;
}

} // namespace Luau
