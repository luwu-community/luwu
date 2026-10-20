// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/TypeFunctionRuntimeBuilder.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConstraintSolver.h"
#include "Luau/Common.h"
#include "Luau/DenseHash.h"
#include "Luau/RecursionCounter.h"
#include "Luau/StringUtils.h"
#include "Luau/Type.h"
#include "Luau/TypeArena.h"
#include "Luau/TypeFwd.h"
#include "Luau/TypeFunctionRuntime.h"
#include "Luau/TypePack.h"
#include "Luau/TypeOrPack.h"
#include "Luau/ToString.h"

#include <optional>

// used to control the recursion limit of any operations done by user-defined type functions
// currently, controls serialization, deserialization, and `type.copy`
LUAU_DYNAMIC_FASTINTVARIABLE(LuauTypeFunctionSerdeIterationLimit, 100'000);

LUAU_FASTFLAG(LuauTypeFunctionStructuredErrors)
LUAU_FASTFLAG(LuauTypeFunctionSerializeArgNames)
LUAU_FASTFLAG(LuauTypeFunctionTableIndexerIsReadOnly)

namespace Luau
{

// Forked version of Clone.cpp
class TypeFunctionSerializer
{
    using SeenTypes = DenseHashMap<TypeId, TypeFunctionTypeId>;
    using SeenTypePacks = DenseHashMap<TypePackId, TypeFunctionTypePackId>;

    TypeFunctionRuntimeBuilderState* state = nullptr;
    NotNull<TypeFunctionRuntime> typeFunctionRuntime;

    // A queue of TypeFunctionTypeIds that have been serialized, but whose interior types hasn't
    // been updated to point to itself. Once all of its interior types
    // has been updated, it gets removed from the queue.

    // queue.back() should always return two of same type in their respective sides
    // For example `auto [first, second] = queue.back()`: if first is PrimitiveType,
    // second must be TypeFunctionPrimitiveType; else there should be an error
    std::vector<std::tuple<TypeOrPack, TypeFunctionKind>> queue;

    SeenTypes types;     // Mapping of TypeIds that have been shallow serialized to TypeFunctionTypeIds
    SeenTypePacks packs; // Mapping of TypePackIds that have been shallow serialized to TypeFunctionTypePackIds

    int steps = 0;

public:
    explicit TypeFunctionSerializer(TypeFunctionRuntimeBuilderState* state)
        : state(state)
        , typeFunctionRuntime(state->ctx->typeFunctionRuntime)
        , queue({})
        , types({})
        , packs({})
    {
    }

    TypeFunctionTypeId serialize(TypeId ty)
    {
        shallowSerialize(ty);
        run();

        if (hasExceededIterationLimit() || hasErrors())
            return nullptr;

        return find(ty).value_or(nullptr);
    }

    TypeFunctionTypePackId serialize(TypePackId tp)
    {
        shallowSerialize(tp);
        run();

        if (hasExceededIterationLimit() || hasErrors())
            return nullptr;

        return find(tp).value_or(nullptr);
    }

private:
    bool hasErrors() const
    {
        if (FFlag::LuauTypeFunctionStructuredErrors)
            return !state->errors.empty();
        return state->errors_DEPRECATED.size() != 0;
    }

    bool hasExceededIterationLimit() const
    {
        if (DFInt::LuauTypeFunctionSerdeIterationLimit == 0)
            return false;

        return steps + queue.size() >= size_t(DFInt::LuauTypeFunctionSerdeIterationLimit);
    }

    void run()
    {
        while (!queue.empty())
        {
            ++steps;

            if (hasExceededIterationLimit() || hasErrors())
                break;

            auto [ty, tfti] = queue.back();
            queue.pop_back();

            serializeChildren(ty, tfti);
        }
    }

    std::optional<TypeFunctionTypeId> find(TypeId ty) const
    {
        if (auto result = types.find(ty))
            return *result;

        return std::nullopt;
    }

    std::optional<TypeFunctionTypePackId> find(TypePackId tp) const
    {
        if (auto result = packs.find(tp))
            return *result;

        return std::nullopt;
    }

    std::optional<TypeFunctionKind> find(TypeOrPack kind) const
    {
        if (auto ty = get<TypeId>(kind))
            return find(*ty);
        else if (auto tp = get<TypePackId>(kind))
            return find(*tp);
        else
        {
            LUAU_ASSERT(!"Unknown kind found at TypeFunctionRuntimeSerializer");
            return std::nullopt;
        }
    }

    // Luwu: the first metadata on the way to what `ty` follows to. A type can be bound to another after it got its
    // comments (a field's function type is, while solving), and following skips the one that has them.
    static const std::shared_ptr<const TypeMetadataTable>& metadataAlongBounds(TypeId ty)
    {
        while (!ty->metadata)
        {
            const BoundType* bound = get<BoundType>(ty);
            if (!bound)
                break;
            ty = bound->boundTo;
        }
        return ty->metadata;
    }

    TypeFunctionTypeId shallowSerialize(TypeId ty)
    {
        const std::shared_ptr<const TypeMetadataTable>& metadata = metadataAlongBounds(ty);
        ty = follow(ty);

        if (auto it = find(ty))
            return *it;

        // Create a shallow serialization
        TypeFunctionTypeId target = {};
        if (auto p = get<PrimitiveType>(ty))
        {
            switch (p->type)
            {
            case PrimitiveType::NilType:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::NilType));
                break;
            case PrimitiveType::Boolean:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Boolean));
                break;
            case PrimitiveType::Number:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Number));
                break;
            case PrimitiveType::Integer:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Integer));
                break;
            case PrimitiveType::String:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::String));
                break;
            case PrimitiveType::Thread:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Thread));
                break;
            case PrimitiveType::Buffer:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Buffer));
                break;
            case PrimitiveType::NoneType:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::None));
                break;
            case PrimitiveType::Function:
            case PrimitiveType::Table:
            default:
                if (FFlag::LuauTypeFunctionStructuredErrors)
                    state->errors.emplace_back(Location{}, UnsupportedType{ty});
                else
                    state->errors_DEPRECATED.push_back(
                        format("Argument of primitive type %s is not currently serializable by type functions", toString(ty).c_str())
                    );
            }
        }
        else if (get<UnknownType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionUnknownType{});
        else if (get<NeverType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionNeverType{});
        else if (get<AnyType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionAnyType{});
        else if (auto s = get<SingletonType>(ty))
        {
            if (auto bs = get<BooleanSingleton>(s))
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionSingletonType{TypeFunctionBooleanSingleton{bs->value}});
            else if (auto ss = get<StringSingleton>(s))
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionSingletonType{TypeFunctionStringSingleton{ss->value}});
            else
            {
                if (FFlag::LuauTypeFunctionStructuredErrors)
                    state->errors.emplace_back(Location{}, UnsupportedType{ty});
                else
                    state->errors_DEPRECATED.push_back(
                        format("Argument of singleton type %s is not currently serializable by type functions", toString(ty).c_str())
                    );
            }
        }
        else if (get<UnionType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionUnionType{{}});
        else if (get<IntersectionType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionIntersectionType{{}});
        else if (get<NegationType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionNegationType{{}});
        else if (get<TableType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionTableType{{}, std::nullopt, std::nullopt});
        else if (get<MetatableType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionTableType{{}, std::nullopt, std::nullopt});
        else if (get<FunctionType>(ty))
        {
            TypeFunctionTypePackId emptyTypePack = typeFunctionRuntime->typePackArena.allocate(TypeFunctionTypePack{});
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionFunctionType{{}, {}, emptyTypePack, emptyTypePack});
        }
        else if (get<ExternType>(ty))
        {
            // Since there aren't any new class types being created in type functions, we will deserialize by using a direct reference to the original
            // class
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionExternType{{}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, ty});
        }
        else if (auto g = get<GenericType>(ty))
        {
            Name name = g->name;

            if (!g->explicitName)
                name = format("g%d", g->index);

            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionGenericType{g->explicitName, false, std::move(name)});
        }
        else
        {
            if (FFlag::LuauTypeFunctionStructuredErrors)
                state->errors.emplace_back(Location{}, UnsupportedType{ty});
            else
                state->errors_DEPRECATED.push_back(
                    format("Argument of type %s is not currently serializable by type functions", toString(ty).c_str())
                );
        }

        types[ty] = target;
        queue.emplace_back(ty, target);

        if (target)
        {
            TypeFunctionType* mutableTarget = const_cast<TypeFunctionType*>(target);
            mutableTarget->origin = ty;
            mutableTarget->metadata = serializeMetadata(metadata);
        }

        return target;
    }

    // Luwu: a type's metadata, with the types in it serialized like any other (TypeMetadataTable)
    std::shared_ptr<const TypeFunctionMetadataTable> serializeMetadata(const std::shared_ptr<const TypeMetadataTable>& metadata)
    {
        if (!metadata)
            return nullptr;

        auto serializeValue = [&](const TypeMetadataValue& value) -> TypeFunctionMetadataValue
        {
            if (const bool* b = get_if<bool>(&value))
                return *b;
            if (const double* n = get_if<double>(&value))
                return *n;
            if (const std::string* str = get_if<std::string>(&value))
                return *str;
            if (const TypeId* ty = get_if<TypeId>(&value))
                return shallowSerialize(*ty);

            const auto* table = get_if<std::shared_ptr<const TypeMetadataTable>>(&value);
            LUAU_ASSERT(table);
            return serializeMetadata(*table);
        };

        auto result = std::make_shared<TypeFunctionMetadataTable>();
        result->entries.reserve(metadata->entries.size());
        for (const auto& [key, value] : metadata->entries)
            result->entries.emplace_back(serializeValue(key), serializeValue(value));
        result->docContext = metadata->docContext;

        return result;
    }

    // Luwu: a property's documentation goes along with it (TypeFunctionProperty)
    TypeFunctionProperty serializedProperty(
        const Property& p,
        std::optional<TypeFunctionTypeId> readTy,
        std::optional<TypeFunctionTypeId> writeTy
    )
    {
        TypeFunctionProperty result{readTy, writeTy};
        result.location = p.location;
        result.typeLocation = p.typeLocation;
        result.documentationSymbol = p.documentationSymbol;
        result.deprecated = p.deprecated;
        result.deprecatedSuggestion = p.deprecatedSuggestion;
        result.metadata = serializeMetadata(p.metadata);
        return result;
    }

    TypeFunctionTypePackId shallowSerialize(TypePackId tp)
    {
        tp = follow(tp);

        if (auto it = find(tp))
            return *it;

        // Create a shallow serialization
        TypeFunctionTypePackId target = {};
        if (get<TypePack>(tp))
            target = typeFunctionRuntime->typePackArena.allocate(TypeFunctionTypePack{{}});
        else if (get<VariadicTypePack>(tp))
            target = typeFunctionRuntime->typePackArena.allocate(TypeFunctionVariadicTypePack{});
        else if (auto gPack = get<GenericTypePack>(tp))
        {
            Name name = gPack->name;

            if (!gPack->explicitName)
                name = format("g%d", gPack->index);

            target = typeFunctionRuntime->typePackArena.allocate(TypeFunctionGenericTypePack{gPack->explicitName, std::move(name)});
        }
        else
        {
            if (FFlag::LuauTypeFunctionStructuredErrors)
                state->errors.emplace_back(Location{}, UnsupportedTypePack{tp});
            else
                state->errors_DEPRECATED.push_back(
                    format("Argument of type pack %s is not currently serializable by type functions", toString(tp).c_str())
                );
        }

        packs[tp] = target;
        queue.emplace_back(tp, target);
        return target;
    }

    void serializeChildren(const TypeId ty, TypeFunctionTypeId tfti)
    {
        if (auto [p1, p2] = std::tuple{get<PrimitiveType>(ty), getMutable<TypeFunctionPrimitiveType>(tfti)}; p1 && p2)
            serializeChildren(p1, p2);
        else if (auto [u1, u2] = std::tuple{get<UnknownType>(ty), getMutable<TypeFunctionUnknownType>(tfti)}; u1 && u2)
            serializeChildren(u1, u2);
        else if (auto [n1, n2] = std::tuple{get<NeverType>(ty), getMutable<TypeFunctionNeverType>(tfti)}; n1 && n2)
            serializeChildren(n1, n2);
        else if (auto [a1, a2] = std::tuple{get<AnyType>(ty), getMutable<TypeFunctionAnyType>(tfti)}; a1 && a2)
            serializeChildren(a1, a2);
        else if (auto [s1, s2] = std::tuple{get<SingletonType>(ty), getMutable<TypeFunctionSingletonType>(tfti)}; s1 && s2)
            serializeChildren(s1, s2);
        else if (auto [u1, u2] = std::tuple{get<UnionType>(ty), getMutable<TypeFunctionUnionType>(tfti)}; u1 && u2)
            serializeChildren(u1, u2);
        else if (auto [i1, i2] = std::tuple{get<IntersectionType>(ty), getMutable<TypeFunctionIntersectionType>(tfti)}; i1 && i2)
            serializeChildren(i1, i2);
        else if (auto [n1, n2] = std::tuple{get<NegationType>(ty), getMutable<TypeFunctionNegationType>(tfti)}; n1 && n2)
            serializeChildren(n1, n2);
        else if (auto [t1, t2] = std::tuple{get<TableType>(ty), getMutable<TypeFunctionTableType>(tfti)}; t1 && t2)
            serializeChildren(t1, t2);
        else if (auto [m1, m2] = std::tuple{get<MetatableType>(ty), getMutable<TypeFunctionTableType>(tfti)}; m1 && m2)
            serializeChildren(m1, m2);
        else if (auto [f1, f2] = std::tuple{get<FunctionType>(ty), getMutable<TypeFunctionFunctionType>(tfti)}; f1 && f2)
            serializeChildren(f1, f2);
        else if (auto [c1, c2] = std::tuple{get<ExternType>(ty), getMutable<TypeFunctionExternType>(tfti)}; c1 && c2)
            serializeChildren(c1, c2);
        else if (auto [g1, g2] = std::tuple{get<GenericType>(ty), getMutable<TypeFunctionGenericType>(tfti)}; g1 && g2)
            serializeChildren(g1, g2);
        else
        {
            // Either this or ty and tfti do not represent the same type
            if (FFlag::LuauTypeFunctionStructuredErrors)
                state->errors.emplace_back(Location{}, UnsupportedType{ty});
            else
                state->errors_DEPRECATED.push_back(
                    format("Argument of type %s is not currently serializable by type functions", toString(ty).c_str())
                );
        }
    }

    void serializeChildren(const TypePackId tp, TypeFunctionTypePackId tftp)
    {
        if (auto [tPack1, tPack2] = std::tuple{get<TypePack>(tp), getMutable<TypeFunctionTypePack>(tftp)}; tPack1 && tPack2)
            serializeChildren(tPack1, tPack2);
        else if (auto [vPack1, vPack2] = std::tuple{get<VariadicTypePack>(tp), getMutable<TypeFunctionVariadicTypePack>(tftp)}; vPack1 && vPack2)
            serializeChildren(vPack1, vPack2);
        else if (auto [gPack1, gPack2] = std::tuple{get<GenericTypePack>(tp), getMutable<TypeFunctionGenericTypePack>(tftp)}; gPack1 && gPack2)
            serializeChildren(gPack1, gPack2);
        else
        {
            // Either this or tp and tftp do not represent the same type
            if (FFlag::LuauTypeFunctionStructuredErrors)
                state->errors.emplace_back(Location{}, UnsupportedTypePack{tp});
            else
                state->errors_DEPRECATED.push_back(format("Type functions do not currently support types of the form '%s'", toString(tp).c_str()));
        }
    }

    void serializeChildren(TypeOrPack kind, TypeFunctionKind tfkind)
    {
        if (auto [ty, tfty] = std::tuple{get<TypeId>(kind), get<TypeFunctionTypeId>(tfkind)}; ty && tfty)
            serializeChildren(*ty, *tfty);
        else if (auto [tp, tftp] = std::tuple{get<TypePackId>(kind), get<TypeFunctionTypePackId>(tfkind)}; tp && tftp)
            serializeChildren(*tp, *tftp);
        else
            state->ctx->ice->ice("Serializing user defined type function arguments: kind and tfkind do not represent the same type");
    }

    void serializeChildren(const PrimitiveType* p1, TypeFunctionPrimitiveType* p2)
    {
        // noop.
    }

    void serializeChildren(const UnknownType* u1, TypeFunctionUnknownType* u2)
    {
        // noop.
    }

    void serializeChildren(const NeverType* n1, TypeFunctionNeverType* n2)
    {
        // noop.
    }

    void serializeChildren(const AnyType* a1, TypeFunctionAnyType* a2)
    {
        // noop.
    }

    void serializeChildren(const SingletonType* s1, TypeFunctionSingletonType* s2)
    {
        // noop.
    }

    void serializeChildren(const UnionType* u1, TypeFunctionUnionType* u2)
    {
        for (const TypeId& ty : u1->options)
            u2->components.push_back(shallowSerialize(ty));
    }

    void serializeChildren(const IntersectionType* i1, TypeFunctionIntersectionType* i2)
    {
        for (const TypeId& ty : i1->parts)
            i2->components.push_back(shallowSerialize(ty));
    }

    void serializeChildren(const NegationType* n1, TypeFunctionNegationType* n2)
    {
        n2->type = shallowSerialize(n1->ty);
    }

    void serializeChildren(const TableType* t1, TypeFunctionTableType* t2)
    {
        for (const auto& [k, p] : t1->props)
        {
            std::optional<TypeFunctionTypeId> readTy = std::nullopt;
            if (p.readTy)
                readTy = shallowSerialize(*p.readTy);

            std::optional<TypeFunctionTypeId> writeTy = std::nullopt;
            if (p.writeTy)
                writeTy = shallowSerialize(*p.writeTy);

            t2->props[k] = serializedProperty(p, readTy, writeTy);
        }

        if (t1->indexer)
        {
            t2->indexer = TypeFunctionTableIndexer(
                shallowSerialize(t1->indexer->indexType),
                shallowSerialize(t1->indexer->indexResultType),
                FFlag::LuauTypeFunctionTableIndexerIsReadOnly ? t1->indexer->isReadOnly : false
            );
        }
    }

    void serializeChildren(const MetatableType* m1, TypeFunctionTableType* m2)
    {
        // Serialize main part of the metatable immediately
        if (auto tableTy = get<TableType>(follow(m1->table)))
            serializeChildren(tableTy, m2);

        m2->metatable = shallowSerialize(m1->metatable);
    }

    void serializeChildren(const FunctionType* f1, TypeFunctionFunctionType* f2)
    {
        f2->generics.reserve(f1->generics.size());
        for (auto ty : f1->generics)
            f2->generics.push_back(shallowSerialize(ty));

        f2->genericPacks.reserve(f1->genericPacks.size());
        for (auto tp : f1->genericPacks)
            f2->genericPacks.push_back(shallowSerialize(tp));

        f2->argTypes = shallowSerialize(f1->argTypes);
        f2->retTypes = shallowSerialize(f1->retTypes);

        if (FFlag::LuauTypeFunctionSerializeArgNames)
        {
            f2->argNames.reserve(f1->argNames.size());
            for (const auto& argName : f1->argNames)
            {
                if (argName)
                    f2->argNames.emplace_back(argName->name);
                else
                    f2->argNames.emplace_back();
            }
        }
    }

    void serializeChildren(const ExternType* c1, TypeFunctionExternType* c2)
    {
        // Luwu Classes (rfcs/classes): a type function sees a class's or trait's members as code outside it does. Private
        // members are left out, and so is the constructor (`__init`, a trait's `__create`): it isn't part of the type,
        // and a generic class's `__init` is generic over the class's generics, which the type it would make has lost.
        const NotNull<BuiltinTypes> builtins = state->ctx->builtins;
        const bool isUserNominal = c1->root == builtins->objectType || c1->root == builtins->classType || c1->root == builtins->traitType;

        for (const auto& [k, p] : c1->props())
        {
            const bool hiddenFromOutside = p.isPrivate || k == "__init" || k == "__create";
            if (isUserNominal && hiddenFromOutside)
                continue;

            std::optional<TypeFunctionTypeId> readTy = std::nullopt;
            if (p.readTy)
                readTy = shallowSerialize(*p.readTy);

            std::optional<TypeFunctionTypeId> writeTy = std::nullopt;
            if (p.writeTy)
                writeTy = shallowSerialize(*p.writeTy);

            c2->props[k] = serializedProperty(p, readTy, writeTy);
        }

        if (c1->indexer)
            c2->indexer = TypeFunctionTableIndexer(shallowSerialize(c1->indexer->indexType), shallowSerialize(c1->indexer->indexResultType));

        if (c1->metatable)
            c2->metatable = shallowSerialize(*c1->metatable);

        if (c1->parent)
        {
            TypeFunctionTypeId parent = shallowSerialize(*c1->parent);

            // we don't yet have read/write parents in the type inference engine.
            c2->readParent = parent;
            c2->writeParent = parent;
        }
    }

    void serializeChildren(const GenericType* g1, TypeFunctionGenericType* g2)
    {
        // noop.
    }

    void serializeChildren(const TypePack* t1, TypeFunctionTypePack* t2)
    {
        for (const TypeId& ty : t1->head)
            t2->head.push_back(shallowSerialize(ty));

        if (t1->tail.has_value())
            t2->tail = shallowSerialize(*t1->tail);
    }

    void serializeChildren(const VariadicTypePack* v1, TypeFunctionVariadicTypePack* v2)
    {
        v2->type = shallowSerialize(v1->ty);
    }

    void serializeChildren(const GenericTypePack* v1, TypeFunctionGenericTypePack* v2)
    {
        // noop.
    }
};

