// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/Config.h"
#include "Luau/Helpifier.h"
#include "Luau/Location.h"
#include "Luau/NotNull.h"
#include "Luau/Type.h"
#include "Luau/TypeFunctionError.h"
#include "Luau/TypeIds.h"
#include "Luau/Variant.h"

#include <set>
#include <string>
#include <vector>

namespace Luau
{

struct FileResolver;
struct TypeArena;
struct TypeError;

// Luwu (helpful subtyping errors): path notation a mismatch explanation can use. Kept as bits on the error so
// whatever shows several errors together (an editor hover, the CLI's output) can print one legend for
// all of them.
enum MismatchNotation : uint8_t
{
    MismatchNotationUnionMember = 1 << 0,  // `Drop#2`
    MismatchNotationIndexer = 1 << 1,      // `[string]`, a value in a map
    MismatchNotationExpectedRoot = 1 << 2, // a path starting at `expected`
    MismatchNotationGotRoot = 1 << 3,      // a path starting at `given`
    MismatchNotationOverload = 1 << 4,     // `Lookup#2`, one overload of a function
    MismatchNotationArrayElement = 1 << 5, // `[i]`, an element of an array
};

// The legend for `notation`, a help; an empty string when it uses none of the notation or help is off.
std::string mismatchNotationLegend(uint8_t notation, const Helpifier& helpifier);

// Luwu (helpful subtyping errors): the `TypeMismatch::contextVerb` phrases for a mismatch in a function's
// parameters and in its return values. The renderer words the rest of the message by which one it is.
inline constexpr const char* mismatchContextFunctionTakes = "this function to take";
inline constexpr const char* mismatchContextFunctionReturns = "this function to return";

// Luwu (helpful subtyping errors): `type Animal = { ... }` under an expected/got message, for a type the
// message would otherwise print several times. The name is made up for the message (or is an alias in
// scope with the same structure), and the message prints it in every place the type appears.
struct NamedTypeDeclaration
{
    std::string name;
    std::string expansion;

    bool operator==(const NamedTypeDeclaration& rhs) const
    {
        return name == rhs.name && expansion == rhs.expansion;
    }
};

struct TypeMismatch
{
    enum Context
    {
        CovariantContext,
        InvariantContext
    };

    TypeMismatch() = default;
    TypeMismatch(TypeId wantedType, TypeId givenType);
    TypeMismatch(TypeId wantedType, TypeId givenType, std::string reason);
    TypeMismatch(TypeId wantedType, TypeId givenType, std::string reason, std::optional<TypeError> error);

    TypeMismatch(TypeId wantedType, TypeId givenType, Context context);
    TypeMismatch(TypeId wantedType, TypeId givenType, std::string reason, Context context);
    TypeMismatch(TypeId wantedType, TypeId givenType, std::string reason, std::optional<TypeError> error, Context context);

    TypeId wantedType = nullptr;
    TypeId givenType = nullptr;
    Context context = CovariantContext;

    std::string reason;
    std::shared_ptr<TypeError> error;

    // When set, overrides the generic "Expected this to be" preamble with a more specific
    // phrase (e.g. "this function to return"), so the message reads "Expected this function to
    // return 'X' but got 'Y'" instead of leaving the reader to infer context from the reason
    // text alone. Not part of any constructor; set directly on the constructed value.
    std::optional<std::string> contextVerb;

    // Luwu (helpful subtyping errors): when `contextVerb` is set, the preamble names a specific part of
    // the type, so printing the root `wantedType`/`givenType` after it would contradict what the preamble
    // just promised ("Expected this function to return '() -> Widget'"). These hold the stringified types
    // at the end of that shared context path, and are printed in their place when set.
    std::optional<std::string> contextWantedDisplay;
    std::optional<std::string> contextGivenDisplay;

