// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Common.h"
#include "Luau/Scope.h"
#include "Luau/TypeFunctionError.h"
#include "Luau/TypeFunctionRuntimeBuilder.h"
#include "Luau/Type.h"
#include "Luau/Variant.h"

#include <optional>
#include <string>
#include <map>
#include <vector>

using lua_State = struct lua_State;

namespace Luau
{

struct InternalErrorReporter;
struct TypeCheckLimits;
struct TypeFunctionRuntimeBuilderState;

struct LuauTempThreadPopper
{
    explicit LuauTempThreadPopper(lua_State* L);
    ~LuauTempThreadPopper();

    lua_State* L = nullptr;
};

using StateRef = std::unique_ptr<lua_State, void (*)(lua_State*)>;

void* typeFunctionAlloc(void* ud, void* ptr, size_t osize, size_t nsize);

struct TypeFunctionPrimitiveType
{
    enum Type
    {
        NilType,
        Boolean,
        Number,
        Integer,
        String,
        Thread,
        Buffer,
        None,
    };

    Type type;

    TypeFunctionPrimitiveType(Type type)
        : type(type)
    {
    }
};

struct TypeFunctionBooleanSingleton
{
    bool value = false;
};

struct TypeFunctionStringSingleton
{
    std::string value;
};

using TypeFunctionSingletonVariant = Variant<TypeFunctionBooleanSingleton, TypeFunctionStringSingleton>;

struct TypeFunctionSingletonType
{
    TypeFunctionSingletonVariant variant;

    explicit TypeFunctionSingletonType(TypeFunctionSingletonVariant variant)
        : variant(std::move(variant))
    {
    }
};

template<typename T>
const T* get(const TypeFunctionSingletonType* tv)
{
    LUAU_ASSERT(tv);

    return tv ? get_if<T>(&tv->variant) : nullptr;
}

template<typename T>
T* getMutable(const TypeFunctionSingletonType* tv)
{
    LUAU_ASSERT(tv);

    return tv ? get_if<T>(&const_cast<TypeFunctionSingletonType*>(tv)->variant) : nullptr;
}

struct TypeFunctionUnionType
{
    std::vector<TypeFunctionTypeId> components;
};

struct TypeFunctionIntersectionType
{
    std::vector<TypeFunctionTypeId> components;
};

struct TypeFunctionAnyType
{
};

struct TypeFunctionUnknownType
{
};

struct TypeFunctionNeverType
{
};

// Luwu: `types.error(message)`. Returning it from a type function reports `message` as a type error where the type function
// is applied; it isn't a type, so it can't appear inside one.
struct TypeFunctionErrorType
{
    std::string message;
};

struct TypeFunctionNegationType
{
    TypeFunctionTypeId type;
};

struct TypeFunctionTypePack
{
    std::vector<TypeFunctionTypeId> head;
    std::optional<TypeFunctionTypePackId> tail;
};

struct TypeFunctionVariadicTypePack
{
    TypeFunctionTypeId type;
};

struct TypeFunctionGenericTypePack
{
    bool isNamed = false;

    std::string name;
};

using TypeFunctionTypePackVariant = Variant<TypeFunctionTypePack, TypeFunctionVariadicTypePack, TypeFunctionGenericTypePack>;

struct TypeFunctionTypePackVar
{
    TypeFunctionTypePackVariant type;

    TypeFunctionTypePackVar(TypeFunctionTypePackVariant type)
        : type(std::move(type))
    {
    }

    bool operator==(const TypeFunctionTypePackVar& rhs) const;
};

struct TypeFunctionFunctionType
{
    std::vector<TypeFunctionTypeId> generics;
    std::vector<TypeFunctionTypePackId> genericPacks;

    TypeFunctionTypePackId argTypes;
    TypeFunctionTypePackId retTypes;