template<typename T>
struct SerializedGeneric
{
    bool isNamed = false;
    std::string name;
    T type = nullptr;

    explicit SerializedGeneric(std::string name)
        : name(std::move(name))
    {
    }

    SerializedGeneric(bool isNamed, std::string name, T type)
        : isNamed(isNamed)
        , name(std::move(name))
        , type(std::move(type))
    {
    }
};

struct SerializedFunctionScope
{
    size_t oldQueueSize = 0;
    TypeFunctionFunctionType* function = nullptr;

    explicit SerializedFunctionScope(size_t oldQueueSize)
        : oldQueueSize(oldQueueSize)
    {
    }

    SerializedFunctionScope(size_t oldQueueSize, TypeFunctionFunctionType* function)
        : oldQueueSize(oldQueueSize)
        , function(function)
    {
    }
};

// Complete inverse of TypeFunctionSerializer
class TypeFunctionDeserializer
{
    using SeenTypes = DenseHashMap<TypeFunctionTypeId, TypeId>;
    using SeenTypePacks = DenseHashMap<TypeFunctionTypePackId, TypePackId>;

    TypeFunctionRuntimeBuilderState* state = nullptr;
    NotNull<TypeFunctionRuntime> typeFunctionRuntime;

    // A queue of TypeIds that have been deserialized, but whose interior types hasn't
    // been updated to point to itself. Once all of its interior types
    // has been updated, it gets removed from the queue.

