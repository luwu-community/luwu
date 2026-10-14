// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/RepeatedTypeNames.h"

#include "Luau/Scope.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"
#include "Luau/TypeNaming.h"
#include "Luau/TypePack.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace Luau
{

namespace
{

// A type printed shorter than this reads fine where it is, however often it appears.
constexpr size_t kMinNamedWidth = 40;
// Names one message may introduce: past a few, the `where` block is itself something to read through.
constexpr size_t kMaxNames = 5;
// Types visited while looking for repeats, across everything one message prints. Types can be cyclic,
// so the walk also stops at this depth.
constexpr size_t kMaxWalkNodes = 512;
constexpr int kMaxWalkDepth = 16;
// Aliases in scope printed while looking for one with the same structure as a repeated type.
constexpr size_t kMaxScopeAliases = 256;
// Properties a table printed as a block shows, past the ones the message is about.
constexpr size_t kMaxPropertiesShown = 6;
// How much of a type is printed to compare it with others. Two types that agree this far share a name.
constexpr size_t kMaxKeyWidth = 1000;

// The name the reader wrote for a type, which it already prints by.
std::optional<std::string> aliasNameOf(TypeId ty)
{
    if (const TableType* table = get<TableType>(ty))
    {
        if (table->name)
            return table->name;
        return std::nullopt;
    }
    if (const MetatableType* metatable = get<MetatableType>(ty))
        return metatable->syntheticName;
    if (const UnionType* u = get<UnionType>(ty))
        return u->name;
    if (const IntersectionType* i = get<IntersectionType>(ty))
        return i->name;
    if (const FunctionType* f = get<FunctionType>(ty))
        return f->name;
    return std::nullopt;
}

enum class Shape
{
    Record,
    Array,
    Map,
    Function,
    Union,
    Intersection,
};

bool isList(Shape shape)
{
    return shape == Shape::Array || shape == Shape::Map;
}

// A binding or property name a type was found under.
struct Hint
{
    std::string word;
    // The word names the list or map holding this type (`animals` for an element of `animals: {T}`), not
    // the type itself, so it doesn't name the type.
    bool element = false;
};

// One printed form of a type, and every type that prints as it.
struct Candidate
{
    std::string key;
    Shape shape;
    std::vector<TypeId> types;
    std::vector<Hint> hints;
    // A list's element type, whose name a list without one of its own is named after.
    std::optional<TypeId> element;
    std::optional<std::string> name;
    // The name is an alias the reader wrote, which a list of this type is named after.
    bool nameIsAlias = false;
};

struct Collector
{
    ToStringOptions keyOptions;

    std::vector<Candidate> candidates;
    std::unordered_map<std::string, size_t> byKey;
    std::unordered_map<TypeId, size_t> byType;
    // Names the message prints already, which a made-up name must not reuse, and the types they name, in
    // the order the walk met them.
    std::unordered_set<std::string> printedNames;
    std::vector<std::pair<std::string, TypeId>> aliases;
    // Off to only collect `aliases`.
    bool findRepeats = true;

    size_t visited = 0;
    std::vector<TypeId> descending;

    explicit Collector(const ToStringOptions& keyOptions)
        : keyOptions(keyOptions)
    {
    }

    std::optional<Shape> shapeOf(TypeId ty) const
    {
        if (const TableType* table = get<TableType>(ty))
        {
            // A table literal held in a local prints as the local's name, which is short and isn't
            // printed anywhere a name could replace it.
            if (table->syntheticName && !keyOptions.ignoreSyntheticName)
                return std::nullopt;
            if (!table->props.empty() || !table->indexer)
                return Shape::Record;
            bool numberKeys = get<PrimitiveType>(follow(table->indexer->indexType)) &&
                              get<PrimitiveType>(follow(table->indexer->indexType))->type == PrimitiveType::Number;
            return numberKeys ? Shape::Array : Shape::Map;
        }
        if (get<FunctionType>(ty))
            return Shape::Function;
        // `T?` prints as its `T` with a `?`: naming the `T` covers it.
        if (const UnionType* u = get<UnionType>(ty))
        {
            bool optional = std::any_of(
                u->options.begin(),
                u->options.end(),
                [](TypeId option)
                {
                    return isNil(follow(option));
                }
            );
            return optional ? std::nullopt : std::optional<Shape>{Shape::Union};
        }
        if (get<IntersectionType>(ty))
            return Shape::Intersection;
        return std::nullopt;
    }

    void note(TypeId ty, Shape shape, const std::optional<Hint>& hint, std::optional<TypeId> element)
    {
        auto known = byType.find(ty);
        size_t index;
        if (known != byType.end())
        {
            index = known->second;
        }
        else
        {
            std::string key = toString(ty, keyOptions);
            if (key.size() < kMinNamedWidth)
                return;
            auto [it, inserted] = byKey.try_emplace(key, candidates.size());
            if (inserted)
                candidates.push_back(Candidate{key, shape, {}, {}, element, std::nullopt});
            index = it->second;
            byType.emplace(ty, index);
            candidates[index].types.push_back(ty);
        }

        Candidate& candidate = candidates[index];
        bool newHint = hint && std::none_of(
                                   candidate.hints.begin(),
                                   candidate.hints.end(),
                                   [&](const Hint& h)
                                   {
                                       return h.word == hint->word && h.element == hint->element;
                                   }
                               );
        if (newHint)
            candidate.hints.push_back(*hint);
    }

    void walk(TypeId ty, std::optional<Hint> hint, int depth)
    {
        ty = follow(ty);
        bool cycles = std::find(descending.begin(), descending.end(), ty) != descending.end();
        if (cycles || visited >= kMaxWalkNodes || depth > kMaxWalkDepth)
            return;
        ++visited;

        std::optional<std::string> alias = aliasNameOf(ty);
        if (alias && printedNames.insert(*alias).second)
            aliases.emplace_back(*alias, ty);

        const TableType* table = get<TableType>(ty);
        std::optional<TypeId> element;
        if (table && table->indexer)
            element = follow(table->indexer->indexResultType);

        if (std::optional<Shape> shape = shapeOf(ty); shape && !alias && findRepeats)
            note(ty, *shape, hint, element);

        descending.push_back(ty);
        if (table)
        {
            for (const auto& [name, prop] : table->props)
            {
                if (std::optional<TypeId> propTy = prop.readTy ? prop.readTy : prop.writeTy)
                    walk(*propTy, Hint{name}, depth + 1);
            }
            if (element)
            {
                std::optional<Hint> elementHint;
                if (hint)
                    elementHint = Hint{hint->word, true};
                walk(*element, elementHint, depth + 1);
            }
        }
        else if (const MetatableType* metatable = get<MetatableType>(ty))
        {
            walk(metatable->table, hint, depth + 1);
        }
        else if (const FunctionType* function = get<FunctionType>(ty))
        {
            auto [parameters, variadic] = flatten(function->argTypes);
            for (size_t i = 0; i < parameters.size(); ++i)
            {
                std::optional<Hint> parameterHint;
                if (i < function->argNames.size() && function->argNames[i])
                    parameterHint = Hint{function->argNames[i]->name};
                walk(parameters[i], parameterHint, depth + 1);
            }
            walkPack(function->retTypes, depth + 1);
        }
        else if (const UnionType* u = get<UnionType>(ty))
        {
            for (TypeId option : u->options)
                walk(option, hint, depth + 1);
        }
        else if (const IntersectionType* intersection = get<IntersectionType>(ty))
        {
            for (TypeId part : intersection->parts)
                walk(part, hint, depth + 1);
        }
        descending.pop_back();
    }

    void walkPack(TypePackId pack, int depth)
    {
        auto [types, tail] = flatten(pack);
        for (TypeId ty : types)
            walk(ty, std::nullopt, depth);
        if (tail)
        {
            if (const VariadicTypePack* variadic = get<VariadicTypePack>(follow(*tail)))
                walk(variadic->ty, std::nullopt, depth);
        }
    }
};

bool isIdentifierChar(char c)
{
    return isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Whether the text at `at` is a type on its own rather than a piece of a larger union or intersection:
// `"Cat" | "Dog"` appears in `"Cat" | "Dog" | "Bird"`, but isn't that type.
bool standsAlone(const std::string& text, size_t at, size_t length)
{
    auto joinsBefore = [&](const char* joiner)
    {
        return at >= 2 && text.compare(at - 2, 2, joiner) == 0;
    };
    auto joinsAfter = [&](const char* joiner)
    {
        return text.compare(at + length, 2, joiner) == 0;
    };
    return !joinsBefore("| ") && !joinsBefore("& ") && !joinsAfter(" |") && !joinsAfter(" &");
}

// How many times `text` prints `key` as a type of its own.
size_t occurrences(const std::string& text, const std::string& key)
{
    size_t count = 0;
    size_t at = text.find(key);
    while (at != std::string::npos)
    {
        if (standsAlone(text, at, key.size()))
        {
            ++count;
            at = text.find(key, at + key.size());
        }
        else
            at = text.find(key, at + 1);
    }
    return count;
}

// `text` with each line break and the indentation after it as one space, which is how a type printed a
// field per line reads on one line: `{\n    a: number,\n    b: string\n}` is `{ a: number, b: string }`.
std::string onOneLine(const std::string& text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '\n')
        {
            result += text[i];
            continue;
        }
        result += ' ';
        while (i + 1 < text.size() && text[i + 1] == ' ')
            ++i;
    }
    return result;
}

// Picks the repeated types worth naming. A type printed inside a larger one that gets a name is printed
// once more, in that name's declaration, and no longer everywhere the larger one was.
std::vector<size_t> chooseRepeated(const std::vector<Candidate>& candidates, const std::string& message)
{
    std::vector<size_t> order;
    for (size_t i = 0; i < candidates.size(); ++i)
        order.push_back(i);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](size_t a, size_t b)
        {
            return candidates[a].key.size() > candidates[b].key.size();
        }
    );

    std::vector<size_t> chosen;
    std::unordered_map<size_t, size_t> counts;
    for (size_t i : order)
    {
        size_t count = occurrences(message, candidates[i].key);
        for (size_t larger : chosen)
        {
            size_t inside = occurrences(candidates[larger].key, candidates[i].key);
            size_t removed = (counts[larger] - 1) * inside;
            count = count > removed ? count - removed : 0;
        }
        if (count < 2)
            continue;
        chosen.push_back(i);
        counts[i] = count;
        if (chosen.size() == kMaxNames)
            break;
    }
    return chosen;
}

