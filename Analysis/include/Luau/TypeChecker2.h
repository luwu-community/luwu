// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#pragma once

#include "Luau/Common.h"
#include "Luau/Error.h"
#include "Luau/Normalize.h"
#include "Luau/NotNull.h"
#include "Luau/Subtyping.h"
#include "Luau/Type.h"
#include "Luau/TypeFwd.h"
#include "Luau/TypeUtils.h"
#include "Luau/UnknownVariable.h"

#include <memory>

namespace Luau
{

struct BuiltinTypes;
struct DcrLogger;
struct TypeCheckLimits;
struct UnifierSharedState;
struct SourceModule;
struct Module;
struct InternalErrorReporter;
struct Scope;
struct PropertyType;
struct PropertyTypes;
struct StackPusher;

struct Reasonings
{
    // the list of reasons
    std::vector<std::string> reasons;

    // this should be true if _all_ of the reasons have an error suppressing type, and false otherwise.
    bool suppressed;

    // When every reasoning shares the same top-level context (e.g. all of them are about a
    // function's return type, or all about its arguments), a short phrase describing that
    // context (e.g. "this function to return"), for use in place of the generic "this to be" in
    // the enclosing TypeMismatch preamble. Unset when reasonings disagree on context or don't
    // originate from a recognized one.
    std::optional<std::string> contextVerb;

    // Luwu (helpful subtyping errors): when `contextVerb` is set, the preamble promises a specific part of
    // the type ("Expected this function to return"), but the enclosing TypeMismatch still holds the
    // *root* types, so it would go on to print the whole function type after that promise. These carry
    // the stringified types at the end of the shared context path instead, so the message names what it
    // claims to.
    std::optional<std::string> contextWantedDisplay;
    std::optional<std::string> contextGivenDisplay;

    std::string toString()
    {
        if (reasons.empty())
            return "";

        // DenseHashSet ordering is entirely undefined, so we want to
        // sort the reasons here to achieve a stable error
        // stringification.
        std::sort(reasons.begin(), reasons.end());

        // Dropping path narration (see explainReasonings_) means multiple distinct reasoning
        // entries can end up rendering to the exact same text (e.g. two different union/pack
        // slots that both boil down to "`nil` is not a subtype of `number`") -- printing the
        // same line twice is just noise, so collapse them.
        reasons.erase(std::unique(reasons.begin(), reasons.end()), reasons.end());

        std::string allReasons = reasons.size() < 2 ? "\n" : "\nthis is because";
        for (const std::string& reason : reasons)
        {
            if (reasons.size() > 1)
                allReasons += "\n    * ";

            allReasons += reason;
        }

        return allReasons;
    }
};


void check(
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<TypeFunctionRuntime> typeFunctionRuntime,
    NotNull<UnifierSharedState> unifierState,
    NotNull<TypeCheckLimits> limits,
    DcrLogger* logger,
    const SourceModule& sourceModule,
    Module* module,
    Helpifier helpifier = {}
);

struct TypeChecker2
{
    NotNull<BuiltinTypes> builtinTypes;
    NotNull<TypeFunctionRuntime> typeFunctionRuntime;
    DcrLogger* logger;
    const NotNull<TypeCheckLimits> limits;
    // Luwu: writes the help in the errors this reports; off when the frontend's `showHelp` is
    Helpifier helpifier;
    const NotNull<InternalErrorReporter> ice;
    const SourceModule* sourceModule;
    Module* module;

    TypeContext typeContext = TypeContext::Default;
    std::vector<NotNull<Scope>> stack;
    std::vector<TypeId> functionDeclStack;
    // The function expressions being visited, innermost last.
    std::vector<const AstExprFunction*> enclosingFunctions;
    // Luwu Classes (rfcs/classes): per class `__init`, the locals that always hold its `self` (see
    // isInitWritingItsSelf). Filled only when a `const` write would otherwise be reported.
    mutable DenseHashMap<const AstExprFunction*, DenseHashSet<const AstLocal*>> initSelfAliases{nullptr};