    // Luwu (helpful subtyping errors): a bespoke explanation, printed verbatim in place of the whole
    // "Expected this to be 'X', but got 'Y'" rendering. Some mismatches are far better explained by a
    // sentence written for that one case, but they are still type mismatches: keeping them as one preserves
    // `wantedType` and `givenType` for everything downstream that reads them rather than the message.
    std::optional<std::string> overrideMessage;

    // Luwu (helpful subtyping errors): printed in place of a type that has a name the reader wrote.
    std::optional<std::string> wantedName;
    std::optional<std::string> givenName;
    // The name the given value has in the code: the variable it was read from, or where a literal is being put. The
    // message calls the value by it (`Expected 'settings' to be`), and the reason's paths start at it.
    std::optional<std::string> givenVariable;
    // Luwu (helpful subtyping errors): the `where` block under the two types. When it isn't empty,
    // `wantedName` and `givenName` hold the two types printed with these names.
    std::vector<NamedTypeDeclaration> whereDeclarations;

    // Luwu (helpful subtyping errors): what `explainMismatch` adds when it explains the mismatch.
    struct Explained
    {
        // An aliased union has no name of its own to print, so its `= ...` expansion goes on the line below the name.
        std::optional<std::string> wantedExpansion;
        std::optional<std::string> givenExpansion;
        // `MismatchNotation` bits used by `reason`.
        uint8_t notation = 0;

        bool operator==(const Explained& rhs) const;
    };

    // Set when `explainMismatch` explained the mismatch. The message then prints union members in the order they were
    // written, not sorted, because the reason numbers them (`Drop#2`) and the numbers have to match what is printed;
    // and it says "but was given", matching the `given` its paths are rooted at.
    std::optional<Explained> explained;

    bool operator==(const TypeMismatch& rhs) const;
};

struct UnknownSymbol
{
    enum Context
    {
        Binding,
        Type,
    };
    Name name;
    Context context;

    // Luwu: for a module-qualified type `x.A`, what `x` turned out to be, so the message can say which half is wrong.
    // Upstream reports every case as "Unknown type 'x.A'".
    enum class Qualifier
    {
        // Not qualified, or a require that didn't resolve (which has its own error already)
        Unknown,
        // `x` isn't bound to a require
        NotAModule,
        // `x` is a required module that exports no type `A`
        NotExported,
    };
    Qualifier qualifier = Qualifier::Unknown;
    Name module;
    Name member;

    // Luwu: why a variable's name isn't bound where it's used. Upstream says "Unknown global 'x'; consider assigning
    // to it first" every time.
    enum class Unbound
    {
        // No local in the file has the name
        NotDefined,
        // `meant` is in scope and spelled almost the same
        Typo,
        // The local at `definition` is declared after this use
        BeforeDefinition,
        // The local at `definition` is in a block that doesn't contain this use
        InnerScope,
    };
    Unbound unbound = Unbound::NotDefined;
    Name meant;
    std::optional<Location> definition;

    bool operator==(const UnknownSymbol& rhs) const;
};

// The error for a type reference that names no type in `scope`.
UnknownSymbol unknownTypeReference(const Scope& scope, const AstTypeReference& reference);

struct UnknownProperty
{
    TypeId table;
    Name key;
    // Luwu: the language of the module the key is read in, for keys that exist in only one of the languages
    // (`class.classof` is upstream's). Set when the error is reported, from the module's config.
    Language language = Language::Luwu;

    bool operator==(const UnknownProperty& rhs) const;
};

struct NotATable
{
    TypeId ty;

    bool operator==(const NotATable& rhs) const;
};

struct CannotExtendTable
{
    enum Context
    {
        Property,
        Indexer,
        Metatable
    };
    TypeId tableType;
    Context context;
    Name prop;

    bool operator==(const CannotExtendTable& rhs) const;
};

struct CannotCompareUnrelatedTypes
{
    TypeId left;
    TypeId right;
    AstExprBinary::Op op;

    // Luwu Classes (rfcs/classes): how the source spells each operand (`o`, `a.pet`) when it is a name, for the help on
    // comparing a class with an object
    std::optional<std::string> leftName = std::nullopt;
    std::optional<std::string> rightName = std::nullopt;

