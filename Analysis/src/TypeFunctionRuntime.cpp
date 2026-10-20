// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/TypeFunctionRuntime.h"

#include "Luau/Allocator.h"
#include "Luau/Common.h"
#include "Luau/Lexer.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/ParseResult.h"
#include "Luau/Compiler.h"
#include "Luau/DenseHash.h"
#include "Luau/StringUtils.h"
#include "Luau/Type.h"
#include "Luau/TypeFunction.h"
#include "Luau/TypeFunctionRuntimeBuilder.h"
#include "Luau/RecursionCounter.h"
#include "Luau/ToString.h"
#include "Luau/TypeFormat.h"
#include "Luau/TypeUtils.h"
#include "Luau/VisitType.h"

#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

LUAU_DYNAMIC_FASTINT(LuauTypeFunctionSerdeIterationLimit)
// Luwu: the most memory the type functions' Lua state may hold, in MiB; 0 for no limit (typeFunctionAlloc)
LUAU_FASTINTVARIABLE(LuwuTypeFunctionMemoryLimitMb, 256)
LUAU_FASTFLAG(LuauIntegerType2)

LUAU_FASTFLAGVARIABLE(LuauTypeFunctionSupportsFrozen)
LUAU_FASTFLAGVARIABLE(LuauTypeFunctionStructuredErrors)
LUAU_FASTFLAGVARIABLE(LuauTypeFunctionSerializeArgNames)
LUAU_FASTFLAGVARIABLE(LuauUdtfTypeIsSubtypeOf)
LUAU_FASTFLAGVARIABLE(LuauTypeFunctionTableIndexerIsReadOnly)
LUAU_FASTFLAGVARIABLE(LuauUdtfCreateSingletonFixErrorMessage)
LUAU_FASTFLAGVARIABLE(LuauUdtfTypeUseTaggedMetatable)
LUAU_FASTFLAGVARIABLE(LuauUdtfTypeToStringMetamethod)

namespace Luau
{

LuauTempThreadPopper::LuauTempThreadPopper(lua_State* L)
    : L(L)
{
}

LuauTempThreadPopper::~LuauTempThreadPopper()
{
    lua_pop(L, 1);
}

static void dummyStateClose(lua_State*) {}

TypeFunctionRuntime::TypeFunctionRuntime(NotNull<InternalErrorReporter> ice, NotNull<TypeCheckLimits> limits)
    : ice(ice)
    , limits(limits)
    , state(nullptr, dummyStateClose)
{
}

TypeFunctionRuntime::~TypeFunctionRuntime() {}

std::optional<std::string> TypeFunctionRuntime::registerFunction_DEPRECATED(AstStatTypeFunction* function)
{
    // If evaluation is disabled, we do not generate additional error messages
    if (!allowEvaluation)
        return std::nullopt;

    // Do not evaluate type functions with parse errors inside
    if (function->hasErrors)
        return std::nullopt;

    prepareState();

    lua_State* global = state.get();

    // Fetch to check if function is already registered
    lua_pushlightuserdata(global, function);
    lua_gettable(global, LUA_REGISTRYINDEX);

    if (!lua_isnil(global, -1))
    {
        lua_pop(global, 1);
        return std::nullopt;
    }

    lua_pop(global, 1);

    AstName name = function->name;

    // Construct ParseResult containing the type function
    Allocator allocator;
    AstNameTable names(allocator);

    AstExpr* exprFunction = function->body;
    AstArray<AstExpr*> exprReturns{&exprFunction, 1};
    AstStatReturn stmtReturn{Location{}, exprReturns, Location{}};
    AstStat* stmtArray[] = {&stmtReturn};
    AstArray<AstStat*> stmts{stmtArray, 1};
    AstStatBlock exec{Location{}, stmts};
    ParseResult parseResult{&exec, 1, {}, {}, {}, CstNodeMap{nullptr}};

    BytecodeBuilder builder;
    try
    {
        compileOrThrow(builder, parseResult, names);
    }
    catch (CompileError& e)
    {
        return format("'%s' type function failed to compile with error message: %s", name.value, e.what());
    }

    std::string bytecode = builder.getBytecode();

    // Separate sandboxed thread for individual execution and private globals
    lua_State* L = lua_newthread(global);
    LuauTempThreadPopper popper(global);

    // Create individual environment for the type function
    luaL_sandboxthread(L);

    // Do not allow global writes to that environment
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    // Load bytecode into Luau state
    if (auto error = checkResultForError_DEPRECATED(L, name.value, luau_load(L, name.value, bytecode.data(), bytecode.size(), 0)))
        return error;

    // Execute the global function which should return our user-defined type function
    if (auto error = checkResultForError_DEPRECATED(L, name.value, lua_resume(L, nullptr, 0)))
        return error;

    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        return format("Could not find '%s' type function in the global scope", name.value);
    }

    // Store resulting function in the registry
    lua_pushlightuserdata(global, function);
    lua_xmove(L, global, 1);
    lua_settable(global, LUA_REGISTRYINDEX);

    return std::nullopt;
}

std::optional<TypeFunctionError> TypeFunctionRuntime::registerFunction(AstStatTypeFunction* function)
{
    // If evaluation is disabled, we do not generate additional error messages
    if (!allowEvaluation)
        return std::nullopt;

    // Do not evaluate type functions with parse errors inside
    if (function->hasErrors)
        return std::nullopt;

    prepareState();

    lua_State* global = state.get();

    // Fetch to check if function is already registered
    lua_pushlightuserdata(global, function);
    lua_gettable(global, LUA_REGISTRYINDEX);

    if (!lua_isnil(global, -1))
    {
        lua_pop(global, 1);
        return std::nullopt;
    }

    lua_pop(global, 1);

    AstName name = function->name;

    // Construct ParseResult containing the type function
    Allocator allocator;
    AstNameTable names(allocator);

    AstExpr* exprFunction = function->body;
    AstArray<AstExpr*> exprReturns{&exprFunction, 1};
    AstStatReturn stmtReturn{Location{}, exprReturns, Location{}};
    AstStat* stmtArray[] = {&stmtReturn};
    AstArray<AstStat*> stmts{stmtArray, 1};
    AstStatBlock exec{Location{}, stmts};
    ParseResult parseResult{&exec, 1, {}, {}, {}, CstNodeMap{nullptr}};

    BytecodeBuilder builder;
    try
    {
        compileOrThrow(builder, parseResult, names);
    }
    catch (CompileError& e)
    {
        return TypeFunctionError{Location{}, FailedToCompile{name.value, e.what()}};
    }

    std::string bytecode = builder.getBytecode();

    // Separate sandboxed thread for individual execution and private globals
    lua_State* L = lua_newthread(global);
    LuauTempThreadPopper popper(global);

    // Create individual environment for the type function
    luaL_sandboxthread(L);

    // Do not allow global writes to that environment
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setreadonly(L, -1, true);
    lua_pop(L, 1);

    // Load bytecode into Luau state
    if (auto error = checkResultForError(L, name.value, luau_load(L, name.value, bytecode.data(), bytecode.size(), 0)))
        return error;

    // Execute the global function which should return our user-defined type function
    if (auto error = checkResultForError(L, name.value, lua_resume(L, nullptr, 0)))
        return error;

    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        return TypeFunctionError{Location{}, TypeFunctionMissing{name.value}};
    }

    // Store resulting function in the registry
    lua_pushlightuserdata(global, function);
    lua_xmove(L, global, 1);
    lua_settable(global, LUA_REGISTRYINDEX);

    return std::nullopt;
}

void TypeFunctionRuntime::prepareState()
{
    if (state)
        return;

    state = StateRef(lua_newstate(typeFunctionAlloc, this), lua_close);
    lua_State* L = state.get();

    lua_setthreaddata(L, this);

    setTypeFunctionEnvironment(L);

    registerTypeUserData(L);

    registerTypesLibrary(L);

    luaL_sandbox(L);
    luaL_sandboxthread(L);
}

constexpr int kTypeUserdataTag = 42;

void* typeFunctionAlloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    // Luwu: the type functions' heap is capped, so one that allocates without end (`while true do t[#t + 1] = {} end`,
    // easy to have half-typed in an editor) fails with "not enough memory" instead of taking the machine's memory.
    // Upstream doesn't limit it. Refusing leaves `ptr` as it was, which Lua expects.
    TypeFunctionRuntime* runtime = static_cast<TypeFunctionRuntime*>(ud);
    size_t limit = size_t(std::max(FInt::LuwuTypeFunctionMemoryLimitMb.value, 0)) << 20;
    if (runtime && limit > 0 && nsize > osize && runtime->allocatedBytes + (nsize - osize) > limit)
        return nullptr;

    if (runtime)
        runtime->allocatedBytes = runtime->allocatedBytes + nsize - osize;

    if (nsize == 0)
    {
        ::operator delete(ptr);
        return nullptr;
    }
    else if (osize == 0)
    {
        return ::operator new(nsize);
    }
    else
    {
        void* data = ::operator new(nsize);
        memcpy(data, ptr, nsize < osize ? nsize : osize);

        ::operator delete(ptr);

        return data;
    }
}

std::optional<std::string> checkResultForError_DEPRECATED(lua_State* L, const char* typeFunctionName, int luaResult)
{
    switch (luaResult)
    {
    case LUA_OK:
        return std::nullopt;
    case LUA_YIELD:
    case LUA_BREAK:
        return format("'%s' type function errored: unexpected yield or break", typeFunctionName);
    default:
        if (!lua_gettop(L))
            return format("'%s' type function errored unexpectedly", typeFunctionName);

        if (lua_isstring(L, -1))
            return format("'%s' type function errored at runtime: %s", typeFunctionName, lua_tostring(L, -1));

        return format("'%s' type function errored at runtime: raised an error of type %s", typeFunctionName, lua_typename(L, -1));
    }
}

std::optional<TypeFunctionError> checkResultForError(lua_State* L, const char* typeFunctionName, int luaResult)
{
    switch (luaResult)
    {
    case LUA_OK:
        return std::nullopt;
    case LUA_YIELD:
    case LUA_BREAK:
        return TypeFunctionError{Location{}, RuntimeError{format("'%s' type function errored: unexpected yield or break", typeFunctionName)}};
    default:
        if (!lua_gettop(L))
            return TypeFunctionError{Location{}, RuntimeError{format("'%s' type function errored unexpectedly", typeFunctionName)}};

        if (lua_isstring(L, -1))
            return TypeFunctionError{
                Location{}, RuntimeError{format("'%s' type function errored at runtime: %s", typeFunctionName, lua_tostring(L, -1))}
            };

        return TypeFunctionError{
            Location{},
            RuntimeError{format("'%s' type function errored at runtime: raised an error of type %s", typeFunctionName, lua_typename(L, -1))}
        };
    }
}

TypeFunctionRuntime* getTypeFunctionRuntime(lua_State* L)
{
    return static_cast<TypeFunctionRuntime*>(lua_getthreaddata(lua_mainthread(L)));
}

TypeFunctionType* allocateTypeFunctionType(lua_State* L, TypeFunctionTypeVariant type)
{
    auto ctx = getTypeFunctionRuntime(L);
    return ctx->typeArena.allocate(std::move(type));
}

TypeFunctionTypePackVar* allocateTypeFunctionTypePack(lua_State* L, TypeFunctionTypePackVariant type)
{
    auto ctx = getTypeFunctionRuntime(L);
    return ctx->typePackArena.allocate(std::move(type));
}

void pushType(lua_State* L, TypeFunctionTypeId type)
{
    luaL_checkstack(L, 2, "allocating type");

    if (FFlag::LuauUdtfTypeUseTaggedMetatable)
    {
        TypeFunctionTypeId* ptr = static_cast<TypeFunctionTypeId*>(lua_newuserdatataggedwithmetatable(L, sizeof(TypeFunctionTypeId), kTypeUserdataTag));
        *ptr = type;
    }
    else
    {
        TypeFunctionTypeId* ptr = static_cast<TypeFunctionTypeId*>(lua_newuserdatatagged(L, sizeof(TypeFunctionTypeId), kTypeUserdataTag));
        *ptr = type;

        // set the new userdata's metatable to type metatable
        luaL_getmetatable(L, "type");
        lua_setmetatable(L, -2);
    }
}

// Pushes a new type userdata onto the stack
void allocTypeUserData(lua_State* L, TypeFunctionTypeVariant type, bool frozen)
{
    luaL_checkstack(L, 2, "allocating type");

    // allocate a new type userdata
    if (FFlag::LuauUdtfTypeUseTaggedMetatable)
    {
        TypeFunctionTypeId* ptr = static_cast<TypeFunctionTypeId*>(lua_newuserdatataggedwithmetatable(L, sizeof(TypeFunctionTypeId), kTypeUserdataTag));
        *ptr = allocateTypeFunctionType(L, std::move(type));
        const_cast<TypeFunctionType*>(*ptr)->frozen = frozen;
    }
    else
    {
        TypeFunctionTypeId* ptr = static_cast<TypeFunctionTypeId*>(lua_newuserdatatagged(L, sizeof(TypeFunctionTypeId), kTypeUserdataTag));
        *ptr = allocateTypeFunctionType(L, std::move(type));
        const_cast<TypeFunctionType*>(*ptr)->frozen = frozen;

        // set the new userdata's metatable to type metatable
        luaL_getmetatable(L, "type");
        lua_setmetatable(L, -2);
    }
}

// Luwu: a new type userdata holding a copy of `node`, its origin and metadata included (allocTypeUserData copies only
// the type itself)
void allocTypeUserDataCopy(lua_State* L, TypeFunctionTypeId node, bool frozen)
{
    allocTypeUserData(L, node->type, frozen);

    TypeFunctionType* pushed = const_cast<TypeFunctionType*>(getTypeUserData(L, -1));
    pushed->origin = node->origin;
    pushed->metadata = node->metadata;
    pushed->edited = node->edited;
}

void deallocTypeUserData(lua_State* L, void* data)
{
    // only non-owning pointers into an arena is stored
}

bool isTypeUserData(lua_State* L, int idx)
{
    if (!FFlag::LuauUdtfTypeUseTaggedMetatable && !lua_isuserdata(L, idx))
        return false;

    return lua_touserdatatagged(L, idx, kTypeUserdataTag) != nullptr;
}

TypeFunctionTypeId getTypeUserData(lua_State* L, int idx)
{
    if (FFlag::LuauUdtfTypeUseTaggedMetatable)
    {
        return *static_cast<TypeFunctionTypeId*>(luaL_checkudatatagged(L, idx, kTypeUserdataTag));
    }
    else
    {
        if (auto typ = static_cast<TypeFunctionTypeId*>(lua_touserdatatagged(L, idx, kTypeUserdataTag)))
            return *typ;

        luaL_typeerrorL(L, idx, "type");
    }
}

std::optional<TypeFunctionTypeId> optionalTypeUserData(lua_State* L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return std::nullopt;
    else
        return getTypeUserData(L, idx);
}

// Luwu Classes (rfcs/classes): an extern type node is a class's object type (`Cat`, or a trait's, `Greeter`: an object
// implementing a trait is an object), a class value (`class<Cat>`), a trait value (`trait<Greeter>`), or a host's
// extern type
enum class NominalKind
{
    Extern,
    Object,
    Class,
    Trait,
};

static NominalKind nominalKind(lua_State* L, const TypeFunctionExternType* nominal)
{
    TypeFunctionRuntimeBuilderState* builder = getTypeFunctionRuntime(L)->runtimeBuilder;
    const ExternType* et = get<ExternType>(follow(nominal->externTy));
    if (!builder || !et || !et->root)
        return NominalKind::Extern;

    TypeId root = follow(*et->root);
    NotNull<BuiltinTypes> builtins = builder->ctx->builtins;
    if (root == builtins->objectType)
        return NominalKind::Object;
    if (root == builtins->classType)
        return NominalKind::Class;
    if (root == builtins->traitType)
        return NominalKind::Trait;
    return NominalKind::Extern;
}