// Aliases in scope that print as one of `keys`, by key: a type the reader already named, wherever it came
// from.
std::unordered_map<std::string, std::string> aliasesInScope(const Scope* scope, const std::unordered_set<std::string>& keys, ToStringOptions options)
{
    std::unordered_map<std::string, std::string> found;
    options.alwaysExpandRootAlias = true;
    size_t printed = 0;
    for (const Scope* s = scope; s && printed < kMaxScopeAliases; s = s->parent.get())
    {
        for (const auto* bindings : {&s->privateTypeBindings, &s->exportedTypeBindings})
        {
            for (const auto& [name, fun] : *bindings)
            {
                if (printed >= kMaxScopeAliases)
                    break;
                if (!fun.typeParams.empty() || !fun.typePackParams.empty())
                    continue;
                ++printed;
                std::string key = toString(fun.type, options);
                if (keys.count(key))
                    found.try_emplace(key, name);
            }
        }
    }
    return found;
}

struct Namer
{
    const std::vector<Candidate>& candidates;
    const std::unordered_map<TypeId, size_t>& byType;
    TypeNameAllocator allocator;
    // Elements of lists that print by an alias in scope, though they aren't printed by it otherwise.
    std::unordered_map<TypeId, std::string> elementAliases;

    // A list or map of an aliased type is named for the alias, plural: `{Animal}` and
    // `{ [string]: Animal }` are `Animals`. Only an alias the reader wrote names one: a list of `Nyla`s, or
    // of a type named after a binding, isn't a type the reader knows by any name.
    std::optional<std::string> fromAlias(const Candidate& candidate) const
    {
        if (!isList(candidate.shape) || !candidate.element)
            return std::nullopt;
        TypeId element = follow(*candidate.element);
        if (std::optional<std::string> alias = aliasNameOf(element))
            return listNameOf(*alias);
        if (auto inScope = elementAliases.find(element); inScope != elementAliases.end())
            return listNameOf(inScope->second);
        auto it = byType.find(element);
        if (it == byType.end())
            return std::nullopt;
        const Candidate& elementCandidate = candidates[it->second];
        if (elementCandidate.name && elementCandidate.nameIsAlias)
            return listNameOf(*elementCandidate.name);
        return std::nullopt;
    }