    bool operator==(const CannotCompareUnrelatedTypes& rhs) const;
};

struct OnlyTablesCanHaveMethods
{
    TypeId tableType;

    bool operator==(const OnlyTablesCanHaveMethods& rhs) const;
};

struct DuplicateTypeDefinition
{
    Name name;
    std::optional<Location> previousLocation;

    bool operator==(const DuplicateTypeDefinition& rhs) const;
};

struct CountMismatch
{
    enum Context
    {
        Arg,
        FunctionResult,
        ExprListResult,
        Return,
    };
    size_t expected;
    std::optional<size_t> maximum;
    size_t actual;
    Context context = Arg;
    bool isVariadic = false;
    std::string function;

    bool operator==(const CountMismatch& rhs) const;
};

struct FunctionDoesNotTakeSelf
{
    bool operator==(const FunctionDoesNotTakeSelf& rhs) const;
};

struct FunctionRequiresSelf
{
    bool operator==(const FunctionRequiresSelf& rhs) const;
};

struct OccursCheckFailed
{
    bool operator==(const OccursCheckFailed& rhs) const;
};

struct UnknownRequire
{
    std::string modulePath;

    bool operator==(const UnknownRequire& rhs) const;
};

struct IncorrectGenericParameterCount
{
    Name name;
    TypeFun typeFun;
    size_t actualParameters;
    size_t actualPackParameters;

    bool operator==(const IncorrectGenericParameterCount& rhs) const;
};

struct SyntaxError
{
    std::string message;

    bool operator==(const SyntaxError& rhs) const;
};

struct CodeTooComplex
{
    bool operator==(const CodeTooComplex&) const;
};

struct UnificationTooComplex
{
    bool operator==(const UnificationTooComplex&) const;
};

// Could easily be folded into UnknownProperty with an extra field, std::set<Name> candidates.
// But for telemetry purposes, we want to have this be a distinct variant.
struct UnknownPropButFoundLikeProp
{
    TypeId table;
    Name key;
    std::set<Name> candidates;

    bool operator==(const UnknownPropButFoundLikeProp& rhs) const;
};

struct GenericError
{
    std::string message;

    bool operator==(const GenericError& rhs) const;
};

struct InternalError
{
    std::string message;

    bool operator==(const InternalError& rhs) const;
};

struct ConstraintSolvingIncompleteError
{
    bool operator==(const ConstraintSolvingIncompleteError& rhs) const;
};

struct CannotCallNonFunction
{
    TypeId ty;

    bool operator==(const CannotCallNonFunction& rhs) const;
};

struct ExtraInformation
{
    std::string message;
    bool operator==(const ExtraInformation& rhs) const;
};

struct DeprecatedApiUsed
{
    std::string symbol;
    std::string useInstead;
    bool operator==(const DeprecatedApiUsed& rhs) const;
};

struct ModuleHasCyclicDependency
{
    std::vector<ModuleName> cycle;
    bool operator==(const ModuleHasCyclicDependency& rhs) const;
};

struct FunctionExitsWithoutReturning
{
    TypePackId expectedReturnType;

    // Luwu (helpful subtyping errors): see TypeMismatch::overrideMessage. A function with no return
    // statement at all is not a case of "not all code paths", and when the declared type is `nil` the
    // distinction it is failing on -- returning the value `nil` versus returning nothing -- is worth
    // spelling out.
    std::optional<std::string> overrideMessage;

    bool operator==(const FunctionExitsWithoutReturning& rhs) const;
};

struct IllegalRequire
{
    std::string moduleName;
    std::string reason;

    // Luwu: a module whose returns can't be required gets its own message and help, which `reason` isn't used for
    enum class Returns
    {
        Other,
        Nothing,
        TooMany,
    };
    Returns returns = Returns::Other;
    // with TooMany: how many values the module returns, a minimum when the last one is a call or `...`
    size_t returnCount = 0;
    bool returnCountIsMinimum = false;