    // queue.back() should always return two of same type in their respective sides
    // For example `auto [first, second] = queue.back()`: if first is TypeFunctionPrimitiveType,
    // second must be PrimitiveType; else there should be an error
    std::vector<std::tuple<TypeFunctionKind, TypeOrPack>> queue;

    // Generic types and packs currently in scope
    // Generics are resolved by name even if runtime generic type pointers are different
    // Multiple names mapping to the same generic can be in scope for nested generic functions
    std::vector<SerializedGeneric<TypeId>> genericTypes;
    std::vector<SerializedGeneric<TypePackId>> genericPacks;

    // To track when generics go out of scope, we have a list of queue positions at which a specific function has introduced generics
    std::vector<SerializedFunctionScope> functionScopes;

    SeenTypes types;     // Mapping of TypeFunctionTypeIds that have been shallow deserialized to TypeIds
    SeenTypePacks packs; // Mapping of TypeFunctionTypePackIds that have been shallow deserialized to TypePackIds

    int steps = 0;

public:
    explicit TypeFunctionDeserializer(TypeFunctionRuntimeBuilderState* state)
        : state(state)
        , typeFunctionRuntime(state->ctx->typeFunctionRuntime)
        , queue({})
        , types({})
        , packs({})
    {
    }

    TypeId deserialize(TypeFunctionTypeId ty)
    {
        shallowDeserialize(ty);
        run();

        if (hasExceededIterationLimit() || hasErrors())
        {
            TypeId error = state->ctx->builtins->errorType;
            types[ty] = error;
            return error;
        }

        return find(ty).value_or(state->ctx->builtins->errorType);
    }

