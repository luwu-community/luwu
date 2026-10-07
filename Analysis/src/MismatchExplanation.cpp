// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/MismatchExplanation.h"

#include "Luau/Error.h"
#include "Luau/MismatchWording.h"
#include "Luau/Scope.h"
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
// An aliased union is spelled out whole after `type X =` when it has at most this many members and
// fits in this many characters; a longer one lists only the members the message refers to.
constexpr size_t kMaxSpelledOutMembers = 10;
constexpr size_t kMaxSpelledOutWidth = 240;
// Members of an expected union a misspelled string is compared against.
constexpr size_t kMaxSpellingCandidates = 256;
// Strings longer than this aren't checked for typos: the edit distance is quadratic in their length.
constexpr size_t kMaxSpellingLength = 128;
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
    return options;
}

size_t editDistance(const std::string& a, const std::string& b)
{
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        row[j] = j;

    for (size_t i = 1; i <= a.size(); ++i)
    {
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
        {
            size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// The edit distance between `a` and `b` when it's at most `limit`. Strings whose lengths alone differ by
// more can't be that close, and long strings aren't compared at all.
std::optional<size_t> editDistanceWithin(const std::string& a, const std::string& b, size_t limit)
{
    size_t lengthGap = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
    if (lengthGap > limit || a.size() > kMaxSpellingLength || b.size() > kMaxSpellingLength)
        return std::nullopt;
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

// The expected string a given string singleton was most likely a misspelling of: within two edits,
// or an abbreviation of it (`"LeftCtrl"` for `"LeftControl"`).
std::optional<std::string> misspelledSingleton(TypeId sub, TypeId super)
{
    const SingletonType* given = get<SingletonType>(follow(sub));
    const StringSingleton* givenString = given ? get<StringSingleton>(given) : nullptr;
    if (!givenString)
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

    constexpr size_t kMaxTypoEdits = 2;
    std::optional<std::string> best;
    size_t bestDistance = kMaxTypoEdits + 1;
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
        return d;
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
        std::vector<TypeId> walking;
        std::optional<std::string> shown = displayWalk(ty, walking);
        return shown ? *shown : toString(ty, printOptions(kTypeWidth));
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

        bool cycles = std::find(walking.begin(), walking.end(), ty) != walking.end();
        if (cycles || walking.size() >= size_t(kMaxWalkDepth))
            return std::nullopt;

        walking.push_back(ty);
        std::optional<std::string> shown;
        if (const TableType* table = get<TableType>(ty); table && table->props.empty() && table->indexer)
        {
            TypeId key = follow(table->indexer->indexType);
            std::optional<std::string> value = displayWalk(table->indexer->indexResultType, walking);
            if (key == follow(builtinTypes->numberType))
                shown = value ? std::optional<std::string>{"{" + *value + "}"} : std::nullopt;
            else if (std::optional<std::string> keyShown = value ? displayWalk(key, walking) : std::nullopt)
                shown = "{ [" + *keyShown + "]: " + *value + " }";
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
            shown = toString(ty, printOptions(kTypeWidth));
        walking.pop_back();
        return shown;
    }

    std::string display(TypePackId tp)
    {
        std::string s = toString(tp, printOptions(kTypeWidth));
        return s.empty() ? "()" : s;
    }

    // A function's type with its parameter names, when it still has them: the names are what a bullet
    // about one of its parameters refers to.
    std::string displayFunction(TypeId ty)
    {
        ToStringOptions options = printOptions(kFunctionTypeWidth);
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
        push(Difference{Difference::Kind::Mismatch, path, got, display(super)}, sub, super);
    }

    // The member of an expected union whose singleton tag the given table carries, when exactly one
    // member has it.
    std::optional<size_t> taggedMember(TypeId sub, const std::vector<TypeId>& members, std::optional<Tag>* pickedBy)
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
                if (memberSingleton && *memberSingleton == *tag)
                {
                    onlyMatch = i;
                    ++matches;
                }
            }

            if (matches == 1)
            {
                if (pickedBy)
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
        if (std::optional<size_t> tagged = taggedMember(sub, members, pickedBy))
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
        constexpr size_t kMaxNameEdits = 2;
        for (const std::string& candidate : candidates)
        {
            bool unusedByExpected = expected->props.count(candidate) == 0;
            if (unusedByExpected && candidate.size() > kMaxNameEdits && editDistanceWithin(candidate, name, kMaxNameEdits))
                return candidate;
        }
        return std::nullopt;
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
            Difference notAMap{Difference::Kind::NotAMap, path, display(sub), display(super)};
            notAMap.expectsArray = follow(superTable->indexer->indexType) == follow(builtinTypes->numberType);
            notAMap.givenEmpty = givenTable->props.empty();
            push(std::move(notAMap), sub, super);
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
                push(std::move(missing), wanted);
                continue;
            }

            diff(*have, wanted, propertyPath, depth + 1);
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
                    push(Difference{Difference::Kind::NothingPassed, parameterPath, "", display(*accepted)}, *accepted);
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
                helps.push_back(swappedParametersHelp(parameterName(given, i), parameterName(given, j), i, j));
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
                helps.push_back(swappedReturnsHelp(i, j));
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
bool isNarrowedParameter(const Difference& d)
{
    bool inFunction = d.anchor && d.anchor->at < d.path.size();
    return d.kind == Difference::Kind::CouldBeNil && inFunction && d.path[d.anchor->at].kind == Segment::Kind::Parameter;
}

// A difference only names a place when it has a path; one at the root restates the two printed types.
// One involving an unresolved type is dropped, and `suppressedAny` notes that it happened.
std::vector<Difference> locatedDifferences(const std::vector<Difference>& differences, bool& suppressedAny)
{
    std::vector<Difference> located;
    for (const Difference& d : differences)
    {
        if (d.suppressing)
            suppressedAny = true;
        else if (!d.path.empty())
            located.push_back(d);
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

// Close enough that the reader most likely meant it: something agrees, and either only a few things
// differ or most of it agrees. With nothing agreeing, a single difference is the whole type: `{unknown}`
// against `{number}` differs in its only part, the element type, which is not "similar".
bool isNear(size_t differences, int matched)
{
    constexpr size_t kFewDifferences = 3;
    constexpr int kAgreeingPercent = 75;
    if (matched == 0)
        return false;
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
            lines.push_back(andMore(commonLines.size() - shownCommon, true, "fields"));
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

// What follows `type X =` for an aliased union or intersection whose members the message counts: every
// member when that's short, else only the `shown` ones by number.
std::string declaredExpansion(Differ& differ, TypeId ty, const std::set<size_t>& shown)
{
    const UnionType* u = get<UnionType>(ty);
    const std::vector<TypeId>& options = u ? u->options : get<IntersectionType>(ty)->parts;
    const char* separator = u ? " | " : " & ";

    // Only a union's members are counted, so an intersection is always spelled out.
    if (!u)
        return differ.expansion(options, separator, kMaxSpelledOutWidth);

    std::vector<TypeId> members = numberedMembers(options);
    if (members.size() <= kMaxSpelledOutMembers)
    {
        std::string whole = differ.expansion(options, separator, kMaxSpelledOutWidth);
        if (whole.size() <= kMaxSpelledOutWidth)
            return whole;
    }

    std::vector<std::pair<size_t, std::string>> listed;
    for (size_t number : shown)
    {
        if (number >= 1 && number <= members.size())
            listed.emplace_back(number, differ.display(members[number - 1]));
    }
    bool optional = members.size() != options.size();
    return unionSummary(members.size(), optional, listed);
}

} // namespace

std::optional<MismatchExplanation> explainMismatch(
    TypeId given,
    TypeId expected,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    std::optional<std::string> callee,
    std::string_view arrival,
    std::optional<std::string> variable
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

    // A string that is one typo away from one of the strings expected needs nothing but the correction.
    if (std::optional<std::string> suggestion = misspelledSingleton(given, expected))
    {
        explanation.reason = paragraph(misspelledStringHelp(*suggestion));
        return explanation;
    }

    std::vector<Line> lines;
    std::string header;
    // A `Help (...)` block saying how to declare a table whose type says nothing about what it holds.
    std::string declarationHelp;
    bool suppressedAny = false;

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
                std::string memberType = differ.display(expectedMembers[*closest]);
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

        // The whole value is a table with no indexer: there is no path to point at, so it's one sentence.
        const Difference* rootNotAMap = nullptr;
        for (const Difference& d : differ.differences)
        {
            if (d.kind == Difference::Kind::NotAMap && d.path.empty() && !d.suppressing)
                rootNotAMap = &d;
        }

        if (onlyNil)
            header = couldBeNilHeader(nilSubject());
        else if (located.empty() && rootNotAMap)
        {
            header = noIndexerSentence(emptySubject, *rootNotAMap);
            if (rootNotAMap->givenEmpty && variable)
                declarationHelp = emptyTableHelp(*variable, rootNotAMap->super);
        }
        else if (located.empty() && involvesStructure && !suppressedAny)
            return std::nullopt;
        else if (!located.empty())
        {
            if (comparedSlotBySlot)
                header = becauseHeader();
            else if (isNear(located.size(), differ.matched))
                header = similarHeader(arrival, located.size());
            else
                header = pathsHeader(arrival, located.size());
            lines = bullets(located, rendering, callee, kMaxBullets);

            // `table.create(n)` with no value gives `{unknown}`: nothing says what it holds, so the fix is to say it.
            const TableType* givenCoreTable = get<TableType>(givenCore);
            bool holdsUnknown = givenCoreTable && givenCoreTable->indexer && get<UnknownType>(follow(givenCoreTable->indexer->indexResultType));
            if (holdsUnknown && variable)
                declarationHelp = unknownElementsHelp(*variable, differ.display(expectedCore));

            // Every difference is in the root function's parameters, or every one is in its returns:
            // say which, and print only that part of the two function types.
            const FunctionType* givenFunction = get<FunctionType>(given);
            const FunctionType* expectedFunction = get<FunctionType>(expected);
            if (givenFunction && expectedFunction)
            {
                auto allAt = [&](Segment::Kind kind)
                {
                    return std::all_of(
                        located.begin(),
                        located.end(),
                        [kind](const Difference& d)
                        {
                            return d.path[0].kind == kind;
                        }
                    );
                };

                if (allAt(Segment::Kind::Parameter))
                {
                    explanation.contextVerb = mismatchContextFunctionTakes;
                    explanation.contextWantedDisplay = differ.display(expectedFunction->argTypes);
                    explanation.contextGivenDisplay = differ.display(givenFunction->argTypes);
                }
                else if (allAt(Segment::Kind::Return))
                {
                    explanation.contextVerb = mismatchContextFunctionReturns;
                    explanation.contextWantedDisplay = differ.display(expectedFunction->retTypes);
                    explanation.contextGivenDisplay = differ.display(givenFunction->retTypes);
                }
            }
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
            explanation.reason = paragraph(unrelatedSentence(givenAlias, printedGiven, differ.display(expectedCore)));
            return explanation;
        }
    }

    // Everything found involved an unresolved type, whose error has already been reported: the
    // subtyping test's own reasonings decide whether there is anything to add.
    if (header.empty() && suppressedAny)
        return std::nullopt;

    if (!header.empty())
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

        std::string why = narrowedParameterHelp(narrowed, rendering);
        if (!declarationHelp.empty())
            why += (why.empty() ? "" : "\n\n") + declarationHelp;

        explanation.reason = composeReason(differ.helps, header, lines, why, rendering);
    }

    // An aliased union's members are spelled out under its name only if something above counts them.
    if (givenNamed && !rendering.givenMembersShown.empty())
        explanation.givenExpansion = declaredExpansion(differ, given, rendering.givenMembersShown);
    if (expectedNamed && !rendering.expectedMembersShown.empty())
        explanation.wantedExpansion = declaredExpansion(differ, expected, rendering.expectedMembersShown);

    // Missing fields named by tag or shared by every member need nothing more. A `Shape#2` needs the
    // union printed above to mean anything, so that keeps the types.
    if (describedEmpty && rendering.expectedMembersShown.empty() && !givenNilFails)
    {
        LUAU_ASSERT(explanation.reason.rfind("\n\n", 0) == 0);
        explanation.reason.erase(0, 2);
        explanation.standalone = true;
    }

    explanation.notation = rendering.notation;
    return explanation;
}

} // namespace Luau