    bool operator==(const IllegalRequire& rhs) const;
};

struct MissingProperties
{
    enum Context
    {
        Missing,
        Extra
    };
    TypeId superType;
    TypeId subType;
    std::vector<Name> properties;
    Context context = Missing;

    // Luwu (helpful subtyping errors): the properties are missing from a table literal. The message names the literal
    // by `givenName` (what it's being assigned to) when it has one, and lists each missing property with its type.
    bool fromTableLiteral = false;
    std::optional<std::string> givenName;
    // The name of the type the table is for, which can be a union the literal was narrowed to one member
    // of (`GameEvent`), so the message doesn't name a member the reader never wrote.
    std::optional<std::string> wantedName;

    bool operator==(const MissingProperties& rhs) const;
};

struct DuplicateGenericParameter
{
    std::string parameterName;

    bool operator==(const DuplicateGenericParameter& rhs) const;
};

struct CannotInferBinaryOperation
{
    enum OpKind
    {
        Operation,
        Comparison,
    };

    AstExprBinary::Op op;
    std::optional<std::string> suggestedToAnnotate;
    OpKind kind;

    bool operator==(const CannotInferBinaryOperation& rhs) const;
};

struct SwappedGenericTypeParameter
{
    enum Kind
    {
        Type,
        Pack,
    };

    std::string name;
    // What was `name` being used as?
    Kind kind;

    bool operator==(const SwappedGenericTypeParameter& rhs) const;
};

struct OptionalValueAccess
{
    TypeId optional;

    bool operator==(const OptionalValueAccess& rhs) const;
};

struct MissingUnionProperty
{
    TypeId type;
    std::vector<TypeId> missing;
    Name key;
    // Luwu: whether the message carries help (see Helpifier)
    bool showHelp = true;

    bool operator==(const MissingUnionProperty& rhs) const;
};

struct TypesAreUnrelated
{
    TypeId left;
    TypeId right;

    bool operator==(const TypesAreUnrelated& rhs) const;
};

struct NormalizationTooComplex
{
    bool operator==(const NormalizationTooComplex&) const
    {
        return true;
    }
};

struct TypePackMismatch
{
    TypePackId wantedTp;
    TypePackId givenTp;
    std::string reason;

    // Luwu (helpful subtyping errors): see TypeMismatch::overrideMessage.
    std::optional<std::string> overrideMessage;

    // Luwu (helpful subtyping errors): see TypeMismatch::whereDeclarations. When it isn't empty, these hold
    // the two packs printed with its names.
    std::optional<std::string> wantedName;
    std::optional<std::string> givenName;
    std::vector<NamedTypeDeclaration> whereDeclarations;

    bool operator==(const TypePackMismatch& rhs) const;
};

struct DynamicPropertyLookupOnExternTypesUnsafe
{
    TypeId ty;

    bool operator==(const DynamicPropertyLookupOnExternTypesUnsafe& rhs) const;
};

struct UninhabitedTypeFunction
{
    TypeId ty;

    bool operator==(const UninhabitedTypeFunction& rhs) const;
};

struct ExplicitFunctionAnnotationRecommended
{
    std::vector<std::pair<std::string, TypeId>> recommendedArgs;
    TypeId recommendedReturn;
    bool operator==(const ExplicitFunctionAnnotationRecommended& rhs) const;
};

struct UninhabitedTypePackFunction
{
    TypePackId tp;

    bool operator==(const UninhabitedTypePackFunction& rhs) const;
};

struct WhereClauseNeeded
{
    TypeId ty;

    bool operator==(const WhereClauseNeeded& rhs) const;
};

struct PackWhereClauseNeeded
{
    TypePackId tp;

