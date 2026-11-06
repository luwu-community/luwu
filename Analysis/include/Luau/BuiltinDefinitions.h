// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Scope.h"
#include "Luau/Type.h"

#include <optional>

namespace Luau
{

inline constexpr char kRequireTagName[] = "require";

struct Frontend;
struct GlobalTypes;
struct TypeChecker;
struct TypeArena;
struct Subtyping;

struct MagicRequire final : MagicFunction
{
    std::optional<WithPredicate<TypePackId>> handleOldSolver(
        struct TypeChecker&,
        const std::shared_ptr<struct Scope>&,
        const class AstExprCall&,
        WithPredicate<TypePackId>
    ) override;
    bool infer(const MagicFunctionCallContext& context) override;
};

// Overrides `class.fields`'s declared `({ [string]: unknown }, boolean)` return with the precise
// per-field type map (and a literal `complete` boolean) when the argument is a known class or
// object type. Exposed here (rather than kept file-local in BuiltinDefinitions.cpp) so fixtures
// that load their own class library declarations, like ClassesFixture in
// tests/TypeInfer.classes.test.cpp, can attach it too.
struct MagicClassFields final : MagicFunction
{
    std::optional<WithPredicate<TypePackId>> handleOldSolver(
        struct TypeChecker&,
        const std::shared_ptr<struct Scope>&,
        const class AstExprCall&,
        WithPredicate<TypePackId>
    ) override;
    bool infer(const MagicFunctionCallContext& context) override;
};

// Narrows `class.name`'s declared `string` return to the class's name as a string singleton when the
// argument is a class or object type, and to a union of singletons for a union of them. Exposed for
// the same reason as MagicClassFields.
// Luwu class reflection (rfcs/classes): types `class.member(x, "name")` as `ClassMember<T>`, with `T` the member's type,
// and reports a name the class doesn't have
struct MagicClassMember final : MagicFunction
{
    std::optional<WithPredicate<TypePackId>> handleOldSolver(
        struct TypeChecker&,
        const std::shared_ptr<struct Scope>&,
        const class AstExprCall&,
        WithPredicate<TypePackId>
    ) override;
    bool infer(const MagicFunctionCallContext& context) override;
    bool typeCheck(const MagicFunctionTypeCheckContext& context) override;
};

struct MagicClassName final : MagicFunction
{
    std::optional<WithPredicate<TypePackId>> handleOldSolver(
        struct TypeChecker&,
        const std::shared_ptr<struct Scope>&,
        const class AstExprCall&,
        WithPredicate<TypePackId>
    ) override;
    bool infer(const MagicFunctionCallContext& context) override;
};

// Luwu Classes (rfcs/classes): narrows `class.of`'s declared `class?` to the class of an object type, `class<Trait>` for
// an object known by a trait, and a union of them for a union. Exposed for the same reason as MagicClassFields.
struct MagicClassOf final : MagicFunction
{
    std::optional<WithPredicate<TypePackId>> handleOldSolver(
        struct TypeChecker&,
        const std::shared_ptr<struct Scope>&,
        const class AstExprCall&,
        WithPredicate<TypePackId>
    ) override;
    bool infer(const MagicFunctionCallContext& context) override;
};

// Luwu Classes (rfcs/classes): `class.isinstance(x, C)` and `class.implements(x, Trait)` refine `x` to the object type of
// their second argument. It's done by the function's type rather than by spelling, so an alias refines too (`const is =
// class.isinstance`), as it fuses in the compiler (getBuiltin follows the local). Exposed for the same reason as
// MagicClassFields.
struct MagicClassInstanceCheck final : MagicFunction
{
    std::optional<WithPredicate<TypePackId>> handleOldSolver(
        struct TypeChecker&,
        const std::shared_ptr<struct Scope>&,
        const class AstExprCall&,
        WithPredicate<TypePackId>
    ) override;
    bool infer(const MagicFunctionCallContext& context) override;
    void refine(const MagicRefinementContext& context) override;
};

void registerBuiltinGlobals(Frontend& frontend, GlobalTypes& globals, bool typeCheckForAutocomplete = false);
TypeId makeUnion(TypeArena& arena, std::vector<TypeId>&& types);
TypeId makeIntersection(TypeArena& arena, std::vector<TypeId>&& types);

/** Build an optional 't'
 */
TypeId makeOption(NotNull<BuiltinTypes> builtinTypes, TypeArena& arena, TypeId t);

/** Small utility function for building up type definitions from C++.
 */
TypeId makeFunction( // Monomorphic
    TypeArena& arena,
    std::optional<TypeId> selfType,
    std::initializer_list<TypeId> paramTypes,
    std::initializer_list<TypeId> retTypes,
    bool checked = false
);

TypeId makeFunction( // Polymorphic
    TypeArena& arena,
    std::optional<TypeId> selfType,
    std::initializer_list<TypeId> generics,
    std::initializer_list<TypePackId> genericPacks,
    std::initializer_list<TypeId> paramTypes,
    std::initializer_list<TypeId> retTypes,
    bool checked = false
);

TypeId makeFunction( // Monomorphic
    TypeArena& arena,
    std::optional<TypeId> selfType,
    std::initializer_list<TypeId> paramTypes,
    std::initializer_list<std::string> paramNames,
    std::initializer_list<TypeId> retTypes,
    bool checked = false
);

TypeId makeFunction( // Polymorphic
    TypeArena& arena,
    std::optional<TypeId> selfType,
    std::initializer_list<TypeId> generics,
    std::initializer_list<TypePackId> genericPacks,
    std::initializer_list<TypeId> paramTypes,
    std::initializer_list<std::string> paramNames,
    std::initializer_list<TypeId> retTypes,
    bool checked = false
);

void attachMagicFunction(TypeId ty, std::shared_ptr<MagicFunction> magic);
Property makeProperty(TypeId ty, std::optional<std::string> documentationSymbol = std::nullopt);
void assignPropDocumentationSymbols(TableType::Props& props, const std::string& baseName);

std::string getBuiltinDefinitionSource();

// Luwu try expressions: whether Analysis has the `Ok`, `Err`, `Some` and `None` globals: the flags the VM opens them
// with (luaL_openlibs), LuwuDeclareStatements, which parses their declarations, and LuwuGenericNominals, which types
// their generic classes
bool resultLibraryEnabled();
// Luwu class reflection (rfcs/classes): whether `class.member`, `class.ClassMember` and `class.fields` listing
// ClassMembers are declared
bool classReflectionEnabled();
// Luwu collections: whether Analysis has the `collections` library: the flags the VM opens it with
// (luaL_openlibs), and LuwuDeclareStatements, which parses its declarations
bool collectionsLibraryEnabled();
// Luwu collections: the library's declarations, a definition file of its own (see registerBuiltinGlobals)
std::string getCollectionsDefinitionSource();
// Luwu bundles: the `bundle` library's declarations, opened with `collections` (see registerBuiltinGlobals)
std::string getBundleDefinitionSource();
std::string getTypeFunctionDefinitionSource();

void addGlobalBinding(GlobalTypes& globals, const std::string& name, TypeId ty, const std::string& packageName);
void addGlobalBinding(GlobalTypes& globals, const std::string& name, Binding binding);
void addGlobalBinding(GlobalTypes& globals, const ScopePtr& scope, const std::string& name, TypeId ty, const std::string& packageName);
void addGlobalBinding(GlobalTypes& globals, const ScopePtr& scope, const std::string& name, Binding binding);
std::optional<Binding> tryGetGlobalBinding(GlobalTypes& globals, const std::string& name);
Binding* tryGetGlobalBindingRef(GlobalTypes& globals, const std::string& name);
TypeId getGlobalBinding(GlobalTypes& globals, const std::string& name);


/** A number of built-in functions are magical enough that we need to match on them specifically by
 * name when they are called. These are listed here to be used whenever necessary, instead of duplicating this logic repeatedly.
 */

bool matchSetMetatable(const AstExprCall& call);
bool matchTableFreeze(const AstExprCall& call);
bool matchAssert(const AstExprCall& call);
bool matchTypeOf(const AstExprCall& call);

// Returns `true` if the function should introduce typestate for its first argument.
bool shouldTypestateForFirstArgument(const AstExprCall& call);

} // namespace Luau