    DenseHashSet<TypeId> seenTypeFunctionInstances{nullptr};
    // Luwu: see requiredProperties
    DenseHashMap<const TableType*, std::vector<std::string>> requiredPropertiesCache{nullptr};

    Normalizer normalizer;
    Subtyping _subtyping;
    NotNull<Subtyping> subtyping;

    TypeChecker2(
        NotNull<BuiltinTypes> builtinTypes,
        NotNull<TypeFunctionRuntime> typeFunctionRuntime,
        NotNull<UnifierSharedState> unifierState,
        NotNull<TypeCheckLimits> limits,
        DcrLogger* logger,
        const SourceModule* sourceModule,
        Module* module
    );

    void visit(AstStatBlock* block);
    void reportError(TypeErrorData data, const Location& location);
    Reasonings explainReasonings(TypeId subTy, TypeId superTy, Location location, const SubtypingResult& r);
    Reasonings explainReasonings(TypePackId subTp, TypePackId superTp, Location location, const SubtypingResult& r);

    // Luwu (helpful subtyping errors): set while checking the packs of a `return` statement, so a pack
    // mismatch reported from there can phrase itself as being about what the function returns. The same
    // test runs for argument packs and assignments, where that phrasing would be wrong.
    bool checkingReturnStatement = false;

    // Luwu (helpful subtyping errors): set while checking one argument of a call, so a mismatch reported from
    // there can name the function being called and point a second diagnostic at the parameter it came
    // from. The same mismatch also arises from assignments and returns, which have neither. Holds only
    // what the clean path already has at hand: the callee's name and its parameter's annotation are
    // looked up from these once there is an error to explain.
    struct ArgumentContext
    {
        AstExpr* callee = nullptr;
        const FunctionType* calleeType = nullptr;
        size_t parameterIndex = 0;
    };

    std::optional<ArgumentContext> argumentContext;

    // Luwu (helpful subtyping errors): a table literal checked against the one member of a union it was
    // narrowed to, and that union, so missing fields are reported as being for `GameEvent` rather than
    // for an unnamed member. Only for that exact expression, not the literals inside it.
    struct NarrowedLiteral
    {
        const AstExpr* literal = nullptr;
        const UnionType* narrowedFrom = nullptr;
    };

    std::optional<NarrowedLiteral> narrowedLiteralUnion;

    // Luwu (helpful subtyping errors): the module's functions by location and type aliases by name, so an
    // explanation can find what the user wrote without walking the AST per lookup. Built by
    // `getDeclarationIndex` the first time an error needs it, never on the clean path.
    struct DeclarationIndex;
    std::shared_ptr<DeclarationIndex> declarationIndex;