    // Luwu doc references: just `ty`'s metadata, for a type the deserializer doesn't make (a class put on the call's type)
    std::shared_ptr<const TypeMetadataTable> deserializeMetadataOf(TypeFunctionTypeId ty)
    {
        return withDocContext(deserializeMetadata(ty->metadata));
    }

    TypePackId deserialize(TypeFunctionTypePackId tp)
    {
        shallowDeserialize(tp);
        run();

        if (hasExceededIterationLimit() || hasErrors())
        {
            TypePackId error = state->ctx->builtins->errorTypePack;
            packs[tp] = error;
            return error;
        }

        return find(tp).value_or(state->ctx->builtins->errorTypePack);
    }

private:
    bool hasExceededIterationLimit() const
    {
        if (DFInt::LuauTypeFunctionSerdeIterationLimit == 0)
            return false;

        return steps + queue.size() >= size_t(DFInt::LuauTypeFunctionSerdeIterationLimit);
    }

    bool hasErrors() const
    {
        if (FFlag::LuauTypeFunctionStructuredErrors)
            return !state->errors.empty();
        return state->errors_DEPRECATED.size() != 0;
    }

    void pushRuntimeError(std::string message)
    {
        if (FFlag::LuauTypeFunctionStructuredErrors)
            state->errors.emplace_back(Location{}, RuntimeError{std::move(message)});
        else
            state->errors_DEPRECATED.push_back(std::move(message));
    }

    void run()
    {
        while (!queue.empty())
        {
            ++steps;

            if (hasExceededIterationLimit() || hasErrors())
                break;

            auto [tfti, ty] = queue.back();
            queue.pop_back();

            deserializeChildren(tfti, ty);

            // If we have completed working on all children of a function, remove the generic parameters from scope
            if (!functionScopes.empty() && queue.size() == functionScopes.back().oldQueueSize && !hasErrors())
            {
                closeFunctionScope(functionScopes.back().function);
                functionScopes.pop_back();
            }
        }
    }

    std::optional<TypeId> find(TypeFunctionTypeId ty) const
    {
        if (auto result = types.find(ty))
            return *result;

        return std::nullopt;
    }

    std::optional<TypePackId> find(TypeFunctionTypePackId tp) const
    {
        if (auto result = packs.find(tp))
            return *result;

        return std::nullopt;
    }

    std::optional<TypeOrPack> find(TypeFunctionKind kind) const
    {
        if (auto ty = get<TypeFunctionTypeId>(kind))
            return find(*ty);
        else if (auto tp = get<TypeFunctionTypePackId>(kind))
            return find(*tp);
        else
        {
            LUAU_ASSERT(!"Unknown kind found at TypeFunctionDeserializer");
            return std::nullopt;
        }
    }