    bool operator==(const PackWhereClauseNeeded& rhs) const;
};

struct CheckedFunctionCallError
{
    TypeId expected;
    TypeId passed;
    std::string checkedFunctionName;
    // TODO: make this a vector<argumentIndices>
    size_t argumentIndex;
    bool operator==(const CheckedFunctionCallError& rhs) const;
};

struct NonStrictFunctionDefinitionError
{
    std::string functionName;
    std::string argument;
    TypeId argumentType;
    bool operator==(const NonStrictFunctionDefinitionError& rhs) const;
};

// Luwu Classes (rfcs/classes): accessing a `private` member of a class from outside of that class's
// own definition block.
struct PrivatePropertyAccess
{
    TypeId table;
    Name key;
    // the member is one of the class's functions rather than one of its fields
    bool isFunction = false;
    // the class that declares the member, which may be an ancestor of `table`'s class
    Name className;

    bool operator==(const PrivatePropertyAccess& rhs) const;
};

// Luwu Classes (rfcs/classes): calling `ClassName(...)` directly from outside the class's own
// definition block, when the class's `__init` constructor is `private`.
struct PrivateConstructorAccess
{
    TypeId classTy;

    bool operator==(const PrivateConstructorAccess& rhs) const;
};

// Luwu Classes (rfcs/classes): a field of a class with a primary constructor that nothing can ever
// initialize: the class body gives it no default value, no parameter shares its name, and its type does
// not admit `nil`.
struct UninitializableClassField
{
    TypeId classTy;
    Name key;

    bool operator==(const UninitializableClassField& rhs) const;
};

// Luwu Classes (rfcs/classes): a class whose fields are all `private` and which has no functions: it
// can be constructed, but no code can ever read or write what it holds.
struct UnusableClass
{
    TypeId classTy;

    bool operator==(const UnusableClass& rhs) const;
};

// A class whose constructor is private (a `private` primary constructor or `private function __init`) and that
// never calls it from its own body: nothing can ever create an instance (rfcs/classes).
struct UninstantiableClass
{
    TypeId classTy;

    bool operator==(const UninstantiableClass& rhs) const;
};

// Luwu Classes (rfcs/classes): reading `__init` by name from a class or one of its objects
// (`Class.__init`, `obj:__init()`): construction is the only way to run a constructor, and the read raises
// at runtime.
struct ConstructorReadByName
{
    TypeId table;
    Name className;

    bool operator==(const ConstructorReadByName& rhs) const;
};

// Luwu (helpful subtyping errors): something written where a similar thing was almost certainly meant, when that is the
// whole problem: a property of a table literal named like one the expected table has and the literal leaves out (table
// types aren't exact, so an optional one is otherwise never reported, and the value is never read), a string one typo
// from an expected one (`"LeftCtrl"` for `"LeftControl"`), or a table whose one problem is a misspelled property.
struct Typo
{
    Name written;
    Name meant;

    bool operator==(const Typo& rhs) const;
};

// Luwu: what a value was used for, where its type says it can't exist (`never`) or couldn't be worked out
// (`*error-type*`).
enum class ImproperUse
{
    Call,
    Index,
};

// Luwu: calling or indexing a value whose type is `never`. Either the code can't run, or a type checker bug made the
// value `never` and nothing it's used for is checked; the second has crashed real code. Upstream says nothing, and for a
// `never` it didn't simplify (`number & ~number`) reports that it can't be called.
struct UnreachableCodeUse
{
    enum class Reason
    {
        // The checks before the use rule out every member of the local's declared type on their own (provesUnreachable)
        NarrowedAway,
        // The local holds the result of a call that never returns (`error(...)`)
        NeverReturningCall,
        // The local is annotated `never`
        DeclaredNever,
        // The local is annotated with a type no value has (`{ x: number } & { x: string }`)
        DeclaredUninhabited,
        // The local holds a `never` cast to it (`x :: never`)
        CastToNever,
        // Nothing the reader wrote proves it: inference or the solver's refinements made the value `never`, which a
        // refinement bug can do while the code runs
        PossibleTypeBug,
    };

    ImproperUse use = ImproperUse::Call;
    // The local, or the name the value is read through (`self.dave`); empty when it has none
    std::string name;
    Reason reason = Reason::NarrowedAway;
    bool showHelp = true;