// returns a string tag of TypeFunctionTypeId
static std::string getTag(lua_State* L, TypeFunctionTypeId ty)
{
    if (auto n = get<TypeFunctionPrimitiveType>(ty); n && n->type == TypeFunctionPrimitiveType::Type::NilType)
        return "nil";
    else if (auto b = get<TypeFunctionPrimitiveType>(ty); b && b->type == TypeFunctionPrimitiveType::Type::Boolean)
        return "boolean";
    else if (auto n = get<TypeFunctionPrimitiveType>(ty); n && n->type == TypeFunctionPrimitiveType::Type::Number)
        return "number";
    else if (auto n = get<TypeFunctionPrimitiveType>(ty); n && (FFlag::LuauIntegerType2 && (n->type == TypeFunctionPrimitiveType::Type::Integer)))
        return "integer";
    else if (auto s = get<TypeFunctionPrimitiveType>(ty); s && s->type == TypeFunctionPrimitiveType::Type::String)
        return "string";
    else if (auto s = get<TypeFunctionPrimitiveType>(ty); s && s->type == TypeFunctionPrimitiveType::Type::Thread)
        return "thread";
    else if (auto s = get<TypeFunctionPrimitiveType>(ty); s && s->type == TypeFunctionPrimitiveType::Type::Buffer)
        return "buffer";
    else if (auto s = get<TypeFunctionPrimitiveType>(ty); s && s->type == TypeFunctionPrimitiveType::Type::None)
        return "none";
    else if (get<TypeFunctionUnknownType>(ty))
        return "unknown";
    else if (get<TypeFunctionNeverType>(ty))
        return "never";
    else if (get<TypeFunctionAnyType>(ty))
        return "any";
    else if (get<TypeFunctionSingletonType>(ty))
        return "singleton";
    else if (get<TypeFunctionNegationType>(ty))
        return "negation";
    else if (get<TypeFunctionUnionType>(ty))
        return "union";
    else if (get<TypeFunctionIntersectionType>(ty))
        return "intersection";
    else if (get<TypeFunctionTableType>(ty))
        return "table";
    else if (get<TypeFunctionFunctionType>(ty))
        return "function";
    else if (const TypeFunctionExternType* nominal = get<TypeFunctionExternType>(ty))
    {
        switch (nominalKind(L, nominal))
        {
        case NominalKind::Object:
            return "object";
        case NominalKind::Class:
            return "class";
        case NominalKind::Trait:
            return "trait";
        case NominalKind::Extern:
            return "extern";
        }
    }
    else if (get<TypeFunctionGenericType>(ty))
        return "generic";
    else if (get<TypeFunctionErrorType>(ty))
        return "error";

    LUAU_ASSERT(!"Unsupported type in getTag");
    luaL_error(L, "VM encountered unexpected type variant when determining tag");
}

// Luau: `type.unknown`
// Returns the type instance representing unknown
static int createUnknown(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionUnknownType{});

    return 1;
}

// Luau: `type.never`
// Returns the type instance representing never
static int createNever(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionNeverType{});

    return 1;
}

// Luwu: `types.error(message: string)`
// Returned from a type function, reports `message` as a type error where the type function is applied
static int createError(lua_State* L)
{
    size_t length = 0;
    const char* message = luaL_checklstring(L, 1, &length);
    allocTypeUserData(L, TypeFunctionErrorType{std::string(message, length)});

    return 1;
}

// Luau: `type.any`
// Returns the type instance representing any
static int createAny(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionAnyType{});

    return 1;
}

// Luau: `type.boolean`
// Returns the type instance representing boolean
static int createBoolean(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionPrimitiveType{TypeFunctionPrimitiveType::Boolean});

    return 1;
}

// Luau: `type.number`
// Returns the type instance representing number
static int createNumber(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionPrimitiveType{TypeFunctionPrimitiveType::Number});

    return 1;
}

// Luau: `type.string`
// Returns the type instance representing string
static int createString(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionPrimitiveType{TypeFunctionPrimitiveType::String});

    return 1;
}

// Luau: `type.thread`
static int createThread(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionPrimitiveType{TypeFunctionPrimitiveType::Thread});

    return 1;
}

// Luau: `type.buffer`
static int createBuffer(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionPrimitiveType{TypeFunctionPrimitiveType::Buffer});

    return 1;
}

// Luau: `type.none`
static int createNone(lua_State* L)
{
    allocTypeUserData(L, TypeFunctionPrimitiveType{TypeFunctionPrimitiveType::None});

    return 1;
}

// Luau: `type.singleton(value: string | boolean | nil) -> type`
// Returns the type instance representing string or boolean singleton or nil
static int createSingleton(lua_State* L)
{
    if (lua_isboolean(L, 1)) // Create boolean singleton
    {
        bool value = luaL_checkboolean(L, 1);
        allocTypeUserData(L, TypeFunctionSingletonType{TypeFunctionBooleanSingleton{value}});

        return 1;
    }

    // n.b. we cannot use lua_isstring here because lua committed the cardinal sin of calling a number a string
    if (lua_type(L, 1) == LUA_TSTRING) // Create string singleton
    {
        const char* value = luaL_checkstring(L, 1);
        allocTypeUserData(L, TypeFunctionSingletonType{TypeFunctionStringSingleton{value}});

        return 1;
    }

    if (lua_isnil(L, 1))
    {
        allocTypeUserData(L, TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::NilType));

        return 1;
    }

    if (FFlag::LuauUdtfCreateSingletonFixErrorMessage)
        luaL_error(L, "types.singleton: can't create a singleton from a %s", luaL_typename(L, 1));
    else
        luaL_error(L, "types.singleton: can't create singleton from `%s` type", lua_typename(L, 1));
}

// Luau: `types.generic(name: string, ispack: boolean?) -> type
// Create a generic type with the specified type. If an optional boolean is set to true, result is a generic pack
static int createGeneric(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    bool isPack = luaL_optboolean(L, 2, false);

    if (strlen(name) == 0)
        luaL_error(L, "types.generic: generic name cannot be empty");

    allocTypeUserData(L, TypeFunctionGenericType{/* isNamed */ true, isPack, name});
    return 1;
}

// Luau: `self:value() -> type`
// Returns the value of a singleton
static int getSingletonValue(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.value: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (auto tfpt = get<TypeFunctionPrimitiveType>(self))
    {
        if (tfpt->type != TypeFunctionPrimitiveType::NilType)
            luaL_error(L, "type.value: expected self to be a singleton, but got %s instead", getTag(L, self).c_str());

        lua_pushnil(L);
        return 1;
    }

    auto tfst = get<TypeFunctionSingletonType>(self);
    if (!tfst)
        luaL_error(L, "type.value: expected self to be a singleton, but got %s instead", getTag(L, self).c_str());

    if (auto tfbst = get<TypeFunctionBooleanSingleton>(tfst))
    {
        lua_pushboolean(L, tfbst->value);
        return 1;
    }

    if (auto tfsst = get<TypeFunctionStringSingleton>(tfst))
    {
        lua_pushlstring(L, tfsst->value.c_str(), tfsst->value.length());
        return 1;
    }

    luaL_error(L, "type.value: can't call `value` method on `%s` type", getTag(L, self).c_str());
}

// Luau: `types.optional(ty: type) -> type`
// Returns the type instance representing an optional version of `ty`.
// If `ty` is a union, this adds `nil` to the components of the union.
// Otherwise, makes a union of the two things.
static int createOptional(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "types.optional: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId argument = getTypeUserData(L, 1);

    std::vector<TypeFunctionTypeId> components;

    if (auto unionTy = get<TypeFunctionUnionType>(argument))
    {
        components.reserve(unionTy->components.size() + 1);

        components.insert(components.begin(), unionTy->components.begin(), unionTy->components.end());
    }
    else
        components.emplace_back(argument);

    components.emplace_back(allocateTypeFunctionType(L, TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::NilType)));

    allocTypeUserData(L, TypeFunctionUnionType{std::move(components)});

    return 1;
}

// Luau: `types.unionof(...: type) -> type`
// Returns the type instance representing union
static int createUnion(lua_State* L)
{
    // get the number of arguments for union
    int argSize = lua_gettop(L);

    std::vector<TypeFunctionTypeId> components;
    components.reserve(argSize);

    for (int i = 1; i <= argSize; i++)
    {
        TypeFunctionTypeId component = getTypeUserData(L, i);

        if (auto unionComponent = get<TypeFunctionUnionType>(component))
            components.insert(components.end(), unionComponent->components.begin(), unionComponent->components.end());
        else if (get<TypeFunctionNeverType>(component))
            continue;
        else
            components.push_back(component);
    }

    if (components.size() == 0)
        allocTypeUserData(L, TypeFunctionNeverType{});
    else if (components.size() == 1)
        pushType(L, components[0]);
    else
        allocTypeUserData(L, TypeFunctionUnionType{std::move(components)});

    return 1;
}

// Luau: `types.intersectionof(...: type) -> type`
// Returns the type instance representing intersection
static int createIntersection(lua_State* L)
{
    // get the number of arguments for intersection
    int argSize = lua_gettop(L);

    std::vector<TypeFunctionTypeId> components;
    components.reserve(argSize);

    for (int i = 1; i <= argSize; i++)
    {
        TypeFunctionTypeId component = getTypeUserData(L, i);

        if (auto intersectionComponent = get<TypeFunctionIntersectionType>(component))
            components.insert(components.end(), intersectionComponent->components.begin(), intersectionComponent->components.end());
        else if (get<TypeFunctionUnknownType>(component))
            continue;
        else
            components.push_back(component);
    }

    if (components.size() == 0)
        allocTypeUserData(L, TypeFunctionUnknownType{});
    else if (components.size() == 1)
        pushType(L, components[0]);
    else
        allocTypeUserData(L, TypeFunctionIntersectionType{std::move(components)});

    return 1;
}

// Luau: `self:components() -> {type}`
// Returns the components of union or intersection
static int getComponents(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.components: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfut = get<TypeFunctionUnionType>(self);
    if (tfut)
    {
        int argSize = int(tfut->components.size());

        lua_createtable(L, argSize, 0);
        for (int i = 0; i < argSize; i++)
        {
            TypeFunctionTypeId component = tfut->components[i];
            allocTypeUserDataCopy(L, component);
            lua_rawseti(L, -2, i + 1); // Luau is 1-indexed while C++ is 0-indexed
        }

        return 1;
    }

    auto tfit = get<TypeFunctionIntersectionType>(self);
    if (tfit)
    {
        int argSize = int(tfit->components.size());

        lua_createtable(L, argSize, 0);
        for (int i = 0; i < argSize; i++)
        {
            TypeFunctionTypeId component = tfit->components[i];
            allocTypeUserDataCopy(L, component);
            lua_rawseti(L, -2, i + 1); // Luau is 1-indexed while C++ is 0-indexed
        }

        return 1;
    }

    luaL_error(L, "type.components: cannot call components of `%s` type", getTag(L, self).c_str());
}

// Luau: `types.negationof(arg: type) -> type`
// Returns the type instance representing negation
static int createNegation(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "types.negationof: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId arg = getTypeUserData(L, 1);
    if (get<TypeFunctionTableType>(arg) || get<TypeFunctionFunctionType>(arg))
        luaL_error(L, "types.negationof: cannot perform negation on `%s` type", getTag(L, arg).c_str());

    allocTypeUserData(L, TypeFunctionNegationType{arg});

    return 1;
}

// Luau: `self:inner() -> type`
// Returns the type instance being negated
static int getNegatedValue(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.inner: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);

    if (auto tfnt = get<TypeFunctionNegationType>(self); tfnt)
        allocTypeUserDataCopy(L, tfnt->type);
    else
        luaL_error(L, "type.inner: cannot call inner method on non-negation type: `%s` type", getTag(L, self).c_str());

    return 1;
}

// Luau: `types.newtable(props: {[type]: type | { read: type, write: type }}?, indexer: {index: type, readresult: type, writeresult: type}?,
// metatable: type?) -> type` Returns the type instance representing table
static int createTable(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount > 3)
        luaL_error(L, "types.newtable: expected 0-3 arguments, but got %d", argumentCount);

    // Parse prop
    TypeFunctionTableType::Props props{};
    if (lua_istable(L, 1))
    {
        lua_pushnil(L);
        while (lua_next(L, 1) != 0)
        {
            TypeFunctionTypeId key = getTypeUserData(L, -2);

            auto tfst = get<TypeFunctionSingletonType>(key);
            if (!tfst)
                luaL_error(L, "types.newtable: expected to be given a singleton type, but got %s instead", getTag(L, key).c_str());

            auto tfsst = get<TypeFunctionStringSingleton>(tfst);
            if (!tfsst)
                luaL_error(L, "types.newtable: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "read");
                std::optional<TypeFunctionTypeId> readTy;
                if (!lua_isnil(L, -1))
                    readTy = getTypeUserData(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "write");
                std::optional<TypeFunctionTypeId> writeTy;
                if (!lua_isnil(L, -1))
                    writeTy = getTypeUserData(L, -1);
                lua_pop(L, 1);

                props[tfsst->value] = TypeFunctionProperty{readTy, writeTy};
            }
            else
            {
                TypeFunctionTypeId value = getTypeUserData(L, -1);
                props[tfsst->value] = TypeFunctionProperty::rw(value);
            }

            lua_pop(L, 1);
        }
    }
    else if (!lua_isnoneornil(L, 1))
        luaL_typeerrorL(L, 1, "table");

    // Parse indexer
    std::optional<TypeFunctionTableIndexer> indexer;
    if (lua_istable(L, 2))
    {
        // Parse keyType and valueType
        lua_getfield(L, 2, "index");
        TypeFunctionTypeId keyType = getTypeUserData(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "readresult");
        TypeFunctionTypeId valueType = getTypeUserData(L, -1);
        lua_pop(L, 1);

        indexer = TypeFunctionTableIndexer(keyType, valueType);
    }
    else if (!lua_isnoneornil(L, 2))
        luaL_typeerrorL(L, 2, "table");

    // Parse metatable
    std::optional<TypeFunctionTypeId> metatable = optionalTypeUserData(L, 3);
    if (metatable && !get<TypeFunctionTableType>(*metatable))
        luaL_error(L, "types.newtable: expected to be given a table type as a metatable, but got %s instead", getTag(L, *metatable).c_str());

    allocTypeUserData(L, TypeFunctionTableType{std::move(props), indexer, metatable});
    return 1;
}

// Luwu: a type function changed `self`, so it no longer is the alias it was serialized from (TypeFunctionType::edited)
static void markEdited(TypeFunctionTypeId self)
{
    const_cast<TypeFunctionType*>(self)->edited = true;
}