    // `candidate`'s name: the reader's alias for it, else the plural of the alias it's a list of, else the
    // bindings and properties it was found under, else a pet name.
    std::string choose(const Candidate& candidate, const std::optional<std::string>& existingAlias)
    {
        if (existingAlias && allocator.claimAlias(*existingAlias))
            return *existingAlias;

        std::vector<std::string> preferred;
        if (std::optional<std::string> name = fromAlias(candidate))
            preferred.push_back(*name);
        for (const Hint& hint : candidate.hints)
        {
            if (!hint.element)
                preferred.push_back(pascalCase(hint.word));
        }
        return allocator.allocate(preferred);
    }
};

} // namespace

bool mentionsName(const std::string& text, const std::string& name)
{
    size_t at = text.find(name);
    while (at != std::string::npos)
    {
        bool startsWord = at == 0 || !isIdentifierChar(text[at - 1]);
        bool endsWord = at + name.size() == text.size() || !isIdentifierChar(text[at + name.size()]);
        if (startsWord && endsWord)
            return true;
        at = text.find(name, at + 1);
    }
    return false;
}

RepeatedTypeNames nameRepeatedTypes(
    const std::vector<PrintedType>& printed,
    const std::string& message,
    const ToStringOptions& printing,
    const Scope* scope
)
{
    RepeatedTypeNames result;
    // Most messages can't hold two copies of a type long enough to name, and needn't be walked at all.
    if (message.size() < 2 * kMinNamedWidth)
        return result;

    ToStringOptions keyOptions = printing;
    keyOptions.repeatedTypeNames = nullptr;
    keyOptions.propertyFocus = nullptr;
    keyOptions.maxTypeLength = kMaxKeyWidth;

    Collector collector{keyOptions};
    for (const PrintedType& p : printed)
    {
        std::optional<Hint> hint;
        if (p.noun)
            hint = Hint{*p.noun};
        collector.walk(p.ty, hint, 0);
    }

    std::vector<size_t> chosen = chooseRepeated(collector.candidates, onOneLine(message));
    if (chosen.empty())
        return result;

    // The element of a repeated list is looked up too: a list of a type the reader has an alias for is
    // named after it, even when the element isn't repeated on its own.
    std::unordered_set<std::string> keys;
    std::unordered_map<TypeId, std::string> elementKeys;
    for (size_t i : chosen)
    {
        const Candidate& candidate = collector.candidates[i];
        keys.insert(candidate.key);
        if (isList(candidate.shape) && candidate.element && !aliasNameOf(*candidate.element))
        {
            std::string elementKey = toString(*candidate.element, keyOptions);
            keys.insert(elementKey);
            elementKeys.emplace(*candidate.element, std::move(elementKey));
        }
    }
    std::unordered_map<std::string, std::string> existing = scope ? aliasesInScope(scope, keys, keyOptions)
                                                                   : std::unordered_map<std::string, std::string>{};

    // Shorter types first, so a list's element has its name before the list is named after it.
    std::sort(
        chosen.begin(),
        chosen.end(),
        [&](size_t a, size_t b)
        {
            return collector.candidates[a].key.size() < collector.candidates[b].key.size();
        }
    );
    Namer namer{collector.candidates, collector.byType, TypeNameAllocator{scope}};
    for (const std::string& name : collector.printedNames)
        namer.allocator.reserve(name);

    // An element printed by the reader's alias needs no declaration: the reader wrote it.
    for (const auto& [element, key] : elementKeys)
    {
        auto alias = existing.find(key);
        auto known = collector.byType.find(element);
        bool repeatedItself = known != collector.byType.end() &&
                              std::find(chosen.begin(), chosen.end(), known->second) != chosen.end();
        if (alias == existing.end() || repeatedItself || !namer.allocator.claimAlias(alias->second))
            continue;
        namer.elementAliases[element] = alias->second;
        std::vector<TypeId> sameTypes{element};
        if (known != collector.byType.end())
            sameTypes = collector.candidates[known->second].types;
        for (TypeId ty : sameTypes)
            result.names[ty] = alias->second;
    }
    for (size_t i : chosen)
    {
        Candidate& candidate = collector.candidates[i];
        auto alias = existing.find(candidate.key);
        std::optional<std::string> existingAlias;
        if (alias != existing.end())
            existingAlias = alias->second;
        candidate.name = namer.choose(candidate, existingAlias);
        candidate.nameIsAlias = existingAlias && *candidate.name == *existingAlias;
        for (TypeId ty : candidate.types)
            result.names[ty] = *candidate.name;
    }

    // Declared in the order the walk first met them, which is the order the message prints them.
    std::sort(chosen.begin(), chosen.end());
    ToStringOptions declarationOptions = blockPrinting(printing);
    declarationOptions.alwaysExpandRootAlias = true;
    declarationOptions.repeatedTypeNames = &result.names;
    for (size_t i : chosen)
    {
        const Candidate& candidate = collector.candidates[i];
        result.declarations.push_back(NamedTypeDeclaration{*candidate.name, toString(candidate.types.front(), declarationOptions)});
    }
    return result;
}