    bool operator==(const UnreachableCodeUse& rhs) const;
};

// Luwu: the first call or index of a local whose type couldn't be worked out (`*error-type*`), usually because of an
// error in its declaration. Every other use of the local gets a note (`Module::notes`) pointing at that error.
// Upstream says nothing: an error type stands for an error already reported.
struct NoTypeUse
{
    ImproperUse use = ImproperUse::Call;
    std::string name;
    // The error that left the local without a type, when one was reported
    std::optional<Location> origin;
    bool showHelp = true;

    bool operator==(const NoTypeUse& rhs) const;
};

// Luwu: a remark on code that an error elsewhere explains. It isn't an error (it changes no error count), so it is kept
// in `Module::notes`; editors show it less prominently.
struct DiagnosticNote
{
    Location location;
    std::string message;
    // The error it's about
    std::optional<Location> origin;
};

// The note on a use of the local `name`, which has no type because of the error at `origin`.
std::string noTypeNote(const std::string& name, std::optional<Location> origin);

struct PropertyAccessViolation
{
    TypeId table;
    Name key;

    enum
    {
        CannotRead,
        CannotWrite
    } context;

    bool operator==(const PropertyAccessViolation& rhs) const;
};

// Luwu Classes (rfcs/classes): assigning to a `const` member of a class from outside of that class's
// own `__init` constructor.
struct ConstPropertyAssignment
{
    TypeId table;
    Name key;
    // the class that declares the field, which may be an ancestor of `table`'s class
    Name className;

    bool operator==(const ConstPropertyAssignment& rhs) const;
};

struct CheckedFunctionIncorrectArgs
{
    std::string functionName;
    size_t expected;
    size_t actual;
    bool operator==(const CheckedFunctionIncorrectArgs& rhs) const;
};

struct CannotAssignToNever
{
    // type of the rvalue being assigned
    TypeId rhsType;

    // Originating type.
    std::vector<TypeId> cause;

    enum class Reason
    {
        // when assigning to a property in a union of tables, the properties type
        // is narrowed to the intersection of its type in each variant.
        PropertyNarrowed,
    };

    Reason reason;

    bool operator==(const CannotAssignToNever& rhs) const;
};

struct UnexpectedTypeInSubtyping
{
    TypeId ty;

    bool operator==(const UnexpectedTypeInSubtyping& rhs) const;
};

struct UnexpectedTypePackInSubtyping
{
    TypePackId tp;

    bool operator==(const UnexpectedTypePackInSubtyping& rhs) const;
};

struct UserDefinedTypeFunctionError
{
    std::string message;

    bool operator==(const UserDefinedTypeFunctionError& rhs) const;
};

struct BuiltInTypeFunctionError
{
    TypeFunctionError error;

    bool operator==(const BuiltInTypeFunctionError& rhs) const;
};

struct ReservedIdentifier
{
    std::string name;

    bool operator==(const ReservedIdentifier& rhs) const;
};

struct UnexpectedArrayLikeTableItem
{
    bool operator==(const UnexpectedArrayLikeTableItem&) const
    {
        return true;
    }
};

struct CannotCheckDynamicStringFormatCalls
{
    bool operator==(const CannotCheckDynamicStringFormatCalls&) const
    {
        return true;
    }
};

// Error during subtyping when the number of generic types between compared types does not match
struct GenericTypeCountMismatch
{
    size_t subTyGenericCount;
    size_t superTyGenericCount;

    bool operator==(const GenericTypeCountMismatch& rhs) const;
};

// Error during subtyping when the number of generic type packs between compared types does not match
struct GenericTypePackCountMismatch
{
    size_t subTyGenericPackCount;
    size_t superTyGenericPackCount;

    bool operator==(const GenericTypePackCountMismatch& rhs) const;
};

// Error during subtyping when the number of generic type packs between compared types does not match
struct MultipleNonviableOverloads
{
    size_t attemptedArgCount;