// Luau: `self:setproperty(key: type, value: type?)`
// Sets the properties of a table
static int setTableProp(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 2 || argumentCount > 3)
        luaL_error(L, "type.setproperty: expected 2-3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = getMutable<TypeFunctionTableType>(self);
    markEdited(self);
    if (!tftt)
        luaL_error(L, "type.setproperty: expected self to be a table, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setproperty: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    auto tfst = get<TypeFunctionSingletonType>(key);
    if (!tfst)
        luaL_error(L, "type.setproperty: expected to be given a singleton type, but got %s instead", getTag(L, key).c_str());

    auto tfsst = get<TypeFunctionStringSingleton>(tfst);
    if (!tfsst)
        luaL_error(L, "type.setproperty: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

    if (argumentCount == 2 || lua_isnil(L, 3))
    {
        tftt->props.erase(tfsst->value);
        return 0;
    }

    // Luwu: setting a property's type keeps what it's documented with
    TypeFunctionTypeId value = getTypeUserData(L, 3);
    TypeFunctionProperty& prop = tftt->props[tfsst->value];
    prop.readTy = value;
    prop.writeTy = value;

    return 0;
}

// Luau: `self:setreadproperty(key: type, value: type?)`
// Sets the properties of a table
static int setReadTableProp(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 2 || argumentCount > 3)
        luaL_error(L, "type.setreadproperty: expected 2-3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = getMutable<TypeFunctionTableType>(self);
    markEdited(self);
    if (!tftt)
        luaL_error(L, "type.setreadproperty: expected self to be a table, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setreadproperty: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    auto tfst = get<TypeFunctionSingletonType>(key);
    if (!tfst)
        luaL_error(L, "type.setreadproperty: expected to be given a singleton type, but got %s instead", getTag(L, key).c_str());

    auto tfsst = get<TypeFunctionStringSingleton>(tfst);
    if (!tfsst)
        luaL_error(L, "type.setreadproperty: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

    auto iter = tftt->props.find(tfsst->value);

    if (argumentCount == 2 || lua_isnil(L, 3))
    {
        // if it's read-only, remove it altogether
        if (iter != tftt->props.end() && iter->second.isReadOnly())
            tftt->props.erase(tfsst->value);
        // but if it's not, just null out the read type.
        else if (iter != tftt->props.end())
            iter->second.readTy = std::nullopt;

        return 0;
    }

    TypeFunctionTypeId value = getTypeUserData(L, 3);
    if (iter == tftt->props.end())
        tftt->props[tfsst->value] = TypeFunctionProperty::readonly(value);
    else
        iter->second.readTy = value;

    return 0;
}

// Luau: `self:setwriteproperty(key: type, value: type?)`
// Sets the properties of a table
static int setWriteTableProp(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 2 || argumentCount > 3)
        luaL_error(L, "type.setwriteproperty: expected 2-3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = getMutable<TypeFunctionTableType>(self);
    markEdited(self);
    if (!tftt)
        luaL_error(L, "type.setwriteproperty: expected self to be a table, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setwriteproperty: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    auto tfst = get<TypeFunctionSingletonType>(key);
    if (!tfst)
        luaL_error(L, "type.setwriteproperty: expected to be given a singleton type, but got %s instead", getTag(L, key).c_str());

    auto tfsst = get<TypeFunctionStringSingleton>(tfst);
    if (!tfsst)
        luaL_error(L, "type.setwriteproperty: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

    auto iter = tftt->props.find(tfsst->value);

    if (argumentCount == 2 || lua_isnil(L, 3))
    {
        // if it's write-only, remove it altogether
        if (iter != tftt->props.end() && iter->second.isWriteOnly())
            tftt->props.erase(tfsst->value);
        // but if it's not, just null out the write type.
        else if (iter != tftt->props.end())
            iter->second.writeTy = std::nullopt;

        return 0;
    }

    TypeFunctionTypeId value = getTypeUserData(L, 3);
    if (iter == tftt->props.end())
        tftt->props[tfsst->value] = TypeFunctionProperty::writeonly(value);
    else
        iter->second.writeTy = value;

    return 0;
}

// Luau: `self:readproperty(key: type) -> type`
// Returns the property of a table associated with the key
static int readTableProp(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 2)
        luaL_error(L, "type.readproperty: expected 2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = get<TypeFunctionTableType>(self);
    if (!tftt)
        luaL_error(L, "type.readproperty: expected self to be either a table, but got %s instead", getTag(L, self).c_str());

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    auto tfst = get<TypeFunctionSingletonType>(key);
    if (!tfst)
        luaL_error(L, "type.readproperty: expected to be given a singleton type, but got %s instead", getTag(L, key).c_str());

    auto tfsst = get<TypeFunctionStringSingleton>(tfst);
    if (!tfsst)
        luaL_error(L, "type.readproperty: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

    // Check if key is a valid prop
    if (tftt->props.find(tfsst->value) == tftt->props.end())
    {
        lua_pushnil(L);
        return 1;
    }

    auto prop = tftt->props.at(tfsst->value);
    if (prop.readTy)
        allocTypeUserDataCopy(L, *prop.readTy);
    else
        lua_pushnil(L);

    return 1;
}
//
// Luau: `self:writeproperty(key: type) -> type`
// Returns the property of a table associated with the key
static int writeTableProp(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 2)
        luaL_error(L, "type.writeproperty: expected 2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = get<TypeFunctionTableType>(self);
    if (!tftt)
        luaL_error(L, "type.writeproperty: expected self to be either a table, but got %s instead", getTag(L, self).c_str());

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    auto tfst = get<TypeFunctionSingletonType>(key);
    if (!tfst)
        luaL_error(L, "type.writeproperty: expected to be given a singleton type, but got %s instead", getTag(L, key).c_str());

    auto tfsst = get<TypeFunctionStringSingleton>(tfst);
    if (!tfsst)
        luaL_error(L, "type.writeproperty: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

    // Check if key is a valid prop
    if (tftt->props.find(tfsst->value) == tftt->props.end())
    {
        lua_pushnil(L);
        return 1;
    }

    auto prop = tftt->props.at(tfsst->value);
    if (prop.writeTy)
        allocTypeUserDataCopy(L, *prop.writeTy);
    else
        lua_pushnil(L);

    return 1;
}

// Luau: `self:setindexer(key: type, value: type)`
// Sets the indexer of the table, if the key type is `never`, the indexer is removed
static int setTableIndexer(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 3)
        luaL_error(L, "type.setindexer: expected 3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = getMutable<TypeFunctionTableType>(self);
    markEdited(self);
    if (!tftt)
        luaL_error(L, "type.setindexer: expected self to be either a table, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setindexer: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    TypeFunctionTypeId value = getTypeUserData(L, 3);

    if (get<TypeFunctionNeverType>(key))
    {
        tftt->indexer = std::nullopt;
        return 0;
    }

    tftt->indexer = TypeFunctionTableIndexer{key, value};
    return 0;
}

// Luau: `self:setreadindexer(key: type, value: type)`
// Sets the read indexer of the table
static int setTableReadIndexer(lua_State* L)
{
    luaL_error(L, "type.setreadindexer: luau does not yet support separate read/write types for indexers.");
}

// Luau: `self:setwriteindexer(key: type, value: type)`
// Sets the write indexer of the table
static int setTableWriteIndexer(lua_State* L)
{
    luaL_error(L, "type.setwriteindexer: luau does not yet support separate read/write types for indexers.");
}

// Luau: `self:setmetatable(arg: type)`
// Sets the metatable of the table
static int setTableMetatable(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 2)
        luaL_error(L, "type.setmetatable: expected 2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);

    auto tftt = getMutable<TypeFunctionTableType>(self);
    markEdited(self);
    if (!tftt)
        luaL_error(L, "type.setmetatable: expected self to be a table, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setmetatable: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    TypeFunctionTypeId arg = getTypeUserData(L, 2);
    if (!get<TypeFunctionTableType>(arg))
    {
        luaL_error(L, "type.setmetatable: expected the argument to be a table, but got %s instead", getTag(L, arg).c_str());
    }

    tftt->metatable = arg;

    return 0;
}

static std::tuple<std::vector<TypeFunctionTypeId>, std::vector<TypeFunctionTypePackId>> getGenerics(lua_State* L, int idx, const char* fname)
{
    std::vector<TypeFunctionTypeId> types;
    std::vector<TypeFunctionTypePackId> packs;

    if (lua_istable(L, idx))
    {
        lua_pushvalue(L, idx);

        for (int i = 1; i <= lua_objlen(L, -1); i++)
        {
            lua_pushinteger(L, i);
            lua_gettable(L, -2);

            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                break;
            }

            TypeFunctionTypeId ty = getTypeUserData(L, -1);

            if (auto gty = get<TypeFunctionGenericType>(ty))
            {
                if (gty->isPack)
                {
                    packs.push_back(allocateTypeFunctionTypePack(L, TypeFunctionGenericTypePack{gty->isNamed, gty->name}));
                }
                else
                {
                    if (!packs.empty())
                        luaL_error(L, "%s: generic type cannot follow a generic pack", fname);

                    types.push_back(ty);
                }
            }
            else
            {
                luaL_error(L, "%s: table member was not a generic type", fname);
            }

            lua_pop(L, 1);
        }

        lua_pop(L, 1);
    }
    else if (!lua_isnoneornil(L, idx))
    {
        luaL_typeerrorL(L, idx, "table");
    }

    return {types, packs};
}

static TypeFunctionTypePackId getTypePack(lua_State* L, int headIdx, int tailIdx)
{
    TypeFunctionTypePackId result = allocateTypeFunctionTypePack(L, TypeFunctionTypePack{});

    std::vector<TypeFunctionTypeId> head;

    if (lua_istable(L, headIdx))
    {
        lua_pushvalue(L, headIdx);

        for (int i = 1; i <= lua_objlen(L, -1); i++)
        {
            lua_pushinteger(L, i);
            lua_gettable(L, -2);

            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                break;
            }

            head.push_back(getTypeUserData(L, -1));
            lua_pop(L, 1);
        }

        lua_pop(L, 1);
    }

    std::optional<TypeFunctionTypePackId> tail;

    if (auto type = optionalTypeUserData(L, tailIdx))
    {
        if (auto gty = get<TypeFunctionGenericType>(*type); gty && gty->isPack)
            tail = allocateTypeFunctionTypePack(L, TypeFunctionGenericTypePack{gty->isNamed, gty->name});
        else
            tail = allocateTypeFunctionTypePack(L, TypeFunctionVariadicTypePack{*type});
    }

    if (head.size() == 0 && tail.has_value())
        result = *tail;
    else
        result = allocateTypeFunctionTypePack(L, TypeFunctionTypePack{std::move(head), tail});

    return result;
}

void pushTypePack(lua_State* L, TypeFunctionTypePackId tp)
{
    if (auto tftp = get<TypeFunctionTypePack>(tp))
    {
        lua_createtable(L, 0, 2);

        if (!tftp->head.empty())
        {
            lua_createtable(L, int(tftp->head.size()), 0);
            int pos = 1;

            for (auto el : tftp->head)
            {
                allocTypeUserDataCopy(L, el);
                lua_rawseti(L, -2, pos++);
            }

            lua_setfield(L, -2, "head");
        }

        if (tftp->tail.has_value())
        {
            if (auto tfvp = get<TypeFunctionVariadicTypePack>(*tftp->tail))
                allocTypeUserDataCopy(L, tfvp->type);
            else if (auto tfgp = get<TypeFunctionGenericTypePack>(*tftp->tail))
                allocTypeUserData(L, TypeFunctionGenericType{tfgp->isNamed, true, tfgp->name});
            else
                luaL_error(L, "unsupported type pack type");

            lua_setfield(L, -2, "tail");
        }
    }
    else if (auto tfvp = get<TypeFunctionVariadicTypePack>(tp))
    {
        lua_createtable(L, 0, 1);

        allocTypeUserDataCopy(L, tfvp->type);
        lua_setfield(L, -2, "tail");
    }
    else if (auto tfgp = get<TypeFunctionGenericTypePack>(tp))
    {
        lua_createtable(L, 0, 1);

        allocTypeUserData(L, TypeFunctionGenericType{tfgp->isNamed, true, tfgp->name});
        lua_setfield(L, -2, "tail");
    }
    else
    {
        luaL_error(L, "unsupported type pack type");
    }
}

// Luau: `types.newfunction(parameters: {head: {type}?, tail: type?}, returns: {head: {type}?, tail: type?}, generics: {type}?) -> type`
// Returns the type instance representing a function
static int createFunction(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount > 3)
        luaL_error(L, "types.newfunction: expected 0-3 arguments, but got %d", argumentCount);

    TypeFunctionTypePackId argTypes = nullptr;

    if (lua_istable(L, 1))
    {
        lua_getfield(L, 1, "head");
        lua_getfield(L, 1, "tail");

        argTypes = getTypePack(L, -2, -1);

        lua_pop(L, 2);
    }
    else if (!lua_isnoneornil(L, 1))
    {
        luaL_typeerrorL(L, 1, "table");
    }
    else
    {
        argTypes = allocateTypeFunctionTypePack(L, TypeFunctionTypePack{});
    }

    TypeFunctionTypePackId retTypes = nullptr;

    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "head");
        lua_getfield(L, 2, "tail");

        retTypes = getTypePack(L, -2, -1);

        lua_pop(L, 2);
    }
    else if (!lua_isnoneornil(L, 2))
    {
        luaL_typeerrorL(L, 2, "table");
    }
    else
    {
        retTypes = allocateTypeFunctionTypePack(L, TypeFunctionTypePack{});
    }

    auto [genericTypes, genericPacks] = getGenerics(L, 3, "types.newfunction");

    allocTypeUserData(L, TypeFunctionFunctionType{std::move(genericTypes), std::move(genericPacks), argTypes, retTypes});

    return 1;
}

// Luau: `self:setparameters(head: {type}?, tail: type?)`
// Sets the parameters of the function
static int setFunctionParameters(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount > 3 || argumentCount < 1)
        luaL_error(L, "type.setparameters: expected 1-3, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = getMutable<TypeFunctionFunctionType>(self);
    markEdited(self);
    if (!tfft)
        luaL_error(L, "type.setparameters: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setparameters: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    tfft->argTypes = getTypePack(L, 2, 3);

    return 0;
}

// Luau: `self:parameters() -> {head: {type}?, tail: type?}`
// Returns the parameters of the function
static int getFunctionParameters(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.parameters: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = get<TypeFunctionFunctionType>(self);
    if (!tfft)
        luaL_error(L, "type.parameters: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    pushTypePack(L, tfft->argTypes);

    return 1;
}

// Luwu: `self:getparameternames() -> {[number]: string}`, the names of the parameters that have one, by position
static int getFunctionParameterNames(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.getparameternames: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = get<TypeFunctionFunctionType>(self);
    if (!tfft)
        luaL_error(L, "type.getparameternames: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    lua_createtable(L, int(tfft->argNames.size()), 0);
    for (size_t i = 0; i < tfft->argNames.size(); ++i)
    {
        if (!tfft->argNames[i])
            continue;

        lua_pushlstring(L, tfft->argNames[i]->data(), tfft->argNames[i]->size());
        lua_rawseti(L, -2, int(i + 1));
    }

    return 1;
}

// Luwu: `self:setparametername(index: number, name: string?)`, naming (or unnaming) the parameter at `index`
static int setFunctionParameterName(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 2 || argumentCount > 3)
        luaL_error(L, "type.setparametername: expected 2-3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = getMutable<TypeFunctionFunctionType>(self);
    markEdited(self);
    if (!tfft)
        luaL_error(L, "type.setparametername: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setparametername: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    // only the parameters before the variadic tail have names
    const TypeFunctionTypePack* params = get<TypeFunctionTypePack>(tfft->argTypes);
    size_t paramCount = params ? params->head.size() : 0;

    int index = luaL_checkinteger(L, 2);
    if (index < 1 || size_t(index) > paramCount)
        luaL_error(L, "type.setparametername: index %d is out of range; the function has %d parameters before its tail", index, int(paramCount));

    std::optional<std::string> name;
    if (!lua_isnoneornil(L, 3))
        name = luaL_checkstring(L, 3);

    if (tfft->argNames.size() < paramCount)
        tfft->argNames.resize(paramCount);

    tfft->argNames[index - 1] = std::move(name);
    return 0;
}

// Luwu: TypeFunctionMetadataTable as a new Lua table. The types in it are the same type values, not copies.
static void pushMetadata(lua_State* L, const TypeFunctionMetadataTable& metadata);

static void pushMetadataValue(lua_State* L, const TypeFunctionMetadataValue& value)
{
    if (const bool* b = get_if<bool>(&value))
        lua_pushboolean(L, *b);
    else if (const double* n = get_if<double>(&value))
        lua_pushnumber(L, *n);
    else if (const std::string* str = get_if<std::string>(&value))
        lua_pushlstring(L, str->data(), str->size());
    else if (const TypeFunctionTypeId* ty = get_if<TypeFunctionTypeId>(&value))
        pushType(L, *ty);
    else if (const auto* table = get_if<std::shared_ptr<const TypeFunctionMetadataTable>>(&value))
        pushMetadata(L, **table);
}

static void pushMetadata(lua_State* L, const TypeFunctionMetadataTable& metadata)
{
    luaL_checkstack(L, 3, "metadata");
    lua_createtable(L, 0, int(metadata.entries.size()));

    for (const auto& [key, value] : metadata.entries)
    {
        pushMetadataValue(L, key);
        pushMetadataValue(L, value);
        lua_rawset(L, -3);
    }
}

// How deeply metadata tables may nest
static constexpr int kMetadataDepthLimit = 64;

static std::shared_ptr<const TypeFunctionMetadataTable> readMetadataTable(lua_State* L, int idx, std::vector<const void*>& enclosing);

// Luwu: a Lua value as metadata, or an error naming what can't be kept after the type function returns
static TypeFunctionMetadataValue readMetadataValue(lua_State* L, int idx, std::vector<const void*>& enclosing)
{
    switch (lua_type(L, idx))
    {
    case LUA_TBOOLEAN:
        return bool(lua_toboolean(L, idx));
    case LUA_TNUMBER:
        return double(lua_tonumber(L, idx));
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char* str = lua_tolstring(L, idx, &length);
        return std::string(str, length);
    }
    case LUA_TTABLE:
        return readMetadataTable(L, idx, enclosing);
    default:
        if (isTypeUserData(L, idx))
            return getTypeUserData(L, idx);

        luaL_error(
            L, "type.setmetadata: metadata can only hold booleans, numbers, strings, types and tables of those, not a %s", luaL_typename(L, idx)
        );
    }
}

static std::shared_ptr<const TypeFunctionMetadataTable> readMetadataTable(lua_State* L, int idx, std::vector<const void*>& enclosing)
{
    idx = lua_absindex(L, idx);

    const void* table = lua_topointer(L, idx);
    if (std::find(enclosing.begin(), enclosing.end(), table) != enclosing.end())
        luaL_error(L, "type.setmetadata: a metadata table can't contain itself");
    if (int(enclosing.size()) >= kMetadataDepthLimit)
        luaL_error(L, "type.setmetadata: metadata tables can't be nested more than %d deep", kMetadataDepthLimit);

    enclosing.push_back(table);
    luaL_checkstack(L, 3, "metadata");

    auto result = std::make_shared<TypeFunctionMetadataTable>();
    lua_pushnil(L);
    while (lua_next(L, idx) != 0)
    {
        TypeFunctionMetadataValue key = readMetadataValue(L, -2, enclosing);
        TypeFunctionMetadataValue value = readMetadataValue(L, -1, enclosing);
        result->entries.emplace_back(std::move(key), std::move(value));
        lua_pop(L, 1);
    }

    enclosing.pop_back();
    return result;
}

// Luwu doc references: `self:setpropertycomments(key: type, comments: { Comment }?)`, the doc comments of a table's
// property; nil removes them
static int setPropertyCommentsOf(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 2 || argumentCount > 3)
        luaL_error(L, "type.setpropertycomments: expected 2-3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tftt = getMutable<TypeFunctionTableType>(self);
    if (!tftt)
        luaL_error(L, "type.setpropertycomments: expected self to be a table, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setpropertycomments: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    TypeFunctionTypeId key = getTypeUserData(L, 2);
    auto singleton = get<TypeFunctionSingletonType>(key);
    auto name = singleton ? get<TypeFunctionStringSingleton>(singleton) : nullptr;
    if (!name)
        luaL_error(L, "type.setpropertycomments: expected to be given a string singleton type, but got %s instead", getTag(L, key).c_str());

    auto prop = tftt->props.find(name->value);
    if (prop == tftt->props.end())
        luaL_error(L, "type.setpropertycomments: the table has no property '%s'", name->value.c_str());

    if (lua_isnoneornil(L, 3))
    {
        prop->second.metadata = nullptr;
        return 0;
    }

    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<const void*> enclosing;
    auto metadata = std::make_shared<TypeFunctionMetadataTable>();
    metadata->entries.emplace_back(std::string("comments"), readMetadataTable(L, 3, enclosing));
    prop->second.metadata = std::move(metadata);
    return 0;
}

// Luwu: `self:metadata() -> {[any]: any}`, a new table holding the type's metadata: what a type function set on it,
// and `comments` from around its declaration
static int getMetadata(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.metadata: expected 1 argument, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (self->metadata)
        pushMetadata(L, *self->metadata);
    else
        lua_newtable(L);

    return 1;
}

// Luwu: `self:setmetadata(metadata: {[any]: any}?)`, replacing the type's metadata with a copy of `metadata`
static int setMetadata(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 1 || argumentCount > 2)
        luaL_error(L, "type.setmetadata: expected 1-2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setmetadata: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    // Only a type the type function makes when it returns can have metadata of its own; these stay the one type every
    // use of them shares (TypeFunctionDeserializer::ownsDeserializedType)
    bool shared = get<TypeFunctionPrimitiveType>(self) || get<TypeFunctionAnyType>(self) || get<TypeFunctionUnknownType>(self) ||
                  get<TypeFunctionNeverType>(self) || get<TypeFunctionGenericType>(self) || get<TypeFunctionErrorType>(self);
    if (shared)
        luaL_error(L, "type.setmetadata: a %s type is shared by every use of it, so it can't have metadata of its own", getTag(L, self).c_str());

    // Luwu doc references: a class, object or trait is shared too, but documenting one is common enough to allow: the
    // metadata goes on what the type function call stands for (`Cat` in `type Cat = Explain<CatBase>`), so it has to be
    // what the type function returns
    if (get<TypeFunctionExternType>(self))
        getTypeFunctionRuntime(L)->nominalMetadata.push_back(self);

    std::shared_ptr<const TypeFunctionMetadataTable> metadata;
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        std::vector<const void*> enclosing;
        metadata = readMetadataTable(L, 2, enclosing);
    }

    const_cast<TypeFunctionType*>(self)->metadata = std::move(metadata);
    return 0;
}

// Luau: `self:setreturns(head: {type}?, tail: type?)`
// Sets the returns of the function
static int setFunctionReturns(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount < 2 || argumentCount > 3)
        luaL_error(L, "type.setreturns: expected 1-3 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = getMutable<TypeFunctionFunctionType>(self);
    markEdited(self);
    if (!tfft)
        luaL_error(L, "type.setreturns: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setreturns: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    tfft->retTypes = getTypePack(L, 2, 3);

    return 0;
}

// Luau: `self:returns() -> {head: {type}?, tail: type?}`
// Returns the returns of the function
static int getFunctionReturns(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.returns: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = get<TypeFunctionFunctionType>(self);
    if (!tfft)
        luaL_error(L, "type.returns: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    pushTypePack(L, tfft->retTypes);

    return 1;
}

// Luau: `self:setgenerics(generics: {type}?)`
static int setFunctionGenerics(lua_State* L)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = getMutable<TypeFunctionFunctionType>(self);
    markEdited(self);
    if (!tfft)
        luaL_error(L, "type.setgenerics: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    if (FFlag::LuauTypeFunctionSupportsFrozen && self->frozen)
        luaL_error(L, "type.setgenerics: cannot be called to mutate a frozen type, use `types.copy` to make a copy");

    int argumentCount = lua_gettop(L);

    if (argumentCount > 2)
        luaL_error(L, "type.setgenerics: expected 2 arguments, but got %d", argumentCount);

    auto [genericTypes, genericPacks] = getGenerics(L, 2, "types.setgenerics");

    tfft->generics = std::move(genericTypes);
    tfft->genericPacks = std::move(genericPacks);

    return 0;
}

// Luau: `self:generics() -> {type}`
static int getFunctionGenerics(lua_State* L)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfft = get<TypeFunctionFunctionType>(self);
    if (!tfft)
        luaL_error(L, "type.generics: expected self to be a function, but got %s instead", getTag(L, self).c_str());

    lua_createtable(L, int(tfft->generics.size()) + int(tfft->genericPacks.size()), 0);

    int pos = 1;

    for (const auto& el : tfft->generics)
    {
        allocTypeUserDataCopy(L, el);
        lua_rawseti(L, -2, pos++);
    }

    for (const auto& el : tfft->genericPacks)
    {
        auto gty = get<TypeFunctionGenericTypePack>(el);
        LUAU_ASSERT(gty);
        allocTypeUserData(L, TypeFunctionGenericType{gty->isNamed, true, gty->name});
        lua_rawseti(L, -2, pos++);
    }

    return 1;
}

// Luau: `self:readparent() -> type`
// Returns the read type of the class' parent
static int getReadParent(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.parent: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfct = get<TypeFunctionExternType>(self);
    if (!tfct)
        luaL_error(L, "type.parent: expected self to be a class, but got %s instead", getTag(L, self).c_str());

    // If the parent does not exist, we should return nil
    if (!tfct->readParent)
        lua_pushnil(L);
    else
        allocTypeUserDataCopy(L, *tfct->readParent);

    return 1;
}
//
// Luau: `self:writeparent() -> type`
// Returns the write type of the class' parent
static int getWriteParent(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.parent: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfct = get<TypeFunctionExternType>(self);
    if (!tfct)
        luaL_error(L, "type.parent: expected self to be a class, but got %s instead", getTag(L, self).c_str());

    // If the parent does not exist, we should return nil
    if (!tfct->writeParent)
        lua_pushnil(L);
    else
        allocTypeUserDataCopy(L, *tfct->writeParent);

    return 1;
}

// Luau: `self:name() -> string?`
// Returns the name of the generic or 'nil' if the generic is unnamed
static int getGenericName(lua_State* L)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);

    // Luwu: an extern type's, object's, class's or trait's name
    if (const TypeFunctionExternType* nominal = get<TypeFunctionExternType>(self))
    {
        const ExternType* et = get<ExternType>(follow(nominal->externTy));
        lua_pushstring(L, et ? et->name.c_str() : "");
        return 1;
    }

    auto tfgt = get<TypeFunctionGenericType>(self);
    if (!tfgt)
        luaL_error(L, "type.name: expected self to be a generic, but got %s instead", getTag(L, self).c_str());

    if (tfgt->isNamed)
        lua_pushstring(L, tfgt->name.c_str());
    else
        lua_pushnil(L);

    return 1;
}

// Luwu Classes (rfcs/classes): a type the API returns from an object, class or trait, serialized when asked for
static void pushSerializedType(lua_State* L, TypeId ty, const std::string& what)
{
    TypeFunctionRuntimeBuilderState* builder = getTypeFunctionRuntime(L)->runtimeBuilder;
    // serialize looks the result up by the type it was given, but stores it by the type followed
    TypeFunctionTypeId serialized = serialize(follow(ty), builder);

    bool failed = !serialized || (FFlag::LuauTypeFunctionStructuredErrors ? !builder->errors.empty() : !builder->errors_DEPRECATED.empty());
    if (failed)
        luaL_error(L, "%s's type '%s' can't be passed to a type function", what.c_str(), toString(ty).c_str());

    pushType(L, serialized);
}

// The ExternType of `self`, which has to be one of `kinds`
static const ExternType* nominalSelf(lua_State* L, const char* method, std::initializer_list<NominalKind> kinds)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);
    const TypeFunctionExternType* nominal = get<TypeFunctionExternType>(self);
    bool allowed = nominal && std::find(kinds.begin(), kinds.end(), nominalKind(L, nominal)) != kinds.end();
    const ExternType* et = nominal ? get<ExternType>(follow(nominal->externTy)) : nullptr;
    if (!allowed || !et)
        luaL_error(L, "type.%s: not a method of %s types", method, getTag(L, self).c_str());

    return et;
}

// The other half of a class or trait: its value for an object type, its object type for a value
static std::optional<TypeId> relatedNominal(const ExternType* et)
{
    if (!et->relation)
        return std::nullopt;
    if (const Klass* value = get_if<Klass>(&*et->relation))
        return value->ty;
    if (const Obj* object = get_if<Obj>(&*et->relation))
        return object->ty;
    return std::nullopt;
}

static const ExternType* relatedExternType(const ExternType* et)
{
    std::optional<TypeId> related = relatedNominal(et);
    return related ? get<ExternType>(follow(*related)) : nullptr;
}

// The value (`trait<T>`) of a trait object type
static std::optional<TypeId> traitValue(TypeId traitObject)
{
    const ExternType* trait = get<ExternType>(follow(traitObject));
    return trait ? relatedNominal(trait) : std::nullopt;
}

static void pushTypeList(lua_State* L, const std::vector<TypeId>& types)
{
    lua_createtable(L, int(types.size()), 0);
    for (size_t i = 0; i < types.size(); ++i)
    {
        pushSerializedType(L, types[i], "a listed type");
        lua_rawseti(L, -2, int(i + 1));
    }
}

// Luwu Classes (rfcs/classes): `self:class() -> type?`, the class of an object type; nil for a trait's (`Greeter`), which
// no one class has
static int getObjectClass(lua_State* L)
{
    const ExternType* object = nominalSelf(L, "class", {NominalKind::Object});
    std::optional<TypeId> value = object->traitInfo ? std::nullopt : relatedNominal(object);
    if (value)
        pushSerializedType(L, *value, "the class");
    else
        lua_pushnil(L);
    return 1;
}

// Luwu Classes (rfcs/classes): `self:object() -> type`, the object type of a class or trait
static int getNominalObject(lua_State* L)
{
    const ExternType* value = nominalSelf(L, "object", {NominalKind::Class, NominalKind::Trait});
    std::optional<TypeId> object = relatedNominal(value);
    if (object)
        pushSerializedType(L, *object, "the object type");
    else
        lua_pushnil(L);
    return 1;
}

// Luwu Traits (rfcs/classes/traits.md): `self:traits() -> {type}`, the traits an object type or class implements, implied
// ones included. A trait's object type (`Greeter`) implements that trait and what it needs.
static int getNominalTraits(lua_State* L)
{
    const ExternType* et = nominalSelf(L, "traits", {NominalKind::Object, NominalKind::Class});
    const TypeFunctionExternType* nominal = get<TypeFunctionExternType>(getTypeUserData(L, 1));
    const ExternType* object = nominalKind(L, nominal) == NominalKind::Object ? et : relatedExternType(et);

    std::vector<TypeId> traits;
    if (object && object->traitInfo)
    {
        if (std::optional<TypeId> own = relatedNominal(object))
            traits.push_back(*own);
    }

    for (TypeId implemented : object ? object->implementedTraits : std::vector<TypeId>{})
    {
        if (std::optional<TypeId> value = traitValue(implemented))
            traits.push_back(*value);
    }

    pushTypeList(L, traits);
    return 1;
}

// Luwu Traits (rfcs/classes/traits.md): `self:needs() -> {type}`, the traits a trait's `needs` list names
static int getTraitNeeds(lua_State* L)
{
    const ExternType* object = relatedExternType(nominalSelf(L, "needs", {NominalKind::Trait}));

    std::vector<TypeId> needed;
    for (TypeId traitObject : object ? object->implementedTraits : std::vector<TypeId>{})
    {
        if (std::optional<TypeId> value = traitValue(traitObject))
            needed.push_back(*value);
    }

    pushTypeList(L, needed);
    return 1;
}

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): `self:typearguments() -> {type}`, `List<number>`'s `{ number }`
static int getObjectTypeArguments(lua_State* L)
{
    const ExternType* object = nominalSelf(L, "typearguments", {NominalKind::Object});
    pushTypeList(L, object->instantiatedTypeParams);
    return 1;
}

// A field or function entry: `{ type = ..., private = ..., ... }`. `flags` are the boolean fields.
static void pushMember(lua_State* L, const std::string& name, TypeId ty, std::initializer_list<std::pair<const char*, bool>> flags)
{
    lua_createtable(L, 0, int(flags.size()) + 1);
    pushSerializedType(L, ty, "'" + name + "'");
    lua_setfield(L, -2, "type");
    for (const auto& [name, value] : flags)
    {
        lua_pushboolean(L, value);
        lua_setfield(L, -2, name);
    }
}

// The object type `fields`/`functions` read members from: an object type's own, a trait value's object type
static const ExternType* memberObject(lua_State* L, const ExternType* et)
{
    const TypeFunctionExternType* nominal = get<TypeFunctionExternType>(getTypeUserData(L, 1));
    return nominalKind(L, nominal) == NominalKind::Object ? et : relatedExternType(et);
}

static bool isFieldOf(const ExternType* object, const std::string& name)
{
    const ClassFieldUserData* fields = dynamic_cast<const ClassFieldUserData*>(object->userData.get());
    return fields && fields->fieldNames.count(name);
}

// Luwu Classes (rfcs/classes): `self:fields() -> {[string]: Field}`, an object type's or trait's fields, private ones
// included. Type functions are analysis: a library hides members by declaring the class without them.
static int getNominalFields(lua_State* L)
{
    const ExternType* object = memberObject(L, nominalSelf(L, "fields", {NominalKind::Object, NominalKind::Trait}));

    lua_newtable(L);
    if (!object)
        return 1;

    const ExternType::TraitInfo* trait = object->traitInfo ? &*object->traitInfo : nullptr;
    for (const auto& [name, prop] : object->props())
    {
        std::optional<TypeId> ty = prop.readTy ? prop.readTy : prop.writeTy;
        if (!ty || !isFieldOf(object, name))
            continue;

        pushMember(
            L,
            name,
            *ty,
            {{"private", prop.isPrivate},
             {"const", prop.isConst},
             {"final", trait && trait->finals.count(name)},
             {"expected", trait && trait->expectations.count(name)}}
        );
        lua_setfield(L, -2, name.c_str());
    }

    return 1;
}

// Adds `props`' functions to the table on top of the stack, skipping names it already has and the constructors
static void addFunctions(lua_State* L, const ExternType::Props& props, const ExternType* object, const ExternType::TraitInfo* trait)
{
    for (const auto& [name, prop] : props)
    {
        bool constructor = name == "__init" || name == "__create";
        if (!prop.readTy || constructor || (object && isFieldOf(object, name)))
            continue;

        lua_getfield(L, -1, name.c_str());
        bool present = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (present)
            continue;

        auto expectation = trait ? trait->expectations.find(name) : std::map<Name, bool>::const_iterator{};
        bool expected = trait && expectation != trait->expectations.end();
        pushMember(
            L,
            name,
            expectedTraitFunction(*prop.readTy),
            {{"private", prop.isPrivate},
             {"final", trait && trait->finals.count(name)},
             {"expected", expected},
             {"optional", expected && expectation->second}}
        );
        lua_setfield(L, -2, name.c_str());
    }
}

// Luwu Classes (rfcs/classes): `self:functions() -> {[string]: FunctionMember}`. An object type's methods and
// metamethods; a class's static functions and methods; a trait's methods, statics and `__create`. Private ones included.
static int getNominalFunctions(lua_State* L)
{
    const ExternType* et = nominalSelf(L, "functions", {NominalKind::Object, NominalKind::Class, NominalKind::Trait});
    const TypeFunctionExternType* nominal = get<TypeFunctionExternType>(getTypeUserData(L, 1));
    NominalKind kind = nominalKind(L, nominal);
    const ExternType* object = kind == NominalKind::Object ? et : relatedExternType(et);
    const ExternType::TraitInfo* trait = object && object->traitInfo ? &*object->traitInfo : nullptr;

    lua_newtable(L);

    if (kind == NominalKind::Class)
    {
        addFunctions(L, et->props(), object, nullptr);
        return 1;
    }

    if (object)
    {
        addFunctions(L, object->props(), object, trait);
        const TableType* metamethods = object->metatable ? get<TableType>(follow(*object->metatable)) : nullptr;
        if (metamethods)
            addFunctions(L, metamethods->props, nullptr, trait);
    }

    if (kind == NominalKind::Trait)
    {
        // the trait's statics, and `__create`
        for (const auto& [name, prop] : et->props())
        {
            if (name != "__create" || !prop.readTy)
                continue;

            pushMember(L, name, *prop.readTy, {{"private", prop.isPrivate}, {"final", false}, {"expected", false}, {"optional", false}});
            lua_setfield(L, -2, name.c_str());
        }
        addFunctions(L, et->props(), object, trait);
    }

    return 1;
}

// Luwu Classes (rfcs/classes): `self:constructor() -> { type: type, private: boolean }?`, a class's constructor as
// `Cat(...)` calls it
static int getClassConstructor(lua_State* L)
{
    const ExternType* value = nominalSelf(L, "constructor", {NominalKind::Class});
    const ExternType* object = relatedExternType(value);

    const TableType* metatable = value->metatable ? get<TableType>(follow(*value->metatable)) : nullptr;
    auto call = metatable ? metatable->props.find("__call") : TableType::Props::const_iterator{};

    const FunctionType* ctor = nullptr;
    if (metatable && call != metatable->props.end() && call->second.readTy)
        ctor = get<FunctionType>(follow(*call->second.readTy));

    if (!ctor)
    {
        lua_pushnil(L);
        return 1;
    }

    // `__call` takes the class first; `Cat(...)` passes it implicitly
    auto [params, tail] = flatten(ctor->argTypes);
    std::vector<TypeId> written(params.size() > 0 ? params.begin() + 1 : params.begin(), params.end());

    TypeFunctionRuntimeBuilderState* builder = getTypeFunctionRuntime(L)->runtimeBuilder;
    NotNull<TypeArena> arena = builder->ctx->arena;
    FunctionType asCalled{arena->addTypePack(std::move(written), tail), ctor->retTypes};
    if (ctor->argNames.size() > 1)
        asCalled.argNames.assign(ctor->argNames.begin() + 1, ctor->argNames.end());

    const Property* init = nullptr;
    if (object)
    {
        auto it = object->props().find("__init");
        init = it != object->props().end() ? &it->second : nullptr;
    }

    pushMember(L, "the constructor", arena->addType(std::move(asCalled)), {{"private", init && init->isPrivate}});
    return 1;
}

// Luau: `self:ispack() -> boolean`
// Returns true if the generic is a pack
static int getGenericIsPack(lua_State* L)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);
    auto tfgt = get<TypeFunctionGenericType>(self);
    if (!tfgt)
        luaL_error(L, "type.ispack: expected self to be a generic, but got %s instead", getTag(L, self).c_str());

    lua_pushboolean(L, tfgt->isPack);
    return 1;
}

// Luau: `self:properties() -> {[type]: { read: type?, write: type? }}`
// Luwu doc references: `comments` in a property's entry in `properties()`, when it has any
static void setPropertyComments(lua_State* L, const TypeFunctionProperty& prop)
{
    if (!prop.metadata)
        return;
    for (const auto& [key, value] : prop.metadata->entries)
    {
        const std::string* name = get_if<std::string>(&key);
        if (name && *name == "comments")
        {
            pushMetadataValue(L, value);
            lua_setfield(L, -2, "comments");
            return;
        }
    }
}

// Returns the properties of a table or class type
static int getProps(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.properties: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (auto tftt = get<TypeFunctionTableType>(self))
    {
        lua_createtable(L, int(tftt->props.size()), 0);
        for (auto& [name, prop] : tftt->props)
        {
            allocTypeUserData(L, TypeFunctionSingletonType{TypeFunctionStringSingleton{name}});

            int size = 0;
            if (prop.readTy)
                size++;
            if (prop.writeTy)
                size++;

            lua_createtable(L, 0, size);
            if (prop.readTy)
            {
                allocTypeUserDataCopy(L, *prop.readTy);
                lua_setfield(L, -2, "read");
            }

            if (prop.writeTy)
            {
                allocTypeUserDataCopy(L, *prop.writeTy);
                lua_setfield(L, -2, "write");
            }

            setPropertyComments(L, prop);
            lua_settable(L, -3);
        }

        return 1;
    }

    if (auto tfct = get<TypeFunctionExternType>(self))
    {
        lua_createtable(L, int(tfct->props.size()), 0);
        for (auto& [name, prop] : tfct->props)
        {
            allocTypeUserData(L, TypeFunctionSingletonType{TypeFunctionStringSingleton{name}});

            int size = 0;
            if (prop.readTy)
                size++;
            if (prop.writeTy)
                size++;

            lua_createtable(L, 0, size);
            if (prop.readTy)
            {
                allocTypeUserDataCopy(L, *prop.readTy);
                lua_setfield(L, -2, "read");
            }

            if (prop.writeTy)
            {
                allocTypeUserDataCopy(L, *prop.writeTy);
                lua_setfield(L, -2, "write");
            }

            setPropertyComments(L, prop);
            lua_settable(L, -3);
        }

        return 1;
    }

    luaL_error(L, "type.properties: expected self to be either a table or class, but got %s instead", getTag(L, self).c_str());
}

// Luau: `self:indexer() -> {index: type, readresult: type, writeresult: type}?`
// Returns the indexer of a table or class type
static int getIndexer(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.indexer: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (auto tftt = get<TypeFunctionTableType>(self))
    {
        // if the indexer does not exist, we should return nil
        if (!tftt->indexer.has_value())
            lua_pushnil(L);
        else
        {
            lua_createtable(L, 0, 3);
            allocTypeUserDataCopy(L, tftt->indexer->keyType);
            lua_setfield(L, -2, "index");
            allocTypeUserDataCopy(L, tftt->indexer->valueType);
            lua_setfield(L, -2, "readresult");
            allocTypeUserDataCopy(L, tftt->indexer->valueType);
            lua_setfield(L, -2, "writeresult");
        }

        return 1;
    }

    if (auto tfct = get<TypeFunctionExternType>(self))
    {
        // if the indexer does not exist, we should return nil
        if (!tfct->indexer.has_value())
            lua_pushnil(L);
        else
        {
            lua_createtable(L, 0, 3);
            allocTypeUserDataCopy(L, tfct->indexer->keyType);
            lua_setfield(L, -2, "index");
            allocTypeUserDataCopy(L, tfct->indexer->valueType);
            lua_setfield(L, -2, "readresult");
            allocTypeUserDataCopy(L, tfct->indexer->valueType);
            lua_setfield(L, -2, "writeresult");
        }

        return 1;
    }

    luaL_error(L, "type.indexer: self to be either a table or class, but got %s instead", getTag(L, self).c_str());
}

// Luau: `self:readindexer() -> {index: type, result: type}?`
// Returns the read indexer of a table or class type
static int getReadIndexer(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.readindexer: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (auto tftt = get<TypeFunctionTableType>(self))
    {
        // if the indexer does not exist, we should return nil
        if (!tftt->indexer.has_value())
            lua_pushnil(L);
        else
        {
            lua_createtable(L, 0, 2);
            allocTypeUserDataCopy(L, tftt->indexer->keyType);
            lua_setfield(L, -2, "index");
            allocTypeUserDataCopy(L, tftt->indexer->valueType);
            lua_setfield(L, -2, "result");
        }

        return 1;
    }

    if (auto tfct = get<TypeFunctionExternType>(self))
    {
        // if the indexer does not exist, we should return nil
        if (!tfct->indexer.has_value())
            lua_pushnil(L);
        else
        {
            lua_createtable(L, 0, 2);
            allocTypeUserDataCopy(L, tfct->indexer->keyType);
            lua_setfield(L, -2, "index");
            allocTypeUserDataCopy(L, tfct->indexer->valueType);
            lua_setfield(L, -2, "result");
        }

        return 1;
    }

    luaL_error(L, "type.readindexer: expected self to be either a table or class, but got %s instead", getTag(L, self).c_str());
}

// Luau: `self:writeindexer() -> {index: type, result: type}?`
// Returns the write indexer of a table or class type
static int getWriteIndexer(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.writeindexer: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (auto tftt = get<TypeFunctionTableType>(self))
    {
        // if the indexer does not exist, we should return nil
        if (!tftt->indexer.has_value())
            lua_pushnil(L);
        else
        {
            lua_createtable(L, 0, 2);
            allocTypeUserDataCopy(L, tftt->indexer->keyType);
            lua_setfield(L, -2, "index");
            allocTypeUserDataCopy(L, tftt->indexer->valueType);
            lua_setfield(L, -2, "result");
        }

        return 1;
    }

    if (auto tfct = get<TypeFunctionExternType>(self))
    {
        // if the indexer does not exist, we should return nil
        if (!tfct->indexer.has_value())
            lua_pushnil(L);
        else
        {
            lua_createtable(L, 0, 2);
            allocTypeUserDataCopy(L, tfct->indexer->keyType);
            lua_setfield(L, -2, "index");
            allocTypeUserDataCopy(L, tfct->indexer->valueType);
            lua_setfield(L, -2, "result");
        }

        return 1;
    }

    luaL_error(L, "type.writeindexer: expected self to be either a table or class, but got %s instead", getTag(L, self).c_str());
}

// Luau: `self:metatable() -> type?`
// Returns the metatable of a table or class type
static int getMetatable(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "type.metatable: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    if (auto tfmt = get<TypeFunctionTableType>(self))
    {
        // if the metatable does not exist, we should return nil
        if (!tfmt->metatable.has_value())
            lua_pushnil(L);
        else
            allocTypeUserDataCopy(L, *tfmt->metatable);

        return 1;
    }

    if (auto tfct = get<TypeFunctionExternType>(self))
    {
        // if the metatable does not exist, we should return nil
        if (!tfct->metatable.has_value())
            lua_pushnil(L);
        else
            allocTypeUserDataCopy(L, *tfct->metatable);

        return 1;
    }

    luaL_error(L, "type.metatable: expected self to be a table or class, but got %s instead", getTag(L, self).c_str());
}

// Luau: `self:is(arg: string) -> boolean`
// Returns true if given argument is a tag of self
static int checkTag(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 2)
        luaL_error(L, "type.is: expected 2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    std::string arg = luaL_checkstring(L, 2);

    lua_pushboolean(L, getTag(L, self) == arg);
    return 1;
}

// Luau `self:issubtypeof(arg: type) -> boolean`
// Returns true if self is a subtype of the given type
static int isSubtypeOf(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 2)
        luaL_error(L, "type.issubtypeof: expected 2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    TypeFunctionTypeId arg = getTypeUserData(L, 2);

    TypeFunctionRuntimeBuilderState* runtimeBuilder = Luau::getTypeFunctionRuntime(L)->runtimeBuilder;
    NotNull<TypeFunctionContext> ctx = runtimeBuilder->ctx;

    TypeId subTy = Luau::deserialize(self, runtimeBuilder);
    if (FFlag::LuauTypeFunctionStructuredErrors ? !runtimeBuilder->errors.empty() : !runtimeBuilder->errors_DEPRECATED.empty())
        luaL_error(L, "failed to deserialize the self type");

    TypeId superTy = Luau::deserialize(arg, runtimeBuilder);
    if (FFlag::LuauTypeFunctionStructuredErrors ? !runtimeBuilder->errors.empty() : !runtimeBuilder->errors_DEPRECATED.empty())
        luaL_error(L, "failed to deserialize the argument type");

    SubtypingResult result = ctx->subtyping->isSubtype(subTy, superTy, ctx->scope);

    lua_pushboolean(L, static_cast<int>(result.isSubtype));
    return 1;
}

TypeFunctionTypeId deepClone(NotNull<TypeFunctionRuntime> runtime, TypeFunctionTypeId ty); // Forward declaration

// Luwu: `types.newclass` and `types.newtrait`. A class or trait a type function makes is a type and nothing else, like
// `declare class type`: no value exists for it unless something at run time makes one and casts it. Its identity is its
// key (ExternType::generatedKey), since the type function runs again wherever it's used.

namespace
{
struct GeneratedMember
{
    std::string name;
    // a method's type without `self`
    TypeId type = nullptr;
    bool isField = false;
    bool method = false;
    bool isPrivate = false;
    bool isConst = false;
    bool isFinal = false;
    bool expected = false;
    bool optional = false;
};

struct GeneratedNominal
{
    bool isTrait = false;
    std::string name;
    std::vector<GeneratedMember> members;
    // the object types of the traits a class implements, or a trait needs
    std::vector<TypeId> traits;
    // a class's constructor's parameters, as `Cat(...)` is called
    std::optional<TypeId> constructor;
    bool privateConstructor = false;
};

struct GeneratedNominalReferences : TypeOnceVisitor
{
    std::set<size_t> hashes;

    GeneratedNominalReferences()
        : TypeOnceVisitor("GeneratedNominalReferences", /* skipBoundTypes */ true)
    {
    }

    bool visit(TypeId ty, const ExternType& et) override
    {
        if (et.generatedKey)
            hashes.insert(et.generatedKey->hash);
        return false;
    }
};
} // namespace

static bool readFlag(lua_State* L, int table, const char* name)
{
    lua_getfield(L, table, name);
    bool value = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

static TypeId deserializeMemberType(lua_State* L, TypeFunctionTypeId ty, const char* fn, const std::string& what)
{
    TypeFunctionRuntimeBuilderState* builder = getTypeFunctionRuntime(L)->runtimeBuilder;
    TypeId result = deserialize(ty, builder);

    bool failed = FFlag::LuauTypeFunctionStructuredErrors ? !builder->errors.empty() : !builder->errors_DEPRECATED.empty();
    if (failed)
        luaL_error(L, "%s: %s's type can't be used", fn, what.c_str());

    return result;
}

// `def[list]`: `{ [string]: type | { type: type, private: boolean?, ... } }`
static void readGeneratedMembers(lua_State* L, int def, const char* list, bool fields, const char* fn, GeneratedNominal& nominal)
{
    lua_getfield(L, def, list);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

    if (!lua_istable(L, -1))
        luaL_error(L, "%s: '%s' should be a table of members by name", fn, list);

    int members = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, members) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "%s: '%s' should be keyed by member name", fn, list);

        GeneratedMember member;
        member.name = lua_tostring(L, -2);
        member.isField = fields;

        int entry = lua_absindex(L, -1);
        TypeFunctionTypeId ty = nullptr;
        if (isTypeUserData(L, entry))
            ty = getTypeUserData(L, entry);
        else if (lua_istable(L, entry))
        {
            lua_getfield(L, entry, "type");
            if (!isTypeUserData(L, -1))
                luaL_error(L, "%s: '%s' needs a 'type'", fn, member.name.c_str());
            ty = getTypeUserData(L, -1);
            lua_pop(L, 1);

            member.isPrivate = readFlag(L, entry, "private");
            member.isConst = fields && readFlag(L, entry, "const");
            member.method = !fields && readFlag(L, entry, "method");
            member.isFinal = nominal.isTrait && readFlag(L, entry, "final");
            member.expected = nominal.isTrait && readFlag(L, entry, "expected");
            member.optional = member.expected && !fields && readFlag(L, entry, "optional");
        }
        else
            luaL_error(L, "%s: '%s' should be a type or a table with one", fn, member.name.c_str());

        if (!fields && !get<TypeFunctionFunctionType>(ty))
            luaL_error(L, "%s: function '%s' needs a function type", fn, member.name.c_str());

        member.type = deserializeMemberType(L, ty, fn, "'" + member.name + "'");
        nominal.members.push_back(std::move(member));
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
}

// `def[list]`: `{ Trait }`, as the traits' object types
static void readGeneratedTraits(lua_State* L, int def, const char* list, const char* fn, GeneratedNominal& nominal)
{
    lua_getfield(L, def, list);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

    if (!lua_istable(L, -1))
        luaL_error(L, "%s: '%s' should be a list of traits", fn, list);

    int traits = lua_absindex(L, -1);
    for (int i = 1;; ++i)
    {
        lua_rawgeti(L, traits, i);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            break;
        }

        const TypeFunctionExternType* trait = isTypeUserData(L, -1) ? get<TypeFunctionExternType>(getTypeUserData(L, -1)) : nullptr;
        const ExternType* value = trait && nominalKind(L, trait) == NominalKind::Trait ? get<ExternType>(follow(trait->externTy)) : nullptr;
        std::optional<TypeId> object = value ? relatedNominal(value) : std::nullopt;
        if (!object)
            luaL_error(L, "%s: '%s' entry %d isn't a trait", fn, list, i);

        nominal.traits.push_back(*object);
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
}

// How a trait or class this one names is identified in its key
static std::string nominalIdentity(TypeId ty)
{
    const ExternType* et = get<ExternType>(follow(ty));
    if (!et)
        return toString(ty);
    if (et->generatedKey)
        return format("generated %zx", et->generatedKey->hash);

    std::string location = et->definitionLocation ? toString(*et->definitionLocation) : "";
    return et->name + "@" + et->definitionModuleName + ":" + location;
}

// ExternType::generatedKey: everything that makes the class or trait what it is, written out
static std::string generatedKey(const GeneratedNominal& nominal)
{
    ToStringOptions exhaustive{true};
    GeneratedNominalReferences references;

    std::vector<const GeneratedMember*> members;
    for (const GeneratedMember& member : nominal.members)
        members.push_back(&member);
    std::sort(
        members.begin(),
        members.end(),
        [](const GeneratedMember* a, const GeneratedMember* b)
        {
            return a->name < b->name;
        }
    );

    std::string key = std::string(nominal.isTrait ? "trait " : "class ") + nominal.name + " {";
    for (const GeneratedMember* member : members)
    {
        key += member->isField ? " field " : " function ";
        key += member->name;
        key += member->isPrivate ? " private" : "";
        key += member->isConst ? " const" : "";
        key += member->method ? " method" : "";
        key += member->isFinal ? " final" : "";
        key += member->expected ? " expected" : "";
        key += member->optional ? " optional" : "";
        key += ": " + toString(member->type, exhaustive) + ";";
        references.traverse(member->type);
    }

    key += " } traits {";
    for (TypeId trait : nominal.traits)
        key += " " + nominalIdentity(trait) + ";";

    key += " } constructor ";
    if (nominal.constructor)
    {
        key += toString(*nominal.constructor, exhaustive) + (nominal.privateConstructor ? " private" : "");
        references.traverse(*nominal.constructor);
    }

    // the generated classes the members name print by name only, so their hashes come along (not their keys, which would
    // make the key grow with every class it reaches)
    for (size_t reference : references.hashes)
        key += format(" uses %zx", reference);

    return key;
}

// `fn`, with `self` in front of its parameters
static TypeId withSelf(NotNull<TypeArena> arena, TypeId fn, TypeId self)
{
    const FunctionType* ftv = get<FunctionType>(follow(fn));
    LUAU_ASSERT(ftv);

    auto [params, tail] = flatten(ftv->argTypes);
    params.insert(params.begin(), self);

    FunctionType method{ftv->generics, ftv->genericPacks, arena->addTypePack(std::move(params), tail), ftv->retTypes, std::nullopt, true};
    method.argNames = ftv->argNames;
    method.argNames.insert(method.argNames.begin(), FunctionArgument{"self", {}});
    return arena->addType(std::move(method));
}

// The trait object types `traits` stand for, and the ones they need, each once
static std::vector<TypeId> traitClosure(const std::vector<TypeId>& traits)
{
    std::vector<TypeId> result;
    std::vector<TypeId> pending(traits.rbegin(), traits.rend());
    while (!pending.empty())
    {
        TypeId trait = follow(pending.back());
        pending.pop_back();
        if (std::find(result.begin(), result.end(), trait) != result.end())
            continue;

        result.push_back(trait);
        if (const ExternType* et = get<ExternType>(trait))
            pending.insert(pending.end(), et->implementedTraits.rbegin(), et->implementedTraits.rend());
    }

    return result;
}

static int createNominal(lua_State* L, bool isTrait)
{
    const char* fn = isTrait ? "types.newtrait" : "types.newclass";

    GeneratedNominal nominal;
    nominal.isTrait = isTrait;
    nominal.name = luaL_checkstring(L, 1);

    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        readGeneratedMembers(L, 2, "fields", /* fields */ true, fn, nominal);
        readGeneratedMembers(L, 2, "functions", /* fields */ false, fn, nominal);
        readGeneratedTraits(L, 2, isTrait ? "needs" : "implements", fn, nominal);

        lua_getfield(L, 2, "constructor");
        if (!isTrait && !lua_isnil(L, -1))
        {
            int constructor = lua_absindex(L, -1);
            TypeFunctionTypeId ty = nullptr;
            if (isTypeUserData(L, constructor))
                ty = getTypeUserData(L, constructor);
            else if (lua_istable(L, constructor))
            {
                lua_getfield(L, constructor, "type");
                ty = isTypeUserData(L, -1) ? getTypeUserData(L, -1) : nullptr;
                lua_pop(L, 1);
                nominal.privateConstructor = readFlag(L, constructor, "private");
            }

            if (!ty || !get<TypeFunctionFunctionType>(ty))
                luaL_error(L, "%s: 'constructor' needs a function type", fn);

            nominal.constructor = deserializeMemberType(L, ty, fn, "the constructor");
        }
        lua_pop(L, 1);
    }

    TypeFunctionRuntime* runtime = getTypeFunctionRuntime(L);
    TypeFunctionRuntimeBuilderState* builder = runtime->runtimeBuilder;
    NotNull<TypeArena> arena = builder->ctx->arena;
    NotNull<BuiltinTypes> builtins = builder->ctx->builtins;
    std::string key = generatedKey(nominal);

    // made in this module already: the same types, rather than a copy per use
    if (TypeId* existing = runtime->generatedNominals.find(key))
    {
        pushSerializedType(L, *existing, std::string("the ") + (isTrait ? "trait" : "class"));
        return 1;
    }

    auto fieldNames = std::make_shared<ClassFieldUserData>();
    for (const GeneratedMember& member : nominal.members)
    {
        if (member.isField)
            fieldNames->fieldNames.insert(member.name);
    }

    TypeId object = arena->addType(ExternType{nominal.name, {}, builtins->objectType, std::nullopt, Tags{}, fieldNames, ModuleName{}, std::nullopt});
    ExternType* objectEtv = getMutable<ExternType>(object);

    ExternType::Props statics;
    TableType::Props metamethods;
    ExternType::TraitInfo traitInfo;
    for (const GeneratedMember& member : nominal.members)
    {
        TypeId ty = member.method ? withSelf(arena, member.type, object) : member.type;
        // a const field stays writable, as in a written class: `isConst` is what reports writing it
        Property prop = member.isField ? Property::rw(ty) : Property::readonly(ty);
        prop.isPrivate = member.isPrivate;
        prop.isConst = member.isConst;
        prop.isFinal = member.isFinal;

        if (member.expected)
            traitInfo.expectations[member.name] = member.optional;
        if (member.isFinal)
            traitInfo.finals.insert(member.name);

        bool metamethod = !member.isField && member.method && member.name.rfind("__", 0) == 0;
        if (metamethod)
            metamethods[member.name] = prop;
        else
            objectEtv->props()[member.name] = prop;

        if (!member.isField && !metamethod && !member.expected)
            statics[member.name] = prop;
    }

    // what the traits provide and the class or trait doesn't itself; a trait's expectations stay on the trait
    objectEtv->implementedTraits = traitClosure(nominal.traits);
    for (TypeId trait : objectEtv->implementedTraits)
    {
        const ExternType* traitEtv = get<ExternType>(trait);
        if (!traitEtv)
            continue;

        for (const auto& [name, prop] : traitEtv->props())
        {
            bool expectedThere = traitEtv->traitInfo && traitEtv->traitInfo->expectations.count(name);
            if (objectEtv->props().count(name) || (!isTrait && expectedThere))
                continue;

            objectEtv->props()[name] = prop;
            if (isTrait)
                traitInfo.fromNeeds.insert(name);
        }
    }

    // a class has to have what its traits expect, as `implements` checks for a written class
    if (!isTrait)
    {
        for (TypeId trait : objectEtv->implementedTraits)
        {
            const ExternType* traitEtv = get<ExternType>(trait);
            if (!traitEtv || !traitEtv->traitInfo)
                continue;

            for (const auto& [name, optional] : traitEtv->traitInfo->expectations)
            {
                bool present = objectEtv->props().count(name) || metamethods.count(name);
                if (!present && !optional && name != "__init")
                    luaL_error(L, "%s: class '%s' doesn't have '%s', which trait '%s' expects", fn, nominal.name.c_str(), name.c_str(), traitEtv->name.c_str());
            }
        }
    }

    if (!metamethods.empty())
        objectEtv->metatable = arena->addType(TableType{std::move(metamethods), std::nullopt, TypeLevel{}, builder->ctx->scope.get(), TableState::Sealed});
    if (isTrait)
        objectEtv->traitInfo = std::move(traitInfo);

    TypeId valueRoot = isTrait ? builtins->traitType : builtins->classType;
    TypeId value = arena->addType(ExternType{nominal.name, std::move(statics), valueRoot, std::nullopt, Tags{}, nullptr, ModuleName{}, std::nullopt});
    ExternType* valueEtv = getMutable<ExternType>(value);

    if (nominal.constructor)
    {
        const FunctionType* ctor = get<FunctionType>(follow(*nominal.constructor));
        auto [params, tail] = flatten(ctor->argTypes);

        std::vector<TypeId> callParams{value};
        callParams.insert(callParams.end(), params.begin(), params.end());
        FunctionType call{arena->addTypePack(std::move(callParams), tail), arena->addTypePack({object}), std::nullopt, true};
        call.argNames.push_back(std::nullopt);
        call.argNames.insert(call.argNames.end(), ctor->argNames.begin(), ctor->argNames.end());

        TableType::Props valueMetatable;
        valueMetatable["__call"] = Property::readonly(arena->addType(std::move(call)));
        valueEtv->metatable = arena->addType(TableType{std::move(valueMetatable), std::nullopt, TypeLevel{}, builder->ctx->scope.get(), TableState::Sealed});

        // where a written class keeps its constructor's privacy
        Property init = Property::readonly(withSelf(arena, *nominal.constructor, object));
        init.isPrivate = nominal.privateConstructor;
        objectEtv->props()["__init"] = init;
    }

    valueEtv->relation.emplace(Obj{object});
    objectEtv->relation.emplace(Klass{value});

    auto keyFor = [&](const char* side)
    {
        auto result = std::make_shared<GeneratedNominalKey>();
        result->key = std::string(side) + " " + key;
        result->hash = std::hash<std::string>{}(result->key);
        return result;
    };
    objectEtv->generatedKey = keyFor("object");
    valueEtv->generatedKey = keyFor("value");
    runtime->generatedNominals[key] = value;

    pushSerializedType(L, value, std::string("the new ") + (isTrait ? "trait" : "class"));
    return 1;
}