std::vector<std::pair<std::string, TypeId>> aliasesReachableFrom(const std::vector<TypeId>& roots)
{
    Collector collector{ToStringOptions{}};
    collector.findRepeats = false;
    for (TypeId root : roots)
        collector.walk(root, std::nullopt, 0);
    return std::move(collector.aliases);
}

void appendPrintedPack(std::vector<PrintedType>& printed, TypePackId pack)
{
    auto [types, tail] = flatten(pack);
    for (TypeId ty : types)
        printed.push_back(PrintedType{ty, std::nullopt});
    if (tail)
    {
        if (const VariadicTypePack* variadic = get<VariadicTypePack>(follow(*tail)))
            printed.push_back(PrintedType{variadic->ty, std::nullopt});
    }
}

ToStringOptions blockPrinting(ToStringOptions options)
{
    options.useLineBreaks = true;
    options.breakOnlyLargeTables = true;
    options.maxPropertiesShown = kMaxPropertiesShown;
    return options;
}

namespace
{

// The two sides of an expected/got error, printed the way the message shows them.
struct PrintedSides
{
    std::string wanted;
    std::string given;
    std::vector<NamedTypeDeclaration> declarations;
};

// `wanted` and `given` (types or packs) printed as blocks with names for what they repeat, or nullopt when
// that prints them just as `ErrorConverter` would on its own.
template<typename T>
std::optional<PrintedSides> printSides(T wanted, T given, const std::vector<PrintedType>& printed, const Scope* scope)
{
    // How `ErrorConverter` prints the two types of an error `explainMismatch` didn't explain.
    ToStringOptions printing;
    std::string wantedInline = toString(wanted, printing);
    std::string givenInline = toString(given, printing);

    // Two types that print alike are told apart by where they're from; one name for both would hide that.
    RepeatedTypeNames names;
    if (wantedInline != givenInline)
        names = nameRepeatedTypes(printed, wantedInline + "\n" + givenInline, printing, scope);

    ToStringOptions block = blockPrinting(printing);
    if (!names.empty())
        block.repeatedTypeNames = &names.names;
    std::string wantedBlock = toString(wanted, block);
    std::string givenBlock = toString(given, block);
    if (names.empty() && wantedBlock == wantedInline && givenBlock == givenInline)
        return std::nullopt;
    return PrintedSides{std::move(wantedBlock), std::move(givenBlock), std::move(names.declarations)};
}

} // namespace