    // Luwu (helpful subtyping errors): the function called, as the code names it (empty when it has no name), and every
    // overload's signature, listed by number in the one error. Upstream reports the list as a second error,
    // "Available overloads: ...", all on one line.
    std::string callee;
    std::vector<std::string> overloads;
    // The arguments as a parameter list, `("chat", (message: number) -> ())`. Empty when they weren't recorded.
    std::string arguments;

    bool operator==(const MultipleNonviableOverloads& rhs) const;
};

// Luwu (helpful subtyping errors): overloads one per line, numbered as an explanation numbers them: `  • #2 '(sig)'`.
std::string numberedOverloads(const std::vector<std::string>& signatures);

// Error where a type alias violates the recursive restraint, ie when a type alias T<A> has T with different arguments on the RHS.
struct RecursiveRestraintViolation
{
    bool operator==(const RecursiveRestraintViolation& rhs) const
    {
        return true;
    }
};

// Error during subtyping when the inferred bounds of a generic type are incompatible
struct GenericBoundsMismatch
{
    std::string_view genericName;
    std::vector<TypeId> lowerBounds;
    std::vector<TypeId> upperBounds;

    GenericBoundsMismatch(std::string_view genericName, TypeIds lowerBoundSet, TypeIds upperBoundSet);

    bool operator==(const GenericBoundsMismatch& rhs) const;
};

// Used `f<<T>>` where f is not a function
struct InstantiateGenericsOnNonFunction
{
    enum class InterestingEdgeCase
    {
        None,
        MetatableCall,
        Intersection,
    };

    InterestingEdgeCase interestingEdgeCase;

    bool operator==(const InstantiateGenericsOnNonFunction&) const;
};

// Provided too many generics inside `f<<T>>`
struct TypeInstantiationCountMismatch
{
    std::optional<std::string> functionName;
    TypeId functionType;

    size_t providedTypes = 0;
    size_t maximumTypes = 0;

    size_t providedTypePacks = 0;
    size_t maximumTypePacks = 0;

    bool operator==(const TypeInstantiationCountMismatch&) const;
};

// Error when referencing a type function without providing explicit generics.
//
//  type function create_table_with_key()
//      local tbl = types.newtable()
//      tbl:setproperty(types.singleton "key", types.unionof(types.string, types.singleton(nil)))
//      return tbl
//  end
//  local a: create_table_with_key = {}
//           ^^^^^^^^^^^^^^^^^^^^^ This should have `<>` at the end.
//
struct UnappliedTypeFunction
{
    bool operator==(const UnappliedTypeFunction& rhs) const;
};

// Error when we have an ambiguous overload and cannot determine the correct
// function call.
//
//  local f: ((number | string) -> "one") & ((number | boolean) -> "two")
//  local g = f(42)
//            ^^^^^ We cannot determine the correct call here
struct AmbiguousFunctionCall
{
    TypeId function;
    TypePackId arguments;