// Luwu: `types.newclass(name: string, def: { fields: ..., functions: ..., implements: { Trait }?, constructor: ... }?)`
static int createClass(lua_State* L)
{
    return createNominal(L, /* isTrait */ false);
}

// Luwu: `types.newtrait(name: string, def: { fields: ..., functions: ..., needs: { Trait }? }?)`
static int createTrait(lua_State* L)
{
    return createNominal(L, /* isTrait */ true);
}

// Luau: `types.copy(arg: type) -> type`
// Returns a deep copy of the argument
static int deepCopy(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 1)
        luaL_error(L, "types.copy: expected 1 arguments, but got %d", argumentCount);

    TypeFunctionTypeId arg = getTypeUserData(L, 1);

    TypeFunctionTypeId copy = deepClone(NotNull{getTypeFunctionRuntime(L)}, arg);

    if (!copy)
        luaL_error(L, "types.copy: complexity limit reached during type copy");

    allocTypeUserDataCopy(L, copy);
    return 1;
}

// Luau: `self == arg -> boolean`
// Used to set the __eq metamethod
static int isEqualToType(lua_State* L)
{
    int argumentCount = lua_gettop(L);
    if (argumentCount != 2)
        luaL_error(L, "expected 2 arguments, but got %d", argumentCount);

    TypeFunctionTypeId self = getTypeUserData(L, 1);
    TypeFunctionTypeId arg = getTypeUserData(L, 2);

    lua_pushboolean(L, *self == *arg);
    return 1;
}