    void closeFunctionScope(TypeFunctionFunctionType* f)
    {
        if (!f->generics.empty())
        {
            LUAU_ASSERT(genericTypes.size() >= f->generics.size());
            genericTypes.erase(genericTypes.begin() + int(genericTypes.size() - f->generics.size()), genericTypes.end());
        }

        if (!f->genericPacks.empty())
        {
            LUAU_ASSERT(genericPacks.size() >= f->genericPacks.size());
            genericPacks.erase(genericPacks.begin() + int(genericPacks.size() - f->genericPacks.size()), genericPacks.end());
        }
    }

    TypeId shallowDeserialize(TypeFunctionTypeId ty)
    {
        if (auto it = find(ty))
            return *it;

        // Create a shallow deserialization
        TypeId target = {};
        if (auto p = get<TypeFunctionPrimitiveType>(ty))
        {
            switch (p->type)
            {
            case TypeFunctionPrimitiveType::Type::NilType:
                target = state->ctx->builtins->nilType;
                break;
            case TypeFunctionPrimitiveType::Type::Boolean:
                target = state->ctx->builtins->booleanType;
                break;
            case TypeFunctionPrimitiveType::Type::Number:
                target = state->ctx->builtins->numberType;
                break;
            case TypeFunctionPrimitiveType::Type::Integer:
                target = state->ctx->builtins->integerType;
                break;
            case TypeFunctionPrimitiveType::Type::String:
                target = state->ctx->builtins->stringType;
                break;
            case TypeFunctionPrimitiveType::Type::Thread:
                target = state->ctx->builtins->threadType;
                break;
            case TypeFunctionPrimitiveType::Type::Buffer:
                target = state->ctx->builtins->bufferType;
                break;
            case TypeFunctionPrimitiveType::Type::None:
                target = state->ctx->builtins->noneType;
                break;
            default:
                state->ctx->ice->ice("Deserializing user defined type function arguments: mysterious type is being deserialized");
            }
        }
        else if (get<TypeFunctionUnknownType>(ty))
            target = state->ctx->builtins->unknownType;
        else if (get<TypeFunctionNeverType>(ty))
            target = state->ctx->builtins->neverType;
        else if (get<TypeFunctionAnyType>(ty))
            target = state->ctx->builtins->anyType;
        else if (auto s = get<TypeFunctionSingletonType>(ty))
        {
            if (auto bs = get<TypeFunctionBooleanSingleton>(s))
                target = state->ctx->arena->addType(SingletonType{BooleanSingleton{bs->value}});
            else if (auto ss = get<TypeFunctionStringSingleton>(s))
                target = state->ctx->arena->addType(SingletonType{StringSingleton{ss->value}});
            else
                state->ctx->ice->ice("Deserializing user defined type function arguments: mysterious type is being deserialized");
        }
        else if (get<TypeFunctionUnionType>(ty))
            target = state->ctx->arena->addTV(Type(UnionType{{}}));
        else if (get<TypeFunctionIntersectionType>(ty))
            target = state->ctx->arena->addTV(Type(IntersectionType{{}}));
        else if (get<TypeFunctionNegationType>(ty))
            target = state->ctx->arena->addType(NegationType{state->ctx->builtins->unknownType});
        else if (auto t = get<TypeFunctionTableType>(ty); t && !t->metatable.has_value())
            target = state->ctx->arena->addType(TableType{TableType::Props{}, std::nullopt, TypeLevel{}, TableState::Sealed});
        else if (auto m = get<TypeFunctionTableType>(ty); m && m->metatable.has_value())
        {
            TypeId emptyTable = state->ctx->arena->addType(TableType{TableType::Props{}, std::nullopt, TypeLevel{}, TableState::Sealed});
            target = state->ctx->arena->addType(MetatableType{emptyTable, emptyTable});
        }
        else if (get<TypeFunctionFunctionType>(ty))
        {
            TypePackId emptyTypePack = state->ctx->arena->addTypePack(TypePack{});
            target = state->ctx->arena->addType(FunctionType{emptyTypePack, emptyTypePack, {}, false});
        }
        else if (auto c = get<TypeFunctionExternType>(ty))
        {
            target = c->externTy;
        }
        else if (get<TypeFunctionErrorType>(ty))
        {
            // Luwu: only a type function's own result can be `types.error` (UserDefinedTypeFunction.cpp reports it)
            pushRuntimeError("types.error(...) can only be returned from a type function, not used inside another type");
            return nullptr;
        }
        else if (auto g = get<TypeFunctionGenericType>(ty))
        {
            if (g->isPack)
            {
                pushRuntimeError(format("Generic type pack '%s...' cannot be placed in a type position", g->name.c_str()));
                return nullptr;
            }
            else
            {
                auto it = std::find_if(
                    genericTypes.rbegin(),
                    genericTypes.rend(),
                    [&](const SerializedGeneric<TypeId>& el)
                    {
                        return g->isNamed == el.isNamed && g->name == el.name;
                    }
                );

                if (it == genericTypes.rend())
                {
                    pushRuntimeError(format("Generic type '%s' is not in a scope of the active generic function", g->name.c_str()));
                    return nullptr;
                }

                target = it->type;
            }
        }
        else
            state->ctx->ice->ice("Deserializing user defined type function arguments: mysterious type is being deserialized");

        types[ty] = target;
        queue.emplace_back(ty, target);

        // Luwu: a type made here gets the documentation and metadata of what it was serialized from. Builtins, extern types
        // and generics are shared by every use, so they keep their own. A singleton can be a builtin too (`true`, `false`).
        if (target && ownsDeserializedType(ty) && !target->persistent)
        {
            if (ty->origin)
                asMutable(target)->documentationSymbol = ty->origin->documentationSymbol;
            asMutable(target)->metadata = withDocContext(deserializeMetadata(ty->metadata));
        }

        return target;
    }

    // Luwu: whether deserializing `ty` made a new type, rather than reusing a builtin, extern type or generic
    static bool ownsDeserializedType(TypeFunctionTypeId ty)
    {
        return get<TypeFunctionSingletonType>(ty) || get<TypeFunctionUnionType>(ty) || get<TypeFunctionIntersectionType>(ty) ||
               get<TypeFunctionNegationType>(ty) || get<TypeFunctionTableType>(ty) || get<TypeFunctionFunctionType>(ty);
    }

    // Luwu doc references: metadata a type function set has its doc comments written at the call, so references in them
    // resolve there
    std::shared_ptr<const TypeMetadataTable> withDocContext(std::shared_ptr<const TypeMetadataTable> metadata)
    {
        if (!metadata || metadata->docContext)
            return metadata;

        ConstraintSolver* solver = state->ctx->solver;
        const Constraint* constraint = state->ctx->constraint;
        if (!solver || !solver->module || !constraint)
            return metadata;

        auto withContext = std::make_shared<TypeMetadataTable>(*metadata);
        withContext->docContext = MetadataDocContext{solver->module->name, constraint->location, /* generated= */ true};
        return withContext;
    }