    // Parallel to argTypes.head; nullopt entries mean the parameter has no name.
    std::vector<std::optional<std::string>> argNames;
};

template<typename T>
const T* get(TypeFunctionTypePackId tv)
{
    LUAU_ASSERT(tv);

    return tv ? get_if<T>(&tv->type) : nullptr;
}

template<typename T>
T* getMutable(TypeFunctionTypePackId tv)
{
    LUAU_ASSERT(tv);

    return tv ? get_if<T>(&const_cast<TypeFunctionTypePackVar*>(tv)->type) : nullptr;
}

struct TypeFunctionTableIndexer
{
    TypeFunctionTableIndexer(TypeFunctionTypeId keyType, TypeFunctionTypeId valueType, bool isReadOnly = false)
        : keyType(keyType)
        , valueType(valueType)
        , isReadOnly(isReadOnly)
    {
    }

    TypeFunctionTypeId keyType;
    TypeFunctionTypeId valueType;
    bool isReadOnly = false;
};

struct TypeFunctionMetadataTable;

struct TypeFunctionProperty
{
    static TypeFunctionProperty readonly(TypeFunctionTypeId ty);
    static TypeFunctionProperty writeonly(TypeFunctionTypeId ty);
    static TypeFunctionProperty rw(TypeFunctionTypeId ty);                             // Shared read-write type.
    static TypeFunctionProperty rw(TypeFunctionTypeId read, TypeFunctionTypeId write); // Separate read-write type.

    bool isShared() const;
    bool isReadOnly() const;
    bool isWriteOnly() const;

    std::optional<TypeFunctionTypeId> readTy;
    std::optional<TypeFunctionTypeId> writeTy;

    // Luwu: what the property was documented and deprecated with, which a type function keeps as long as it keeps the
    // property (setting its type included). luwu-lsp finds a property's doc comment through these.
    std::optional<Location> location;
    std::optional<Location> typeLocation;
    std::optional<std::string> documentationSymbol;
    bool deprecated = false;
    std::string deprecatedSuggestion;
    // Luwu doc references: see Property::metadata
    std::shared_ptr<const TypeFunctionMetadataTable> metadata;
};

struct TypeFunctionTableType
{
    using Name = std::string;
    using Props = std::map<Name, TypeFunctionProperty>;

    Props props;

    std::optional<TypeFunctionTableIndexer> indexer;

    // Should always be a TypeFunctionTableType
    std::optional<TypeFunctionTypeId> metatable;
};

struct TypeFunctionExternType
{
    using Name = std::string;
    using Props = std::map<Name, TypeFunctionProperty>;

    Props props;

    std::optional<TypeFunctionTableIndexer> indexer;

    std::optional<TypeFunctionTypeId> metatable; // metaclass?

    std::optional<TypeFunctionTypeId> readParent;
    std::optional<TypeFunctionTypeId> writeParent;

    TypeId externTy;
};

struct TypeFunctionGenericType
{
    bool isNamed = false;
    bool isPack = false;

    std::string name;
};

using TypeFunctionTypeVariant = Luau::Variant<
    TypeFunctionPrimitiveType,
    TypeFunctionAnyType,
    TypeFunctionUnknownType,
    TypeFunctionNeverType,
    TypeFunctionSingletonType,
    TypeFunctionUnionType,
    TypeFunctionIntersectionType,
    TypeFunctionNegationType,
    TypeFunctionFunctionType,
    TypeFunctionTableType,
    TypeFunctionExternType,
    TypeFunctionGenericType,
    TypeFunctionErrorType>;

// Luwu: a type's metadata inside a type function (`type:metadata()`); the runtime's side of TypeMetadataTable.
struct TypeFunctionMetadataTable;
using TypeFunctionMetadataValue = Variant<bool, double, std::string, TypeFunctionTypeId, std::shared_ptr<const TypeFunctionMetadataTable>>;

struct TypeFunctionMetadataTable
{
    std::vector<std::pair<TypeFunctionMetadataValue, TypeFunctionMetadataValue>> entries;
    // Luwu doc references: carried through a type function untouched; `setmetadata` makes a table without one
    std::optional<MetadataDocContext> docContext;
};

struct TypeFunctionType
{
    TypeFunctionTypeVariant type;
    bool frozen = false;

    // Luwu: the type this was serialized from. Deserializing keeps its documentation (definition locations, doc symbol),
    // so a type function that rebuilds a type doesn't strip the docs off it.
    TypeId origin = nullptr;
    // Luwu: a type function changed this type's structure (`setproperty`, `setparameters`, ...). Deserializing gives a type
    // back the alias name its origin printed as (`Pathlike`) only when neither it nor anything it reaches was edited.
    bool edited = false;