// Luau: `tostring(self) -> string`,
// or other cases where the `__tostring` metamethod is invoked
static int typeToString(lua_State* L)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);

    TypeFunctionRuntimeBuilderState* runtimeBuilder = Luau::getTypeFunctionRuntime(L)->runtimeBuilder;
    TypeId selfTy = Luau::deserialize(self, runtimeBuilder);
    if (FFlag::LuauTypeFunctionStructuredErrors ? !runtimeBuilder->errors.empty() : !runtimeBuilder->errors_DEPRECATED.empty())
        luaL_error(L, "failed to deserialize the self type");

    // Luwu: `types.format(self)`: what a hover shows, on one line
    std::string asString = formatType(selfTy, TypeFormatOptions{});

    lua_pushlstring(L, asString.data(), asString.size());
    return 1;
}

// Luwu: `types.format(arg: type, options: TypeFormatOptions?) -> string`
static int formatTypeFunctionType(lua_State* L)
{
    TypeFunctionTypeId arg = getTypeUserData(L, 1);

    TypeFormatOptions options;
    if (!lua_isnoneornil(L, 2))
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 2) != 0)
        {
            if (lua_type(L, -2) != LUA_TSTRING)
                luaL_error(L, "types.format: an option's name should be a string, but got %s", luaL_typename(L, -2));
            std::string key = lua_tostring(L, -2);

            TypeFormatValue value;
            if (lua_type(L, -1) == LUA_TBOOLEAN)
                value = bool(lua_toboolean(L, -1));
            else if (lua_type(L, -1) == LUA_TNUMBER)
                value = lua_tonumber(L, -1);
            else if (lua_type(L, -1) == LUA_TSTRING)
                value = std::string(lua_tostring(L, -1));
            else
                luaL_error(L, "types.format: option '%s' can't be a %s", key.c_str(), luaL_typename(L, -1));

            if (std::optional<std::string> error = setTypeFormatOption(options, key, value))
                luaL_error(L, "types.format: %s", error->c_str());
            lua_pop(L, 1);
        }
    }

    TypeFunctionRuntimeBuilderState* runtimeBuilder = Luau::getTypeFunctionRuntime(L)->runtimeBuilder;
    TypeId ty = Luau::deserialize(arg, runtimeBuilder);
    if (FFlag::LuauTypeFunctionStructuredErrors ? !runtimeBuilder->errors.empty() : !runtimeBuilder->errors_DEPRECATED.empty())
        luaL_error(L, "types.format: failed to deserialize the type");

    std::string formatted = formatType(ty, options);
    lua_pushlstring(L, formatted.data(), formatted.size());
    return 1;
}