    // Luwu: the inverse of TypeFunctionSerializer::serializeMetadata
    std::shared_ptr<const TypeMetadataTable> deserializeMetadata(const std::shared_ptr<const TypeFunctionMetadataTable>& metadata)
    {
        if (!metadata)
            return nullptr;

        auto deserializeValue = [&](const TypeFunctionMetadataValue& value) -> TypeMetadataValue
        {
            if (const bool* b = get_if<bool>(&value))
                return *b;
            if (const double* n = get_if<double>(&value))
                return *n;
            if (const std::string* str = get_if<std::string>(&value))
                return *str;
            if (const TypeFunctionTypeId* ty = get_if<TypeFunctionTypeId>(&value))
                return shallowDeserialize(*ty);

            const auto* table = get_if<std::shared_ptr<const TypeFunctionMetadataTable>>(&value);
            LUAU_ASSERT(table);
            return deserializeMetadata(*table);
        };

        auto result = std::make_shared<TypeMetadataTable>();
        result->entries.reserve(metadata->entries.size());
        for (const auto& [key, value] : metadata->entries)
            result->entries.emplace_back(deserializeValue(key), deserializeValue(value));
        result->docContext = metadata->docContext;

        return result;
    }

    // Luwu: the table a TypeFunctionTableType was serialized from, a metatable type's own table included
    static const TableType* originTable(TypeFunctionTypeId ty)
    {
        if (!ty->origin)
            return nullptr;

        TypeId origin = follow(ty->origin);
        if (const MetatableType* mt = get<MetatableType>(origin))
            origin = follow(mt->table);

        return get<TableType>(origin);
    }

    Property deserializedProperty(const TypeFunctionProperty& p, std::optional<TypeId> readTy, std::optional<TypeId> writeTy)
    {
        Property result;
        if (readTy && writeTy)
            result = Property::rw(*readTy, *writeTy);
        else if (readTy)
            result = Property::readonly(*readTy);
        else
            result = Property::writeonly(*writeTy);

        result.location = p.location;
        result.typeLocation = p.typeLocation;
        result.documentationSymbol = p.documentationSymbol;
        result.deprecated = p.deprecated;
        result.deprecatedSuggestion = p.deprecatedSuggestion;
        result.metadata = withDocContext(deserializeMetadata(p.metadata));
        return result;
    }

    TypePackId shallowDeserialize(TypeFunctionTypePackId tp)
    {
        if (auto it = find(tp))
            return *it;

        // Create a shallow deserialization
        TypePackId target = {};
        if (get<TypeFunctionTypePack>(tp))
        {
            target = state->ctx->arena->addTypePack(TypePack{});
        }
        else if (get<TypeFunctionVariadicTypePack>(tp))
        {
            target = state->ctx->arena->addTypePack(VariadicTypePack{});
        }
        else if (auto gPack = get<TypeFunctionGenericTypePack>(tp))
        {
            auto it = std::find_if(
                genericPacks.rbegin(),
                genericPacks.rend(),
                [&](const SerializedGeneric<TypePackId>& el)
                {
                    return gPack->isNamed == el.isNamed && gPack->name == el.name;
                }
            );

            if (it == genericPacks.rend())
            {
                pushRuntimeError(format("Generic type pack '%s...' is not in a scope of the active generic function", gPack->name.c_str()));
                return nullptr;
            }

            target = it->type;
        }
        else
        {
            state->ctx->ice->ice("Deserializing user defined type function arguments: mysterious type is being deserialized");
        }

        packs[tp] = target;
        queue.emplace_back(tp, target);
        return target;
    }

    void deserializeChildren(TypeFunctionTypeId tfti, TypeId ty)
    {
        if (auto [p1, p2] = std::tuple{getMutable<PrimitiveType>(ty), getMutable<TypeFunctionPrimitiveType>(tfti)}; p1 && p2)
            deserializeChildren(p2, p1);
        else if (auto [u1, u2] = std::tuple{getMutable<UnknownType>(ty), getMutable<TypeFunctionUnknownType>(tfti)}; u1 && u2)
            deserializeChildren(u2, u1);
        else if (auto [n1, n2] = std::tuple{getMutable<NeverType>(ty), getMutable<TypeFunctionNeverType>(tfti)}; n1 && n2)
            deserializeChildren(n2, n1);
        else if (auto [a1, a2] = std::tuple{getMutable<AnyType>(ty), getMutable<TypeFunctionAnyType>(tfti)}; a1 && a2)
            deserializeChildren(a2, a1);
        else if (auto [s1, s2] = std::tuple{getMutable<SingletonType>(ty), getMutable<TypeFunctionSingletonType>(tfti)}; s1 && s2)
            deserializeChildren(s2, s1);
        else if (auto [u1, u2] = std::tuple{getMutable<UnionType>(ty), getMutable<TypeFunctionUnionType>(tfti)}; u1 && u2)
            deserializeChildren(u2, u1);
        else if (auto [i1, i2] = std::tuple{getMutable<IntersectionType>(ty), getMutable<TypeFunctionIntersectionType>(tfti)}; i1 && i2)
            deserializeChildren(i2, i1);
        else if (auto [n1, n2] = std::tuple{getMutable<NegationType>(ty), getMutable<TypeFunctionNegationType>(tfti)}; n1 && n2)
            deserializeChildren(n2, n1);
        else if (auto [t1, t2] = std::tuple{getMutable<TableType>(ty), getMutable<TypeFunctionTableType>(tfti)};
                 t1 && t2 && !t2->metatable.has_value())
            deserializeChildren(t2, t1);
        else if (auto [m1, m2] = std::tuple{getMutable<MetatableType>(ty), getMutable<TypeFunctionTableType>(tfti)};
                 m1 && m2 && m2->metatable.has_value())
            deserializeChildren(tfti, m2, m1);
        else if (auto [f1, f2] = std::tuple{getMutable<FunctionType>(ty), getMutable<TypeFunctionFunctionType>(tfti)}; f1 && f2)
            deserializeChildren(f2, f1);
        else if (auto [c1, c2] = std::tuple{getMutable<ExternType>(ty), getMutable<TypeFunctionExternType>(tfti)}; c1 && c2)
            deserializeChildren(c2, c1);
        else if (auto [g1, g2] = std::tuple{getMutable<GenericType>(ty), getMutable<TypeFunctionGenericType>(tfti)}; g1 && g2)
            deserializeChildren(g2, g1);
        else
            state->ctx->ice->ice("Deserializing user defined type function arguments: mysterious type is being deserialized");

        restoreDefinition(tfti, ty);
    }