    // Luwu: a call or index of a value that is `never` or has an error type (see UnreachableCodeUse, NoTypeUse).
    // Returns whether `subjectType` is one; only types that are so without normalizing are recognized.
    bool reportImproperUse(AstExpr* subject, TypeId subjectType, ImproperUse use);
    void reportUnreachableUse(AstExpr* subject, ImproperUse use);
    // Why the local read at `use` is `never`, when something the reader wrote says so; nullopt when it may be a type
    // checker bug
    std::optional<UnreachableCodeUse::Reason> unreachableReason(const AstExprLocal* use);
    // The first error reported in `local`'s declaration, which explains what is wrong with its type
    std::optional<Location> declarationError(const AstLocal* local);
    std::optional<Location> firstErrorIn(const Location& range);
    // The first error in an annotation, or in the declaration of an alias it names (`type Result = Nope`)
    std::optional<Location> annotationError(AstNode* annotation);
    // Why a local has no type: the error in its declaration, or the one behind another local with no type that its
    // declaration uses (`local alias = w`). `root` is the local the chain starts at; it gets one error, at the first
    // improper use of any local in the chain.
    struct NoTypeCause
    {
        std::optional<Location> origin;
        const AstLocal* root = nullptr;
    };
    NoTypeCause noTypeCause(const AstLocal* local, size_t depth = 0);
    DenseHashMap<const AstLocal*, NoTypeCause> noTypeCauses{nullptr};
    DenseHashSet<const AstLocal*> noTypeRootsReported{nullptr};
    // Whether a value of the normalized type `norm` can't exist
    bool isUninhabited(const NormalizedType* norm);
    // A note on every use of an error-typed local that didn't get the error (see NoTypeUse)
    void reportNoTypeNotes();
    // Locals with an improper use reported: each gets one error
    DenseHashSet<const AstLocal*> improperlyUsedLocals{nullptr};
    DenseHashSet<const AstExpr*> improperlyUsedRoots{nullptr};
    // Every use of an error-typed local, and those reported as an improper use
    std::vector<const AstExprLocal*> noTypeUses;
    DenseHashSet<const AstExprLocal*> noTypeUsesReported{nullptr};
    // Uses cast to a type (`self :: Class`): the cast gives the value a type, so they get no note
    DenseHashSet<const AstExprLocal*> noTypeUsesCast{nullptr};
    DenseHashMap<const AstLocal*, std::optional<Location>> declarationErrors{nullptr};
    const DeclarationIndex& getDeclarationIndex();

    bool testIsSubtype(TypeId subTy, TypeId superTy, Location location);
    bool testIsSubtype(TypePackId subTy, TypePackId superTy, Location location);

private:
    static bool allowsNoReturnValues(const TypePackId tp);
    static Location getEndLocation(const AstExprFunction* function);
    bool isErrorCall(const AstExprCall* call);
    bool hasBreak(AstStat* node);
    const AstStat* getFallthrough(const AstStat* node);
    std::optional<StackPusher> pushStack(AstNode* node);
    void checkForInternalTypeFunction(TypeId ty, Location location);
    TypeId checkForTypeFunctionInhabitance(TypeId instance, Location location);
    TypePackId lookupPack(AstExpr* expr) const;
    TypeId lookupType(AstExpr* expr);
    TypeId lookupAnnotation(AstType* annotation);
    std::optional<TypePackId> lookupPackAnnotation(AstTypePack* annotation) const;
    TypeId lookupExpectedType(AstExpr* expr) const;
    TypePackId lookupExpectedPack(AstExpr* expr, TypeArena& arena) const;
    TypePackId reconstructPack(AstArray<AstExpr*> exprs, TypeArena& arena);
    Scope* findInnermostScope(Location location) const;
    void visit(AstStat* stat);
    void visit(AstStatIf* ifStatement);
    void visit(AstStatWhile* whileStatement);
    void visit(AstStatRepeat* repeatStatement);
    void visit(AstStatBreak*);
    void visit(AstStatContinue*);
    void visit(AstStatReturn* ret);
    void visit(AstStatExpr* expr);
    void visit(AstStatLocal* local);
    void visit(AstStatFor* forStatement);
    void visit(AstStatForIn* forInStatement);
    std::optional<TypeId> getBindingType(AstExpr* expr);
    void reportErrorsFromAssigningToNever(AstExpr* lhs, TypeId rhsType);
    void visit(AstStatAssign* assign);
    void visit(AstStatCompoundAssign* stat);
    void visit(AstStatFunction* stat);
    void visit(AstStatLocalFunction* stat);
    void visit(const AstTypeList* typeList);
    void visit(AstStatTypeAlias* stat);
    void visit(AstStatTypeFunction* stat);
    void visit(AstTypeList types);
    void visit(AstStatDeclareFunction* stat);
    void visit(AstStatDeclareGlobal* stat);
    void visit(AstStatDeclareExternType* stat);
    void visit(AstStatClass* stat);
    void visit(AstStatError* stat);
    void visit(AstExpr* expr, ValueContext context);
    void visit(AstExprGroup* expr, ValueContext context);
    void visit(AstExprConstantNil* expr);
    void visit(AstExprConstantBool* expr);
    void visit(AstExprConstantNumber* expr);
    void visit(AstExprConstantInteger* expr);
    void visit(AstExprConstantString* expr);
    void visit(AstExprLocal* expr);
    void visit(AstExprGlobal* expr);
    void visit(AstExprVarargs* expr);
    void visitCall(AstExprCall* call);
    void visit(AstExprCall* call);
    std::optional<TypeId> tryStripUnionFromNil(TypeId ty) const;
    TypeId stripFromNilAndReport(TypeId ty, const Location& location);
    // `separator` is the `.` or `:` of `expr.propName`, and 0 for `expr["propName"]`.
    void visitExprName(AstExpr* expr, Location location, const std::string& propName, ValueContext context, TypeId astIndexExprTy, char separator);
    void visit(AstExprIndexName* indexName, ValueContext context);
    void indexExprMetatableHelper(AstExprIndexExpr* indexExpr, const MetatableType* metaTable, TypeId exprType, TypeId indexType);
    void visit(AstExprIndexExpr* indexExpr, ValueContext context);
    void visit(AstExprFunction* fn);
    void visit(AstExprTable* expr);
    void visit(AstExprUnary* expr);
    TypeId visit(AstExprBinary* expr, AstNode* overrideKey = nullptr);
    void visit(AstExprTypeAssertion* expr);
    void visit(AstExprIfElse* expr);
    void visit(AstExprInterpString* interpString);
    void visit(AstExprInstantiate* explicitTypeInstantiation);
    void visit(AstExprError* expr);
    TypeId flattenPack(TypePackId pack);
    void visitGenerics(AstArray<AstGenericType*> generics, AstArray<AstGenericTypePack*> genericPacks);
    void visit(AstType* ty);
    void visit(AstTypeReference* ty);
    void visit(AstTypeTable* table);
    void visit(AstTypeFunction* ty);
    void visit(AstTypeTypeof* ty);
    void visit(AstTypeUnion* ty);
    void visit(AstTypeIntersection* ty);
    void visit(AstTypePack* pack);
    void visit(AstTypePackExplicit* tp);
    void visit(AstTypePackVariadic* tp);
    void visit(AstTypePackGeneric* tp);