    // Luwu: see TypeFunctionMetadataTable
    std::shared_ptr<const TypeFunctionMetadataTable> metadata;

    explicit TypeFunctionType(TypeFunctionTypeVariant type)
        : type(std::move(type))
    {
    }

    bool operator==(const TypeFunctionType& rhs) const;
};

template<typename T>
const T* get(TypeFunctionTypeId tv)
{
    LUAU_ASSERT(tv);

    return tv ? Luau::get_if<T>(&tv->type) : nullptr;
}

template<typename T>
T* getMutable(TypeFunctionTypeId tv)
{
    LUAU_ASSERT(tv);

    return tv ? Luau::get_if<T>(&const_cast<TypeFunctionType*>(tv)->type) : nullptr;
}

struct TypeFunctionRuntime
{
    TypeFunctionRuntime(NotNull<InternalErrorReporter> ice, NotNull<TypeCheckLimits> limits);
    ~TypeFunctionRuntime();

    // Return value is an error message string if registration failed.
    std::optional<std::string> registerFunction_DEPRECATED(AstStatTypeFunction* function);

    // Return value is a structured error if registration failed.
    std::optional<TypeFunctionError> registerFunction(AstStatTypeFunction* function);

    // For user-defined type functions, we store all generated types and packs for the duration of the typecheck
    TypedAllocator<TypeFunctionType> typeArena;
    TypedAllocator<TypeFunctionTypePackVar> typePackArena;

    NotNull<InternalErrorReporter> ice;
    NotNull<TypeCheckLimits> limits;

    StateRef state;

    // Set of functions which have their environment table initialized
    DenseHashSet<AstStatTypeFunction*> initialized{nullptr};

    // Evaluation of type functions should only be performed in the absence of parse errors in the source module
    bool allowEvaluation = true;

    // Root scope in which the type function operates in, set up by ConstraintGenerator
    ScopePtr rootScope;

    // Output created by 'print' function
    std::vector<std::string> messages;

    // Luwu doc references: classes, objects and traits given metadata in the current run. They're shared by every use,
    // so their metadata only applies to the type function's result, and only when it is one of them.
    std::vector<TypeFunctionTypeId> nominalMetadata;

    // Luwu: the class and trait values `types.newclass`/`types.newtrait` made in this module, by key, so a type function
    // used many times makes each one once
    DenseHashMap<std::string, TypeId> generatedNominals{""};

    // Type builder, valid for the duration of a single evaluation
    TypeFunctionRuntimeBuilderState* runtimeBuilder = nullptr;

private:
    void prepareState();
};

std::optional<std::string> checkResultForError_DEPRECATED(lua_State* L, const char* typeFunctionName, int luaResult);
std::optional<TypeFunctionError> checkResultForError(lua_State* L, const char* typeFunctionName, int luaResult);

TypeFunctionRuntime* getTypeFunctionRuntime(lua_State* L);

TypeFunctionType* allocateTypeFunctionType(lua_State* L, TypeFunctionTypeVariant type);
TypeFunctionTypePackVar* allocateTypeFunctionTypePack(lua_State* L, TypeFunctionTypePackVariant type);

void pushType(lua_State* L, TypeFunctionTypeId type);
void pushTypePack(lua_State* L, TypeFunctionTypePackId tp);

void allocTypeUserData(lua_State* L, TypeFunctionTypeVariant type, bool frozen = false);
void allocTypeUserDataCopy(lua_State* L, TypeFunctionTypeId node, bool frozen = false);

bool isTypeUserData(lua_State* L, int idx);
TypeFunctionTypeId getTypeUserData(lua_State* L, int idx);
std::optional<TypeFunctionTypeId> optionalTypeUserData(lua_State* L, int idx);

void registerTypesLibrary(lua_State* L);
void registerTypeUserData(lua_State* L);

void setTypeFunctionEnvironment(lua_State* L);

void resetTypeFunctionState(lua_State* L);

} // namespace Luau