    // Luwu: whether `tfti` and every type it reaches are as they were serialized: none of them was edited
    // (TypeFunctionType::edited). Only then is a type still the alias its origin was named as.
    DenseHashMap<TypeFunctionTypeId, bool> pristineTypes{nullptr};
    // How deep isPristine follows a type. A deeper one counts as edited, which only costs it its alias name.
    static constexpr int kMaxPristineDepth = 200;
    int pristineDepth = 0;

    bool isPristine(TypeFunctionTypeId tfti)
    {
        if (const bool* known = pristineTypes.find(tfti))
            return *known;

        RecursionCounter depth{&pristineDepth};
        if (pristineDepth > kMaxPristineDepth)
            return false;

        // a cycle back to a type being checked says nothing new about it
        pristineTypes[tfti] = true;

        bool pristine = !tfti->edited;
        auto visit = [&](TypeFunctionTypeId child)
        {
            pristine = pristine && isPristine(child);
        };

        if (const TypeFunctionUnionType* u = get<TypeFunctionUnionType>(tfti))
        {
            for (TypeFunctionTypeId component : u->components)
                visit(component);
        }
        else if (const TypeFunctionIntersectionType* i = get<TypeFunctionIntersectionType>(tfti))
        {
            for (TypeFunctionTypeId component : i->components)
                visit(component);
        }
        else if (const TypeFunctionNegationType* n = get<TypeFunctionNegationType>(tfti))
            visit(n->type);
        else if (const TypeFunctionTableType* t = get<TypeFunctionTableType>(tfti))
        {
            for (const auto& [_, prop] : t->props)
            {
                if (prop.readTy)
                    visit(*prop.readTy);
                if (prop.writeTy)
                    visit(*prop.writeTy);
            }
            if (t->indexer)
            {
                visit(t->indexer->keyType);
                visit(t->indexer->valueType);
            }
            if (t->metatable)
                visit(*t->metatable);
        }
        else if (const TypeFunctionFunctionType* f = get<TypeFunctionFunctionType>(tfti))
        {
            for (TypeFunctionTypeId generic : f->generics)
                visit(generic);
            pristine = pristine && isPristine(f->argTypes) && isPristine(f->retTypes);
        }

        pristineTypes[tfti] = pristine;
        return pristine;
    }

    bool isPristine(TypeFunctionTypePackId tftp)
    {
        if (const TypeFunctionTypePack* pack = get<TypeFunctionTypePack>(tftp))
        {
            for (TypeFunctionTypeId head : pack->head)
            {
                if (!isPristine(head))
                    return false;
            }
            return !pack->tail || isPristine(*pack->tail);
        }
        if (const TypeFunctionVariadicTypePack* variadic = get<TypeFunctionVariadicTypePack>(tftp))
            return isPristine(variadic->type);
        return true;
    }

    // Luwu: the alias name a type printed as (`Pathlike`, `Config`), kept when the type function left it as it was, so a
    // type that only passed through a type function prints the same in hovers, errors and `tostring` as it did before
    void restoreAliasName(TypeFunctionTypeId tfti, TypeId ty)
    {
        TypeId origin = follow(tfti->origin);
        if (UnionType* u = getMutable<UnionType>(ty))
        {
            const UnionType* from = get<UnionType>(origin);
            if (from && (from->name || from->syntheticName) && isPristine(tfti))
            {
                u->name = from->name;
                u->syntheticName = from->syntheticName;
            }
        }
        else if (IntersectionType* i = getMutable<IntersectionType>(ty))
        {
            const IntersectionType* from = get<IntersectionType>(origin);
            if (from && (from->name || from->syntheticName) && isPristine(tfti))
            {
                i->name = from->name;
                i->syntheticName = from->syntheticName;
            }
        }
        else if (TableType* t = getMutable<TableType>(ty))
        {
            // a generic alias prints with its arguments (`List<number>`), which aren't carried over
            const TableType* from = get<TableType>(origin);
            if (from && (from->name || from->syntheticName) && from->instantiatedTypeParams.empty() &&
                from->instantiatedTypePackParams.empty() && isPristine(tfti))
            {
                t->name = from->name;
                t->syntheticName = from->syntheticName;
            }
        }
        else if (MetatableType* m = getMutable<MetatableType>(ty))
        {
            const MetatableType* from = get<MetatableType>(origin);
            if (from && from->syntheticName && isPristine(tfti))
                m->syntheticName = from->syntheticName;
        }
        else if (FunctionType* f = getMutable<FunctionType>(ty))
        {
            const FunctionType* from = get<FunctionType>(origin);
            if (from && (from->name || from->syntheticName) && isPristine(tfti))
            {
                f->name = from->name;
                f->syntheticName = from->syntheticName;
            }
        }
    }

    // Luwu: where the original table or function was defined, which is how luwu-lsp finds its doc comment
    void restoreDefinition(TypeFunctionTypeId tfti, TypeId ty)
    {
        if (!tfti->origin)
            return;

        restoreAliasName(tfti, ty);

        if (TableType* table = getMutable<TableType>(ty))
        {
            if (const TableType* origin = originTable(tfti))
            {
                table->definitionModuleName = origin->definitionModuleName;
                table->definitionLocation = origin->definitionLocation;
            }
        }
        else if (FunctionType* function = getMutable<FunctionType>(ty))
        {
            if (const FunctionType* origin = get<FunctionType>(follow(tfti->origin)))
                function->definition = origin->definition;
        }
    }

    void deserializeChildren(TypeFunctionTypePackId tftp, TypePackId tp)
    {
        if (auto [tPack1, tPack2] = std::tuple{getMutable<TypePack>(tp), getMutable<TypeFunctionTypePack>(tftp)}; tPack1 && tPack2)
            deserializeChildren(tPack2, tPack1);
        else if (auto [vPack1, vPack2] = std::tuple{getMutable<VariadicTypePack>(tp), getMutable<TypeFunctionVariadicTypePack>(tftp)};
                 vPack1 && vPack2)
            deserializeChildren(vPack2, vPack1);
        else if (auto [gPack1, gPack2] = std::tuple{getMutable<GenericTypePack>(tp), getMutable<TypeFunctionGenericTypePack>(tftp)}; gPack1 && gPack2)
            deserializeChildren(gPack2, gPack1);
        else
            state->ctx->ice->ice("Deserializing user defined type function arguments: mysterious type is being deserialized");
    }

    void deserializeChildren(TypeFunctionKind tfkind, TypeOrPack kind)
    {
        if (auto [ty, tfty] = std::tuple{get<TypeId>(kind), get<TypeFunctionTypeId>(tfkind)}; ty && tfty)
            deserializeChildren(*tfty, *ty);
        else if (auto [tp, tftp] = std::tuple{get<TypePackId>(kind), get<TypeFunctionTypePackId>(tfkind)}; tp && tftp)
            deserializeChildren(*tftp, *tp);
        else
            state->ctx->ice->ice("Deserializing user defined type function arguments: tfkind and kind do not represent the same type");
    }