    bool operator==(const AmbiguousFunctionCall& rhs) const;
};

using TypeErrorData = Variant<
    TypeMismatch,
    UnknownSymbol,
    UnknownProperty,
    NotATable,
    CannotExtendTable,
    CannotCompareUnrelatedTypes,
    OnlyTablesCanHaveMethods,
    DuplicateTypeDefinition,
    CountMismatch,
    FunctionDoesNotTakeSelf,
    FunctionRequiresSelf,
    OccursCheckFailed,
    UnknownRequire,
    IncorrectGenericParameterCount,
    SyntaxError,
    CodeTooComplex,
    UnificationTooComplex,
    UnknownPropButFoundLikeProp,
    GenericError,
    InternalError,
    ConstraintSolvingIncompleteError,
    CannotCallNonFunction,
    ExtraInformation,
    DeprecatedApiUsed,
    ModuleHasCyclicDependency,
    IllegalRequire,
    FunctionExitsWithoutReturning,
    DuplicateGenericParameter,
    CannotAssignToNever,
    CannotInferBinaryOperation,
    MissingProperties,
    SwappedGenericTypeParameter,
    OptionalValueAccess,
    MissingUnionProperty,
    PrivatePropertyAccess,
    ConstPropertyAssignment,
    PrivateConstructorAccess,
    UninitializableClassField,
    UnusableClass,
    TypesAreUnrelated,
    NormalizationTooComplex,
    TypePackMismatch,
    DynamicPropertyLookupOnExternTypesUnsafe,
    UninhabitedTypeFunction,
    UninhabitedTypePackFunction,
    WhereClauseNeeded,
    PackWhereClauseNeeded,
    CheckedFunctionCallError,
    NonStrictFunctionDefinitionError,
    PropertyAccessViolation,
    CheckedFunctionIncorrectArgs,
    UnexpectedTypeInSubtyping,
    UnexpectedTypePackInSubtyping,
    ExplicitFunctionAnnotationRecommended,
    UserDefinedTypeFunctionError,
    BuiltInTypeFunctionError,
    ReservedIdentifier,
    UnexpectedArrayLikeTableItem,
    CannotCheckDynamicStringFormatCalls,
    GenericTypeCountMismatch,
    GenericTypePackCountMismatch,
    MultipleNonviableOverloads,
    RecursiveRestraintViolation,
    GenericBoundsMismatch,
    UnappliedTypeFunction,
    InstantiateGenericsOnNonFunction,
    TypeInstantiationCountMismatch,
    AmbiguousFunctionCall,
    UninstantiableClass,
    ConstructorReadByName,
    Typo,
    UnreachableCodeUse,
    NoTypeUse>;

struct TypeErrorSummary
{
    Location location;
    ModuleName moduleName;
    int code;

    TypeErrorSummary(const Location& location, const ModuleName& moduleName, int code)
        : location(location)
        , moduleName(moduleName)
        , code(code)
    {
    }
};

struct TypeError
{
    Location location;
    ModuleName moduleName;
    TypeErrorData data;

    static int minCode();
    int code() const;

    TypeError() = default;

    TypeError(const Location& location, const ModuleName& moduleName, const TypeErrorData& data)
        : location(location)
        , moduleName(moduleName)
        , data(data)
    {
    }

    TypeError(const Location& location, const TypeErrorData& data)
        : TypeError(location, {}, data)
    {
    }

    bool operator==(const TypeError& rhs) const;

    TypeErrorSummary summary() const;
};

template<typename T>
const T* get(const TypeError& e)
{
    return get_if<T>(&e.data);
}

template<typename T>
T* get(TypeError& e)
{
    return get_if<T>(&e.data);
}

using ErrorVec = std::vector<TypeError>;

struct TypeErrorToStringOptions
{
    FileResolver* fileResolver = nullptr;
};

std::string toString(const TypeError& error);
std::string toString(const TypeError& error, TypeErrorToStringOptions options);

bool containsParseErrorName(const TypeError& error);

// Copy any types named in the error into destArena.
void copyErrors(ErrorVec& errors, struct TypeArena& destArena, NotNull<BuiltinTypes> builtinTypes);

// Internal Compiler Error
struct InternalErrorReporter
{
    std::function<void(const char*)> onInternalError;
    std::string moduleName;

    [[noreturn]] void ice(const std::string& message, const Location& location) const;
    [[noreturn]] void ice(const std::string& message) const;
};

class InternalCompilerError : public std::exception
{
public:
    explicit InternalCompilerError(const std::string& message)
        : message(message)
    {
    }
    explicit InternalCompilerError(const std::string& message, const std::string& moduleName)
        : message(message)
        , moduleName(moduleName)
    {
    }
    explicit InternalCompilerError(const std::string& message, const std::string& moduleName, const Location& location)
        : message(message)
        , moduleName(moduleName)
        , location(location)
    {
    }
    const char* what() const throw() override;

    const std::string message;
    const std::optional<std::string> moduleName;
    const std::optional<Location> location;
};

} // namespace Luau
