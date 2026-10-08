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
    Module* module
);

struct TypeChecker2
{
    NotNull<BuiltinTypes> builtinTypes;
    NotNull<TypeFunctionRuntime> typeFunctionRuntime;
    DcrLogger* logger;
    const NotNull<TypeCheckLimits> limits;
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
    void visitIfCondition(AstExpr* condition, const AstArray<AstIfClause>& clauses);
    void visit(AstStatWhile* whileStatement);
    void visit(AstStatRepeat* repeatStatement);
    void visit(AstStatBreak*);
    void visit(AstStatContinue*);
    void visit(AstStatReturn* ret);
    void visit(AstStatGive* give);
    void visit(AstStatComprehensionItem* item);
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
    // Luwu Traits (rfcs/classes/traits.md): the class's fields against the types of the fields its traits expect
    std::optional<TypeId> selfFieldFor(const ExternType* traitType, TypeId classTy, const Name& name);
    void checkTraitFieldExpectations(AstStatClass* stat);
    // Luwu Traits (rfcs/classes/traits.md): the class's constructor against the trait's expected `__init`
    void checkTraitConstructorExpectation(AstStatClass* stat, const ExternType* classType, const ExternType* traitType);
    void checkTraitArguments(AstStatClass* stat);
    // Luwu Traits (rfcs/classes/traits.md): the class implements the instantiation of each trait its traits need with
    // the type arguments their `needs` entries give it
    void checkNeededTraitArguments(AstStatClass* stat);
    void checkTraitRefs(AstStatClass* stat, const AstArray<AstClassTraitRef>& refs);
    void checkTraitOverrides(AstStatClass* stat);
    // Luwu Traits (rfcs/classes/traits.md): the members a class is missing, reported on one `implements` entry
    struct MissingTraitMembers
    {
        Location location;
        std::string traitName;
        std::vector<std::string> members;
        size_t fields = 0;
        size_t functions = 0;
    };
    void addMissingTraitMember(
        std::vector<MissingTraitMembers>& missing,
        AstStatClass* stat,
        const ExternType* traitType,
        const Name& name,
        bool isField
    );
    void reportMissingTraitMembers(const ExternType* classType, const MissingTraitMembers& group);
    void visit(AstStatDeclareClass* stat);
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
    // Luwu literal types: reports an argument for a `literal<B>` parameter that isn't a literal, and runs a `V<literal<B>>`
    // parameter's validator on its argument
    void checkLiteralParameters(AstExprCall* call, const FunctionType* fty, size_t selfOffset, NotNull<Scope> scope);
    // Luwu literal types: whether `value` is a cast to a literal type (`s :: literal<string>`)
    bool isLiteralAssertion(AstExpr* value) const;
    // Luwu literal types: whether a local's annotation is `literal<B>`
    bool isLiteralAnnotation(AstType* annotation);
    // Luwu literal types: reports a value given for a `local x: literal<B>` that isn't a literal
    void checkLiteralLocal(TypeId valueTy, TypeId base, Location location);
    void visit(AstExprCall* call);
    std::optional<TypeId> tryStripUnionFromNil(TypeId ty) const;
    TypeId stripFromNilAndReport(TypeId ty, const Location& location);
    void visitExprName(AstExpr* expr, Location location, const std::string& propName, ValueContext context, TypeId astIndexExprTy);
    void visit(AstExprIndexName* indexName, ValueContext context);
    void indexExprMetatableHelper(AstExprIndexExpr* indexExpr, const MetatableType* metaTable, TypeId exprType, TypeId indexType);
    void visit(AstExprIndexExpr* indexExpr, ValueContext context);
    void visit(AstExprFunction* fn);
    void visit(AstExprTable* expr);
    void visit(AstExprUnary* expr);
    TypeId visit(AstExprBinary* expr, AstNode* overrideKey = nullptr);
    // Luwu Classes (rfcs/classes): `type(x) == "table"` where `x` is an object, a class or a trait value, whose `type`
    // is always "object", "class" or "trait"
    void checkLuwuNominalTypeComparison(AstExprBinary* expr);
    void visit(AstExprTypeAssertion* expr);
    void visit(AstExprIfElse* expr);
    void visit(AstExprDo* expr);
    void visit(AstExprTableComprehension* expr);
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

    void explainError(TypeId subTy, TypeId superTy, Location location, const SubtypingResult& result);
    void explainError(TypePackId subTy, TypePackId superTy, Location location, const SubtypingResult& result);

    bool testLiteralOrAstTypeIsSubtype(AstExpr* expr, TypeId expectedType);

    bool testPotentialLiteralIsSubtype(AstExpr* expr, TypeId expectedType);

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
    void checkIndexTypeFromType(TypeId tableTy, const std::string& prop, ValueContext context, const Location& location, TypeId astIndexExprType);
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
    TypeId withoutSelfParameter(TypeId fnTy);
    void reportClassTypeofSpelling(AstTypeTypeof* ty, const std::string& className, TypeId objectTy, const char* valueKind);
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