void nameRepeatedTypesInError(TypeErrorData& data, const Scope* scope)
{
    if (TypeMismatch* tm = get_if<TypeMismatch>(&data))
    {
        // An explained mismatch printed its types already; the others print something other than the two types.
        bool printsBothTypes = !tm->luwuExplanation && !tm->overrideMessage && !tm->contextWantedDisplay && !tm->contextGivenDisplay &&
                               !tm->wantedName && !tm->givenName && !tm->error;
        if (!printsBothTypes)
            return;

        std::vector<PrintedType> printed{{tm->wantedType, std::nullopt}, {tm->givenType, std::nullopt}};
        if (std::optional<PrintedSides> sides = printSides(tm->wantedType, tm->givenType, printed, scope))
        {
            tm->wantedName = std::move(sides->wanted);
            tm->givenName = std::move(sides->given);
            tm->whereDeclarations = std::move(sides->declarations);
        }
    }
    else if (TypePackMismatch* tpm = get_if<TypePackMismatch>(&data))
    {
        if (tpm->overrideMessage || tpm->wantedName || tpm->givenName)
            return;

        std::vector<PrintedType> printed;
        appendPrintedPack(printed, tpm->wantedTp);
        appendPrintedPack(printed, tpm->givenTp);
        if (std::optional<PrintedSides> sides = printSides(tpm->wantedTp, tpm->givenTp, printed, scope))
        {
            tpm->wantedName = std::move(sides->wanted);
            tpm->givenName = std::move(sides->given);
            tpm->whereDeclarations = std::move(sides->declarations);
        }
    }
}

} // namespace Luau