    void deserializeChildren(TypeFunctionPrimitiveType* p2, PrimitiveType* p1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionUnknownType* u2, UnknownType* u1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionNeverType* n2, NeverType* n1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionAnyType* a2, AnyType* a1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionSingletonType* s2, SingletonType* s1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionUnionType* u2, UnionType* u1)
    {
        for (TypeFunctionTypeId& ty : u2->components)
            u1->options.push_back(shallowDeserialize(ty));
    }

    void deserializeChildren(TypeFunctionIntersectionType* i2, IntersectionType* i1)
    {
        for (TypeFunctionTypeId& ty : i2->components)
            i1->parts.push_back(shallowDeserialize(ty));
    }

    void deserializeChildren(TypeFunctionNegationType* n2, NegationType* n1)
    {
        n1->ty = shallowDeserialize(n2->type);
    }

    void deserializeChildren(TypeFunctionTableType* t2, TableType* t1)
    {
        for (const auto& [k, p] : t2->props)
        {
            if (!p.readTy && !p.writeTy)
                continue;

            std::optional<TypeId> readTy = p.readTy ? std::optional<TypeId>{shallowDeserialize(*p.readTy)} : std::nullopt;
            std::optional<TypeId> writeTy = p.writeTy ? std::optional<TypeId>{shallowDeserialize(*p.writeTy)} : std::nullopt;
            t1->props[k] = deserializedProperty(p, readTy, writeTy);
        }

        if (t2->indexer.has_value())
        {
            t1->indexer = TableIndexer(
                shallowDeserialize(t2->indexer->keyType),
                shallowDeserialize(t2->indexer->valueType),
                FFlag::LuauTypeFunctionTableIndexerIsReadOnly ? t2->indexer->isReadOnly : false
            );
        }
    }

    void deserializeChildren(TypeFunctionTypeId tfti, TypeFunctionTableType* m2, MetatableType* m1)
    {
        TypeFunctionTypeId temp = typeFunctionRuntime->typeArena.allocate(TypeFunctionTableType{m2->props, m2->indexer});
        // Luwu: the metatable type's node holds the origin, and originTable finds its table from it
        const_cast<TypeFunctionType*>(temp)->origin = tfti->origin;
        m1->table = shallowDeserialize(temp);

        if (m2->metatable.has_value())
            m1->metatable = shallowDeserialize(*m2->metatable);
    }

    void deserializeChildren(TypeFunctionFunctionType* f2, FunctionType* f1)
    {
        functionScopes.emplace_back(queue.size(), f2);
        std::set<std::pair<bool, std::string>> genericNames;

        // Introduce generic function parameters into scope
        for (auto ty : f2->generics)
        {
            auto gty = get<TypeFunctionGenericType>(ty);
            if (!gty || gty->isPack)
            {
                pushRuntimeError("Encountered unexpected generic");
                return;
            }
            else
                LUAU_ASSERT(gty && !gty->isPack);

            std::pair<bool, std::string> nameKey = std::make_pair(gty->isNamed, gty->name);

            // Duplicates are not allowed
            if (genericNames.find(nameKey) != genericNames.end())
            {
                pushRuntimeError(format("Duplicate type parameter '%s'", gty->name.c_str()));
                return;
            }

            genericNames.insert(nameKey);

            TypeId mapping = state->ctx->arena->addTV(Type(gty->isNamed ? GenericType{state->ctx->scope.get(), gty->name} : GenericType{}));
            genericTypes.emplace_back(gty->isNamed, gty->name, mapping);
        }

        for (auto tp : f2->genericPacks)
        {
            auto gtp = get<TypeFunctionGenericTypePack>(tp);
            if (!gtp)
            {
                pushRuntimeError("Encountered unexpected generic type pack");
                return;
            }
            else
                LUAU_ASSERT(gtp);

            std::pair<bool, std::string> nameKey = std::make_pair(gtp->isNamed, gtp->name);

            // Duplicates are not allowed
            if (genericNames.find(nameKey) != genericNames.end())
            {
                pushRuntimeError(format("Duplicate type parameter '%s'", gtp->name.c_str()));
                return;
            }

            genericNames.insert(nameKey);

            TypePackId mapping =
                state->ctx->arena->addTypePack(TypePackVar(gtp->isNamed ? GenericTypePack{state->ctx->scope.get(), gtp->name} : GenericTypePack{}));
            genericPacks.emplace_back(gtp->isNamed, gtp->name, mapping);
        }

        f1->generics.reserve(f2->generics.size());
        for (auto ty : f2->generics)
            f1->generics.push_back(shallowDeserialize(ty));

        f1->genericPacks.reserve(f2->genericPacks.size());
        for (auto tp : f2->genericPacks)
            f1->genericPacks.push_back(shallowDeserialize(tp));

        if (f2->argTypes)
            f1->argTypes = shallowDeserialize(f2->argTypes);

        if (f2->retTypes)
            f1->retTypes = shallowDeserialize(f2->retTypes);

        if (FFlag::LuauTypeFunctionSerializeArgNames)
        {
            f1->argNames.reserve(f2->argNames.size());
            for (const auto& name : f2->argNames)
            {
                if (name)
                    f1->argNames.emplace_back(FunctionArgument{*name, {}});
                else
                    f1->argNames.emplace_back();
            }
        }
    }

    void deserializeChildren(TypeFunctionExternType* c2, ExternType* c1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionGenericType* g2, GenericType* g1)
    {
        // noop.
    }

    void deserializeChildren(TypeFunctionTypePack* t2, TypePack* t1)
    {
        for (TypeFunctionTypeId& ty : t2->head)
            t1->head.push_back(shallowDeserialize(ty));

        if (t2->tail.has_value())
            t1->tail = shallowDeserialize(*t2->tail);
    }

    void deserializeChildren(TypeFunctionVariadicTypePack* v2, VariadicTypePack* v1)
    {
        v1->ty = shallowDeserialize(v2->type);
    }

    void deserializeChildren(TypeFunctionGenericTypePack* v2, GenericTypePack* v1)
    {
        // noop.
    }
};

TypeFunctionTypeId serialize(TypeId ty, TypeFunctionRuntimeBuilderState* state)
{
    return TypeFunctionSerializer(state).serialize(ty);
}

TypeFunctionTypePackId serialize(TypePackId tp, TypeFunctionRuntimeBuilderState* state)
{
    return TypeFunctionSerializer(state).serialize(tp);
}

TypeId deserialize(TypeFunctionTypeId ty, TypeFunctionRuntimeBuilderState* state)
{
    return TypeFunctionDeserializer(state).deserialize(ty);
}

TypePackId deserialize(TypeFunctionTypePackId tp, TypeFunctionRuntimeBuilderState* state)
{
    return TypeFunctionDeserializer(state).deserialize(tp);
}

std::shared_ptr<const TypeMetadataTable> deserializeMetadata(TypeFunctionTypeId ty, TypeFunctionRuntimeBuilderState* state)
{
    return TypeFunctionDeserializer(state).deserializeMetadataOf(ty);
}

} // namespace Luau