    template<typename TID>
    Reasonings explainReasonings_(TID subTy, TID superTy, Location location, const SubtypingResult& r);

    // `contextVerb` says what the two types are of, when they're a part of what's at `location` (`every returned
    // value to be`), as `TypeMismatch::contextVerb`.
    void explainError(
        TypeId subTy,
        TypeId superTy,
        Location location,
        const SubtypingResult& result,
        std::optional<std::string> contextVerb = std::nullopt
    );
    void explainError(TypePackId subTy, TypePackId superTy, Location location, const SubtypingResult& result);

    bool testLiteralOrAstTypeIsSubtype(AstExpr* expr, TypeId expectedType);

    bool testPotentialLiteralIsSubtype(AstExpr* expr, TypeId expectedType);
    // Luwu: the properties a table literal checked against `table` has to write: readable and not optional. Cached, so
    // literals checked against one wide record (150 optional fields) don't each test every field.
    const std::vector<std::string>& requiredProperties(const TableType* table);

    void maybeReportSubtypingError(TypeId subTy, TypeId superTy, const Location& location);
    // Tests whether subTy is a subtype of superTy in the context of a function iterator for a for-in statement.
    // Includes some extra logic to help locate errors to the values and variables of the for-in statement.
    void testIsSubtypeForInStat(TypeId iterFunc, TypeId prospectiveFunc, const AstStatForIn& forInStat);