void registerTypesLibrary(lua_State* L)
{
    luaL_Reg fields[] = {
        {"unknown", createUnknown},
        {"never", createNever},
        {"any", createAny},
        {"boolean", createBoolean},
        {"number", createNumber},
        {"string", createString},
        {"thread", createThread},
        {"buffer", createBuffer},
        {"none", createNone},
        {nullptr, nullptr}
    };

    luaL_Reg methods[] = {
        {"singleton", createSingleton},
        {"negationof", createNegation},
        {"unionof", createUnion},
        {"intersectionof", createIntersection},
        {"optional", createOptional},
        {"newtable", createTable},
        {"newfunction", createFunction},
        {"copy", deepCopy},
        {"generic", createGeneric},
        {"error", createError},
        {"newclass", createClass},
        {"newtrait", createTrait},
        {"format", formatTypeFunctionType},

        {nullptr, nullptr}
    };

    luaL_register(L, "types", methods);

    // Set fields for type userdata
    for (luaL_Reg* l = fields; l->name; l++)
    {
        l->func(L);
        lua_setfield(L, -2, l->name);
    }

    lua_pop(L, 1);
}

static int typeUserdataIndex(lua_State* L)
{
    TypeFunctionTypeId self = getTypeUserData(L, 1);
    const char* field = luaL_checkstring(L, 2);

    if (strcmp(field, "tag") == 0)
    {
        lua_pushstring(L, getTag(L, self).c_str());
        return 1;
    }

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_getfield(L, -1, field);
    return 1;
}