    void reportError(TypeError e);
    void reportErrors(ErrorVec errors);
    PropertyTypes lookupProp(
        const NormalizedType* norm,
        const std::string& prop,
        ValueContext context,
        const Location& location,
        TypeId astIndexExprType,
        std::vector<TypeError>& errors
    );
    // If the provided type does not have the named property, report an error.
    // Returns whether `tableTy` can't exist (a `never` the normalizer had to work out), for the caller to report.
    // `receiver` is how the indexed value is written before the property (`player.`), for a typo's message.
    bool checkIndexTypeFromType(
        TypeId tableTy,
        const std::string& prop,
        ValueContext context,
        const Location& location,
        TypeId astIndexExprType,
        const std::string& receiver = ""
    );
    // Luwu (helpful subtyping errors): reports `error` for a property `tableTy` doesn't have, or a typo when it has one
    // spelled almost the same.
    void reportMissingProperty(TypeErrorData error, TypeId tableTy, const std::string& prop, const std::string& receiver, const Location& location);
    // If the named property is `private` on some user-defined class in tableTy's hierarchy, and
    // `location` falls outside of that class's own definition block, report an error.
    void checkPrivatePropertyAccess(TypeId tableTy, const std::string& prop, const Location& location);
    // If the named property is `const` on some user-defined class in tableTy's hierarchy, and
    // `location` falls outside of that class's own `__init` constructor, report an error.
    void checkConstPropertyAssignment(
        TypeId tableTy,
        const AstExpr* objectExpr,
        const std::string& prop,
        ValueContext context,
        const Location& location
    );
    bool isInitWritingItsSelf(const ExternType* cls, const AstExpr* objectExpr) const;
    // If classTy's `__init` is `private`, and `location` falls outside of that class's own
    // definition block, report an error.
    void checkPrivateConstructorAccess(TypeId classTy, const Location& location);
    // Luwu Classes (rfcs/classes): steers `typeof(Cat)` to `class<Cat>`.
    void reportClassTypeofSpelling(AstTypeTypeof* ty, const std::string& className, TypeId objectTy);
    // Luwu Classes (rfcs/classes): reports reading `__init` from a class or object; true if it did.
    bool checkConstructorReadByName(TypeId tableTy, const std::string& prop, ValueContext context, const Location& location);
    PropertyType hasIndexTypeFromType(
        TypeId ty,
        const std::string& prop,
        ValueContext context,
        const Location& location,
        DenseHashSet<TypeId>& seen,
        TypeId astIndexExprType,
        std::vector<TypeError>& errors
    );

    // Avoid duplicate warnings being emitted for the same global variable.
    DenseHashSet<std::string> warnedGlobals{""};

    // Luwu: says why a name isn't a variable here. Created at the first unknown name.
    std::unique_ptr<UnknownVariableExplainer> unknownVariables;
    UnknownVariableExplainer& unknownVariableExplainer();
    void reportUnknownVariable(const AstExprGlobal* expr, NotNull<Scope> scope);

    void suggestAnnotations(AstExprFunction* expr, TypeId ty);

    void checkTypeInstantiation(AstExpr* baseFunctionExpr, TypeId fnType, const Location& location, const AstArray<AstTypeOrPack>& typeArguments);

    void diagnoseMissingTableKey(UnknownProperty* utk, TypeErrorData& data) const;
    bool isErrorSuppressing(Location loc, TypeId ty);
    bool isErrorSuppressing(Location loc1, TypeId ty1, Location loc2, TypeId ty2);
    bool isErrorSuppressing(Location loc, TypePackId tp);
    bool isErrorSuppressing(Location loc1, TypePackId tp1, Location loc2, TypePackId tp2);

    // Returns whether we reported any errors
    bool reportNonviableOverloadErrors(
        std::vector<std::pair<TypeId, ErrorVec>> nonviableOverloads,
        Location callFuncLocation,
        size_t argHeadSize,
        Location callLocation
    );
};

} // namespace Luau