void registerTypeUserData(lua_State* L)
{
    luaL_Reg typeUserdataMethods[] = {
        {"is", checkTag},

        // Luwu: any type's metadata, a stopgap until the new type function API
        {"metadata", getMetadata},
        {"setmetadata", setMetadata},

        // Negation type methods
        {"inner", getNegatedValue},

        // Singleton type methods
        {"value", getSingletonValue},

        // Table type methods
        {"setproperty", setTableProp},
        {"setpropertycomments", setPropertyCommentsOf},
        {"setreadproperty", setReadTableProp},
        {"setwriteproperty", setWriteTableProp},
        {"readproperty", readTableProp},
        {"writeproperty", writeTableProp},
        {"properties", getProps},
        {"setindexer", setTableIndexer},
        {"setreadindexer", setTableReadIndexer},
        {"setwriteindexer", setTableWriteIndexer},
        {"indexer", getIndexer},
        {"readindexer", getReadIndexer},
        {"writeindexer", getWriteIndexer},
        {"setmetatable", setTableMetatable},
        {"metatable", getMetatable},

        // Function type methods
        {"setparameters", setFunctionParameters},
        {"parameters", getFunctionParameters},
        {"setreturns", setFunctionReturns},
        {"returns", getFunctionReturns},
        {"setgenerics", setFunctionGenerics},
        {"generics", getFunctionGenerics},
        // Luwu: stopgaps until the new type function API
        {"getparameternames", getFunctionParameterNames},
        {"setparametername", setFunctionParameterName},

        // Union and Intersection type methods
        {"components", getComponents},

        //  Extern type methods
        {"readparent", getReadParent},
        {"writeparent", getWriteParent},

        // Generic type methods
        {"name", getGenericName},
        {"ispack", getGenericIsPack},

        // Luwu Classes (rfcs/classes) and Luwu Traits (rfcs/classes/traits.md): object, class and trait methods (`name` is
        // shared with generics)
        {"class", getObjectClass},
        {"object", getNominalObject},
        {"traits", getNominalTraits},
        {"needs", getTraitNeeds},
        {"typearguments", getObjectTypeArguments},
        {"fields", getNominalFields},
        {"functions", getNominalFunctions},
        {"constructor", getClassConstructor},

        {nullptr, nullptr}
    };

    // Create and register metatable for type userdata
    luaL_newmetatable(L, "type");

    lua_pushstring(L, "type");
    lua_setfield(L, -2, "__type");

    // Protect metatable from being changed
    lua_pushstring(L, "The metatable is locked");
    lua_setfield(L, -2, "__metatable");

    lua_pushcfunction(L, isEqualToType, "__eq");
    lua_setfield(L, -2, "__eq");

    if (FFlag::LuauUdtfTypeToStringMetamethod)
    {
        lua_pushcfunction(L, typeToString, "__tostring");
        lua_setfield(L, -2, "__tostring");
    }

    // Indexing will be a dynamic function because some type fields are dynamic
    lua_newtable(L);
    luaL_register(L, nullptr, typeUserdataMethods);

    if (FFlag::LuauUdtfTypeIsSubtypeOf)
    {
        lua_pushcfunction(L, isSubtypeOf, "issubtypeof");
        lua_setfield(L, -2, "issubtypeof");
    }

    lua_setreadonly(L, -1, true);
    lua_pushcclosure(L, typeUserdataIndex, "__index", 1);
    lua_setfield(L, -2, "__index");

    lua_setreadonly(L, -1, true);

    if (FFlag::LuauUdtfTypeUseTaggedMetatable)
        // Sets up the metatable for the type userdata.
        lua_setuserdatametatable(L, kTypeUserdataTag);
    else
        lua_pop(L, 1);

    // Sets up a destructor for the type userdata.
    lua_setuserdatadtor(L, kTypeUserdataTag, deallocTypeUserData);
}

// Used to redirect all the removed global functions to say "this function is unsupported"
static int unsupportedFunction(lua_State* L)
{
    luaL_errorL(L, "this function is not supported in type functions");
}

static int print(lua_State* L)
{
    std::string result;

    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++)
    {
        size_t l = 0;
        const char* s = luaL_tolstring(L, i, &l); // convert to string using __tostring et al
        if (i > 1)
        {
            result.append(1, '\t');
        }
        result.append(s, l);
        lua_pop(L, 1);
    }

    auto ctx = getTypeFunctionRuntime(L);

    ctx->messages.push_back(std::move(result));

    return 0;
}

// Add libraries / globals for type function environment
void setTypeFunctionEnvironment(lua_State* L)
{
    // Register math library
    luaopen_math(L);
    lua_pop(L, 1);

    // Register table library
    luaopen_table(L);
    lua_pop(L, 1);

    // Register string library
    luaopen_string(L);
    lua_pop(L, 1);

    // Register bit32 library
    luaopen_bit32(L);
    lua_pop(L, 1);

    // Register utf8 library
    luaopen_utf8(L);
    lua_pop(L, 1);

    // Register buffer library
    luaopen_buffer(L);
    lua_pop(L, 1);

    // Register base library
    luaopen_base(L);
    lua_pop(L, 1);

    // Remove certain global functions from the base library
    static const char* unavailableGlobals[] = {"gcinfo", "getfenv", "newproxy", "setfenv", "pcall", "xpcall"};
    for (auto& name : unavailableGlobals)
    {
        lua_pushcfunction(L, unsupportedFunction, name);
        lua_setglobal(L, name);
    }

    lua_pushcfunction(L, print, "print");
    lua_setglobal(L, "print");
}

void resetTypeFunctionState(lua_State* L)
{
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushnumber(L, 0);
    lua_call(L, 1, 0);
    lua_pop(L, 1);
}

/*
 * Below are helper methods for __eq
 */
struct AreEqualState
{
    std::set<std::pair<const void*, const void*>> seen;
    int recursionCount = 0;
};

bool areEqual(AreEqualState& seen, const TypeFunctionType& lhs, const TypeFunctionType& rhs);
bool areEqual(AreEqualState& seen, const TypeFunctionTypePackVar& lhs, const TypeFunctionTypePackVar& rhs);

bool seenSetContains(AreEqualState& seen, const void* lhs, const void* rhs)
{
    if (lhs == rhs)
        return true;

    auto p = std::make_pair(lhs, rhs);
    if (seen.seen.find(p) != seen.seen.end())
        return true;

    seen.seen.insert(p);
    return false;
}

bool areEqual(AreEqualState& seen, const TypeFunctionSingletonType& lhs, const TypeFunctionSingletonType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    {
        const TypeFunctionBooleanSingleton* lp = get<TypeFunctionBooleanSingleton>(&lhs);
        const TypeFunctionBooleanSingleton* rp = get<TypeFunctionBooleanSingleton>(&rhs);
        if (lp && rp)
            return lp->value == rp->value;
    }

    {
        const TypeFunctionStringSingleton* lp = get<TypeFunctionStringSingleton>(&lhs);
        const TypeFunctionStringSingleton* rp = get<TypeFunctionStringSingleton>(&rhs);
        if (lp && rp)
            return lp->value == rp->value;
    }

    return false;
}

bool areEqual(AreEqualState& seen, const TypeFunctionUnionType& lhs, const TypeFunctionUnionType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    if (lhs.components.size() != rhs.components.size())
        return false;

    auto l = lhs.components.begin();
    auto r = rhs.components.begin();

    while (l != lhs.components.end())
    {
        if (!areEqual(seen, **l, **r))
            return false;
        ++l;
        ++r;
    }

    return true;
}

bool areEqual(AreEqualState& seen, const TypeFunctionIntersectionType& lhs, const TypeFunctionIntersectionType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    if (lhs.components.size() != rhs.components.size())
        return false;

    auto l = lhs.components.begin();
    auto r = rhs.components.begin();

    while (l != lhs.components.end())
    {
        if (!areEqual(seen, **l, **r))
            return false;
        ++l;
        ++r;
    }

    return true;
}

bool areEqual(AreEqualState& seen, const TypeFunctionNegationType& lhs, const TypeFunctionNegationType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    return areEqual(seen, *lhs.type, *rhs.type);
}

bool areEqual(AreEqualState& seen, const TypeFunctionTableType& lhs, const TypeFunctionTableType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    if (lhs.props.size() != rhs.props.size())
        return false;

    if (bool(lhs.indexer) != bool(rhs.indexer))
        return false;

    if (lhs.indexer && rhs.indexer)
    {
        if (!areEqual(seen, *lhs.indexer->keyType, *rhs.indexer->keyType))
            return false;

        if (!areEqual(seen, *lhs.indexer->valueType, *rhs.indexer->valueType))
            return false;
    }

    auto l = lhs.props.begin();
    auto r = rhs.props.begin();

    while (l != lhs.props.end())
    {
        if ((l->second.readTy && !r->second.readTy) || (!l->second.readTy && r->second.readTy))
            return false;

        if (l->second.readTy && r->second.readTy && !areEqual(seen, **(l->second.readTy), **(r->second.readTy)))
            return false;

        if ((l->second.writeTy && !r->second.writeTy) || (!l->second.writeTy && r->second.writeTy))
            return false;

        if (l->second.writeTy && r->second.writeTy && !areEqual(seen, **(l->second.writeTy), **(r->second.writeTy)))
            return false;

        ++l;
        ++r;
    }

    return true;
}

bool areEqual(AreEqualState& seen, const TypeFunctionFunctionType& lhs, const TypeFunctionFunctionType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    if (lhs.generics.size() != rhs.generics.size())
        return false;

    for (auto l = lhs.generics.begin(), r = rhs.generics.begin(); l != lhs.generics.end() && r != rhs.generics.end(); ++l, ++r)
    {
        if (!areEqual(seen, **l, **r))
            return false;
    }

    if (lhs.genericPacks.size() != rhs.genericPacks.size())
        return false;

    for (auto l = lhs.genericPacks.begin(), r = rhs.genericPacks.begin(); l != lhs.genericPacks.end() && r != rhs.genericPacks.end(); ++l, ++r)
    {
        if (!areEqual(seen, **l, **r))
            return false;
    }

    if (bool(lhs.argTypes) != bool(rhs.argTypes))
        return false;

    if (lhs.argTypes && rhs.argTypes)
    {
        if (!areEqual(seen, *lhs.argTypes, *rhs.argTypes))
            return false;
    }

    if (bool(lhs.retTypes) != bool(rhs.retTypes))
        return false;

    if (lhs.retTypes && rhs.retTypes)
    {
        if (!areEqual(seen, *lhs.retTypes, *rhs.retTypes))
            return false;
    }

    return true;
}

bool areEqual(AreEqualState& seen, const TypeFunctionExternType& lhs, const TypeFunctionExternType& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    return lhs.externTy == rhs.externTy;
}

bool areEqual(AreEqualState& seen, const TypeFunctionType& lhs, const TypeFunctionType& rhs)
{
    RecursionLimiter _ra("areEqual", &seen.recursionCount, 100);

    if (lhs.type.index() != rhs.type.index())
        return false;

    {
        const TypeFunctionPrimitiveType* lp = get<TypeFunctionPrimitiveType>(&lhs);
        const TypeFunctionPrimitiveType* rp = get<TypeFunctionPrimitiveType>(&rhs);
        if (lp && rp)
            return lp->type == rp->type;
    }

    if (get<TypeFunctionAnyType>(&lhs) && get<TypeFunctionAnyType>(&rhs))
        return true;

    if (get<TypeFunctionUnknownType>(&lhs) && get<TypeFunctionUnknownType>(&rhs))
        return true;

    if (get<TypeFunctionNeverType>(&lhs) && get<TypeFunctionNeverType>(&rhs))
        return true;

    {
        const TypeFunctionErrorType* le = get<TypeFunctionErrorType>(&lhs);
        const TypeFunctionErrorType* re = get<TypeFunctionErrorType>(&rhs);
        if (le && re)
            return le->message == re->message;
    }

    {
        const TypeFunctionSingletonType* lf = get<TypeFunctionSingletonType>(&lhs);
        const TypeFunctionSingletonType* rf = get<TypeFunctionSingletonType>(&rhs);
        if (lf && rf)
            return areEqual(seen, *lf, *rf);
    }

    {
        const TypeFunctionUnionType* lf = get<TypeFunctionUnionType>(&lhs);
        const TypeFunctionUnionType* rf = get<TypeFunctionUnionType>(&rhs);
        if (lf && rf)
            return areEqual(seen, *lf, *rf);
    }

    {
        const TypeFunctionIntersectionType* lf = get<TypeFunctionIntersectionType>(&lhs);
        const TypeFunctionIntersectionType* rf = get<TypeFunctionIntersectionType>(&rhs);
        if (lf && rf)
            return areEqual(seen, *lf, *rf);
    }

    {
        const TypeFunctionNegationType* lf = get<TypeFunctionNegationType>(&lhs);
        const TypeFunctionNegationType* rf = get<TypeFunctionNegationType>(&rhs);
        if (lf && rf)
            return areEqual(seen, *lf, *rf);
    }

    {
        const TypeFunctionTableType* lt = get<TypeFunctionTableType>(&lhs);
        const TypeFunctionTableType* rt = get<TypeFunctionTableType>(&rhs);
        if (lt && rt)
            return areEqual(seen, *lt, *rt);
    }

    {
        const TypeFunctionFunctionType* lf = get<TypeFunctionFunctionType>(&lhs);
        const TypeFunctionFunctionType* rf = get<TypeFunctionFunctionType>(&rhs);
        if (lf && rf)
            return areEqual(seen, *lf, *rf);
    }

    {
        const TypeFunctionExternType* lf = get<TypeFunctionExternType>(&lhs);
        const TypeFunctionExternType* rf = get<TypeFunctionExternType>(&rhs);
        if (lf && rf)
            return areEqual(seen, *lf, *rf);
    }

    {
        const TypeFunctionGenericType* lg = get<TypeFunctionGenericType>(&lhs);
        const TypeFunctionGenericType* rg = get<TypeFunctionGenericType>(&rhs);
        if (lg && rg)
            return lg->isNamed == rg->isNamed && lg->isPack == rg->isPack && lg->name == rg->name;
    }

    return false;
}

bool areEqual(AreEqualState& seen, const TypeFunctionTypePack& lhs, const TypeFunctionTypePack& rhs)
{
    if (lhs.head.size() != rhs.head.size())
        return false;

    auto l = lhs.head.begin();
    auto r = rhs.head.begin();

    while (l != lhs.head.end())
    {
        if (!areEqual(seen, **l, **r))
            return false;
        ++l;
        ++r;
    }

    return true;
}

bool areEqual(AreEqualState& seen, const TypeFunctionVariadicTypePack& lhs, const TypeFunctionVariadicTypePack& rhs)
{
    if (seenSetContains(seen, &lhs, &rhs))
        return true;

    return areEqual(seen, *lhs.type, *rhs.type);
}

bool areEqual(AreEqualState& seen, const TypeFunctionTypePackVar& lhs, const TypeFunctionTypePackVar& rhs)
{
    {
        const TypeFunctionTypePack* lb = get<TypeFunctionTypePack>(&lhs);
        const TypeFunctionTypePack* rb = get<TypeFunctionTypePack>(&rhs);
        if (lb && rb)
            return areEqual(seen, *lb, *rb);
    }

    {
        const TypeFunctionVariadicTypePack* lv = get<TypeFunctionVariadicTypePack>(&lhs);
        const TypeFunctionVariadicTypePack* rv = get<TypeFunctionVariadicTypePack>(&rhs);
        if (lv && rv)
            return areEqual(seen, *lv, *rv);
    }

    {
        const TypeFunctionGenericTypePack* lg = get<TypeFunctionGenericTypePack>(&lhs);
        const TypeFunctionGenericTypePack* rg = get<TypeFunctionGenericTypePack>(&rhs);
        if (lg && rg)
            return lg->isNamed == rg->isNamed && lg->name == rg->name;
    }

    return false;
}

bool TypeFunctionType::operator==(const TypeFunctionType& rhs) const
{
    AreEqualState seen;
    return areEqual(seen, *this, rhs);
}

bool TypeFunctionTypePackVar::operator==(const TypeFunctionTypePackVar& rhs) const
{
    AreEqualState seen;
    return areEqual(seen, *this, rhs);
}

TypeFunctionProperty TypeFunctionProperty::readonly(TypeFunctionTypeId ty)
{
    TypeFunctionProperty p;
    p.readTy = ty;
    return p;
}

TypeFunctionProperty TypeFunctionProperty::writeonly(TypeFunctionTypeId ty)
{
    TypeFunctionProperty p;
    p.writeTy = ty;
    return p;
}

TypeFunctionProperty TypeFunctionProperty::rw(TypeFunctionTypeId ty)
{
    return TypeFunctionProperty::rw(ty, ty);
}

TypeFunctionProperty TypeFunctionProperty::rw(TypeFunctionTypeId read, TypeFunctionTypeId write)
{
    TypeFunctionProperty p;
    p.readTy = read;
    p.writeTy = write;
    return p;
}

bool TypeFunctionProperty::isShared() const
{
    return readTy && writeTy && readTy == writeTy;
}

bool TypeFunctionProperty::isReadOnly() const
{
    return readTy && !writeTy;
}

bool TypeFunctionProperty::isWriteOnly() const
{
    return writeTy && !readTy;
}

/*
 * Below is a helper class for type.copy()
 * Forked version of Clone.cpp
 */

class TypeFunctionCloner
{
    using SeenTypes = DenseHashMap<TypeFunctionTypeId, TypeFunctionTypeId>;
    using SeenTypePacks = DenseHashMap<TypeFunctionTypePackId, TypeFunctionTypePackId>;

    NotNull<TypeFunctionRuntime> typeFunctionRuntime;

    // A queue of TypeFunctionTypeIds that have been cloned, but whose interior types hasn't
    // been updated to point to itself. Once all of its interior types
    // has been updated, it gets removed from the queue.

    // queue.back() should always return two of same type in their respective sides
    // For example `auto [first, second] = queue.back()`: if first is TypeFunctionPrimitiveType,
    // second must be TypeFunctionPrimitiveType; `second` is trying to copy `first`
    std::vector<std::tuple<TypeFunctionKind, TypeFunctionKind>> queue;

    SeenTypes types{{}};     // Mapping of TypeFunctionTypeIds that have been shallow cloned to TypeFunctionTypeIds
    SeenTypePacks packs{{}}; // Mapping of TypeFunctionTypePackIds that have been shallow cloned to TypeFunctionTypePackIds

    int steps = 0;

public:
    explicit TypeFunctionCloner(TypeFunctionRuntime* typeFunctionRuntime)
        : typeFunctionRuntime(typeFunctionRuntime)
    {
    }

    TypeFunctionTypeId clone(TypeFunctionTypeId ty)
    {
        shallowClone(ty);
        run();

        if (hasExceededIterationLimit())
            return nullptr;

        return find(ty).value_or(nullptr);
    }

    TypeFunctionTypePackId clone(TypeFunctionTypePackId tp)
    {
        shallowClone(tp);
        run();

        if (hasExceededIterationLimit())
            return nullptr;

        return find(tp).value_or(nullptr);
    }

private:
    bool hasExceededIterationLimit() const
    {
        return steps + queue.size() >= (size_t)DFInt::LuauTypeFunctionSerdeIterationLimit;
    }

    void run()
    {
        while (!queue.empty())
        {
            ++steps;

            if (hasExceededIterationLimit())
                break;

            auto [ty, tfti] = queue.back();
            queue.pop_back();

            cloneChildren(ty, tfti);
        }
    }

    std::optional<TypeFunctionTypeId> find(TypeFunctionTypeId ty) const
    {
        if (auto result = types.find(ty))
            return *result;

        return std::nullopt;
    }

    std::optional<TypeFunctionTypePackId> find(TypeFunctionTypePackId tp) const
    {
        if (auto result = packs.find(tp))
            return *result;

        return std::nullopt;
    }

    std::optional<TypeFunctionKind> find(TypeFunctionKind kind) const
    {
        if (auto ty = get<TypeFunctionTypeId>(kind))
            return find(*ty);
        else if (auto tp = get<TypeFunctionTypePackId>(kind))
            return find(*tp);
        else
        {
            LUAU_ASSERT(!"Unknown kind?");
            return std::nullopt;
        }
    }

    TypeFunctionTypeId shallowClone(TypeFunctionTypeId ty)
    {
        if (auto it = find(ty))
            return *it;

        // Create a shallow serialization
        TypeFunctionTypeId target = {};
        if (auto p = get<TypeFunctionPrimitiveType>(ty))
        {
            switch (p->type)
            {
            case TypeFunctionPrimitiveType::NilType:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::NilType));
                break;
            case TypeFunctionPrimitiveType::Boolean:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Boolean));
                break;
            case TypeFunctionPrimitiveType::Number:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Number));
                break;
            case TypeFunctionPrimitiveType::Integer:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Integer));
                break;
            case TypeFunctionPrimitiveType::String:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::String));
                break;
            case TypeFunctionPrimitiveType::Thread:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Thread));
                break;
            case TypeFunctionPrimitiveType::Buffer:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::Buffer));
                break;
            case TypeFunctionPrimitiveType::None:
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionPrimitiveType(TypeFunctionPrimitiveType::None));
                break;
            default:
                break;
            }
        }
        else if (get<TypeFunctionUnknownType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionUnknownType{});
        else if (get<TypeFunctionNeverType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionNeverType{});
        else if (get<TypeFunctionAnyType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionAnyType{});
        else if (auto s = get<TypeFunctionSingletonType>(ty))
        {
            if (auto bs = get<TypeFunctionBooleanSingleton>(s))
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionSingletonType{TypeFunctionBooleanSingleton{bs->value}});
            else if (auto ss = get<TypeFunctionStringSingleton>(s))
                target = typeFunctionRuntime->typeArena.allocate(TypeFunctionSingletonType{TypeFunctionStringSingleton{ss->value}});
        }
        else if (get<TypeFunctionUnionType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionUnionType{{}});
        else if (get<TypeFunctionIntersectionType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionIntersectionType{{}});
        else if (get<TypeFunctionNegationType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionNegationType{{}});
        else if (get<TypeFunctionTableType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionTableType{{}, std::nullopt, std::nullopt});
        else if (get<TypeFunctionFunctionType>(ty))
        {
            TypeFunctionTypePackId emptyTypePack = typeFunctionRuntime->typePackArena.allocate(TypeFunctionTypePack{});
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionFunctionType{{}, {}, emptyTypePack, emptyTypePack});
        }
        else if (get<TypeFunctionExternType>(ty))
            target = ty; // Don't copy a class since they are immutable
        else if (auto g = get<TypeFunctionGenericType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionGenericType{g->isNamed, g->isPack, g->name});
        else if (auto e = get<TypeFunctionErrorType>(ty))
            target = typeFunctionRuntime->typeArena.allocate(TypeFunctionErrorType{e->message});
        else
            LUAU_ASSERT(!"Unknown type");

        types[ty] = target;
        queue.emplace_back(ty, target);

        // Luwu: a copy keeps the original's docs and metadata (an extern type isn't copied at all)
        if (target && target != ty)
        {
            TypeFunctionType* mutableTarget = const_cast<TypeFunctionType*>(target);
            mutableTarget->origin = ty->origin;
            mutableTarget->metadata = ty->metadata;
            mutableTarget->edited = ty->edited;
        }

        return target;
    }

    TypeFunctionTypePackId shallowClone(TypeFunctionTypePackId tp)
    {
        if (auto it = find(tp))
            return *it;

        // Create a shallow serialization
        TypeFunctionTypePackId target = {};
        if (get<TypeFunctionTypePack>(tp))
            target = typeFunctionRuntime->typePackArena.allocate(TypeFunctionTypePack{{}});
        else if (get<TypeFunctionVariadicTypePack>(tp))
            target = typeFunctionRuntime->typePackArena.allocate(TypeFunctionVariadicTypePack{});
        else if (auto gPack = get<TypeFunctionGenericTypePack>(tp))
            target = typeFunctionRuntime->typePackArena.allocate(TypeFunctionGenericTypePack{gPack->isNamed, gPack->name});
        else
            LUAU_ASSERT(!"Unknown type");

        packs[tp] = target;
        queue.emplace_back(tp, target);
        return target;
    }

    void cloneChildren(TypeFunctionTypeId ty, TypeFunctionTypeId tfti)
    {
        if (auto [p1, p2] = std::tuple{getMutable<TypeFunctionPrimitiveType>(ty), getMutable<TypeFunctionPrimitiveType>(tfti)}; p1 && p2)
            cloneChildren(p1, p2);
        else if (auto [u1, u2] = std::tuple{getMutable<TypeFunctionUnknownType>(ty), getMutable<TypeFunctionUnknownType>(tfti)}; u1 && u2)
            cloneChildren(u1, u2);
        else if (auto [n1, n2] = std::tuple{getMutable<TypeFunctionNeverType>(ty), getMutable<TypeFunctionNeverType>(tfti)}; n1 && n2)
            cloneChildren(n1, n2);
        else if (auto [a1, a2] = std::tuple{getMutable<TypeFunctionAnyType>(ty), getMutable<TypeFunctionAnyType>(tfti)}; a1 && a2)
            cloneChildren(a1, a2);
        else if (auto [s1, s2] = std::tuple{getMutable<TypeFunctionSingletonType>(ty), getMutable<TypeFunctionSingletonType>(tfti)}; s1 && s2)
            cloneChildren(s1, s2);
        else if (auto [u1, u2] = std::tuple{getMutable<TypeFunctionUnionType>(ty), getMutable<TypeFunctionUnionType>(tfti)}; u1 && u2)
            cloneChildren(u1, u2);
        else if (auto [i1, i2] = std::tuple{getMutable<TypeFunctionIntersectionType>(ty), getMutable<TypeFunctionIntersectionType>(tfti)}; i1 && i2)
            cloneChildren(i1, i2);
        else if (auto [n1, n2] = std::tuple{getMutable<TypeFunctionNegationType>(ty), getMutable<TypeFunctionNegationType>(tfti)}; n1 && n2)
            cloneChildren(n1, n2);
        else if (auto [t1, t2] = std::tuple{getMutable<TypeFunctionTableType>(ty), getMutable<TypeFunctionTableType>(tfti)}; t1 && t2)
            cloneChildren(t1, t2);
        else if (auto [f1, f2] = std::tuple{getMutable<TypeFunctionFunctionType>(ty), getMutable<TypeFunctionFunctionType>(tfti)}; f1 && f2)
            cloneChildren(f1, f2);
        else if (auto [c1, c2] = std::tuple{getMutable<TypeFunctionExternType>(ty), getMutable<TypeFunctionExternType>(tfti)}; c1 && c2)
            cloneChildren(c1, c2);
        else if (auto [g1, g2] = std::tuple{getMutable<TypeFunctionGenericType>(ty), getMutable<TypeFunctionGenericType>(tfti)}; g1 && g2)
            cloneChildren(g1, g2);
        else if (get<TypeFunctionErrorType>(ty) && get<TypeFunctionErrorType>(tfti))
            return; // the message is copied with the type
        else
            LUAU_ASSERT(!"Unknown pair?"); // First and argument should always represent the same types
    }

    void cloneChildren(TypeFunctionTypePackId tp, TypeFunctionTypePackId tftp)
    {
        if (auto [tPack1, tPack2] = std::tuple{getMutable<TypeFunctionTypePack>(tp), getMutable<TypeFunctionTypePack>(tftp)}; tPack1 && tPack2)
            cloneChildren(tPack1, tPack2);
        else if (auto [vPack1, vPack2] = std::tuple{getMutable<TypeFunctionVariadicTypePack>(tp), getMutable<TypeFunctionVariadicTypePack>(tftp)};
                 vPack1 && vPack2)
            cloneChildren(vPack1, vPack2);
        else if (auto [gPack1, gPack2] = std::tuple{getMutable<TypeFunctionGenericTypePack>(tp), getMutable<TypeFunctionGenericTypePack>(tftp)};
                 gPack1 && gPack2)
            cloneChildren(gPack1, gPack2);
        else
            LUAU_ASSERT(!"Unknown pair?"); // First and argument should always represent the same types
    }

    void cloneChildren(TypeFunctionKind kind, TypeFunctionKind tfkind)
    {
        if (auto [ty, tfty] = std::tuple{get<TypeFunctionTypeId>(kind), get<TypeFunctionTypeId>(tfkind)}; ty && tfty)
            cloneChildren(*ty, *tfty);
        else if (auto [tp, tftp] = std::tuple{get<TypeFunctionTypePackId>(kind), get<TypeFunctionTypePackId>(tfkind)}; tp && tftp)
            cloneChildren(*tp, *tftp);
        else
            LUAU_ASSERT(!"Unknown pair?"); // First and argument should always represent the same types
    }

    void cloneChildren(TypeFunctionPrimitiveType* p1, TypeFunctionPrimitiveType* p2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionUnknownType* u1, TypeFunctionUnknownType* u2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionNeverType* n1, TypeFunctionNeverType* n2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionAnyType* a1, TypeFunctionAnyType* a2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionSingletonType* s1, TypeFunctionSingletonType* s2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionUnionType* u1, TypeFunctionUnionType* u2)
    {
        for (TypeFunctionTypeId& ty : u1->components)
            u2->components.push_back(shallowClone(ty));
    }

    void cloneChildren(TypeFunctionIntersectionType* i1, TypeFunctionIntersectionType* i2)
    {
        for (TypeFunctionTypeId& ty : i1->components)
            i2->components.push_back(shallowClone(ty));
    }

    void cloneChildren(TypeFunctionNegationType* n1, TypeFunctionNegationType* n2)
    {
        n2->type = shallowClone(n1->type);
    }

    void cloneChildren(TypeFunctionTableType* t1, TypeFunctionTableType* t2)
    {
        for (auto& [k, p] : t1->props)
        {
            std::optional<TypeFunctionTypeId> readTy;
            if (p.readTy)
                readTy = shallowClone(*p.readTy);

            std::optional<TypeFunctionTypeId> writeTy;
            if (p.writeTy)
                writeTy = shallowClone(*p.writeTy);

            // Luwu: the copy keeps the property's docs
            TypeFunctionProperty& copied = t2->props[k];
            copied = p;
            copied.readTy = readTy;
            copied.writeTy = writeTy;
        }

        if (t1->indexer.has_value())
        {
            t2->indexer = TypeFunctionTableIndexer(
                shallowClone(t1->indexer->keyType),
                shallowClone(t1->indexer->valueType),
                FFlag::LuauTypeFunctionTableIndexerIsReadOnly ? t1->indexer->isReadOnly : false
            );
        }

        if (t1->metatable.has_value())
            t2->metatable = shallowClone(*t1->metatable);
    }

    void cloneChildren(TypeFunctionFunctionType* f1, TypeFunctionFunctionType* f2)
    {
        f2->generics.reserve(f1->generics.size());
        for (auto ty : f1->generics)
            f2->generics.push_back(shallowClone(ty));

        f2->genericPacks.reserve(f1->genericPacks.size());
        for (auto tp : f1->genericPacks)
            f2->genericPacks.push_back(shallowClone(tp));

        f2->argTypes = shallowClone(f1->argTypes);
        f2->retTypes = shallowClone(f1->retTypes);
        if (FFlag::LuauTypeFunctionSerializeArgNames)
            f2->argNames = f1->argNames;
    }

    void cloneChildren(TypeFunctionExternType* c1, TypeFunctionExternType* c2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionGenericType* g1, TypeFunctionGenericType* g2)
    {
        // noop.
    }

    void cloneChildren(TypeFunctionTypePack* t1, TypeFunctionTypePack* t2)
    {
        for (TypeFunctionTypeId& ty : t1->head)
            t2->head.push_back(shallowClone(ty));

        if (t1->tail)
            t2->tail = shallowClone(*t1->tail);
    }

    void cloneChildren(TypeFunctionVariadicTypePack* v1, TypeFunctionVariadicTypePack* v2)
    {
        v2->type = shallowClone(v1->type);
    }

    void cloneChildren(TypeFunctionGenericTypePack* g1, TypeFunctionGenericTypePack* g2)
    {
        // noop.
    }
};

TypeFunctionTypeId deepClone(NotNull<TypeFunctionRuntime> runtime, TypeFunctionTypeId ty)
{
    return TypeFunctionCloner(runtime).clone(ty);
}

} // namespace Luau
