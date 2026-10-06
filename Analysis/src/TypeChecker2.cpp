// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/TypeChecker2.h"

#include "Luau/ApplyTypeFunction.h"
#include "Luau/Ast.h"
#include "Luau/AstUtils.h"
#include "Luau/AstQuery.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Common.h"
#include "Luau/DcrLogger.h"
#include "Luau/DenseHash.h"
#include "Luau/Error.h"
#include "Luau/Instantiation.h"
#include "Luau/Instantiation2.h"
#include "Luau/MismatchExplanation.h"
#include "Luau/Metamethods.h"
#include "Luau/Normalize.h"
#include "Luau/OverloadResolver.h"
#include "Luau/Subtyping.h"
#include "Luau/TimeTrace.h"
#include "Luau/ToString.h"
#include "Luau/TxnLog.h"
#include "Luau/Type.h"
#include "Luau/TypeFunction.h"
#include "Luau/TypeFunctionReductionGuesser.h"
#include "Luau/TypeFwd.h"
#include "Luau/TypePack.h"
#include "Luau/TypePath.h"
#include "Luau/TypeUtils.h"
#include "Luau/TypeOrPack.h"
#include "Luau/VisitType.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "Luau/Simplify.h"

LUAU_FASTFLAG(DebugLuauMagicTypes)

LUAU_FASTFLAGVARIABLE(LuauCheckFunctionStatementTypes)
LUAU_FASTFLAGVARIABLE(LuauPropertyModifierMismatchErrors)
LUAU_FASTFLAG(LuauTweakAccessViolationReporting)
LUAU_FASTFLAG(LuauReadOnlyIndexers)
LUAU_FASTFLAGVARIABLE(LuauIndexerModifierMismatchErrors)
LUAU_FASTFLAG(LuauImproveUniqueTableWidthSubtyping)
LUAU_FASTFLAG(LuauBidirectionalInferenceSimplifyTables)
LUAU_FASTFLAGVARIABLE(LuauBetterPackAndVariadicMismatchErrors)
// Luwu (helpful subtyping errors): explain a failed subtyping test with a structural diff: one rooted path per
// place the two types disagree, how close they are, and which union member was meant
// (`MismatchExplanation.cpp`).
//
// Deliberately not gated by this flag, because each fixes a message upstream ships that readers can't act on:
// the read/write (variance) explanations and their `read` help, the contravariant-argument and nil-widening
// explanations, `explainReturnCountMismatch`, the second "not all code paths return" error pointing at the
// path that escapes, printing only the parameter or return types after "Expected this function to take/return"
// (`Reasonings::contextWantedDisplay`), the variadic argument tail error in `OverloadResolver::reportErrors` (an
// error upstream drops), the "the function takes/returns" subject in `TypePath::toStringHuman`, and types
// indented with four spaces instead of a tab. Also unflagged, and not wording: `ExpectedTypeVisitor` expects
// each field of a table literal that matches no member of a union to be whatever some member allows for it,
// so autocomplete offers the tags (upstream skips that under the new solver, CLI-116814).
LUAU_FASTFLAGVARIABLE(LuwuHelpfulSubtypingErrors)

LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuDefaultArguments)

namespace Luau
{

struct TypeChecker2::DeclarationIndex
{
    // Keyed by where a function starts; its `FunctionDefinition::definitionLocation` is checked in full on lookup.
    std::unordered_map<uint64_t, AstExprFunction*> functions;

    // nullptr for a name declared more than once: picking the wrong one would point the reader at a type
    // they aren't using.
    std::unordered_map<AstName, AstStatTypeAlias*> aliases;

    static uint64_t key(const Position& position)
    {
        return (uint64_t(position.line) << 32) | position.column;
    }

    AstExprFunction* functionAt(const Location& location) const
    {
        auto it = functions.find(key(location.begin));
        return it != functions.end() && it->second->location == location ? it->second : nullptr;
    }

    AstStatTypeAlias* aliasNamed(AstName name) const
    {
        auto it = aliases.find(name);
        return it != aliases.end() ? it->second : nullptr;
    }
};

namespace
{

struct DeclarationCollector : AstVisitor
{
    TypeChecker2::DeclarationIndex& index;

    explicit DeclarationCollector(TypeChecker2::DeclarationIndex& index)
        : index(index)
    {
    }

    bool visit(AstExprFunction* fn) override
    {
        index.functions.emplace(TypeChecker2::DeclarationIndex::key(fn->location.begin), fn);
        return true;
    }

    bool visit(AstStatTypeAlias* alias) override
    {
        auto [it, inserted] = index.aliases.emplace(alias->name, alias);
        if (!inserted)
            it->second = nullptr;
        return true;
    }
};

} // namespace

const TypeChecker2::DeclarationIndex& TypeChecker2::getDeclarationIndex()
{
    if (!declarationIndex)
    {
        declarationIndex = std::make_shared<DeclarationIndex>();
        if (sourceModule->root)
        {
            DeclarationCollector collector{*declarationIndex};
            sourceModule->root->visit(&collector);
        }
    }

    return *declarationIndex;
}

// TypeInfer.h
// TODO move these
using PrintLineProc = void (*)(const std::string&);
extern PrintLineProc luauPrintLine;

// Pushes a function expression onto TypeChecker2::enclosingFunctions for the lifetime of the instance.
struct EnclosingFunctionPusher
{
    std::vector<const AstExprFunction*>& stack;

    EnclosingFunctionPusher(std::vector<const AstExprFunction*>& stack, const AstExprFunction* fn)
        : stack(stack)
    {
        stack.push_back(fn);
    }

    ~EnclosingFunctionPusher()
    {
        stack.pop_back();
    }

    EnclosingFunctionPusher(const EnclosingFunctionPusher&) = delete;
    EnclosingFunctionPusher& operator=(const EnclosingFunctionPusher&) = delete;
};

/* Push a scope onto the end of a stack for the lifetime of the StackPusher instance.
 * TypeChecker2 uses this to maintain knowledge about which scope encloses every
 * given AstNode.
 */
struct StackPusher
{
    std::vector<NotNull<Scope>>* stack;
    NotNull<Scope> scope;

    explicit StackPusher(std::vector<NotNull<Scope>>& stack, Scope* scope)
        : stack(&stack)
        , scope(scope)
    {
        stack.push_back(NotNull{scope});
    }

    ~StackPusher()
    {
        if (stack)
        {
            LUAU_ASSERT(stack->back() == scope);
            stack->pop_back();
        }
    }

    StackPusher(const StackPusher&) = delete;
    StackPusher&& operator=(const StackPusher&) = delete;

    StackPusher(StackPusher&& other)
        : stack(std::exchange(other.stack, nullptr))
        , scope(other.scope)
    {
    }
};

struct PropertyTypes
{
    // a vector of all the typeArguments assigned to the given property.
    std::vector<TypeId> typesOfProp;

    // a vector of all the typeArguments that are missing the given property.
    std::vector<TypeId> missingProp;

    bool foundOneProp() const
    {
        return !typesOfProp.empty();
    }

    bool noneMissingProp() const
    {
        return missingProp.empty();
    }

    bool foundMissingProp() const
    {
        return !missingProp.empty();
    }
};

struct PropertyType
{
    NormalizationResult present;
    std::optional<TypeId> result;
};


static std::optional<std::string> getIdentifierOfBaseVar(AstExpr* node)
{
    if (AstExprGlobal* expr = node->as<AstExprGlobal>())
        return expr->name.value;

    if (AstExprLocal* expr = node->as<AstExprLocal>())
        return expr->local->name.value;

    if (AstExprIndexExpr* expr = node->as<AstExprIndexExpr>())
        return getIdentifierOfBaseVar(expr->expr);

    if (AstExprIndexName* expr = node->as<AstExprIndexName>())
        return getIdentifierOfBaseVar(expr->expr);

    return std::nullopt;
}

template<typename T>
bool areEquivalent(const T& a, const T& b)
{
    if (a.function != b.function)
        return false;

    if (a.typeArguments.size() != b.typeArguments.size() || a.packArguments.size() != b.packArguments.size())
        return false;

    for (size_t i = 0; i < a.typeArguments.size(); ++i)
    {
        if (follow(a.typeArguments[i]) != follow(b.typeArguments[i]))
            return false;
    }

    for (size_t i = 0; i < a.packArguments.size(); ++i)
    {
        if (follow(a.packArguments[i]) != follow(b.packArguments[i]))
            return false;
    }

    return true;
}

struct TypeFunctionFinder : TypeOnceVisitor
{
    DenseHashSet<TypeId> mentionedFunctions{nullptr};
    DenseHashSet<TypePackId> mentionedFunctionPacks{nullptr};

    TypeFunctionFinder()
        : TypeOnceVisitor("TypeFunctionFinder", /* skipBoundTypes */ true)
    {
    }

    bool visit(TypeId ty, const TypeFunctionInstanceType&) override
    {
        mentionedFunctions.insert(ty);
        return true;
    }

    bool visit(TypePackId tp, const TypeFunctionInstanceTypePack&) override
    {
        mentionedFunctionPacks.insert(tp);
        return true;
    }
};

struct InternalTypeFunctionFinder : TypeOnceVisitor
{
    DenseHashSet<TypeId> internalFunctions{nullptr};
    DenseHashSet<TypePackId> internalPackFunctions{nullptr};
    DenseHashSet<TypeId> mentionedFunctions{nullptr};
    DenseHashSet<TypePackId> mentionedFunctionPacks{nullptr};

    explicit InternalTypeFunctionFinder(std::vector<TypeId>& declStack)
        : TypeOnceVisitor("InternalTypeFunctionFinder", /* skipBoundTypes */ true)
    {
        TypeFunctionFinder f;
        for (TypeId fn : declStack)
            f.traverse(fn);

        mentionedFunctions = std::move(f.mentionedFunctions);
        mentionedFunctionPacks = std::move(f.mentionedFunctionPacks);
    }

    bool visit(TypeId ty, const TypeFunctionInstanceType& tfit) override
    {
        bool hasGeneric = false;

        for (TypeId p : tfit.typeArguments)
        {
            if (get<GenericType>(follow(p)))
            {
                hasGeneric = true;
                break;
            }
        }

        for (TypePackId p : tfit.packArguments)
        {
            if (get<GenericTypePack>(follow(p)))
            {
                hasGeneric = true;
                break;
            }
        }

        if (hasGeneric)
        {
            for (TypeId mentioned : mentionedFunctions)
            {
                const TypeFunctionInstanceType* mentionedTfit = get<TypeFunctionInstanceType>(mentioned);
                LUAU_ASSERT(mentionedTfit);
                if (areEquivalent(tfit, *mentionedTfit))
                {
                    return true;
                }
            }

            internalFunctions.insert(ty);
        }

        return true;
    }

    bool visit(TypePackId tp, const TypeFunctionInstanceTypePack& tfitp) override
    {
        bool hasGeneric = false;

        for (TypeId p : tfitp.typeArguments)
        {
            if (get<GenericType>(follow(p)))
            {
                hasGeneric = true;
                break;
            }
        }

        for (TypePackId p : tfitp.packArguments)
        {
            if (get<GenericTypePack>(follow(p)))
            {
                hasGeneric = true;
                break;
            }
        }

        if (hasGeneric)
        {
            for (TypePackId mentioned : mentionedFunctionPacks)
            {
                const TypeFunctionInstanceTypePack* mentionedTfitp = get<TypeFunctionInstanceTypePack>(mentioned);
                LUAU_ASSERT(mentionedTfitp);
                if (areEquivalent(tfitp, *mentionedTfitp))
                {
                    return true;
                }
            }

            internalPackFunctions.insert(tp);
        }

        return true;
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
)
{
    LUAU_TIMETRACE_SCOPE("check", "Typechecking");

    TypeChecker2 typeChecker{builtinTypes, typeFunctionRuntime, unifierState, limits, logger, &sourceModule, module};

    typeChecker.visit(sourceModule.root);

    unfreeze(module->interfaceTypes);
    copyErrors(module->errors, module->interfaceTypes, builtinTypes);
    freeze(module->interfaceTypes);
}

TypeChecker2::TypeChecker2(
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<TypeFunctionRuntime> typeFunctionRuntime,
    NotNull<UnifierSharedState> unifierState,
    NotNull<TypeCheckLimits> limits,
    DcrLogger* logger,
    const SourceModule* sourceModule,
    Module* module
)
    : builtinTypes(builtinTypes)
    , typeFunctionRuntime(typeFunctionRuntime)
    , logger(logger)
    , limits(limits)
    , ice(unifierState->iceHandler)
    , sourceModule(sourceModule)
    , module(module)
    , normalizer{module->internalTypes.get(), builtinTypes, unifierState, SolverMode::New, /* cacheInhabitance */ true}
    , _subtyping{builtinTypes, NotNull{module->internalTypes.get()}, NotNull{&normalizer}, typeFunctionRuntime, NotNull{unifierState->iceHandler}}
    , subtyping(&_subtyping)
{
}

bool TypeChecker2::allowsNoReturnValues(const TypePackId tp)
{
    for (TypeId ty : tp)
    {
        if (!get<ErrorType>(follow(ty)))
            return false;
    }

    return true;
}

Location TypeChecker2::getEndLocation(const AstExprFunction* function)
{
    Location loc = function->location;
    if (loc.begin.line != loc.end.line)
    {
        Position begin = loc.end;
        begin.column = std::max(0u, begin.column - 3);
        loc = Location(begin, 3);
    }

    return loc;
}

bool TypeChecker2::isErrorCall(const AstExprCall* call)
{
    const AstExprGlobal* global = call->func->as<AstExprGlobal>();
    if (!global)
        return false;

    if (global->name == "error")
        return true;
    else if (global->name == "assert")
    {
        // assert() will error because it is missing the first argument
        if (call->args.size == 0)
            return true;

        if (AstExprConstantBool* expr = call->args.data[0]->as<AstExprConstantBool>())
            if (!expr->value)
                return true;
    }

    return false;
}

bool TypeChecker2::hasBreak(AstStat* node)
{
    if (AstStatBlock* stat = node->as<AstStatBlock>())
    {
        for (size_t i = 0; i < stat->body.size; ++i)
        {
            if (hasBreak(stat->body.data[i]))
                return true;
        }

        return false;
    }

    if (node->is<AstStatBreak>())
        return true;

    if (AstStatIf* stat = node->as<AstStatIf>())
    {
        if (hasBreak(stat->thenbody))
            return true;

        if (stat->elsebody && hasBreak(stat->elsebody))
            return true;

        return false;
    }

    return false;
}

const AstStat* TypeChecker2::getFallthrough(const AstStat* node)
{
    if (const AstStatBlock* stat = node->as<AstStatBlock>())
    {
        if (stat->body.size == 0)
            return stat;

        for (size_t i = 0; i < stat->body.size - 1; ++i)
        {
            if (getFallthrough(stat->body.data[i]) == nullptr)
                return nullptr;
        }

        return getFallthrough(stat->body.data[stat->body.size - 1]);
    }

    if (const AstStatIf* stat = node->as<AstStatIf>())
    {
        if (const AstStat* thenf = getFallthrough(stat->thenbody))
            return thenf;

        if (stat->elsebody)
        {
            if (const AstStat* elsef = getFallthrough(stat->elsebody))
                return elsef;

            return nullptr;
        }
        else
            return stat;
    }

    if (node->is<AstStatReturn>())
        return nullptr;

    if (const AstStatExpr* stat = node->as<AstStatExpr>())
    {
        if (AstExprCall* call = stat->expr->as<AstExprCall>(); call && isErrorCall(call))
            return nullptr;

        return stat;
    }

    if (const AstStatWhile* stat = node->as<AstStatWhile>())
    {
        if (AstExprConstantBool* expr = stat->condition->as<AstExprConstantBool>())
        {
            if (expr->value && !hasBreak(stat->body))
                return nullptr;
        }

        return node;
    }

    if (const AstStatRepeat* stat = node->as<AstStatRepeat>())
    {
        if (AstExprConstantBool* expr = stat->condition->as<AstExprConstantBool>())
        {
            if (!expr->value && !hasBreak(stat->body))
                return nullptr;
        }

        if (getFallthrough(stat->body) == nullptr)
            return nullptr;

        return node;
    }

    return node;
}

std::optional<StackPusher> TypeChecker2::pushStack(AstNode* node)
{
    if (Scope** scope = module->astScopes.find(node))
        return StackPusher{stack, *scope};
    else
        return std::nullopt;
}

void TypeChecker2::checkForInternalTypeFunction(TypeId ty, Location location)
{
    InternalTypeFunctionFinder finder(functionDeclStack);
    finder.traverse(ty);

    for (TypeId internal : finder.internalFunctions)
        reportError(WhereClauseNeeded{internal}, location);

    for (TypePackId internal : finder.internalPackFunctions)
        reportError(PackWhereClauseNeeded{internal}, location);
}

TypeId TypeChecker2::checkForTypeFunctionInhabitance(TypeId instance, Location location)
{
    if (seenTypeFunctionInstances.find(instance))
        return instance;
    seenTypeFunctionInstances.insert(instance);

    TypeFunctionContext context{
        NotNull{module->internalTypes.get()}, builtinTypes, stack.back(), NotNull{&normalizer}, typeFunctionRuntime, ice, limits, subtyping
    };

    ErrorVec errors = reduceTypeFunctions(instance, location, NotNull{&context}, true).errors;
    if (!isErrorSuppressing(location, instance))
        reportErrors(std::move(errors));
    return instance;
}

TypePackId TypeChecker2::lookupPack(AstExpr* expr) const
{
    // If a type isn't in the type graph, it probably means that a recursion limit was exceeded.
    // We'll just return anyType in these cases.  Typechecking against any is very fast and this
    // allows us not to think about this very much in the actual typechecking logic.
    TypePackId* tp = module->astTypePacks.find(expr);
    if (tp)
        return follow(*tp);
    else
        return builtinTypes->anyTypePack;
}

TypeId TypeChecker2::lookupType(AstExpr* expr)
{
    // If a type isn't in the type graph, it probably means that a recursion limit was exceeded.
    // We'll just return anyType in these cases.  Typechecking against any is very fast and this
    // allows us not to think about this very much in the actual typechecking logic.
    TypeId* ty = module->astTypes.find(expr);
    if (ty)
        return checkForTypeFunctionInhabitance(follow(*ty), expr->location);

    TypePackId* tp = module->astTypePacks.find(expr);
    if (tp)
        return checkForTypeFunctionInhabitance(flattenPack(*tp), expr->location);

    return builtinTypes->anyType;
}

TypeId TypeChecker2::lookupAnnotation(AstType* annotation)
{
    if (FFlag::DebugLuauMagicTypes)
    {
        if (auto ref = annotation->as<AstTypeReference>(); ref && ref->name == kLuauPrint && ref->parameters.size > 0)
        {
            if (auto ann = ref->parameters.data[0].type)
            {
                TypeId argTy = lookupAnnotation(ann);
                luauPrintLine(
                    format("_luau_print (%d, %d): %s\n", annotation->location.begin.line, annotation->location.begin.column, toString(argTy).c_str())
                );
                return follow(argTy);
            }
        }
        else if (auto ref = annotation->as<AstTypeReference>(); ref && ref->name == kLuauForceConstraintSolvingIncomplete)
        {
            reportError(ConstraintSolvingIncompleteError{}, ref->location);
            return builtinTypes->anyType;
        }
    }

    TypeId* ty = module->astResolvedTypes.find(annotation);

    if (module->constraintGenerationDidNotComplete && !ty)
        return builtinTypes->anyType;

    LUAU_ASSERT(ty);
    return checkForTypeFunctionInhabitance(follow(*ty), annotation->location);
}

std::optional<TypePackId> TypeChecker2::lookupPackAnnotation(AstTypePack* annotation) const
{
    TypePackId* tp = module->astResolvedTypePacks.find(annotation);
    if (tp != nullptr)
        return {follow(*tp)};
    return {};
}

TypeId TypeChecker2::lookupExpectedType(AstExpr* expr) const
{
    if (TypeId* ty = module->astExpectedTypes.find(expr))
        return follow(*ty);

    return builtinTypes->anyType;
}

TypePackId TypeChecker2::lookupExpectedPack(AstExpr* expr, TypeArena& arena) const
{
    if (TypeId* ty = module->astExpectedTypes.find(expr))
        return arena.addTypePack(TypePack{{follow(*ty)}, std::nullopt});

    return builtinTypes->anyTypePack;
}

TypePackId TypeChecker2::reconstructPack(AstArray<AstExpr*> exprs, TypeArena& arena)
{
    if (exprs.size == 0)
        return arena.addTypePack(TypePack{{}, std::nullopt});

    std::vector<TypeId> head;

    for (size_t i = 0; i < exprs.size - 1; ++i)
    {
        head.push_back(lookupType(exprs.data[i]));
    }

    TypePackId tail = lookupPack(exprs.data[exprs.size - 1]);
    return arena.addTypePack(TypePack{std::move(head), tail});
}

Scope* TypeChecker2::findInnermostScope(Location location) const
{
    Scope* bestScope = module->getModuleScope().get();

    bool didNarrow;
    do
    {
        didNarrow = false;
        for (auto scope : bestScope->children)
        {
            if (scope->location.encloses(location))
            {
                bestScope = scope.get();
                didNarrow = true;
                break;
            }
        }
    } while (didNarrow && bestScope->children.size() > 0);

    return bestScope;
}

void TypeChecker2::visit(AstStat* stat)
{
    auto pusher = pushStack(stat);

    if (auto s = stat->as<AstStatBlock>())
        return visit(s);
    else if (auto s = stat->as<AstStatIf>())
        return visit(s);
    else if (auto s = stat->as<AstStatWhile>())
        return visit(s);
    else if (auto s = stat->as<AstStatRepeat>())
        return visit(s);
    else if (auto s = stat->as<AstStatBreak>())
        return visit(s);
    else if (auto s = stat->as<AstStatContinue>())
        return visit(s);
    else if (auto s = stat->as<AstStatReturn>())
        return visit(s);
    else if (auto s = stat->as<AstStatGive>())
        return visit(s);
    else if (auto s = stat->as<AstStatExpr>())
        return visit(s);
    else if (auto s = stat->as<AstStatLocal>())
        return visit(s);
    else if (auto s = stat->as<AstStatFor>())
        return visit(s);
    else if (auto s = stat->as<AstStatForIn>())
        return visit(s);
    else if (auto s = stat->as<AstStatAssign>())
        return visit(s);
    else if (auto s = stat->as<AstStatCompoundAssign>())
        return visit(s);
    else if (auto s = stat->as<AstStatFunction>())
        return visit(s);
    else if (auto s = stat->as<AstStatLocalFunction>())
        return visit(s);
    else if (auto s = stat->as<AstStatTypeAlias>())
        return visit(s);
    else if (auto f = stat->as<AstStatTypeFunction>())
        return visit(f);
    else if (auto s = stat->as<AstStatDeclareFunction>())
        return visit(s);
    else if (auto s = stat->as<AstStatDeclareGlobal>())
        return visit(s);
    else if (auto s = stat->as<AstStatDeclareExternType>())
        return visit(s);
    else if (auto s = stat->as<AstStatDeclareClass>())
        return visit(s);
    else if (auto s = stat->as<AstStatClass>())
        return visit(s);
    else if (auto s = stat->as<AstStatError>())
        return visit(s);
    else
        LUAU_ASSERT(!"TypeChecker2 encountered an unknown node type");
}

void TypeChecker2::visit(AstStatBlock* block)
{
    auto StackPusher = pushStack(block);

    for (AstStat* statement : block->body)
        visit(statement);
}

// Luwu If Local (rfcs/if-local.md): a `when` chain's declarations are checked like any `local`, and its plain
// conditions like the condition of an ordinary `if`.
void TypeChecker2::visitIfCondition(AstExpr* condition, const AstArray<AstIfClause>& clauses)
{
    if (clauses.size == 0)
    {
        InConditionalContext flipper{&typeContext};
        visit(condition, ValueContext::RValue);
        return;
    }

    for (const AstIfClause& clause : clauses)
    {
        if (clause.declaration)
        {
            visit(clause.declaration);
        }
        else
        {
            InConditionalContext flipper{&typeContext};
            visit(clause.expr, ValueContext::RValue);
        }
    }
}

void TypeChecker2::visit(AstStatIf* ifStatement)
{
    visitIfCondition(ifStatement->condition, ifStatement->clauses);

    visit(ifStatement->thenbody);
    if (ifStatement->elsebody)
        visit(ifStatement->elsebody);
}

void TypeChecker2::visit(AstStatWhile* whileStatement)
{
    visit(whileStatement->condition, ValueContext::RValue);
    visit(whileStatement->body);
}

void TypeChecker2::visit(AstStatRepeat* repeatStatement)
{
    visit(repeatStatement->body);
    visit(repeatStatement->condition, ValueContext::RValue);
}

void TypeChecker2::visit(AstStatBreak*) {}

void TypeChecker2::visit(AstStatContinue*) {}

// Luwu (helpful subtyping errors): the kind of statement `getFallthrough` hands back, which is the code path
// that forgot to return. An `if` with no `else` fails because of a branch that isn't written down, and a
// loop because it may not run at all; anything else is just the code path.
enum class FallthroughKind
{
    IfWithoutElse,
    Loop,
    CodePath,
};

struct FallthroughSite
{
    FallthroughKind kind;

    // Where the squiggle goes. Highlighting a whole `if` or loop paints the branch that *does* return, so
    // those get their keyword alone and the message names what is missing.
    Location location;
};

static FallthroughSite classifyFallthrough(const AstStat* fallthrough)
{
    constexpr unsigned whileKeywordLength = 5;
    constexpr unsigned repeatKeywordLength = 6;

    if (const AstStatIf* ifStat = fallthrough->as<AstStatIf>(); ifStat && !ifStat->elsebody)
        return {FallthroughKind::IfWithoutElse, ifStat->ifLocation};

    auto keyword = [](const Location& statement, unsigned length)
    {
        return Location{statement.begin, Position{statement.begin.line, statement.begin.column + length}};
    };

    if (fallthrough->is<AstStatWhile>())
        return {FallthroughKind::Loop, keyword(fallthrough->location, whileKeywordLength)};

    if (fallthrough->is<AstStatRepeat>())
        return {FallthroughKind::Loop, keyword(fallthrough->location, repeatKeywordLength)};

    return {FallthroughKind::CodePath, fallthrough->location};
}

static std::string describeFallthrough(FallthroughKind kind, TypePackId expectedReturnType)
{
    const std::string returns = "return '" + toString(expectedReturnType) + "'.";

    switch (kind)
    {
    case FallthroughKind::IfWithoutElse:
        return "This 'if' has no 'else', so the code path that skips it doesn't " + returns;
    case FallthroughKind::Loop:
        return "This loop may not run, so the code path that skips it doesn't " + returns;
    case FallthroughKind::CodePath:
        break;
    }

    return "This code path doesn't " + returns;
}

// Luwu (helpful subtyping errors): does this function body contain a `return` at all? "Not all codepaths" is
// the right thing to say about a function that returns on some paths and falls off the end on others,
// and the wrong thing to say about one that never returns anything -- there is only the one codepath.
struct ReturnStatementFinder : public AstVisitor
{
    bool found = false;

    bool visit(AstStatReturn*) override
    {
        found = true;
        return false;
    }

    // A nested function's returns are its own.
    bool visit(AstExprFunction*) override
    {
        return false;
    }

    bool visit(AstStat* stat) override
    {
        return !found;
    }

    bool visit(AstExpr* expr) override
    {
        return !found;
    }
};

// Luwu (helpful subtyping errors): a return type pack that is empty -- the function returns no values at all,
// written `()`.
static bool returnsNoValues(TypePackId tp)
{
    auto [head, tail] = flatten(tp);
    return head.empty() && !tail;
}

// Luwu (helpful subtyping errors): a return type pack of exactly one `nil`, which is what `(): nil` means and
// what a lone `return nil` produces. The whole confusion these messages exist for is that this is not
// `()`.
static bool returnsOnlyNil(TypePackId tp)
{
    auto [head, tail] = flatten(tp);
    return head.size() == 1 && !tail && isNil(follow(head[0]));
}

// Luwu (helpful subtyping errors): `return`/falling off the end produces no values; `return nil` produces one
// value that is nil. Lua hides the difference in the common case -- `local x = f()` gives nil either way --
// and upstream reports it as "Expected this to be 'nil', but got '()'", which doesn't say the two differ.
// Returns the whole message, or nullopt when the mismatch isn't between something and nothing.
static std::string valueCount(size_t count)
{
    if (count == 1)
        return "1 value";
    return std::to_string(count) + " values";
}

// Luwu (helpful subtyping errors): two return lists of different lengths. Upstream prints both lists in full ("Expected
// this to be 'number', but got 'number, string'"), which hides the count among the types: a function-typed value makes
// the two sides near-identical walls of text that differ by a `, {T}` at the end.
static std::optional<std::string> explainReturnLengthMismatch(TypePackId givenTp, TypePackId wantedTp)
{
    // A variadic list, or one ending in a call or `...`, can make up the difference
    if (!finite(givenTp) || !finite(wantedTp))
        return std::nullopt;

    const size_t wanted = size(wantedTp);
    const size_t given = size(givenTp);
    if (wanted == given)
        return std::nullopt;

    std::string message = "Expected this function to return " + valueCount(wanted) + ", but it returns " + valueCount(given) + ".\n";
    if (given > wanted)
        message += "Consider annotating every value it returns (e.g. ': (A, B)'), or removing the extra ones.";
    else
        message += "Consider returning every value the annotation lists, or removing the ones it doesn't from the annotation.";

    return message;
}

static std::optional<std::string> explainReturnCountMismatch(TypePackId givenTp, TypePackId wantedTp)
{
    const bool wantsNothing = returnsNoValues(wantedTp);
    const bool givesNothing = returnsNoValues(givenTp);

    if (wantsNothing == givesNothing)
        return explainReturnLengthMismatch(givenTp, wantedTp);

    if (wantsNothing)
    {
        std::string message = "Expected this function to return no values, but it returns ";

        if (!returnsOnlyNil(givenTp))
            return message + "'" + toString(givenTp) + "'.";

        return message + "the value 'nil'.\n"
                         "Consider changing this to a bare 'return', or if you want to return a value annotate the function's "
                         "return type as optional (a type followed by '?').";
    }

    if (!returnsOnlyNil(wantedTp))
        return "Expected this function to return '" + toString(wantedTp) + "', but it returns nothing at all.";

    return "Expected this function to return the value 'nil', but it returns nothing at all.\n"
           "Consider adding 'return nil', or annotating the return type as '()' if it is meant to return nothing.";
}

void TypeChecker2::visit(AstStatReturn* ret)
{
    Scope* scope = findInnermostScope(ret->location);
    TypePackId expectedRetType = scope->returnType;

    // Narrowly scoped to the pack tests themselves: the checks in between report on expressions,
    // and a pack mismatch from one of those is not about what this function returns.
    auto testReturnPack = [this](TypePackId actual, TypePackId expected, Location location)
    {
        ScopedMemberValue<bool> inReturn{checkingReturnStatement, true};
        testIsSubtype(actual, expected, location);
    };

    if (ret->list.size == 0)
    {
        testReturnPack(builtinTypes->emptyTypePack, expectedRetType, ret->location);
        return;
    }

    auto [head, _] = extendTypePack(*module->internalTypes, builtinTypes, expectedRetType, ret->list.size);
    bool isSubtype = true;
    std::vector<TypeId> actualHead;
    std::optional<TypePackId> actualTail;
    for (size_t idx = 0; idx < ret->list.size - 1; idx++)
    {
        if (idx < head.size())
        {
            isSubtype &= testLiteralOrAstTypeIsSubtype(ret->list.data[idx], head[idx]);
            actualHead.push_back(head[idx]);
        }
        else
        {
            actualHead.push_back(lookupType(ret->list.data[idx]));
        }
    }

    // This stanza is deconstructing what constraint generation does to
    // return statements. If we have some statement like:
    //
    //  return E0, E1, E2, ... , EN
    //
    // All expressions *except* the last will be typeArguments, and the last can
    // potentially be a pack. However, if the last expression is a function
    // call or varargs (`...`), then we _could_ have a pack in the final
    // position. Additionally, if we have an argument overflow, then we can't
    // do anything interesting with subtyping.
    //
    // _If_ the last argument is not a function call or varargs and we have
    // at least an argument underflow, then we grab the last type out of
    // the type pack head and use that to check the subtype of
    auto lastExpr = ret->list.data[ret->list.size - 1];
    if (head.size() < ret->list.size || lastExpr->is<AstExprCall>() || lastExpr->is<AstExprVarargs>())
    {
        actualTail = lookupPack(lastExpr);
    }
    else
    {
        auto lastType = head[ret->list.size - 1];
        isSubtype &= testLiteralOrAstTypeIsSubtype(lastExpr, lastType);
        actualHead.push_back(lastType);
    }

    // After all that, we still fire a pack subtype test to determine
    // whether we have a well-formed return statement. We only fire
    // this if all the previous subtype tests have succeeded, lest
    // we double error.
    if (isSubtype)
    {
        auto reconstructedRetType = module->internalTypes->addTypePack(TypePack{std::move(actualHead), std::move(actualTail)});
        testReturnPack(reconstructedRetType, expectedRetType, ret->location);
    }

    for (AstExpr* expr : ret->list)
        visit(expr, ValueContext::RValue);
}

void TypeChecker2::visit(AstStatExpr* expr)
{
    visit(expr->expr, ValueContext::RValue);
}

void TypeChecker2::visit(AstStatLocal* local)
{
    size_t count = std::max(local->values.size, local->vars.size);
    for (size_t i = 0; i < count; ++i)
    {
        AstExpr* value = i < local->values.size ? local->values.data[i] : nullptr;
        const bool isPack = value && (value->is<AstExprCall>() || value->is<AstExprVarargs>());

        if (value)
            visit(value, ValueContext::RValue);

        if (i != local->values.size - 1 || !isPack)
        {
            AstLocal* var = i < local->vars.size ? local->vars.data[i] : nullptr;

            if (var && var->annotation)
            {
                TypeId annotationType = lookupAnnotation(var->annotation);
                TypeId valueType = value ? lookupType(value) : nullptr;
                if (valueType)
                    testPotentialLiteralIsSubtype(value, annotationType);

                visit(var->annotation);
            }
        }
        else if (value)
        {
            TypePackId valuePack = lookupPack(value);
            TypePack valueTypes;
            if (i < local->vars.size)
                valueTypes = extendTypePack(*module->internalTypes, builtinTypes, valuePack, local->vars.size - i);

            Location errorLocation;
            for (size_t j = i; j < local->vars.size; ++j)
            {
                if (j - i >= valueTypes.head.size())
                {
                    errorLocation = local->vars.data[j]->location;
                    break;
                }

                AstLocal* var = local->vars.data[j];
                if (var->annotation)
                {
                    TypeId varType = lookupAnnotation(var->annotation);
                    testIsSubtype(valueTypes.head[j - i], varType, value->location);

                    visit(var->annotation);
                }
            }

            if (valueTypes.head.size() < local->vars.size - i)
            {
                reportError(
                    CountMismatch{
                        // We subtract 1 here because the final AST
                        // expression is not worth one value.  It is worth 0
                        // or more depending on valueTypes.head
                        local->values.size - 1 + valueTypes.head.size(),
                        std::nullopt,
                        local->vars.size,
                        local->values.data[local->values.size - 1]->is<AstExprCall>() ? CountMismatch::FunctionResult : CountMismatch::ExprListResult,
                    },
                    errorLocation
                );
            }
        }
    }
}

void TypeChecker2::visit(AstStatFor* forStatement)
{
    if (forStatement->var->annotation)
    {
        visit(forStatement->var->annotation);

        TypeId annotatedType = lookupAnnotation(forStatement->var->annotation);
        testIsSubtype(builtinTypes->numberType, annotatedType, forStatement->var->location);
    }

    auto checkNumber = [this](AstExpr* expr)
    {
        if (!expr)
            return;

        visit(expr, ValueContext::RValue);
        testIsSubtype(lookupType(expr), builtinTypes->numberType, expr->location);
    };

    checkNumber(forStatement->from);
    checkNumber(forStatement->to);
    checkNumber(forStatement->step);

    visit(forStatement->body);
}

// Luwu Classes (rfcs/classes): `for ... in x` where `x` is an object (or a class or trait value) without `__iter`.
// `kind` is luwuNominalKind(iteratorTy).
static std::string notIterableMessage(TypeId iteratorTy, const char* kind)
{
    if (std::string_view(kind) == "object")
        return "Cannot iterate over " + describeLuwuNominalValue(iteratorTy) + ": its class doesn't define '__iter'";

    return "Cannot iterate over " + describeLuwuNominalValue(iteratorTy) + " itself; only objects can be iterated, when their class defines '__iter'";
}

void TypeChecker2::visit(AstStatForIn* forInStatement)
{
    for (AstLocal* local : forInStatement->vars)
    {
        if (local->annotation)
            visit(local->annotation);
    }

    for (AstExpr* expr : forInStatement->values)
        visit(expr, ValueContext::RValue);

    visit(forInStatement->body);

    // Rule out crazy stuff.  Maybe possible if the file is not syntactically valid.
    if (!forInStatement->vars.size || !forInStatement->values.size)
        return;

    NotNull<Scope> scope = stack.back();
    TypeArena& arena = *module->internalTypes;

    std::vector<TypeId> variableTypes;
    for (AstLocal* var : forInStatement->vars)
    {
        std::optional<TypeId> ty = scope->lookup(var);
        LUAU_ASSERT(ty);
        variableTypes.emplace_back(*ty);
    }

    AstExpr* firstValue = forInStatement->values.data[0];

    // we need to build up a typepack for the iterators/values portion of the for-in statement.
    std::vector<TypeId> valueTypes;
    std::optional<TypePackId> iteratorTail;

    // since the first value may be the only iterator (e.g. if it is a call), we want to
    // look to see if it has a resulting typepack as our iterators.
    TypePackId* retPack = module->astTypePacks.find(firstValue);
    if (retPack)
    {
        auto [head, tail] = flatten(*retPack);
        valueTypes = head;
        iteratorTail = tail;
    }
    else
    {
        valueTypes.emplace_back(lookupType(firstValue));
    }

    // if the initial and expected typeArguments from the iterator unified during constraint solving,
    // we'll have a resolved type to use here, but we'll only use it if either the iterator is
    // directly present in the for-in statement or if we have an iterator state constraining us
    TypeId* resolvedTy = module->astForInNextTypes.find(firstValue);
    if (resolvedTy && (!retPack || valueTypes.size() > 1))
        valueTypes[0] = *resolvedTy;

    for (size_t i = 1; i < forInStatement->values.size - 1; ++i)
    {
        valueTypes.emplace_back(lookupType(forInStatement->values.data[i]));
    }

    // if we had more than one value, the tail from the first value is no longer appropriate to use.
    if (forInStatement->values.size > 1)
    {
        auto [head, tail] = flatten(lookupPack(forInStatement->values.data[forInStatement->values.size - 1]));
        valueTypes.insert(valueTypes.end(), head.begin(), head.end());
        iteratorTail = tail;
    }

    // and now we can put everything together to get the actual typepack of the iterators.
    TypePackId iteratorPack = arena.addTypePack(std::move(valueTypes), iteratorTail);

    // ... and then expand it out to 3 values (if possible)
    TypePack iteratorTypes = extendTypePack(arena, builtinTypes, iteratorPack, 3);
    if (iteratorTypes.head.empty())
    {
        reportError(GenericError{"for..in loops require at least one value to iterate over.  Got zero"}, getLocation(forInStatement->values));
        return;
    }
    TypeId iteratorTy = follow(iteratorTypes.head[0]);

    auto checkFunction = [this, &arena, &forInStatement, &variableTypes](const FunctionType* iterFtv, std::vector<TypeId> iterTys, bool isMm)
    {
        if (iterTys.size() < 1 || iterTys.size() > 3)
        {
            if (isMm)
                reportError(GenericError{"__iter metamethod must return (next[, table[, state]])"}, getLocation(forInStatement->values));
            else
                reportError(GenericError{"for..in loops must be passed (next[, table[, state]])"}, getLocation(forInStatement->values));

            return;
        }

        // It is okay if there aren't enough iterators, but the iteratee must provide enough.
        TypePack expectedVariableTypes = extendTypePack(arena, builtinTypes, iterFtv->retTypes, variableTypes.size());
        if (expectedVariableTypes.head.size() < variableTypes.size())
        {
            if (isMm)
                reportError(GenericError{"__iter metamethod's next() function does not return enough values"}, getLocation(forInStatement->values));
            else
                reportError(GenericError{"next() does not return enough values"}, forInStatement->values.data[0]->location);

            return;
        }

        // nextFn is going to be invoked with (arrayTy, startIndexTy)

        // It will be passed two arguments on every iteration save the
        // first.

        // It may be invoked with 0 or 1 argument on the first iteration.
        // This depends on the typeArguments in iterateePack and therefore
        // iteratorTypes.

        // If the iteratee is an error type, then we can't really say anything else about iteration over it.
        // After all, it _could've_ been a table.
        if (get<ErrorType>(follow(flattenPack(iterFtv->argTypes))))
            return;

        // If iteratorTypes is too short to be a valid call to nextFn, we have to report a count mismatch error.
        // If 2 is too short to be a valid call to nextFn, we have to report a count mismatch error.
        // If 2 is too long to be a valid call to nextFn, we have to report a count mismatch error.
        auto [minCount, maxCount] = getParameterExtents(TxnLog::empty(), iterFtv->argTypes, /*includeHiddenVariadics*/ true);

        TypePack flattenedArgTypes = extendTypePack(arena, builtinTypes, iterFtv->argTypes, 2);
        size_t firstIterationArgCount = iterTys.empty() ? 0 : iterTys.size() - 1;
        size_t actualArgCount = expectedVariableTypes.head.size();
        if (firstIterationArgCount < minCount)
        {
            if (isMm)
                reportError(GenericError{"__iter metamethod must return (next[, table[, state]])"}, getLocation(forInStatement->values));
            else
                reportError(CountMismatch{2, std::nullopt, firstIterationArgCount, CountMismatch::Arg}, forInStatement->values.data[0]->location);

            return;
        }
        else if (actualArgCount < minCount)
        {
            if (isMm)
                reportError(GenericError{"__iter metamethod must return (next[, table[, state]])"}, getLocation(forInStatement->values));
            else
                reportError(CountMismatch{2, std::nullopt, firstIterationArgCount, CountMismatch::Arg}, forInStatement->values.data[0]->location);

            return;
        }

        const TypeId iterFunc = follow(iterTys[0]);

        std::vector<TypeId> prospectiveArgTypes = std::vector(iterTys.begin() + 1, iterTys.end());
        // Right pad with nils if needed
        if (const TypePack* iterFuncArgs = get<TypePack>(follow(iterFtv->argTypes));
            iterFuncArgs && iterFuncArgs->head.size() > prospectiveArgTypes.size())
            prospectiveArgTypes.resize(iterFuncArgs->head.size(), builtinTypes->nilType);
        const TypePackId prospectiveArgs = arena.addTypePack(prospectiveArgTypes, std::nullopt);

        std::vector<TypeId> prospectiveRetTypes = {};
        if (variableTypes.size() > 0) // Type inference intersects the control variable with ~nil, so we make it optional here
            prospectiveRetTypes.emplace_back(arena.addType(UnionType{{variableTypes[0], builtinTypes->nilType}}));
        if (variableTypes.size() > 1)
            prospectiveRetTypes.emplace_back(variableTypes[1]);
        // Right pad with anys, since not all the return values are used (eg for key in pairs(t))
        if (const TypePack* iterFuncRets = get<TypePack>(follow(iterFtv->retTypes));
            iterFuncRets && iterFuncRets->head.size() > prospectiveRetTypes.size())
            prospectiveRetTypes.resize(iterFuncRets->head.size(), builtinTypes->anyType);
        // Add a variadic any tail because sometimes iterFunc returns a variadic pack (see forin_metatable_iter_mm)
        const TypePackId prospectiveRets = arena.addTypePack(prospectiveRetTypes, builtinTypes->anyTypePack);

        const TypeId prospectiveFunction = arena.addType(FunctionType{prospectiveArgs, prospectiveRets, std::nullopt, isMm});

        testIsSubtypeForInStat(iterFunc, prospectiveFunction, *forInStatement);
    };

    std::shared_ptr<const NormalizedType> iteratorNorm = normalizer.normalize(iteratorTy);

    if (!iteratorNorm)
        reportError(NormalizationTooComplex{}, firstValue->location);

    /*
     * If the first iterator argument is a function
     *  * There must be 1 to 3 iterator arguments.  Name them (nextTy,
     *    arrayTy, startIndexTy)
     *  * The return type of nextTy() must correspond to the variables'
     *    typeArguments and counts.  HOWEVER the first iterator will never be nil.
     *  * The first return value of nextTy must be compatible with
     *    startIndexTy.
     *  * The first argument to nextTy() must be compatible with arrayTy if
     *    present.  nil if not.
     *  * The second argument to nextTy() must be compatible with
     *    startIndexTy if it is present.  Else, it must be compatible with
     *    nil.
     *  * nextTy() must be callable with only 2 arguments.
     */
    if (const FunctionType* nextFn = get<FunctionType>(iteratorTy))
    {
        checkFunction(nextFn, iteratorTypes.head, false);
    }
    else if (const TableType* ttv = get<TableType>(iteratorTy))
    {
        if ((forInStatement->vars.size == 1 || forInStatement->vars.size == 2) && ttv->indexer)
        {
            testIsSubtype(variableTypes[0], ttv->indexer->indexType, forInStatement->vars.data[0]->location);
            if (variableTypes.size() == 2)
                testIsSubtype(variableTypes[1], ttv->indexer->indexResultType, forInStatement->vars.data[1]->location);
        }
        else
            reportError(GenericError{"Cannot iterate over a table without indexer"}, forInStatement->values.data[0]->location);
    }
    else if (get<AnyType>(iteratorTy) || get<ErrorType>(iteratorTy) || get<NeverType>(iteratorTy))
    {
        // nothing
    }
    else if (isOptional(iteratorTy) && !(iteratorNorm && iteratorNorm->shouldSuppressErrors()))
    {
        reportError(OptionalValueAccess{iteratorTy}, forInStatement->values.data[0]->location);
    }
    else if (std::optional<TypeId> iterMmTy =
                 findMetatableEntry(builtinTypes, module->errors, iteratorTy, "__iter", forInStatement->values.data[0]->location))
    {
        Instantiation instantiation{TxnLog::empty(), &arena, builtinTypes, TypeLevel{}, scope};

        if (std::optional<TypeId> instantiatedIterMmTy = instantiate(builtinTypes, NotNull{&arena}, limits, scope, *iterMmTy))
        {
            if (const FunctionType* iterMmFtv = get<FunctionType>(*instantiatedIterMmTy))
            {
                TypePackId argPack = arena.addTypePack({iteratorTy});
                testIsSubtype(argPack, iterMmFtv->argTypes, forInStatement->values.data[0]->location);

                TypePack mmIteratorTypes = extendTypePack(arena, builtinTypes, iterMmFtv->retTypes, 3);

                if (mmIteratorTypes.head.size() == 0)
                {
                    reportError(GenericError{"__iter must return at least one value"}, forInStatement->values.data[0]->location);
                    return;
                }

                TypeId nextFn = follow(mmIteratorTypes.head[0]);

                if (std::optional<TypeId> instantiatedNextFn = instantiation.substitute(nextFn))
                {
                    std::vector<TypeId> instantiatedIteratorTypes = mmIteratorTypes.head;
                    instantiatedIteratorTypes[0] = *instantiatedNextFn;

                    if (const FunctionType* nextFtv = get<FunctionType>(*instantiatedNextFn))
                    {
                        checkFunction(nextFtv, std::move(instantiatedIteratorTypes), true);
                    }
                    else if (!isErrorSuppressing(forInStatement->values.data[0]->location, *instantiatedNextFn))
                    {
                        reportError(CannotCallNonFunction{*instantiatedNextFn}, forInStatement->values.data[0]->location);
                    }
                }
                else
                {
                    reportError(UnificationTooComplex{}, forInStatement->values.data[0]->location);
                }
            }
            else if (!isErrorSuppressing(forInStatement->values.data[0]->location, *iterMmTy))
            {
                // TODO: This will not tell the user that this is because the
                // metamethod isn't callable. This is not ideal, and we should
                // improve this error message.

                // TODO: This will also not handle intersections of functions or
                // callable tables (which are supported by the runtime).
                reportError(CannotCallNonFunction{*iterMmTy}, forInStatement->values.data[0]->location);
            }
        }
        else
        {
            reportError(UnificationTooComplex{}, forInStatement->values.data[0]->location);
        }
    }
    else if (iteratorNorm && iteratorNorm->hasTables())
    {
        // Ok. All tables can be iterated.
    }
    else if (!iteratorNorm || !iteratorNorm->shouldSuppressErrors())
    {
        // Luwu Classes (rfcs/classes): upstream reports any other iteratee as "Cannot call a value of type X", which
        // for an object hides that its class just doesn't define `__iter`.
        if (const char* kind = luwuNominalKind(iteratorTy))
            reportError(GenericError{notIterableMessage(iteratorTy, kind)}, forInStatement->values.data[0]->location);
        else
            reportError(CannotCallNonFunction{iteratorTy}, forInStatement->values.data[0]->location);
    }
}

std::optional<TypeId> TypeChecker2::getBindingType(AstExpr* expr)
{
    if (auto localExpr = expr->as<AstExprLocal>())
    {
        Scope* s = stack.back();
        return s->lookup(localExpr->local);
    }
    else if (auto globalExpr = expr->as<AstExprGlobal>())
    {
        Scope* s = stack.back();
        return s->lookup(globalExpr->name);
    }
    else
        return std::nullopt;
}

void TypeChecker2::reportErrorsFromAssigningToNever(AstExpr* lhs, TypeId rhsType)
{

    if (auto indexName = lhs->as<AstExprIndexName>())
    {
        TypeId indexedType = lookupType(indexName->expr);

        // if it's already never, I don't think we have anything to do here.
        if (get<NeverType>(indexedType))
            return;

        std::string prop = indexName->index.value;

        std::shared_ptr<const NormalizedType> norm = normalizer.normalize(indexedType);
        if (!norm)
        {
            reportError(NormalizationTooComplex{}, lhs->location);
            return;
        }

        // if the type is error suppressing, we don't actually have any work left to do.
        if (norm->shouldSuppressErrors())
            return;

        const auto propTypes = lookupProp(norm.get(), prop, ValueContext::LValue, lhs->location, builtinTypes->stringType, module->errors);

        reportError(CannotAssignToNever{rhsType, propTypes.typesOfProp, CannotAssignToNever::Reason::PropertyNarrowed}, lhs->location);
    }
}

void TypeChecker2::visit(AstStatAssign* assign)
{
    size_t count = std::min(assign->vars.size, assign->values.size);

    for (size_t i = 0; i < count; ++i)
    {
        AstExpr* lhs = assign->vars.data[i];
        visit(lhs, ValueContext::LValue);
        TypeId lhsType = lookupType(lhs);

        AstExpr* rhs = assign->values.data[i];
        visit(rhs, ValueContext::RValue);
        TypeId rhsType = lookupType(rhs);

        if (get<NeverType>(lhsType))
        {
            reportErrorsFromAssigningToNever(lhs, rhsType);
            continue;
        }

        // FIXME CLI-142462: Due to the fact that we do not type state
        // tables properly, table typeArguments "time travel." We can take
        // advantage of this for the specific code pattern of:
        //
        //  local t = {}
        //  t.foo = {} -- Type of the RHS gets time warped to `{ bar: {} }`
        //  t.foo.bar = {}
        //
        if (testLiteralOrAstTypeIsSubtype(rhs, lhsType))
        {
            // If rhsType </: lhsType, then it's not useful to also report that rhsType </: bindingType
            if (std::optional<TypeId> bindingType = getBindingType(lhs))
                testLiteralOrAstTypeIsSubtype(rhs, *bindingType);
        }
    }
}

void TypeChecker2::visit(AstStatCompoundAssign* stat)
{
    AstExprBinary fake{stat->location, stat->op, stat->var, stat->value};
    visit(&fake, stat);

    TypeId* resultTy = module->astCompoundAssignResultTypes.find(stat);

    if (module->constraintGenerationDidNotComplete && !resultTy)
        return;

    LUAU_ASSERT(resultTy);
    TypeId varTy = lookupType(stat->var);

    testIsSubtype(*resultTy, varTy, stat->location);
}

void TypeChecker2::visit(AstStatFunction* stat)
{
    visit(stat->name, ValueContext::LValue);
    visit(stat->func);

    if (FFlag::LuauCheckFunctionStatementTypes)
    {
        // Consider a block of code like:
        //
        //  type X = { x: (number) -> number }
        //  function f(t: X)
        //      function t.x(a: string): string
        //          return "Hello, " .. a
        //      end
        //  end
        //
        // We need to check that the function we're assigning to `t.x` has the
        // correct type, like when we're assigning an expression to a local
        auto lhsType = lookupType(stat->name);
        auto rhsType = lookupType(stat->func);
        testIsSubtype(rhsType, lhsType, stat->func->location);
    }
}

void TypeChecker2::visit(AstStatLocalFunction* stat)
{
    visit(stat->func);
}

void TypeChecker2::visit(const AstTypeList* typeList)
{
    for (AstType* ty : typeList->types)
        visit(ty);

    if (typeList->tailType)
        visit(typeList->tailType);
}

void TypeChecker2::visit(AstStatTypeAlias* stat)
{
    // We will not visit type aliases that do not have an associated scope,
    // this means that (probably) this was a duplicate type alias or a
    // type alias with an illegal name (like `typeof`).
    if (!module->astScopes.contains(stat))
        return;

    if (const Scope* scope = findInnermostScope(stat->location))
    {
        if (auto loc = scope->isInvalidTypeAlias(stat->name.value))
            reportError(RecursiveRestraintViolation{}, *loc);
    }

    visitGenerics(stat->generics, stat->genericPacks);
    visit(stat->type);
}

void TypeChecker2::visit(AstStatTypeFunction* stat)
{
    visit(stat->body);
}

void TypeChecker2::visit(AstTypeList types)
{
    for (AstType* type : types.types)
        visit(type);
    if (types.tailType)
        visit(types.tailType);
}

void TypeChecker2::visit(AstStatDeclareFunction* stat)
{
    visitGenerics(stat->generics, stat->genericPacks);
    visit(stat->params);
    visit(stat->retTypes);
}

void TypeChecker2::visit(AstStatDeclareGlobal* stat)
{
    if (stat->type)
        visit(stat->type);
}

void TypeChecker2::visit(AstStatDeclareExternType* stat)
{
    visitGenerics(stat->generics, stat->genericPacks);

    for (const AstDeclaredExternTypeProperty& prop : stat->props)
        visit(prop.ty);
}

// Luwu Declare Statements (rfcs/declare-statements.md): a declared class is only its types; there are no bodies or
// default values to check.
void TypeChecker2::visit(AstStatDeclareClass* stat)
{
    AstStatClass* shape = stat->shape;
    visitGenerics(shape->generics, shape->genericPacks);

    if (shape->primaryConstructor)
    {
        for (AstLocal* arg : shape->primaryConstructor->args)
        {
            if (arg->annotation)
                visit(arg->annotation);
        }
    }

    for (const AstClassMember& member : shape->members)
    {
        if (const AstClassProperty* prop = member.get_if<AstClassProperty>())
        {
            if (prop->ty)
                visit(prop->ty);
        }
        else if (const AstClassMethod* method = member.get_if<AstClassMethod>())
        {
            AstExprFunction* function = method->function;
            visitGenerics(function->generics, function->genericPacks);

            for (AstLocal* arg : function->args)
            {
                if (arg->annotation)
                    visit(arg->annotation);
            }

            if (function->varargAnnotation)
                visit(function->varargAnnotation);

            if (function->returnAnnotation)
                visit(function->returnAnnotation);
        }
    }
}

// Luwu Traits (rfcs/classes/traits.md): where `stat` declares its member `name` (`__init` included: the method, or the
// primary constructor), so an error about how it meets a trait's expectation goes on it
static std::optional<Location> classMemberLocation(AstStatClass* stat, const Name& name)
{
    for (const AstClassMember& member : stat->members)
    {
        if (const AstClassProperty* prop = member.get_if<AstClassProperty>(); prop && name == prop->name.value)
            return prop->nameLocation;
        if (const AstClassMethod* method = member.get_if<AstClassMethod>(); method && name == method->functionName.value)
            return method->nameLocation;
    }

    if (const AstClassPrimaryConstructor* primaryConstructor = stat->primaryConstructor)
    {
        if (name == "__init")
            return primaryConstructor->argLocation;

        for (AstLocal* param : primaryConstructor->args)
            if (name == param->name.value)
                return param->location;
    }

    return std::nullopt;
}

// Luwu Traits (rfcs/classes/traits.md): where to report something about `trait` in `stat`: its `implements` entry, or for
// a trait implied through another one's `needs`, the entry that implies it. Never the class's name: the class header is
// not where a trait's requirement is broken.
static const AstClassTraitRef& traitRefFor(NotNull<Scope> scope, AstStatClass* stat, const ExternType* trait)
{
    LUAU_ASSERT(stat->implements.size > 0);

    for (const AstClassTraitRef& ref : stat->implements)
    {
        AstName name;
        if (AstExprGlobal* global = ref.trait->as<AstExprGlobal>())
            name = global->name;
        else if (AstExprIndexName* index = ref.trait->as<AstExprIndexName>())
            name = index->index;

        if (name.value && trait->name == name.value)
            return ref;
    }

    for (const AstClassTraitRef& ref : stat->implements)
    {
        std::optional<TypeFun> listed = scope->lookupTraitRef(ref);
        const ExternType* listedType = listed ? get<ExternType>(follow(listed->type)) : nullptr;
        if (listedType && isSubclass(listedType, trait))
            return ref;
    }

    // a trait none of the entries resolves to or needs (an instantiation of a generic one) still goes on the list
    return stat->implements.data[0];
}

static Location traitRefLocation(NotNull<Scope> scope, AstStatClass* stat, const ExternType* trait)
{
    return traitRefFor(scope, stat, trait).trait->location;
}

// Luwu Traits (rfcs/classes/traits.md): the class's constructor has to accept what the trait's expected `__init` takes
// after `self`, since the trait's code constructs it with those; parameters beyond them must accept nil.
void TypeChecker2::checkTraitConstructorExpectation(AstStatClass* stat, const ExternType* classType, const ExternType* traitType)
{
    auto expectedInit = traitType->props().find("__init");
    bool expectsInit = traitType->traitInfo->expectations.count("__init") && expectedInit != traitType->props().end() && expectedInit->second.readTy;
    const FunctionType* expected = expectsInit ? get<FunctionType>(follow(*expectedInit->second.readTy)) : nullptr;

    const Klass* klass = classType->relation ? get_if<Klass>(&*classType->relation) : nullptr;
    const ExternType* classValue = klass ? get<ExternType>(follow(klass->ty)) : nullptr;
    const TableType* metatable = classValue && classValue->metatable ? get<TableType>(follow(*classValue->metatable)) : nullptr;
    auto call = metatable ? metatable->props.find("__call") : TableType::Props::const_iterator{};
    bool hasCall = metatable && call != metatable->props.end() && call->second.readTy;
    const FunctionType* ctor = hasCall ? get<FunctionType>(follow(*call->second.readTy)) : nullptr;

    if (!expected || !ctor)
        return;

    // `__init(self, ...)` against the constructor `(class, ...) -> Class`: position 0 is `self` and the class respectively
    auto [expectedArgs, expectedTail] = flatten(expected->argTypes);
    auto [ctorArgs, ctorTail] = flatten(ctor->argTypes);
    NotNull<Scope> scope{findInnermostScope(stat->location)};
    Location location = classMemberLocation(stat, "__init").value_or(traitRefLocation(scope, stat, traitType));

    for (size_t i = 1; i < ctorArgs.size(); ++i)
    {
        TypeId passed = i < expectedArgs.size() ? expectedArgs[i] : builtinTypes->nilType;
        testIsSubtype(passed, ctorArgs[i], location);
    }
}

// Luwu Traits (rfcs/classes/traits.md): `fn` without its `self` parameter, when it has one. A method's `self` is its own
// class's type, so a class's method is never a subtype of the trait's expectation with `self` left in.
TypeId TypeChecker2::withoutSelfParameter(TypeId fnTy)
{
    const FunctionType* fn = get<FunctionType>(follow(fnTy));
    bool hasSelf = fn && !fn->argNames.empty() && fn->argNames[0] && fn->argNames[0]->name == "self";
    if (!hasSelf)
        return fnTy;

    return withoutFirstParameter(*module->internalTypes, fnTy);
}

// Luwu Traits (rfcs/classes/traits.md): a member of a class or trait type, on the type itself or, for a method, in its metatable
static const Property* findClassMember(const ExternType* type, const Name& name)
{
    if (auto it = type->props().find(name); it != type->props().end())
        return &it->second;

    const TableType* metatable = type->metatable ? get<TableType>(follow(*type->metatable)) : nullptr;
    if (metatable)
    {
        if (auto it = metatable->props.find(name); it != metatable->props.end())
            return &it->second;
    }

    return nullptr;
}

// Luwu Traits (rfcs/classes/traits.md): a trait field naming `Self`, as `classTy` has it
std::optional<TypeId> TypeChecker2::selfFieldFor(const ExternType* traitType, TypeId classTy, const Name& name)
{
    const ExternType::TraitInfo& info = *traitType->traitInfo;
    auto fieldTemplate = info.selfFieldTemplates.find(name);
    if (fieldTemplate == info.selfFieldTemplates.end() || !info.selfMarker)
        return std::nullopt;

    DenseHashMap<TypeId, TypeId> replacements{nullptr};
    DenseHashMap<TypePackId, TypePackId> packReplacements{nullptr};
    replacements[*info.selfMarker] = classTy;
    Replacer replacer{NotNull{module->internalTypes.get()}, NotNull{&replacements}, NotNull{&packReplacements}};
    return replacer.substitute(fieldTemplate->second);
}

void TypeChecker2::checkTraitFieldExpectations(AstStatClass* stat)
{
    NotNull<Scope> scope{findInnermostScope(stat->location)};
    std::optional<TypeFun> classTypeFun = scope->lookupType(stat->name->name.value);
    const ExternType* classType = classTypeFun ? get<ExternType>(follow(classTypeFun->type)) : nullptr;
    if (!classType)
        return;

    // every member the class is missing, grouped by the `implements` entry it's reported on, so a class missing several
    // gets one error listing them rather than one error each
    std::vector<MissingTraitMembers> missing;

    for (TypeId trait : classType->implementedTraits)
    {
        const ExternType* traitType = get<ExternType>(follow(trait));
        const ClassFieldUserData* traitFields = traitType ? dynamic_cast<const ClassFieldUserData*>(traitType->userData.get()) : nullptr;
        bool isTrait = traitFields && traitType->traitInfo;
        if (!isTrait)
            continue;

        checkTraitConstructorExpectation(stat, classType, traitType);

        // A field the class overrides has to fit the trait's type, as an expected field does. One the class doesn't
        // override has the trait's own type here, so comparing every trait field the class has finds the overrides. A
        // name that is a function in the class was already reported as one.
        const ClassFieldUserData* classFields = dynamic_cast<const ClassFieldUserData*>(classType->userData.get());

        for (const Name& name : traitFields->fieldNames)
        {
            bool classHasField = classFields && classFields->fieldNames.count(name);
            if (traitType->traitInfo->expectations.count(name) || !classHasField)
                continue;

            const Property* provided = findClassMember(traitType, name);
            const Property* found = findClassMember(classType, name);
            if (!provided || !found || !provided->readTy || !found->readTy)
                continue;

            // A field naming `Self` is this class's own type in the class (the trait's field has the trait for it). Not
            // overridden, the class has it from the trait as written; overridden, it has to fit that.
            TypeId providedTy = *provided->readTy;
            std::optional<Location> declaredAt = classMemberLocation(stat, name);
            if (std::optional<TypeId> selfField = selfFieldFor(traitType, classTypeFun->type, name))
            {
                if (!declaredAt)
                    continue;
                providedTy = *selfField;
            }

            Location location = declaredAt.value_or(traitRefLocation(scope, stat, traitType));
            testIsSubtype(*found->readTy, providedTy, location);
        }

        for (const auto& [name, optional] : traitType->traitInfo->expectations)
        {
            // an expected constructor is checked by checkTraitConstructorExpectation
            if (name != "__init" && !findClassMember(classType, name))
                addMissingTraitMember(missing, stat, traitType, name, traitFields->fieldNames.count(name) > 0);

            const Property* expected = findClassMember(traitType, name);
            const Property* found = findClassMember(classType, name);
            if (name == "__init" || !expected || !found || !expected->readTy || !found->readTy)
                continue;

            Location location = classMemberLocation(stat, name).value_or(traitRefLocation(scope, stat, traitType));

            if (traitFields->fieldNames.count(name))
            {
                testIsSubtype(*found->readTy, *expected->readTy, location);
                continue;
            }

            // An optional function the class leaves out has the trait's own type, made optional. An expectation whose
            // signature has an error (reported where it is written) isn't compared.
            bool bothFunctions = get<FunctionType>(follow(*expected->readTy)) && get<FunctionType>(follow(*found->readTy));
            if (bothFunctions && !containsErrorType(*expected->readTy))
                testIsSubtype(withoutSelfParameter(*found->readTy), withoutSelfParameter(*expected->readTy), location);
        }
    }

    for (const MissingTraitMembers& group : missing)
        reportMissingTraitMembers(classType, group);
}

// Luwu Traits (rfcs/classes/traits.md): "'C' implements 'Base<string>', but 'Mut<number>' needs 'Base<number>'"
static std::string neededTraitMismatchMessage(
    const std::string& className,
    TypeId implemented,
    TypeId needing,
    const std::string& neededName,
    const std::vector<TypeId>& neededArguments
)
{
    std::string needed = neededName + "<";
    for (size_t i = 0; i < neededArguments.size(); ++i)
        needed += (i > 0 ? ", " : "") + toString(neededArguments[i]);
    needed += ">";

    return format(
        "'%s' implements '%s', but '%s' needs '%s'",
        className.c_str(),
        toString(implemented).c_str(),
        toString(needing).c_str(),
        needed.c_str()
    );
}

void TypeChecker2::checkNeededTraitArguments(AstStatClass* stat)
{
    NotNull<Scope> scope{findInnermostScope(stat->location)};
    std::optional<TypeFun> classTypeFun = scope->lookupType(stat->name->name.value);
    const ExternType* classType = classTypeFun ? get<ExternType>(follow(classTypeFun->type)) : nullptr;
    if (!classType)
        return;

    // The class's instantiation of the generic trait `traitTemplate`, or nullptr when it implements none
    auto implementedInstantiation = [&](TypeId traitTemplate) -> TypeId
    {
        for (TypeId trait : classType->implementedTraits)
        {
            const ExternType* traitType = get<ExternType>(follow(trait));
            if (traitType && traitType->genericTemplate && traitTemplateOf(trait) == traitTemplate)
                return follow(trait);
        }

        return nullptr;
    };

    for (TypeId trait : classType->implementedTraits)
    {
        trait = follow(trait);
        const ExternType* traitType = get<ExternType>(trait);
        if (!traitType || !traitType->traitInfo)
            continue;

        const ExternType* traitTemplate = get<ExternType>(traitTemplateOf(trait));
        if (!traitTemplate)
            continue;

        for (const auto& [neededTrait, _] : traitType->traitInfo->neededTypeArguments)
        {
            TypeId needed = follow(neededTrait);
            TypeId implemented = implementedInstantiation(needed);
            const ExternType* implementedType = implemented ? get<ExternType>(implemented) : nullptr;
            const ExternType* neededType = get<ExternType>(needed);
            if (!implementedType || !neededType)
                continue;

            std::vector<TypeId> copiedReferences;
            std::optional<std::vector<TypeId>> arguments = neededTraitTypeArguments(
                module->internalTypes.get(), *traitTemplate, needed, traitType->instantiatedTypeParams, copiedReferences
            );
            if (!arguments || arguments->size() != implementedType->instantiatedTypeParams.size())
                continue;

            // a nominal's type parameters are invariant, so each argument has to be equivalent
            bool matches = true;
            for (size_t i = 0; i < arguments->size() && matches; ++i)
            {
                TypeId expected = (*arguments)[i];
                TypeId found = implementedType->instantiatedTypeParams[i];
                bool narrower = subtyping->isSubtype(expected, found, scope).isSubtype;
                bool wider = subtyping->isSubtype(found, expected, scope).isSubtype;
                matches = narrower && wider;
            }

            if (!matches)
            {
                std::string message = neededTraitMismatchMessage(stat->name->name.value, implemented, trait, neededType->name, *arguments);
                reportError(GenericError{message}, traitRefLocation(scope, stat, traitType));
            }
        }
    }
}

void TypeChecker2::checkTraitArguments(AstStatClass* stat)
{
    NotNull<Scope> scope{findInnermostScope(stat->location)};

    for (const AstClassTraitRef& ref : stat->implements)
    {
        for (AstExpr* arg : ref.args)
            visit(arg, ValueContext::RValue);

        std::optional<TypeFun> traitFun = scope->lookupTraitRef(ref);
        const ExternType* traitType = traitFun ? get<ExternType>(follow(traitFun->type)) : nullptr;

        // a generic trait's parameter types mention its own generics (see ConstraintGenerator::checkTraitArguments)
        bool isGeneric = traitFun && (!traitFun->typeParams.empty() || !traitFun->typePackParams.empty());
        if (!traitType || !traitType->traitInfo || isGeneric)
            continue;

        const std::vector<ExternType::TraitInfo::Parameter>& params = traitType->traitInfo->parameters;

        for (size_t i = 0; i < ref.args.size && i < params.size(); ++i)
        {
            auto field = traitType->props().find(params[i].name);
            if (field != traitType->props().end() && field->second.readTy)
                testIsSubtype(lookupType(ref.args.data[i]), *field->second.readTy, ref.args.data[i]->location);
        }
    }
}

// Luwu Traits (rfcs/classes/traits.md): what a value of type `ty` is, for "expected this to be a trait, but got '...'",
// named like the runtime's luaR_describenontrait without its article: 'nil', 'none', 'true', 'table'. Nullopt when the
// type doesn't say (`any`, a union, an extern type, ...).
static std::optional<std::string> describeNonTraitValue(TypeId ty)
{
    ty = follow(ty);

    if (const SingletonType* singleton = get<SingletonType>(ty))
    {
        if (const BooleanSingleton* boolean = get<BooleanSingleton>(singleton))
            return boolean->value ? "true" : "false";
        return "string";
    }

    if (get<TableType>(ty) || get<MetatableType>(ty))
        return "table";
    if (get<FunctionType>(ty))
        return "function";

    const PrimitiveType* primitive = get<PrimitiveType>(ty);
    if (!primitive)
        return std::nullopt;

    switch (primitive->type)
    {
    case PrimitiveType::NilType:
        return "nil";
    case PrimitiveType::NoneType:
        return "none";
    case PrimitiveType::Boolean:
        return "boolean";
    case PrimitiveType::Number:
        return "number";
    case PrimitiveType::Integer:
        return "integer";
    case PrimitiveType::String:
        return "string";
    case PrimitiveType::Thread:
        return "thread";
    case PrimitiveType::Function:
        return "function";
    case PrimitiveType::Table:
        return "table";
    case PrimitiveType::Buffer:
        return "buffer";
    }

    return std::nullopt;
}

// Luwu Traits (rfcs/classes/traits.md): an entry of `implements` or `needs` that names no trait. Analysis skips such an
// entry everywhere else, so without this the class or trait checks as if the entry weren't there; the runtime raises.
void TypeChecker2::checkTraitRefs(AstStatClass* stat, const AstArray<AstClassTraitRef>& refs)
{
    NotNull<Scope> scope{findInnermostScope(stat->location)};

    for (const AstClassTraitRef& ref : refs)
    {
        // A global's value decides first: `none` is also the name of a type, but `implements none` reads the value.
        AstExprGlobal* global = ref.trait->as<AstExprGlobal>();
        std::optional<TypeId> globalValue = global ? scope->lookup(global->name) : std::nullopt;

        bool isClass = false;
        // what the entry is instead, when it is neither a trait nor a class
        std::optional<std::string> got;
        // the entry names a trait whose type failed to check, e.g. one exported from a module that failed to export
        bool isBrokenTrait = false;

        const ExternType* valueType = globalValue ? get<ExternType>(follow(*globalValue)) : nullptr;
        bool isTraitValue = valueType && valueType->root == builtinTypes->traitType;
        bool isClassValue = valueType && valueType->root == builtinTypes->classType;

        if (isTraitValue)
            continue;
        else if (isClassValue)
            isClass = true;
        else if (globalValue)
            got = describeNonTraitValue(*globalValue);

        bool undecided = !isClass && !got;
        if (undecided)
        {
            if (std::optional<TypeFun> found = scope->lookupTraitRef(ref))
            {
                const ExternType* foundType = get<ExternType>(follow(found->type));
                if (foundType && foundType->traitInfo)
                    continue;

                isBrokenTrait = get<ErrorType>(follow(found->type)) != nullptr;

                isClass = foundType && foundType->relation && get_if<Klass>(&*foundType->relation);
                // a type of that name with no value of it, which the runtime reads as nil
                if (!isClass && global && !globalValue)
                    got = "nil";
            }
            else if (global && !globalValue)
                got = "nil";
            else if (AstExprIndexName* index = ref.trait->as<AstExprIndexName>())
            {
                // `mod.Trait`, where `mod` is a required module that has no such type. Any other expression is only
                // known at runtime.
                AstExprLocal* moduleLocal = index->expr->as<AstExprLocal>();
                bool isModule = false;
                for (const Scope* s = scope.get(); moduleLocal && s && !isModule; s = s->parent.get())
                    isModule = s->importedTypeBindings.count(moduleLocal->local->name.value) > 0;

                if (isModule)
                    got = "nil";
            }
        }

        if (isBrokenTrait)
        {
            const char* kind = stat->isTrait ? "trait" : "class";
            reportError(
                GenericError{format(
                    "This trait failed to typecheck, so %s '%s' gets none of its members; check the errors where it is defined",
                    kind,
                    stat->name->name.value
                )},
                ref.trait->location
            );
        }
        else if (isClass && stat->isTrait)
            reportError(GenericError{"This is a class, traits are not allowed to depend on classes (only other traits)"}, ref.trait->location);
        else if (isClass)
            reportError(GenericError{"This is a class, classes can only implement traits (not other classes)"}, ref.trait->location);
        else if (got)
            reportError(GenericError{format("Expected this to be a trait, but got '%s'", got->c_str())}, ref.trait->location);
    }
}

// Luwu Traits (rfcs/classes/traits.md): a trait's field or function overrides the member of the same name in a trait it
// needs (directly or through others) for the classes implementing both. Checked where the override is written, so code
// holding the needed trait is typed right whichever runs: a field overrides a field and a function a function, the
// overridden member isn't `final`, access (and a field's constness) stays the same, and the type fits. The runtime
// checks all but the type again (luaR_overridingtrait).
void TypeChecker2::checkTraitOverrides(AstStatClass* stat)
{
    NotNull<Scope> scope{findInnermostScope(stat->location)};
    std::optional<TypeFun> traitFun = scope->lookupType(stat->name->name.value);
    const ExternType* traitType = traitFun ? get<ExternType>(follow(traitFun->type)) : nullptr;
    if (!traitType || !traitType->traitInfo)
        return;

    // every trait this one needs, once each however the `needs` graph branches
    std::vector<const ExternType*> needed;
    std::vector<TypeId> pending{traitType->implementedTraits.begin(), traitType->implementedTraits.end()};
    DenseHashSet<const ExternType*> visited{nullptr};
    while (!pending.empty())
    {
        const ExternType* current = get<ExternType>(follow(pending.back()));
        // a trait needed with type arguments is listed as an instantiation (`Base<T>`), once per trait listing it
        const ExternType* currentTrait = get<ExternType>(traitTemplateOf(pending.back()));
        pending.pop_back();

        if (!current || !current->traitInfo || visited.contains(currentTrait) || currentTrait == traitType)
            continue;

        visited.insert(currentTrait);
        needed.push_back(current);
        pending.insert(pending.end(), current->implementedTraits.begin(), current->implementedTraits.end());
    }

    // What this trait writes itself: its fields and functions, and its parameters, each of which declares a field. A
    // trait's field always has a value (the parser refuses one without), so an overriding field does too.
    struct Declared
    {
        Name name;
        Location location;
        bool isField;
    };

    std::vector<Declared> declared;
    for (const AstClassMember& member : stat->members)
    {
        if (const AstClassProperty* prop = member.get_if<AstClassProperty>(); prop && !prop->expectLocation)
            declared.push_back({prop->name.value, prop->nameLocation, /* isField */ true});
        else if (const AstClassMethod* method = member.get_if<AstClassMethod>(); method && !method->expectLocation)
            declared.push_back({method->functionName.value, method->nameLocation, /* isField */ false});
    }

    if (stat->primaryConstructor)
    {
        for (AstLocal* param : stat->primaryConstructor->args)
        {
            bool restated = std::any_of(
                declared.begin(),
                declared.end(),
                [&](const Declared& d)
                {
                    return d.name == param->name.value;
                }
            );

            if (!restated)
                declared.push_back({param->name.value, param->location, /* isField */ true});
        }
    }

    for (const Declared& mine : declared)
    {
        const Property* myProp = findClassMember(traitType, mine.name);

        for (const ExternType* other : needed)
        {
            // only where the member is written: an expectation, or a member the other trait has from its own needs
            const ExternType::TraitInfo& info = *other->traitInfo;
            if (info.expectations.count(mine.name) || info.fromNeeds.count(mine.name))
                continue;

            const Property* theirs = findClassMember(other, mine.name);
            if (!theirs)
                continue;

            const ClassFieldUserData* otherFields = dynamic_cast<const ClassFieldUserData*>(other->userData.get());
            bool theirsIsField = otherFields && otherFields->fieldNames.count(mine.name);

            if (mine.isField != theirsIsField)
                reportError(
                    GenericError{format(
                        "Trait '%s' can't redefine '%s' as a %s: it is a %s in trait '%s'",
                        traitType->name.c_str(),
                        mine.name.c_str(),
                        mine.isField ? "field" : "function",
                        theirsIsField ? "field" : "function",
                        other->name.c_str()
                    )},
                    mine.location
                );
            else if (info.finals.count(mine.name))
                reportError(
                    GenericError{format("'%s' is final in trait '%s' and can't be overridden", mine.name.c_str(), other->name.c_str())}, mine.location
                );
            else if (myProp && myProp->isPrivate != theirs->isPrivate)
                reportError(
                    GenericError{format(
                        "'%s' must be %s to override it from trait '%s'",
                        mine.name.c_str(),
                        theirs->isPrivate ? "private" : "public",
                        other->name.c_str()
                    )},
                    mine.location
                );
            else if (mine.isField && myProp && myProp->isConst != theirs->isConst)
                reportError(
                    GenericError{format(
                        "'%s' must be %s to override it from trait '%s'",
                        mine.name.c_str(),
                        theirs->isConst ? "const" : "non-const",
                        other->name.c_str()
                    )},
                    mine.location
                );
            else if (mine.isField && myProp && myProp->readTy && theirs->readTy && !containsErrorType(*theirs->readTy))
                testIsSubtype(*myProp->readTy, *theirs->readTy, mine.location);
            else if (!mine.isField && myProp && myProp->readTy && theirs->readTy && !containsErrorType(*theirs->readTy))
                testIsSubtype(withoutSelfParameter(*myProp->readTy), withoutSelfParameter(*theirs->readTy), mine.location);
        }
    }
}

void TypeChecker2::addMissingTraitMember(
    std::vector<MissingTraitMembers>& missing,
    AstStatClass* stat,
    const ExternType* traitType,
    const Name& name,
    bool isField
)
{
    NotNull<Scope> scope{findInnermostScope(stat->location)};
    const AstClassTraitRef& ref = traitRefFor(scope, stat, traitType);

    auto group = std::find_if(
        missing.begin(),
        missing.end(),
        [&](const MissingTraitMembers& g)
        {
            return g.location == ref.trait->location;
        }
    );

    if (group == missing.end())
    {
        // the trait the entry names, which a member reached through its `needs` isn't
        std::optional<TypeFun> listed = scope->lookupTraitRef(ref);
        const ExternType* listedType = listed ? get<ExternType>(follow(listed->type)) : nullptr;
        missing.push_back({ref.trait->location, listedType ? listedType->name : traitType->name});
        group = missing.end() - 1;
    }

    // the member as the trait declares it: `name: type` for a field, a signature for a function
    const Property* expected = findClassMember(traitType, name);
    std::optional<TypeId> expectedTy = expected ? expected->readTy : std::nullopt;
    const FunctionType* expectedFn = expectedTy ? get<FunctionType>(follow(*expectedTy)) : nullptr;

    std::string member = name;
    if (isField && expectedTy)
        member += ": " + toString(*expectedTy);
    else if (expectedFn)
        member = "function " + toStringNamedFunction(name, *expectedFn);

    // written as the class has to write it
    if (expected && isField && expected->isConst)
        member = "const " + member;
    if (expected && traitType->traitInfo->hasAccessSpecifiers)
        member = (expected->isPrivate ? "private " : "public ") + member;

    std::string entry = "'" + member + "'";
    if (traitType->name != group->traitName)
        entry += " (from '" + traitType->name + "')";

    group->members.push_back(std::move(entry));
    (isField ? group->fields : group->functions)++;
}

// "'Flag' is missing 3 members to implement 'ArgInProgress':" and a bullet per member, like a table literal's missing
// fields
void TypeChecker2::reportMissingTraitMembers(const ExternType* classType, const MissingTraitMembers& group)
{
    size_t count = group.members.size();
    const char* kind = group.functions == 0 ? "field" : group.fields == 0 ? "function" : "member";

    std::string message = "'" + classType->name + "' is missing ";
    message += count == 1 ? std::string("a ") + kind : std::to_string(count) + " " + kind + "s";
    message += " to implement '" + group.traitName + "':";

    for (const std::string& member : group.members)
        message += "\n  • " + member;

    reportError(GenericError{message}, group.location);
}

void TypeChecker2::visit(AstStatClass* stat)
{
    LUAU_ASSERT(FFlag::LuwuClasses);

    visitGenerics(stat->generics, stat->genericPacks);

    // Luwu Classes (rfcs/classes): a primary constructor's parameters are checked like default
    // function arguments -- annotation resolved, default checked against it.
    if (const AstClassPrimaryConstructor* primaryConstructor = stat->primaryConstructor)
    {
        for (size_t i = 0; i < primaryConstructor->args.size; ++i)
        {
            AstLocal* param = primaryConstructor->args.data[i];
            AstExpr* paramDefault = primaryConstructor->argsDefaults.data[i];

            if (param->annotation)
                visit(param->annotation);

            if (paramDefault)
                visit(paramDefault, ValueContext::RValue);

            if (paramDefault && param->annotation)
                testIsSubtype(lookupType(paramDefault), lookupAnnotation(param->annotation), paramDefault->location);
        }

        // A class with a primary constructor has no table constructor, so a field the body gives no
        // default and no parameter names can never be initialized -- it is `nil` forever. That is
        // fine if the field's type says so, and an error if it doesn't.
        NotNull<Scope> scope{findInnermostScope(stat->location)};
        std::optional<TypeFun> classTypeFun = scope->lookupType(stat->name->name.value);

        for (const AstClassMember& member : stat->members)
        {
            const AstClassProperty* prop = member.get_if<AstClassProperty>();

            // Luwu Traits (rfcs/classes/traits.md): an expected field is the implementing class's to initialize
            if (!prop || prop->defaultValue || !prop->ty || prop->expectLocation)
                continue;

            size_t paramIndex = primaryConstructor->args.size;
            for (size_t i = 0; i < primaryConstructor->args.size; ++i)
                if (primaryConstructor->args.data[i]->name == prop->name)
                {
                    paramIndex = i;
                    break;
                }

            TypeId propTy = follow(lookupAnnotation(prop->ty));

            if (paramIndex < primaryConstructor->args.size)
            {
                // A bare restatement is initialized from the parameter it names, so the parameter's
                // type has to fit the annotation -- `class Cat(breed: CatBreed) private breed: number
                // end` assigns a CatBreed to a number-typed field.
                AstLocal* param = primaryConstructor->args.data[paramIndex];
                AstExpr* paramDefault = primaryConstructor->argsDefaults.data[paramIndex];

                std::optional<TypeId> paramTy;
                if (param->annotation)
                    paramTy = lookupAnnotation(param->annotation);
                else if (paramDefault)
                    paramTy = lookupType(paramDefault);

                // the annotation is what the restatement is *for*, so it is what we blame
                if (paramTy)
                    testIsSubtype(*paramTy, propTy, prop->ty->location);

                continue;
            }

            if (!classTypeFun)
                continue;

            if (!subtyping->isSubtype(builtinTypes->nilType, propTy, scope).isSubtype)
                reportError(UninitializableClassField{classTypeFun->type, prop->name.value}, prop->nameLocation);
        }
    }

    // A class whose fields are all private and which has no functions can be constructed, but nothing
    // can ever read or write what it holds: only the class's own functions may touch a private field,
    // and there are none (rfcs/classes).
    size_t fieldCount = 0;
    bool hasPublicField = false;
    bool hasFunction = false;

    for (const AstClassMember& member : stat->members)
    {
        if (const AstClassProperty* prop = member.get_if<AstClassProperty>())
        {
            ++fieldCount;
            hasPublicField |= prop->visibility == AstClassMemberVisibility::Public;
        }
        else
            hasFunction = true;
    }

    // a primary constructor parameter declares a field too, unless the class body restates it, in
    // which case the restatement was already counted above
    if (const AstClassPrimaryConstructor* primaryConstructor = stat->primaryConstructor)
    {
        for (size_t i = 0; i < primaryConstructor->args.size; ++i)
        {
            AstName paramName = primaryConstructor->args.data[i]->name;

            bool restated = false;
            for (const AstClassMember& member : stat->members)
            {
                const AstClassProperty* prop = member.get_if<AstClassProperty>();
                if (prop && prop->name == paramName)
                    restated = true;
            }

            if (restated)
                continue;

            ++fieldCount;

            if (primaryConstructor->argsQualifiers.size != primaryConstructor->args.size ||
                primaryConstructor->argsQualifiers.data[i].visibility == AstClassMemberVisibility::Public)
                hasPublicField = true;
        }
    }

    if (fieldCount > 0 && !hasPublicField && !hasFunction)
    {
        NotNull<Scope> scope{findInnermostScope(stat->location)};
        std::optional<TypeFun> classTypeFun = scope->lookupType(stat->name->name.value);

        // Luwu Traits (rfcs/classes/traits.md): the traits a class implements add members too, whose code may use the
        // class's private fields
        const ExternType* objectType = classTypeFun ? get<ExternType>(follow(classTypeFun->type)) : nullptr;
        bool traitsAddMembers = false;
        if (objectType && !objectType->implementedTraits.empty())
        {
            for (const auto& [_, prop] : objectType->props())
                traitsAddMembers |= !prop.isPrivate;

            const TableType* metatable = objectType->metatable ? get<TableType>(follow(*objectType->metatable)) : nullptr;
            traitsAddMembers |= metatable && !metatable->props.empty();
        }

        if (classTypeFun && !traitsAddMembers)
            reportError(UnusableClass{classTypeFun->type}, stat->name->location);
    }

    // A class with a private constructor can only be instantiated from its own body. If nothing there calls it
    // (`Name(...)` or `Name { ... }`, in a method, a closure nested in one, or a field default), no instance can
    // ever exist (rfcs/classes).
    bool privateConstructor = stat->primaryConstructor && stat->primaryConstructor->visibility == AstClassMemberVisibility::Private;

    for (const AstClassMember& member : stat->members)
    {
        const AstClassMethod* method = member.get_if<AstClassMethod>();
        if (method && method->functionName == "__init" && method->visibility == AstClassMemberVisibility::Private)
            privateConstructor = true;
    }

    if (privateConstructor)
    {
        // A local must be the class's own binding: a different local with the same name (another
        // module's class of that name) doesn't construct this one. A global matches by name, since
        // the parser makes a reference to the class from before its declaration a global.
        struct ConstructorCallFinder : AstVisitor
        {
            AstLocal* classLocal = nullptr;
            bool found = false;

            bool visit(AstExprCall* call) override
            {
                AstExpr* callee = call->func;
                while (AstExprGroup* group = callee->as<AstExprGroup>())
                    callee = group->expr;

                if (AstExprGlobal* global = callee->as<AstExprGlobal>(); global && global->name == classLocal->name)
                    found = true;
                else if (AstExprLocal* local = callee->as<AstExprLocal>(); local && local->local == classLocal)
                    found = true;

                return !found;
            }
        };

        ConstructorCallFinder finder;
        finder.classLocal = stat->name;

        for (const AstClassMember& member : stat->members)
        {
            if (const AstClassMethod* method = member.get_if<AstClassMethod>())
                method->function->visit(&finder);
            else if (const AstClassProperty* prop = member.get_if<AstClassProperty>(); prop && prop->defaultValue)
                prop->defaultValue->visit(&finder);
        }

        // Luwu Traits (rfcs/classes/traits.md): a trait that expects `__init` constructs its implementing classes through
        // `class<Trait>` values, which no search can follow, so such a class may well be instantiated
        if (!finder.found && stat->implements.size > 0)
        {
            std::optional<TypeFun> classTypeFun = findInnermostScope(stat->location)->lookupType(stat->name->name.value);
            const ExternType* classType = classTypeFun ? get<ExternType>(follow(classTypeFun->type)) : nullptr;

            for (TypeId trait : classType ? classType->implementedTraits : std::vector<TypeId>{})
            {
                const ExternType* traitType = get<ExternType>(follow(trait));
                if (traitType && traitType->traitInfo && traitType->traitInfo->expectations.count("__init"))
                    finder.found = true;
            }
        }

        // Luwu Traits (rfcs/classes/traits.md): a trait the class implements may construct it from its own code -- its
        // `__create` and its statics, the functions that run as the trait's rather than as a class's copy
        if (!finder.found && stat->implements.size > 0 && sourceModule->root)
        {
            auto isImplemented = [&](AstName name)
            {
                for (const AstClassTraitRef& ref : stat->implements)
                    if (AstExprGlobal* global = ref.trait->as<AstExprGlobal>(); global && global->name == name)
                        return true;

                return false;
            };

            for (AstStat* other : sourceModule->root->body)
            {
                AstStatClass* trait = other->as<AstStatClass>();
                if (!trait || !trait->isTrait || !isImplemented(trait->name->name))
                    continue;

                for (const AstClassMember& member : trait->members)
                {
                    const AstClassMethod* method = member.get_if<AstClassMethod>();
                    bool runsAsTrait = method && !method->expectLocation &&
                                       (method->function->args.size == 0 || method->function->args.data[0]->name != "self");
                    if (runsAsTrait)
                        method->function->visit(&finder);
                }
            }
        }

        if (!finder.found)
        {
            NotNull<Scope> scope{findInnermostScope(stat->location)};
            if (std::optional<TypeFun> classTypeFun = scope->lookupType(stat->name->name.value))
                reportError(UninstantiableClass{classTypeFun->type}, stat->name->location);
        }
    }

    // Luwu Traits (rfcs/classes/traits.md): every `implements` and `needs` entry has to name a trait
    if (FFlag::LuwuTraits)
    {
        checkTraitRefs(stat, stat->implements);
        checkTraitRefs(stat, stat->needs);

        if (stat->isTrait && stat->needs.size > 0)
            checkTraitOverrides(stat);
    }

    // Luwu Traits (rfcs/classes/traits.md): a member a trait expects has to fit the type the trait's functions assume. A
    // function's `self` differs by design (the trait's object type there, the class's here), so functions are compared
    // without it (withoutSelfParameter).
    if (stat->implements.size > 0)
    {
        checkTraitFieldExpectations(stat);
        checkTraitArguments(stat);
        checkNeededTraitArguments(stat);
    }

    for (const auto& member : stat->members)
    {
        if (const auto* prop = member.get_if<AstClassProperty>())
        {
            if (prop->ty)
                visit(prop->ty);

            if (prop->defaultValue)
                visit(prop->defaultValue, ValueContext::RValue);

            // If there's no annotation, the property's type was already inferred from the default
            // value itself (see ConstraintGenerator), so there's nothing to compare against here.
            if (prop->ty && prop->defaultValue)
                testIsSubtype(lookupType(prop->defaultValue), lookupAnnotation(prop->ty), prop->defaultValue->location);
        }
        else if (const auto* method = member.get_if<AstClassMethod>())
        {
            // Luwu Traits (rfcs/classes/traits.md): an expected function is a signature, with nothing to check in its body.
            // Its annotations are still checked: a generic type without its arguments, an unknown name.
            if (method->expectLocation)
            {
                const AstExprFunction* fn = method->function;
                visitGenerics(fn->generics, fn->genericPacks);

                for (AstLocal* arg : fn->args)
                    if (arg->annotation)
                        visit(arg->annotation);

                if (fn->varargAnnotation)
                    visit(fn->varargAnnotation);

                if (fn->returnAnnotation)
                    visit(fn->returnAnnotation);

                continue;
            }

            visit(method->function);

            if (method->functionName == "__tostring")
            {
                if (const FunctionType* ftv = get<FunctionType>(lookupType(method->function)))
                {
                    NotNull<Scope> scope{findInnermostScope(method->function->location)};
                    std::optional<TypeId> ret = first(ftv->retTypes);
                    if (!ret || !subtyping->isSubtype(follow(*ret), builtinTypes->stringType, scope).isSubtype)
                        reportError(GenericError{"Metamethod '__tostring' must return a string"}, method->function->location);
                }
            }
            else if (method->functionName == "__init")
            {
                if (const FunctionType* ftv = get<FunctionType>(lookupType(method->function)))
                {
                    if (first(ftv->retTypes))
                        reportError(
                            GenericError{"__init constructor should assign fields to self and should not return a value"},
                            method->function->location
                        );
                }
            }
            else if (method->functionName == "__eq" || method->functionName == "__lt" || method->functionName == "__le")
            {
                if (const FunctionType* ftv = get<FunctionType>(lookupType(method->function)))
                {
                    NotNull<Scope> scope{findInnermostScope(method->function->location)};
                    std::optional<TypeId> ret = first(ftv->retTypes);
                    if (!ret || !subtyping->isSubtype(follow(*ret), builtinTypes->booleanType, scope).isSubtype)
                        reportError(
                            GenericError{format("Metamethod '%s' must return a boolean", method->functionName.value)},
                            method->function->location
                        );
                }
            }
            else if (method->functionName == "__len")
            {
                if (const FunctionType* ftv = get<FunctionType>(lookupType(method->function)))
                {
                    NotNull<Scope> scope{findInnermostScope(method->function->location)};
                    std::optional<TypeId> ret = first(ftv->retTypes);
                    if (!ret || !subtyping->isSubtype(follow(*ret), builtinTypes->numberType, scope).isSubtype)
                        reportError(GenericError{"Metamethod '__len' must return a number"}, method->function->location);
                }
            }
            else if (
                method->functionName == "__add" || method->functionName == "__sub" || method->functionName == "__mul" ||
                method->functionName == "__div" || method->functionName == "__mod" || method->functionName == "__pow" ||
                method->functionName == "__idiv" || method->functionName == "__unm" || method->functionName == "__concat"
            )
            {
                if (const FunctionType* ftv = get<FunctionType>(lookupType(method->function)))
                {
                    if (!first(ftv->retTypes))
                        reportError(
                            GenericError{format("Metamethod '%s' must return a value", method->functionName.value)}, method->function->location
                        );
                }
            }
            else if (method->functionName == "__iter")
            {
                if (const FunctionType* ftv = get<FunctionType>(lookupType(method->function)))
                {
                    if (!first(ftv->retTypes))
                        reportError(GenericError{"Metamethod '__iter' must return a value"}, method->function->location);
                }
            }
        }
        else
            LUAU_ASSERT(!"Unknown class member!");
    }
}

void TypeChecker2::visit(AstStatError* stat)
{
    for (AstExpr* expr : stat->expressions)
        visit(expr, ValueContext::RValue);

    for (AstStat* s : stat->statements)
    {
        // Luwu Classes (rfcs/classes): a class the parser refused (a second one with a name already taken) was never
        // given a type, so its members have none to check; the parse error is what's wrong with it
        if (s->is<AstStatClass>())
            continue;

        visit(s);
    }
}

void TypeChecker2::visit(AstExpr* expr, ValueContext context)
{
    auto StackPusher = pushStack(expr);

    if (auto e = expr->as<AstExprGroup>())
        return visit(e, context);
    else if (auto e = expr->as<AstExprConstantNil>())
        return visit(e);
    else if (auto e = expr->as<AstExprConstantBool>())
        return visit(e);
    else if (auto e = expr->as<AstExprConstantNumber>())
        return visit(e);
    else if (auto e = expr->as<AstExprConstantInteger>())
        return visit(e);
    else if (auto e = expr->as<AstExprConstantString>())
        return visit(e);
    else if (auto e = expr->as<AstExprLocal>())
        return visit(e);
    else if (auto e = expr->as<AstExprGlobal>())
        return visit(e);
    else if (auto e = expr->as<AstExprVarargs>())
        return visit(e);
    else if (auto e = expr->as<AstExprCall>())
        return visit(e);
    else if (auto e = expr->as<AstExprIndexName>())
        return visit(e, context);
    else if (auto e = expr->as<AstExprIndexExpr>())
        return visit(e, context);
    else if (auto e = expr->as<AstExprFunction>())
        return visit(e);
    else if (auto e = expr->as<AstExprTable>())
        return visit(e);
    else if (auto e = expr->as<AstExprUnary>())
        return visit(e);
    else if (auto e = expr->as<AstExprBinary>())
    {
        visit(e);
        return;
    }
    else if (auto e = expr->as<AstExprTypeAssertion>())
        return visit(e);
    else if (auto e = expr->as<AstExprIfElse>())
        return visit(e);
    else if (auto e = expr->as<AstExprDo>())
        return visit(e);
    else if (auto e = expr->as<AstExprInstantiate>())
        return visit(e);
    else if (auto e = expr->as<AstExprInterpString>())
        return visit(e);
    else if (auto e = expr->as<AstExprError>())
        return visit(e);
    else
        LUAU_ASSERT(!"TypeChecker2 encountered an unknown expression type");
}

void TypeChecker2::visit(AstExprGroup* expr, ValueContext context)
{
    visit(expr->expr, context);
}

void TypeChecker2::visit(AstExprConstantNil* expr)
{
#if defined(LUAU_ENABLE_ASSERT)
    TypeId actualType = lookupType(expr);
    TypeId expectedType = builtinTypes->nilType;
    NotNull<Scope> scope{findInnermostScope(expr->location)};

    SubtypingResult r = subtyping->isSubtype(actualType, expectedType, scope);
    LUAU_ASSERT(r.isSubtype || isErrorSuppressing(expr->location, actualType));
#endif
}

void TypeChecker2::visit(AstExprConstantBool* expr)
{
    // booleans use specialized inference logic for singleton typeArguments, which can lead to real type errors here.

    const TypeId bestType = expr->value ? builtinTypes->trueType : builtinTypes->falseType;
    const TypeId inferredType = lookupType(expr);
    NotNull<Scope> scope{findInnermostScope(expr->location)};

    SubtypingResult r = subtyping->isSubtype(bestType, inferredType, scope);
    if (!r.isErrorSuppressing)
    {
        if (!r.isSubtype)
            reportError(TypeMismatch{inferredType, bestType}, expr->location);
        for (auto& e : r.errors)
            e.location = expr->location;
        reportErrors(r.errors);
    }
}

void TypeChecker2::visit(AstExprConstantNumber* expr)
{
#if defined(LUAU_ENABLE_ASSERT)
    const TypeId bestType = builtinTypes->numberType;
    const TypeId inferredType = lookupType(expr);
    NotNull<Scope> scope{findInnermostScope(expr->location)};

    const SubtypingResult r = subtyping->isSubtype(bestType, inferredType, scope);
    LUAU_ASSERT(r.isSubtype || isErrorSuppressing(expr->location, inferredType));
#endif
}

void TypeChecker2::visit(AstExprConstantInteger* expr)
{
#if defined(LUAU_ENABLE_ASSERT)
    const TypeId bestType = builtinTypes->integerType;
    const TypeId inferredType = lookupType(expr);
    NotNull<Scope> scope{findInnermostScope(expr->location)};

    const SubtypingResult r = subtyping->isSubtype(bestType, inferredType, scope);
    LUAU_ASSERT(r.isSubtype || isErrorSuppressing(expr->location, inferredType));
#endif
}

void TypeChecker2::visit(AstExprConstantString* expr)
{
    // strings use specialized inference logic for singleton typeArguments, which can lead to real type errors here.

    const TypeId bestType = module->internalTypes->addType(SingletonType{StringSingleton{std::string{expr->value.data, expr->value.size}}});
    const TypeId inferredType = lookupType(expr);
    NotNull<Scope> scope{findInnermostScope(expr->location)};

    SubtypingResult r = subtyping->isSubtype(bestType, inferredType, scope);
    if (!isErrorSuppressing(expr->location, inferredType))
    {
        if (!r.isSubtype)
            reportError(TypeMismatch{inferredType, bestType}, expr->location);
        for (auto& e : r.errors)
            e.location = expr->location;
        reportErrors(r.errors);
    }
}

void TypeChecker2::visit(AstExprLocal* expr)
{
    // TODO!
}

void TypeChecker2::visit(AstExprGlobal* expr)
{
    NotNull<Scope> scope = stack.back();
    if (!scope->lookup(expr->name))
    {
        reportError(UnknownSymbol{expr->name.value, UnknownSymbol::Binding}, expr->location);
    }
    else
    {
        if (scope->shouldWarnGlobal(expr->name.value) && !warnedGlobals.contains(expr->name.value))
        {
            reportError(UnknownSymbol{expr->name.value, UnknownSymbol::Binding}, expr->location);
            warnedGlobals.insert(expr->name.value);
        }
    }
}

void TypeChecker2::visit(AstExprVarargs* expr)
{
    // TODO!
}

static void reportAvailableOverloads(ErrorVec& errors, Location location, const ModuleName& moduleName, const std::vector<TypeId>& overloads)
{
    if (overloads.empty())
        return;

    std::stringstream s;
    s << "Available overloads: ";

    if (overloads.size() <= 1)
        return;

    for (size_t i = 0; i < overloads.size(); ++i)
    {
        if (i > 0)
            s << ((i == overloads.size() - 1) ? "; and " : "; ");

        s << toString(overloads[i]);
    }

    errors.emplace_back(location, moduleName, ExtraInformation{s.str()});
}

// Luwu (helpful subtyping errors): `a`, `a.b.c` for a local or global and plain property reads off it, else
// nullopt. Anything with a call or a computed index in it isn't a name the reader can look up.
static std::optional<std::string> bindingName(AstExpr* expr)
{
    if (AstExprGroup* group = expr->as<AstExprGroup>())
        return bindingName(group->expr);
    if (AstExprLocal* local = expr->as<AstExprLocal>())
        return std::string{local->local->name.value};
    if (AstExprGlobal* global = expr->as<AstExprGlobal>())
        return std::string{global->name.value};
    if (AstExprIndexName* index = expr->as<AstExprIndexName>(); index && index->op == '.')
    {
        if (std::optional<std::string> base = bindingName(index->expr))
            return *base + "." + index->index.value;
    }
    return std::nullopt;
}

// Luwu (helpful subtyping errors): how to refer to the function being called in a diagnostic: a name as
// `bindingName` gives it, with a method's `:` kept (`damaged:Connect`). Anything else (an immediately-invoked
// function, a call through an expression) has no name the reader would recognize, so it's empty and the
// message drops the clause that would have used it.
static std::string calleeDisplayName(AstExpr* func)
{
    if (AstExprIndexName* indexName = func->as<AstExprIndexName>())
    {
        std::optional<std::string> base = bindingName(indexName->expr);
        return base ? *base + indexName->op + indexName->index.value : std::string(indexName->index.value);
    }

    return bindingName(func).value_or(std::string{});
}

// Luwu (helpful subtyping errors): `reportError` stamps the module being checked, so a diagnostic can only
// point into a function known to be declared there. A function with no recorded module is refused too.
static bool isDeclaredInModule(const FunctionType* fn, const ModuleName& checkedModule)
{
    return fn && fn->definition && fn->definition->definitionModuleName == checkedModule;
}

// Luwu (helpful subtyping errors): the type annotation written on the parameter at `index`, when the callee
// is a function in this module and that parameter was annotated at all. Written rather than inferred, so a
// diagnostic about the parameter can point at what the user typed.
static AstType* parameterAnnotation(
    const FunctionType* callee,
    size_t index,
    const ModuleName& checkedModule,
    const TypeChecker2::DeclarationIndex& declarations
)
{
    if (!isDeclaredInModule(callee, checkedModule))
        return nullptr;

    AstExprFunction* fn = declarations.functionAt(callee->definition->definitionLocation);
    if (!fn)
        return nullptr;

    size_t argIndex = index;

    // `self` takes the first slot of the type's argument list but is never written in `args`.
    if (fn->self)
    {
        if (argIndex == 0)
            return nullptr;

        argIndex -= 1;
    }

    if (argIndex >= fn->args.size)
        return nullptr;

    return fn->args.data[argIndex]->annotation;
}

void TypeChecker2::visitCall(AstExprCall* call)
{
    TypePack args;
    std::vector<AstExpr*> argExprs;
    NotNull<Scope> scope{findInnermostScope(call->location)};
    argExprs.reserve(call->args.size + 1);

    TypeId* originalCallTy = module->astOriginalCallTypes.find(call->func);
    TypeId* selectedOverloadTy = module->astOverloadResolvedTypes.find(call);
    if (!originalCallTy)
        return;

    TypeId fnTy = follow(*originalCallTy);


    if (get<AnyType>(fnTy) || get<ErrorType>(fnTy) || get<NeverType>(fnTy))
        return;
    else if (isOptional(fnTy))
    {
        switch (shouldSuppressErrors(NotNull{&normalizer}, fnTy))
        {
        case ErrorSuppression::Suppress:
            break;
        case ErrorSuppression::NormalizationFailed:
            reportError(NormalizationTooComplex{}, call->func->location);
            [[fallthrough]];
        case ErrorSuppression::DoNotSuppress:
            reportError(OptionalValueAccess{fnTy}, call->func->location);
        }
        return;
    }

    if (call->typeArguments.size)
    {
        checkTypeInstantiation(call, fnTy, call->location, call->typeArguments);
    }

    if (selectedOverloadTy)
    {
        SubtypingResult result = subtyping->isSubtype(*originalCallTy, *selectedOverloadTy, scope);
        if (result.isSubtype)
            fnTy = follow(*selectedOverloadTy);

        if (result.isErrorSuppressing)
        {
            for (auto& e : result.errors)
                e.location = call->location;
        }

        reportErrors(std::move(result.errors));
        if (result.normalizationTooComplex)
        {
            reportError(NormalizationTooComplex{}, call->func->location);
            return;
        }
    }

    if (call->self)
    {
        AstExprIndexName* indexExpr = call->func->as<AstExprIndexName>();
        if (!indexExpr)
        {
            reportError(InternalError{"method call expression has no 'self'"}, call->location);
            return;
        }

        args.head.push_back(lookupType(indexExpr->expr));
        argExprs.push_back(indexExpr->expr);
    }

    const FunctionType* fty = get<FunctionType>(fnTy);
    size_t selfOffset = call->self ? 1 : 0;

    if (!fty && !call->self)
    {
        // `fnTy` isn't itself callable, so this call is dispatched through a
        // `__call` metamethod, which forwards `call->func` as its first
        // argument -- same as `self` above -- so param types need to be read
        // starting from its second parameter. A bad metatable is reported by the solver already.
        ErrorVec metatableErrors;
        if (auto callMm = findMetatableEntry(builtinTypes, metatableErrors, fnTy, "__call", call->func->location))
        {
            fty = get<FunctionType>(follow(*callMm));
            if (fty)
            {
                selfOffset = 1;
                checkPrivateConstructorAccess(fnTy, call->func->location);
            }
        }
    }

    // FIXME: Similar to bidirectional inference prior, this does not support
    // overloaded functions nor generic typeArguments (yet).
    if (fty && fty->generics.empty() && fty->genericPacks.empty() && call->args.size > 0)
    {
        std::vector<TypeId> paramsHead = extendTypePack(*module->internalTypes, builtinTypes, fty->argTypes, call->args.size + selfOffset).head;

        for (size_t idx = 0; idx < call->args.size; ++idx)
        {
            AstExpr* argExpr = call->args.data[idx];

            // The last argument might be an ordinary value, but it can also be an entire pack.
            if (idx == call->args.size - 1)
            {
                if (TypePackId* lastArgPack = module->astTypePacks.find(argExpr))
                {
                    auto [lastArgHead, lastArgTail] = flatten(*lastArgPack);
                    args.head.insert(args.head.end(), lastArgHead.begin(), lastArgHead.end());
                    args.tail = lastArgTail;
                    // Luwu (helpful subtyping errors): so an error about the pack (a variadic tail of the
                    // wrong type) can point at it. Upstream leaves it out, since it never reports one.
                    argExprs.push_back(argExpr);
                    continue;
                }
            }

            TypeId argExprType = lookupType(argExpr);
            argExprs.push_back(argExpr);
            if (idx + selfOffset >= paramsHead.size() || isErrorSuppressing(argExpr->location, argExprType))
                args.head.push_back(argExprType);
            else
            {
                // Scoped to this one test: an explanation reported from inside it can name the
                // callee and point at the parameter, which nothing else reporting a mismatch can.
                {
                    ScopedMemberValue<std::optional<ArgumentContext>> inArgument{
                        argumentContext, ArgumentContext{call->func, fty, idx + selfOffset}
                    };
                    testLiteralOrAstTypeIsSubtype(argExpr, paramsHead[idx + selfOffset]);
                }

                args.head.push_back(paramsHead[idx + selfOffset]);
            }
        }
    }
    else
    {
        for (size_t i = 0; i < call->args.size; ++i)
        {
            AstExpr* arg = call->args.data[i];
            argExprs.push_back(arg);
            TypeId* argTy = module->astTypes.find(arg);
            if (argTy)
                args.head.push_back(*argTy);
            else if (i == call->args.size - 1)
            {
                if (auto argTail = module->astTypePacks.find(arg))
                {
                    auto [head, tail] = flatten(*argTail);
                    args.head.insert(args.head.end(), head.begin(), head.end());
                    args.tail = tail;
                }
                else
                    args.tail = builtinTypes->anyTypePack;
            }
            else
                args.head.push_back(builtinTypes->anyType);
        }
    }

    TypePackId argsTp = module->internalTypes->addTypePack(args);
    if (auto ftv = get<FunctionType>(follow(*originalCallTy)))
    {
        if (ftv->magic)
        {
            bool usedMagic = ftv->magic->typeCheck(MagicFunctionTypeCheckContext{NotNull{this}, builtinTypes, call, argsTp, scope});
            if (usedMagic)
                return;
        }
    }

    OverloadResolver resolver{
        builtinTypes,
        NotNull{module->internalTypes.get()},
        NotNull{&normalizer},
        typeFunctionRuntime,
        NotNull{stack.back()},
        ice,
        limits,
        call->location,
    };
    DenseHashSet<TypeId> uniqueTypes{nullptr};
    findUniqueTypes(NotNull{&uniqueTypes}, argExprs, NotNull{&module->astTypes});

    TypePackId argsPack = module->internalTypes->addTypePack(args);
    const OverloadResolution result2 = resolver.resolveOverload(fnTy, argsPack, call->func->location, NotNull{&uniqueTypes}, false);

    if (!result2.potentialOverloads.empty())
        reportError(InternalError{"Internal error: outstanding free or blocked type in function call"}, call->location);

    /*
     * If one overload matches, stop.  Nothing to report.
     *
     * If 2 or more overloads match, report that it is ambiguous.  List the overloads that match.
     *
     * If 0 overloads match:
     *      If multiple overloads are arity matches, but are nonviable, report MultipleNonviableOverloads and report the overloads that have matching
     * arities. Note: Error suppressing overloads are ignored in this calculation! If only one overload is an arity match but it is nonviable, report
     * subtyping errors for just that.
     *
     *      If no overloads are arity matches, list all overloads.
     */

    if (!result2.ok.empty())
    {
        if (result2.ok.size() > 1)
            reportError(AmbiguousFunctionCall{fnTy, argsPack}, call->location);
        return;
    }

    std::vector<TypeId> overloadsToReport;

    if (1 == result2.incompatibleOverloads.size())
    {
        for (const auto& [ty, reasons] : result2.incompatibleOverloads)
        {
            if (const SubtypingReasonings* sr = get_if<SubtypingReasonings>(&reasons))
            {
                // When the overload is a `__call` metamethod,
                // `OverloadResolver::testFunctionOrCallMetamethod` passes the callee as its first
                // argument. Every path in `reasons` then indexes into a pack one entry longer than
                // the one built from `call->args`, so rebuild that longer pack and the matching
                // expression list here. Otherwise argument N's reasoning would be matched against
                // argument N - 1. For a one-argument call, such as a class constructor, it would be
                // matched against nothing, and `maybeEmplaceError` would silently drop it.
                TypePackId reportedArgsPack = argsPack;
                std::vector<AstExpr*> reportedArgExprs = argExprs;
                if (result2.metamethods.contains(ty))
                {
                    reportedArgsPack = module->internalTypes->addTypePack({fnTy}, argsPack);
                    reportedArgExprs.insert(reportedArgExprs.begin(), call->func);
                }

                size_t firstReported = module->errors.size();
                for (const SubtypingReasoning& reason : *sr)
                    resolver.reportErrors(
                        module->errors, ty, call->func->location, module->name, reportedArgsPack, reportedArgExprs, reason
                    );

                // Luwu (helpful subtyping errors): a variadic tail passed to a variadic parameter (`f(...)`) is
                // reported by the resolver as two bare packs. Say who takes what, and explain the element
                // types the way a single mismatched argument would be.
                if (FFlag::LuwuHelpfulSubtypingErrors)
                {
                    for (size_t i = firstReported; i < module->errors.size(); ++i)
                    {
                        const TypePackMismatch* tpm = get<TypePackMismatch>(module->errors[i]);
                        if (!tpm || tpm->overrideMessage)
                            continue;

                        const TypePackId wantedTp = tpm->wantedTp;
                        const TypePackId givenTp = tpm->givenTp;
                        const VariadicTypePack* wanted = get<VariadicTypePack>(follow(wantedTp));
                        const VariadicTypePack* given = get<VariadicTypePack>(follow(givenTp));
                        if (!wanted || !given)
                            continue;

                        ToStringOptions options;
                        options.sortUnionMembers = false;
                        std::string callee = calleeDisplayName(call->func);
                        std::string message = (callee.empty() ? std::string("This function") : "'" + callee + "'") + " takes '" +
                                              toString(wantedTp, options) + "', but was given '" + toString(givenTp, options) + "'";

                        std::optional<MismatchExplanation> explanation =
                            explainMismatch(given->ty, wanted->ty, subtyping, scope, builtinTypes, std::nullopt, "passed");
                        if (explanation && !explanation->standalone)
                            message += explanation->reason;

                        // Looked up again rather than held across the explainer: `module->errors` can grow.
                        get<TypePackMismatch>(module->errors[i])->overrideMessage = std::move(message);
                    }
                }
            }
            else if (const auto errorVec = get_if<ErrorVec>(&reasons))
            {
                reportErrors(*errorVec);
            }
            else
                LUAU_ASSERT(!"Unreachable");
        }

        return;
    }

    // TODO: arity mismatches need expected/actual counts.
    const auto [argHead, _argTail] = flatten(argsPack);
    if (result2.incompatibleOverloads.size() > 1)
    {
        overloadsToReport.reserve(result2.nonFunctions.size());
        for (const auto& [overloadTy, _reasons] : result2.incompatibleOverloads)
        {
            if (!isErrorSuppressing(call->location, overloadTy))
                overloadsToReport.emplace_back(overloadTy);
        }

        // If all nonviable overloads are error suppressing, don't report anything.
        if (!overloadsToReport.empty())
        {
            reportError(MultipleNonviableOverloads{argHead.size()}, call->location);
            reportAvailableOverloads(module->errors, call->location, module->name, overloadsToReport);
        }

        return;
    }

    LUAU_ASSERT(0 == result2.ok.size() && 0 == result2.incompatibleOverloads.size());

    if (1 == result2.arityMismatches.size())
    {
        const TypeId fnTy = follow(result2.arityMismatches.front());
        const FunctionType* fn = get<FunctionType>(fnTy);
        LUAU_ASSERT(fn);

        if (fn)
        {
            const bool isVariadic = Luau::isVariadic(fn->argTypes);

            // A `__call` metamethod is invoked with `call->func` forwarded as its first argument, which
            // `argHead` (built from `call->args`) doesn't include. Report the counts as the call is written:
            // leave the forwarded argument out of the expected parameters rather than adding it to the
            // arguments. For a class, `Cat("tom")` against `Cat(name, age)` then reads "expects 2, got 1".
            size_t specifiedCount = argHead.size();

            auto [minParams, optMaxParams] = getParameterExtents(TxnLog::empty(), fn->argTypes);
            if (result2.metamethods.contains(fnTy))
            {
                if (minParams > 0)
                    minParams -= 1;
                if (optMaxParams && *optMaxParams > 0)
                    *optMaxParams -= 1;
            }

            reportError(CountMismatch{minParams, optMaxParams, specifiedCount, CountMismatch::Arg, isVariadic}, call->func->location);
            return;
        }
    }

    if (!result2.arityMismatches.empty())
    {
        std::stringstream ss;
        ss << "No overload for function accepts " << argHead.size() << " arguments.";
        reportError(GenericError{ss.str()}, call->func->location);
        reportAvailableOverloads(module->errors, call->func->location, module->name, result2.arityMismatches);
        return;
    }

    if (!result2.nonFunctions.empty())
    {
        auto norm = normalizer.normalize(fnTy);
        if (!norm || normalizer.isInhabited(norm.get()) == NormalizationResult::HitLimits)
            reportError(NormalizationTooComplex{}, call->func->location);
        // At this point norm is non-null and inhabited.
        if (!norm->shouldSuppressErrors())
            reportError(CannotCallNonFunction{fnTy}, call->func->location);
        return;
    }
}

void TypeChecker2::visit(AstExprCall* call)
{
    std::optional<InConditionalContext> flipper;

    // We want to preserve the existing conditional context if we are in a `typeof` call.
    if (!matchTypeOf(*call))
        flipper.emplace(&typeContext, TypeContext::Default);

    visit(call->func, ValueContext::RValue);

    if (matchAssert(*call) && call->args.size > 0)
    {
        {
            InConditionalContext flipper(&typeContext);
            visit(call->args.data[0], ValueContext::RValue);
        }

        for (size_t i = 1; i < call->args.size; ++i)
            visit(call->args.data[i], ValueContext::RValue);
    }
    else
    {
        for (AstExpr* arg : call->args)
            visit(arg, ValueContext::RValue);
    }

    visitCall(call);
}

std::optional<TypeId> TypeChecker2::tryStripUnionFromNil(TypeId ty) const
{
    if (const UnionType* utv = get<UnionType>(ty))
    {
        if (!std::any_of(begin(utv), end(utv), isNil))
            return ty;

        std::vector<TypeId> result;

        for (TypeId option : utv)
        {
            if (!isNil(option))
                result.push_back(option);
        }

        if (result.empty())
            return std::nullopt;

        return result.size() == 1 ? result[0] : module->internalTypes->addType(UnionType{std::move(result)});
    }

    return std::nullopt;
}

TypeId TypeChecker2::stripFromNilAndReport(TypeId ty, const Location& location)
{
    ty = follow(ty);

    if (auto utv = get<UnionType>(ty))
    {
        if (!std::any_of(begin(utv), end(utv), isNil))
            return ty;
    }

    if (std::optional<TypeId> strippedUnion = tryStripUnionFromNil(ty))
    {
        switch (shouldSuppressErrors(NotNull{&normalizer}, ty))
        {
        case ErrorSuppression::Suppress:
            break;
        case ErrorSuppression::NormalizationFailed:
            reportError(NormalizationTooComplex{}, location);
            [[fallthrough]];
        case ErrorSuppression::DoNotSuppress:
            reportError(OptionalValueAccess{ty}, location);
        }

        return follow(*strippedUnion);
    }

    return ty;
}

void TypeChecker2::checkPrivatePropertyAccess(TypeId tableTy, const std::string& prop, const Location& location)
{
    if (!FFlag::LuwuClasses)
        return;

    const ExternType* cls = get<ExternType>(follow(tableTy));
    while (cls)
    {
        auto it = cls->props().find(prop);
        if (it != cls->props().end())
        {
            // a class's functions are its only read-only members; its fields are always read-write,
            // even `const` ones (see ConstraintGenerator)
            if (it->second.isPrivate && !isInsideClassDeclaration(cls, module->name, location))
                reportError(PrivatePropertyAccess{tableTy, prop, it->second.isReadOnly(), cls->name}, location);
            return;
        }

        cls = cls->parent ? get<ExternType>(follow(*cls->parent)) : nullptr;
    }
}

bool TypeChecker2::checkConstructorReadByName(TypeId tableTy, const std::string& prop, ValueContext context, const Location& location)
{
    if (!FFlag::LuwuClasses)
        return false;

    if (context != ValueContext::RValue || prop != "__init")
        return false;

    const ExternType* cls = get<ExternType>(follow(tableTy));
    const bool isClassOrObject =
        cls && (cls->root == builtinTypes->classType || cls->root == builtinTypes->objectType || cls->root == builtinTypes->traitType);
    if (!isClassOrObject)
        return false;

    reportError(ConstructorReadByName{tableTy, cls->name}, location);
    return true;
}

namespace
{

// Luwu Classes (rfcs/classes): the locals of `init` that always hold `self`. A local counts when it is
// initialized from `self` or from another such local, and nothing in `init` assigns to it, closures
// included: a reassigned one may hold another object when the write runs.
struct SelfAliasCollector : AstVisitor
{
    DenseHashSet<const AstLocal*> aliases{nullptr};
    DenseHashSet<const AstLocal*> written{nullptr};

    explicit SelfAliasCollector(const AstLocal* self)
    {
        aliases.insert(self);
    }

    void markWritten(AstExpr* target)
    {
        if (AstExprLocal* local = target->as<AstExprLocal>())
            written.insert(local->local);
    }

    bool visit(AstStatLocal* node) override
    {
        // statements are visited in source order, so an alias of an alias is already known here
        for (size_t i = 0; i < node->vars.size && i < node->values.size; ++i)
        {
            AstExprLocal* value = node->values.data[i]->as<AstExprLocal>();
            if (value && aliases.contains(value->local))
                aliases.insert(node->vars.data[i]);
        }

        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        for (AstExpr* target : node->vars)
            markWritten(target);

        return true;
    }

    bool visit(AstStatCompoundAssign* node) override
    {
        markWritten(node->var);
        return true;
    }

    bool visit(AstStatFunction* node) override
    {
        markWritten(node->name);
        return true;
    }
};

} // namespace

bool TypeChecker2::isInitWritingItsSelf(const ExternType* cls, const AstExpr* objectExpr) const
{
    // Mirrors the runtime rule: the running closure is the class's `__init` itself (a function nested
    // in it is not), and the object is the one it is constructing, in its `self` parameter. The object
    // may be named through a local that always holds `self` (SelfAliasCollector).
    if (enclosingFunctions.empty() || !cls->initLocation || cls->definitionModuleName != module->name)
        return false;

    const AstExprFunction* fn = enclosingFunctions.back();
    if (fn->location != *cls->initLocation)
        return false;

    const AstLocal* self = fn->self ? fn->self : (fn->args.size > 0 ? fn->args.data[0] : nullptr);
    const AstExprLocal* object = objectExpr->as<AstExprLocal>();
    if (!self || !object)
        return false;

    if (object->local == self)
        return true;

    DenseHashSet<const AstLocal*>* aliases = initSelfAliases.find(fn);
    if (!aliases)
    {
        SelfAliasCollector collector(self);
        const_cast<AstExprFunction*>(fn)->body->visit(&collector);

        DenseHashSet<const AstLocal*> stable{nullptr};
        for (const AstLocal* local : collector.aliases)
        {
            if (!collector.written.contains(local))
                stable.insert(local);
        }

        aliases = &initSelfAliases.try_insert(fn, std::move(stable)).first;
    }

    return aliases->contains(object->local);
}

void TypeChecker2::checkConstPropertyAssignment(
    TypeId tableTy,
    const AstExpr* objectExpr,
    const std::string& prop,
    ValueContext context,
    const Location& location
)
{
    if (!FFlag::LuwuClasses)
        return;

    if (context != ValueContext::LValue)
        return;

    const ExternType* cls = get<ExternType>(follow(tableTy));
    while (cls)
    {
        auto it = cls->props().find(prop);
        if (it != cls->props().end())
        {
            if (it->second.isFinal)
                reportError(GenericError{format("'%s' is a final field of '%s' and can't be assigned", prop.c_str(), cls->name.c_str())}, location);
            else if (it->second.isConst && !isInitWritingItsSelf(cls, objectExpr))
                reportError(ConstPropertyAssignment{tableTy, prop, cls->name}, location);
            return;
        }

        cls = cls->parent ? get<ExternType>(follow(*cls->parent)) : nullptr;
    }
}

void TypeChecker2::checkPrivateConstructorAccess(TypeId classTy, const Location& location)
{
    if (!FFlag::LuwuClasses)
        return;

    const ExternType* cls = get<ExternType>(follow(classTy));
    if (!cls || cls->root != builtinTypes->classType || !cls->relation)
        return;

    const Obj* obj = get_if<Obj>(&*cls->relation);
    if (!obj)
        return;

    const ExternType* instanceCls = get<ExternType>(follow(obj->ty));
    if (!instanceCls)
        return;

    auto it = instanceCls->props().find("__init");
    if (it == instanceCls->props().end())
        return;

    // Luwu Traits (rfcs/classes/traits.md): the code of a trait the class implements may construct it too, which is what
    // lets a factory trait's `__create` be the only way to make its classes
    bool insideImplementedTrait = std::any_of(
        instanceCls->implementedTraits.begin(),
        instanceCls->implementedTraits.end(),
        [&](TypeId trait)
        {
            const ExternType* traitCls = get<ExternType>(follow(trait));
            return traitCls && isInsideClassDeclaration(traitCls, module->name, location);
        }
    );

    bool mayConstruct = isInsideClassDeclaration(instanceCls, module->name, location) || insideImplementedTrait;
    if (it->second.isPrivate && !mayConstruct)
        reportError(PrivateConstructorAccess{classTy}, location);
}

void TypeChecker2::visitExprName(AstExpr* expr, Location location, const std::string& propName, ValueContext context, TypeId astIndexExprTy)
{
    visit(expr, ValueContext::RValue);
    TypeId leftType = stripFromNilAndReport(lookupType(expr), location);
    checkIndexTypeFromType(leftType, propName, context, location, astIndexExprTy);
    const bool readsConstructor = checkConstructorReadByName(leftType, propName, context, location);
    if (!readsConstructor)
        checkPrivatePropertyAccess(leftType, propName, location);
    checkConstPropertyAssignment(leftType, expr, propName, context, location);
}

void TypeChecker2::visit(AstExprIndexName* indexName, ValueContext context)
{
    // If we're indexing like _.foo - foo could either be a prop or a string.
    visitExprName(indexName->expr, indexName->location, indexName->index.value, context, builtinTypes->stringType);
}

void TypeChecker2::indexExprMetatableHelper(AstExprIndexExpr* indexExpr, const MetatableType* metaTable, TypeId exprType, TypeId indexType)
{
    if (auto tt = get<TableType>(follow(metaTable->table)); tt && tt->indexer)
        testIsSubtype(indexType, tt->indexer->indexType, indexExpr->index->location);
    else if (auto mt = get<MetatableType>(follow(metaTable->table)))
        indexExprMetatableHelper(indexExpr, mt, exprType, indexType);
    else if (auto tmt = get<TableType>(follow(metaTable->metatable)); tmt && tmt->indexer)
        testIsSubtype(indexType, tmt->indexer->indexType, indexExpr->index->location);
    else if (auto mtmt = get<MetatableType>(follow(metaTable->metatable)))
        indexExprMetatableHelper(indexExpr, mtmt, exprType, indexType);
    else
    {
        // CLI-122161: We're not handling unions correctly (probably).
        reportError(CannotExtendTable{exprType, CannotExtendTable::Indexer, "indexer??"}, indexExpr->location);
    }
}

void TypeChecker2::visit(AstExprIndexExpr* indexExpr, ValueContext context)
{
    // CLI-169235: This should probably all be the same logic as `index`, we
    // do some really weird stuff here.
    if (auto str = indexExpr->index->as<AstExprConstantString>())
    {
        TypeId astIndexExprType = lookupType(indexExpr->index);
        const std::string stringValue(str->value.data, str->value.size);
        visitExprName(indexExpr->expr, indexExpr->location, stringValue, context, astIndexExprType);
        return;
    }

    visit(indexExpr->expr, ValueContext::RValue);
    visit(indexExpr->index, ValueContext::RValue);

    TypeId exprType = follow(lookupType(indexExpr->expr));
    TypeId indexType = follow(lookupType(indexExpr->index));

    if (auto tt = get<TableType>(exprType))
    {
        if (tt->indexer)
        {
            testIsSubtype(indexType, tt->indexer->indexType, indexExpr->index->location);
            if (context == ValueContext::LValue && tt->indexer->isReadOnly)
                reportError(PropertyAccessViolation{exprType, "indexer", PropertyAccessViolation::CannotWrite}, indexExpr->location);
        }
        else
            reportError(CannotExtendTable{exprType, CannotExtendTable::Indexer, "indexer??"}, indexExpr->location);
    }
    else if (auto mt = get<MetatableType>(exprType))
    {
        return indexExprMetatableHelper(indexExpr, mt, exprType, indexType);
    }
    else if (auto cls = get<ExternType>(exprType))
    {
        if (cls->indexer)
            testIsSubtype(indexType, cls->indexer->indexType, indexExpr->index->location);
        else
            reportError(DynamicPropertyLookupOnExternTypesUnsafe{exprType}, indexExpr->location);
    }
    else if (get<UnionType>(exprType) && isOptional(exprType))
    {
        switch (shouldSuppressErrors(NotNull{&normalizer}, exprType))
        {
        case ErrorSuppression::Suppress:
            break;
        case ErrorSuppression::NormalizationFailed:
            reportError(NormalizationTooComplex{}, indexExpr->location);
            [[fallthrough]];
        case ErrorSuppression::DoNotSuppress:
            reportError(OptionalValueAccess{exprType}, indexExpr->location);
        }
    }
    else if (auto ut = get<UnionType>(exprType))
    {
        // if all of the typeArguments are a table type, the union must be a table, and so we shouldn't error.
        if (!std::all_of(begin(ut), end(ut), getTableType))
        {
            switch (shouldSuppressErrors(NotNull{&normalizer}, exprType))
            {
            case ErrorSuppression::Suppress:
                break;
            case ErrorSuppression::NormalizationFailed:
                reportError(NormalizationTooComplex{}, indexExpr->location);
                [[fallthrough]];
            case ErrorSuppression::DoNotSuppress:
                reportError(NotATable{exprType}, indexExpr->location);
            }
        }
    }
    else if (auto it = get<IntersectionType>(exprType))
    {
        // if any of the typeArguments are a table type, the intersection must be a table, and so we shouldn't error.
        if (!std::any_of(begin(it), end(it), getTableType))
            reportError(NotATable{exprType}, indexExpr->location);
    }
    else if (get<NeverType>(exprType) || isErrorSuppressing(indexExpr->location, exprType))
    {
        // Nothing
    }
    else
        reportError(NotATable{exprType}, indexExpr->location);
}

void TypeChecker2::visit(AstExprFunction* fn)
{
    InConditionalContext flipper(&typeContext, TypeContext::Default);

    auto StackPusher = pushStack(fn);

    EnclosingFunctionPusher functionPusher{enclosingFunctions, fn};

    visitGenerics(fn->generics, fn->genericPacks);

    // a default value is an ordinary expression, so it needs the same checks as any other (unknown
    // globals, bad calls), not just the subtype test against its parameter's annotation below
    for (AstExpr* argDefault : fn->argsDefaults)
        if (argDefault)
            visit(argDefault, ValueContext::RValue);

    TypeId inferredFnTy = lookupType(fn);
    functionDeclStack.push_back(inferredFnTy);

    std::shared_ptr<const NormalizedType> normalizedFnTy = normalizer.normalize(inferredFnTy);
    if (!normalizedFnTy)
    {
        reportError(CodeTooComplex{}, fn->location);
    }
    else if (get<ErrorType>(normalizedFnTy->errors))
    {
        // If we have an error type, we don't want to do anything else involving the normalized type
        normalizedFnTy = nullptr;
    }
    else if (!normalizedFnTy->hasFunctions())
    {
        reportError(InternalError{"Internal error: Lambda has non-function type " + toString(inferredFnTy)}, fn->location);
        return;
    }
    else
    {
        if (1 != normalizedFnTy->functions.parts.size())
        {
            reportError(InternalError{"Unexpected: Lambda has unexpected type " + toString(inferredFnTy)}, fn->location);
            return;
        }

        const FunctionType* inferredFtv = get<FunctionType>(normalizedFnTy->functions.parts.front());
        LUAU_ASSERT(inferredFtv);

        // There is no way to write an annotation for the self argument, so we
        // cannot do anything to check it.
        auto argIt = begin(inferredFtv->argTypes);
        if (fn->self)
            ++argIt;

        for (size_t i = 0; i < fn->args.size; ++i)
        {
            const auto& arg = fn->args.data[i];

            if (argIt == end(inferredFtv->argTypes))
                break;

            TypeId inferredArgTy = *argIt;

            if (arg->annotation)
            {
                // we need to typecheck any argument annotations themselves.
                visit(arg->annotation);

                TypeId annotatedArgTy = lookupAnnotation(arg->annotation);

                if (FFlag::LuwuDefaultArguments)
                {
                    TypeId argTy = fn->argsDefaults.data[i] ? stripNil(builtinTypes, *module->internalTypes, inferredArgTy) : inferredArgTy;
                    testIsSubtype(argTy, annotatedArgTy, arg->location);

                    if (AstExpr* defaultValue = fn->argsDefaults.data[i])
                        testIsSubtype(lookupType(defaultValue), annotatedArgTy, defaultValue->location);
                }
                else
                {
                    testIsSubtype(inferredArgTy, annotatedArgTy, arg->location);
                }
            }

            // Some Luau constructs can result in an argument type being
            // reduced to never by inference. In this case, we want to
            // report an error at the function, instead of reporting an
            // error at every callsite.
            if (is<NeverType>(follow(inferredArgTy)))
            {
                // If the annotation simplified to never, we don't want to
                // even look at contributors.
                bool explicitlyNever = false;
                if (arg->annotation)
                {
                    TypeId annotatedArgTy = lookupAnnotation(arg->annotation);
                    explicitlyNever = is<NeverType>(annotatedArgTy);
                }

                // Not following here is deliberate: the contribution map is
                // keyed by type pointer, but that type pointer has, at some
                // point, been transmuted to a bound type pointing to never.
                if (const auto contributors = module->upperBoundContributors.find(inferredArgTy); contributors && !explicitlyNever)
                {
                    // It's unfortunate that we can't link error messages
                    // together. For now, this will work.
                    reportError(
                        GenericError{format(
                            "Parameter '%s' has been reduced to never. This function is not callable with any possible value.", arg->name.value
                        )},
                        arg->location
                    );
                    for (const auto& [site, component] : *contributors)
                        reportError(
                            ExtraInformation{
                                format("Parameter '%s' is required to be a subtype of '%s' here.", arg->name.value, toString(component).c_str())
                            },
                            site
                        );
                }
            }

            ++argIt;
        }

        // we need to typecheck the vararg annotation, if it exists.
        if (fn->vararg && fn->varargAnnotation)
            visit(fn->varargAnnotation);

        const AstStat* fallthrough = getFallthrough(fn->body);
        if (fallthrough && !allowsNoReturnValues(follow(inferredFtv->retTypes)))
        {
            ReturnStatementFinder finder;
            fn->body->visit(&finder);

            if (!finder.found)
            {
                // With no return statement anywhere, this is the same mistake as writing a bare
                // `return`, so it gets the same explanation rather than a second phrasing of it.
                // There is no code path worth pointing at either -- there is only the one.
                FunctionExitsWithoutReturning error{inferredFtv->retTypes};
                error.overrideMessage = explainReturnCountMismatch(builtinTypes->emptyTypePack, inferredFtv->retTypes);
                reportError(std::move(error), getEndLocation(fn));
            }
            else
            {
                // Upstream reports only the summary on the function's `end`. Luwu adds a second error on
                // the code path that escapes, since the `end` doesn't say which path that is.
                reportError(FunctionExitsWithoutReturning{inferredFtv->retTypes}, getEndLocation(fn));

                const FallthroughSite site = classifyFallthrough(fallthrough);
                FunctionExitsWithoutReturning pathError{inferredFtv->retTypes};
                pathError.overrideMessage = describeFallthrough(site.kind, inferredFtv->retTypes);
                reportError(std::move(pathError), site.location);
            }
        }
    }

    visit(fn->body);

    // we need to typecheck the return annotation itself, if it exists.
    if (fn->returnAnnotation)
        visit(fn->returnAnnotation);


    // If the function type has a function annotation, we need to see if we can suggest an annotation
    if (normalizedFnTy)
        suggestAnnotations(fn, normalizedFnTy->functions.parts.front());

    functionDeclStack.pop_back();
}

void TypeChecker2::visit(AstExprTable* expr)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);

    for (const AstExprTable::Item& item : expr->items)
    {
        if (item.key)
            visit(item.key, ValueContext::RValue);
        visit(item.value, ValueContext::RValue);
    }
}

void TypeChecker2::visit(AstExprUnary* expr)
{
    std::optional<InConditionalContext> inContext;
    if (expr->op != AstExprUnary::Op::Not)
        inContext.emplace(&typeContext, TypeContext::Default);

    visit(expr->expr, ValueContext::RValue);

    TypeId operandType = lookupType(expr->expr);
    TypeId resultType = lookupType(expr);

    if (isErrorSuppressing(expr->expr->location, operandType))
        return;

    if (auto it = kUnaryOpMetamethods.find(expr->op); it != kUnaryOpMetamethods.end())
    {
        std::optional<TypeId> mm = findMetatableEntry(builtinTypes, module->errors, operandType, it->second, expr->location);
        if (mm)
        {
            if (const FunctionType* ftv = get<FunctionType>(follow(*mm)))
            {
                if (std::optional<TypeId> ret = first(ftv->retTypes))
                {
                    if (expr->op == AstExprUnary::Op::Len)
                    {
                        testIsSubtype(follow(*ret), builtinTypes->numberType, expr->location);
                    }
                }
                else
                {
                    reportError(GenericError{format("Metamethod '%s' must return a value", it->second)}, expr->location);
                }

                std::optional<TypeId> firstArg = first(ftv->argTypes);
                if (!firstArg)
                {
                    reportError(GenericError{"__unm metamethod must accept one argument"}, expr->location);
                    return;
                }

                TypePackId expectedArgs = module->internalTypes->addTypePack({operandType});
                TypePackId expectedRet = module->internalTypes->addTypePack({resultType});

                TypeId expectedFunction = module->internalTypes->addType(FunctionType{expectedArgs, expectedRet});

                bool success = testIsSubtype(*mm, expectedFunction, expr->location);
                if (!success)
                    return;
            }

            return;
        }
    }

    if (expr->op == AstExprUnary::Op::Len)
    {
        DenseHashSet<TypeId> seen{nullptr};
        int recursionCount = 0;
        std::shared_ptr<const NormalizedType> nty = normalizer.normalize(operandType);

        if (nty && nty->shouldSuppressErrors())
            return;

        switch (normalizer.isInhabited(nty.get()))
        {
        case NormalizationResult::True:
            break;
        case NormalizationResult::False:
            return;
        case NormalizationResult::HitLimits:
            reportError(NormalizationTooComplex{}, expr->location);
            return;
        }

        if (!hasLength(operandType, seen, &recursionCount))
        {
            if (isOptional(operandType))
                reportError(OptionalValueAccess{operandType}, expr->location);
            else
                reportError(NotATable{operandType}, expr->location);
        }
    }
    else if (expr->op == AstExprUnary::Op::Minus)
    {
        testIsSubtype(operandType, builtinTypes->numberType, expr->location);
    }
    else if (expr->op == AstExprUnary::Op::Not)
    {
    }
    else
    {
        LUAU_ASSERT(!"Unhandled unary operator");
    }
}

// Comparisons between disjoint typeArguments is usually something we warn on, but there are some special exceptions.
static bool isOkToCompare(
    Normalizer& normalizer,
    NormalizationResult typesHaveIntersection,
    const std::shared_ptr<const NormalizedType>& normLeft,
    const std::shared_ptr<const NormalizedType>& normRight
)
{
    // We only consider warning if we know that the typeArguments are disjoint. If
    // normalization fails here, it should have also failed elsewhere and will
    // already have been reported.
    if (NormalizationResult::False != typesHaveIntersection)
        return true;

    // We allow anything to be compared to nil or none.
    if (normLeft->isNil() || normRight->isNil() || normLeft->isNone() || normRight->isNone())
        return true;

    // Comparison with never is always ok.
    else if (NormalizationResult::True != normalizer.isInhabited(normLeft.get()) ||
             NormalizationResult::True != normalizer.isInhabited(normRight.get()))
        return true;

    // Comparisons between different string singleton typeArguments is allowed even
    // if their intersection is technically uninhabited.
    else if (!normLeft->strings.isNever() && !normRight->strings.isNever())
        return true;

    return false;
};

static bool isComparisonOp(AstExprBinary::Op op)
{
    return op == AstExprBinary::CompareNe || op == AstExprBinary::CompareEq || op == AstExprBinary::CompareGe || op == AstExprBinary::CompareGt ||
           op == AstExprBinary::CompareLe || op == AstExprBinary::CompareLt;
}

struct TypeQuery
{
    std::string_view function; // "type" or "typeof"
    AstExpr* argument;
};

// `type(x)` or `typeof(x)`, where `type`/`typeof` is the global
static std::optional<TypeQuery> matchTypeQuery(AstExpr* expr)
{
    AstExprCall* call = expr->as<AstExprCall>();
    if (!call || call->self || call->args.size != 1)
        return std::nullopt;

    AstExprGlobal* global = call->func->as<AstExprGlobal>();
    if (!global)
        return std::nullopt;

    std::string_view name = global->name.value;
    if (name != "type" && name != "typeof")
        return std::nullopt;

    return TypeQuery{name, call->args.data[0]};
}

// What to check instead, for a value of type `queriedTy` (an object, class or trait value): its class, or the trait its
// declared type names.
static std::optional<std::string> luwuNominalCheckHint(TypeId queriedTy)
{
    const ExternType* etv = get<ExternType>(follow(queriedTy));
    if (!etv || !etv->relation)
        return std::nullopt;

    const Klass* klass = etv->relation->get_if<Klass>();
    if (!klass)
        return std::nullopt;

    const char* declarationKind = luwuNominalKind(klass->ty);
    bool isTrait = declarationKind && std::string_view(declarationKind) == "trait";
    return luwuNominalCheckSuggestion(etv->name, isTrait);
}

static std::string luwuNominalTypeComparisonMessage(
    std::string_view queryFunction,
    std::string_view compared,
    TypeId queriedTy,
    const char* kind,
    bool comparesEqual
)
{
    std::string_view result = comparesEqual ? "false" : "true";
    std::string message = "'" + std::string(queryFunction) + "' of " + describeLuwuNominalValue(queriedTy) + " is always \"" + kind +
                          "\", so comparing it with \"" + std::string(compared) + "\" is always " + std::string(result);

    if (std::optional<std::string> hint = luwuNominalCheckHint(queriedTy))
        message += "; " + *hint;

    return message;
}

void TypeChecker2::checkLuwuNominalTypeComparison(AstExprBinary* expr)
{
    std::optional<TypeQuery> query = matchTypeQuery(expr->left);
    AstExpr* other = expr->right;
    if (!query)
    {
        query = matchTypeQuery(expr->right);
        other = expr->left;
    }

    AstExprConstantString* compared = other->as<AstExprConstantString>();
    if (!query || !compared)
        return;

    TypeId queriedTy = lookupType(query->argument);
    const char* kind = luwuNominalKind(queriedTy);
    std::string_view comparedName{compared->value.data, compared->value.size};
    if (!kind || comparedName == kind)
        return;

    bool comparesEqual = expr->op == AstExprBinary::Op::CompareEq;
    reportError(GenericError{luwuNominalTypeComparisonMessage(query->function, comparedName, queriedTy, kind, comparesEqual)}, expr->location);
}

TypeId TypeChecker2::visit(AstExprBinary* expr, AstNode* overrideKey)
{
    std::optional<InConditionalContext> inContext;
    if (expr->op != AstExprBinary::And && expr->op != AstExprBinary::Or && expr->op != AstExprBinary::CompareEq &&
        expr->op != AstExprBinary::CompareNe)
        inContext.emplace(&typeContext, TypeContext::Default);

    // In compound assignments, the left side is both read-from and written-to, so we have to visit it in both contexts.
    if (overrideKey && overrideKey->is<AstStatCompoundAssign>())
        visit(expr->left, ValueContext::LValue);

    visit(expr->left, ValueContext::RValue);
    visit(expr->right, ValueContext::RValue);

    NotNull<Scope> scope = stack.back();

    bool isEquality = expr->op == AstExprBinary::Op::CompareEq || expr->op == AstExprBinary::Op::CompareNe;
    bool isComparison = isComparisonOp(expr->op);
    bool isLogical = expr->op == AstExprBinary::Op::And || expr->op == AstExprBinary::Op::Or;

    if (isEquality)
        checkLuwuNominalTypeComparison(expr);

    TypeId leftType = follow(lookupType(expr->left));
    TypeId rightType = follow(lookupType(expr->right));
    TypeId expectedResult = follow(lookupType(expr));

    if (get<TypeFunctionInstanceType>(expectedResult))
    {
        checkForInternalTypeFunction(expectedResult, expr->location);
        return expectedResult;
    }

    if (expr->op == AstExprBinary::Op::Or)
    {
        leftType = stripNil(builtinTypes, *module->internalTypes, leftType);
    }

    std::shared_ptr<const NormalizedType> normLeft = normalizer.normalize(leftType);
    std::shared_ptr<const NormalizedType> normRight = normalizer.normalize(rightType);

    bool isStringOperation =
        (normLeft ? normLeft->isSubtypeOfString() : isString(leftType)) && (normRight ? normRight->isSubtypeOfString() : isString(rightType));
    leftType = follow(leftType);
    if (get<AnyType>(leftType) || get<ErrorType>(leftType) || get<NeverType>(leftType))
        return leftType;
    else if (get<AnyType>(rightType) || get<ErrorType>(rightType) || get<NeverType>(rightType))
        return rightType;
    else if ((normLeft && normLeft->shouldSuppressErrors()) || (normRight && normRight->shouldSuppressErrors()))
        return builtinTypes->anyType; // we can't say anything better if it's error suppressing but not any or error alone.

    if ((get<BlockedType>(leftType) || get<FreeType>(leftType) || get<GenericType>(leftType)) && !isEquality && !isLogical)
    {
        auto name = getIdentifierOfBaseVar(expr->left);
        reportError(
            CannotInferBinaryOperation{
                expr->op,
                std::move(name),
                isComparison ? CannotInferBinaryOperation::OpKind::Comparison : CannotInferBinaryOperation::OpKind::Operation
            },
            expr->location
        );
        return leftType;
    }

    NormalizationResult typesHaveIntersection = normalizer.isIntersectionInhabited(leftType, rightType);

    if (isEquality || isComparison)
    {
        if (!isOkToCompare(normalizer, typesHaveIntersection, normLeft, normRight))
        {
            CannotCompareUnrelatedTypes error{leftType, rightType, expr->op, bindingName(expr->left), bindingName(expr->right)};
            reportError(std::move(error), expr->location);
            return builtinTypes->errorType;
        }

        auto eitherExprIsNil = (normLeft && (normLeft->isNil() || normLeft->hasNones())) || (normRight && (normRight->isNil() || normRight->hasNones()));

        // For equality operations, if either operand is nil, we should allow this comparison through
        if (isEquality && eitherExprIsNil)
            return builtinTypes->booleanType;
    }
    if (auto it = kBinaryOpMetamethods.find(expr->op); it != kBinaryOpMetamethods.end())
    {
        std::optional<TypeId> leftMt = getMetatable(leftType, builtinTypes);
        std::optional<TypeId> rightMt = getMetatable(rightType, builtinTypes);
        bool matches = leftMt == rightMt;


        if (isEquality && !matches)
        {
            auto testUnion = [&matches, builtinTypes = this->builtinTypes](const UnionType* utv, std::optional<TypeId> otherMt)
            {
                for (TypeId option : utv)
                {
                    if (getMetatable(follow(option), builtinTypes) == otherMt)
                    {
                        matches = true;
                        break;
                    }
                }
            };

            if (const UnionType* utv = get<UnionType>(leftType); utv && rightMt)
            {
                testUnion(utv, rightMt);
            }

            if (const UnionType* utv = get<UnionType>(rightType); utv && leftMt && !matches)
            {
                testUnion(utv, leftMt);
            }
        }

        // If we're working with things that are not tables, the metatable comparisons above are a little excessive
        // It's ok for one type to have a meta table and the other to not. In that case, we should fall back on
        // checking if the intersection of the typeArguments is inhabited. If `typesHaveIntersection` failed due to limits,
        // TODO: Maybe add more checks here (e.g. for functions, extern typeArguments, etc)
        if (!(get<TableType>(leftType) || get<TableType>(rightType)))
            if (!leftMt.has_value() || !rightMt.has_value())
                matches = matches || typesHaveIntersection != NormalizationResult::False;

        if (!matches && isComparison)
        {
            reportError(
                GenericError{format(
                    "Types %s and %s cannot be compared with %s because they do not have the same metatable",
                    toString(leftType).c_str(),
                    toString(rightType).c_str(),
                    toString(expr->op).c_str()
                )},
                expr->location
            );

            return builtinTypes->errorType;
        }

        std::optional<TypeId> mm;
        if (std::optional<TypeId> leftMm = findMetatableEntry(builtinTypes, module->errors, leftType, it->second, expr->left->location))
            mm = leftMm;
        else if (std::optional<TypeId> rightMm = findMetatableEntry(builtinTypes, module->errors, rightType, it->second, expr->right->location))
        {
            mm = rightMm;
            std::swap(leftType, rightType);
        }

        if (mm)
        {
            AstNode* key = expr;
            if (overrideKey != nullptr)
                key = overrideKey;

            TypeId* selectedOverloadTy = module->astOverloadResolvedTypes.find(key);
            if (!selectedOverloadTy)
            {
                // reportError(CodeTooComplex{}, expr->location);
                // was handled by a type function
                return expectedResult;
            }

            else if (const FunctionType* ftv = get<FunctionType>(follow(*selectedOverloadTy)))
            {
                TypePackId expectedArgs;
                // For >= and > we invoke __lt and __le respectively with
                // swapped argument ordering.
                if (expr->op == AstExprBinary::Op::CompareGe || expr->op == AstExprBinary::Op::CompareGt)
                {
                    expectedArgs = module->internalTypes->addTypePack({rightType, leftType});
                }
                else
                {
                    expectedArgs = module->internalTypes->addTypePack({leftType, rightType});
                }

                TypePackId expectedRets;
                if (expr->op == AstExprBinary::CompareEq || expr->op == AstExprBinary::CompareNe || expr->op == AstExprBinary::CompareGe ||
                    expr->op == AstExprBinary::CompareGt || expr->op == AstExprBinary::Op::CompareLe || expr->op == AstExprBinary::Op::CompareLt)
                {
                    expectedRets = module->internalTypes->addTypePack({builtinTypes->booleanType});
                }
                else
                {
                    expectedRets = module->internalTypes->addTypePack({module->internalTypes->freshType(builtinTypes, scope, TypeLevel{})});
                }

                TypeId expectedTy = module->internalTypes->addType(FunctionType(expectedArgs, expectedRets));

                testIsSubtype(follow(*mm), expectedTy, expr->location);

                std::optional<TypeId> ret = first(ftv->retTypes);
                if (ret)
                {
                    if (isComparison)
                    {
                        if (!isBoolean(follow(*ret)))
                        {
                            reportError(GenericError{format("Metamethod '%s' must return a boolean", it->second)}, expr->location);
                        }

                        return builtinTypes->booleanType;
                    }
                    else
                    {
                        return follow(*ret);
                    }
                }
                else
                {
                    if (isComparison)
                    {
                        reportError(GenericError{format("Metamethod '%s' must return a boolean", it->second)}, expr->location);
                    }
                    else
                    {
                        reportError(GenericError{format("Metamethod '%s' must return a value", it->second)}, expr->location);
                    }

                    return builtinTypes->errorType;
                }
            }
            else
            {
                reportError(CannotCallNonFunction{*mm}, expr->location);
            }

            return builtinTypes->errorType;
        }
        // If this is a string comparison, or a concatenation of strings, we
        // want to fall through to primitive behavior.
        else if (!isEquality && !(isStringOperation && (expr->op == AstExprBinary::Op::Concat || isComparison)))
        {
            if ((leftMt && !isString(leftType)) || (rightMt && !isString(rightType)))
            {
                if (isComparison)
                {
                    reportError(CannotCompareUnrelatedTypes{leftType, rightType, expr->op}, expr->location);
                }
                else
                {
                    reportError(
                        GenericError{format(
                            "Operator %s is not applicable for '%s' and '%s' because neither type's metatable has a '%s' metamethod",
                            toString(expr->op).c_str(),
                            toString(leftType).c_str(),
                            toString(rightType).c_str(),
                            it->second
                        )},
                        expr->location
                    );
                }

                return builtinTypes->errorType;
            }
            else if (!leftMt && !rightMt && (get<TableType>(leftType) || get<TableType>(rightType)))
            {
                if (isComparison)
                {
                    reportError(CannotCompareUnrelatedTypes{leftType, rightType, expr->op}, expr->location);
                }
                else
                {
                    reportError(
                        GenericError{format(
                            "Operator %s is not applicable for '%s' and '%s' because neither type has a metatable",
                            toString(expr->op).c_str(),
                            toString(leftType).c_str(),
                            toString(rightType).c_str()
                        )},
                        expr->location
                    );
                }

                return builtinTypes->errorType;
            }
        }
    }

    switch (expr->op)
    {
    case AstExprBinary::Op::Add:
    case AstExprBinary::Op::Sub:
    case AstExprBinary::Op::Mul:
    case AstExprBinary::Op::Div:
    case AstExprBinary::Op::FloorDiv:
    case AstExprBinary::Op::Pow:
    case AstExprBinary::Op::Mod:
        testIsSubtype(leftType, builtinTypes->numberType, expr->left->location);
        testIsSubtype(rightType, builtinTypes->numberType, expr->right->location);

        return builtinTypes->numberType;
    case AstExprBinary::Op::Concat:
    {
        const TypeId numberOrString = module->internalTypes->addType(UnionType{{builtinTypes->numberType, builtinTypes->stringType}});
        testIsSubtype(leftType, numberOrString, expr->left->location);
        testIsSubtype(rightType, numberOrString, expr->right->location);
        return builtinTypes->stringType;
    }
    case AstExprBinary::Op::CompareGe:
    case AstExprBinary::Op::CompareGt:
    case AstExprBinary::Op::CompareLe:
    case AstExprBinary::Op::CompareLt:
    {
        if (normLeft && normLeft->shouldSuppressErrors())
            return builtinTypes->booleanType;

        // if we're comparing against an uninhabited type, it's unobservable that the comparison did not run
        if (normLeft && normalizer.isInhabited(normLeft.get()) == NormalizationResult::False)
            return builtinTypes->booleanType;

        // This could be a little wasteful, as we already have normalized
        // types, but correctly handles cases like `_: (T & number) <= _: (T & number)`.
        if (subtyping->isSubtype(leftType, builtinTypes->numberType, scope).isSubtype)
        {
            testIsSubtype(rightType, builtinTypes->numberType, expr->right->location);
            return builtinTypes->booleanType;
        }

        if (subtyping->isSubtype(leftType, builtinTypes->stringType, scope).isSubtype)
        {
            testIsSubtype(rightType, builtinTypes->stringType, expr->right->location);
            return builtinTypes->booleanType;
        }

        // A relational operator on an optional fails because of the `nil`, not because the two types
        // are unrelated to each other; say which operand it is. See describeOptionalOperands.
        std::string comparisonError = format(
            "Types '%s' and '%s' cannot be compared with relational operator %s",
            toString(leftType).c_str(),
            toString(rightType).c_str(),
            toString(expr->op).c_str()
        );

        if (std::optional<std::string> nilClause = describeOptionalOperands(leftType, rightType))
            comparisonError += "; " + *nilClause;

        reportError(GenericError{std::move(comparisonError)}, expr->location);
        return builtinTypes->errorType;
    }

    case AstExprBinary::Op::And:
    case AstExprBinary::Op::Or:
    case AstExprBinary::Op::CompareEq:
    case AstExprBinary::Op::CompareNe:
        // Ugly case: we don't care about this possibility, because a
        // compound assignment will never exist with one of these operators.
        return builtinTypes->anyType;
    default:
        // Unhandled AstExprBinary::Op possibility.
        LUAU_ASSERT(false);
        return builtinTypes->errorType;
    }
}

void TypeChecker2::visit(AstExprTypeAssertion* expr)
{
    visit(expr->expr, ValueContext::RValue);
    visit(expr->annotation);

    TypeId annotationType = lookupAnnotation(expr->annotation);
    TypeId computedType = lookupType(expr->expr);

    switch (shouldSuppressErrors(NotNull{&normalizer}, computedType).orElse(shouldSuppressErrors(NotNull{&normalizer}, annotationType)))
    {
    case ErrorSuppression::Suppress:
        return;
    case ErrorSuppression::NormalizationFailed:
        reportError(NormalizationTooComplex{}, expr->location);
        return;
    case ErrorSuppression::DoNotSuppress:
        break;
    }

    switch (normalizer.isInhabited(computedType))
    {
    case NormalizationResult::True:
        break;
    case NormalizationResult::False:
        return;
    case NormalizationResult::HitLimits:
        reportError(NormalizationTooComplex{}, expr->location);
        return;
    }

    switch (normalizer.isIntersectionInhabited(computedType, annotationType))
    {
    case NormalizationResult::True:
        return;
    case NormalizationResult::False:
        reportError(TypesAreUnrelated{computedType, annotationType}, expr->location);
        break;
    case NormalizationResult::HitLimits:
        reportError(NormalizationTooComplex{}, expr->location);
        break;
    }
}

void TypeChecker2::visit(AstExprIfElse* expr)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);
    visitIfCondition(expr->condition, expr->clauses);
    visit(expr->trueExpr, ValueContext::RValue);
    visit(expr->falseExpr, ValueContext::RValue);
}

// Luwu Do Expressions (rfcs/do-expressions.md)
void TypeChecker2::visit(AstExprDo* expr)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);
    visit(expr->body);
}

// Luwu Do Expressions (rfcs/do-expressions.md)
void TypeChecker2::visit(AstStatGive* give)
{
    visit(give->value, ValueContext::RValue);
}

void TypeChecker2::visit(AstExprInstantiate* explicitTypeInstantiation)
{
    visit(explicitTypeInstantiation->expr, ValueContext::RValue);

    // Luwu Classes (rfcs/classes): `Box<<string>>` instantiates the constructor (a trait's `__create`)
    TypeId instantiated = lookupType(explicitTypeInstantiation->expr);
    if (FFlag::LuwuClasses)
        instantiated = classValueCallType(builtinTypes, instantiated).value_or(instantiated);

    checkTypeInstantiation(
        explicitTypeInstantiation->expr,
        instantiated,
        explicitTypeInstantiation->location,
        explicitTypeInstantiation->typeArguments
    );
}

void TypeChecker2::visit(AstExprInterpString* interpString)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);

    for (AstExpr* expr : interpString->expressions)
        visit(expr, ValueContext::RValue);
}

void TypeChecker2::visit(AstExprError* expr)
{
    // TODO!
    for (AstExpr* e : expr->expressions)
        visit(e, ValueContext::RValue);
}

TypeId TypeChecker2::flattenPack(TypePackId pack)
{
    pack = follow(pack);

    if (auto fst = first(pack, /*ignoreHiddenVariadics*/ false))
        return *fst;
    else if (auto ftp = get<FreeTypePack>(pack))
    {
        TypeId result = module->internalTypes->freshType(builtinTypes, ftp->scope);
        TypePackId freeTail = module->internalTypes->addTypePack(FreeTypePack{ftp->scope});

        TypePack* resultPack = emplaceTypePack<TypePack>(asMutable(pack));
        resultPack->head.assign(1, result);
        resultPack->tail = freeTail;

        return result;
    }
    else if (get<ErrorTypePack>(pack))
        return builtinTypes->errorType;
    else if (finite(pack) && size(pack) == 0)
        return builtinTypes->nilType; // `(f())` where `f()` returns no values is coerced into `nil`
    else
    {
        reportError(InternalError{"flattenPack got a weird pack!"}, Location{});
        return builtinTypes->errorType; // todo test this
    }
}

void TypeChecker2::visitGenerics(AstArray<AstGenericType*> generics, AstArray<AstGenericTypePack*> genericPacks)
{
    DenseHashSet<AstName> seen{AstName{}};

    for (const auto* g : generics)
    {
        if (seen.contains(g->name))
            reportError(DuplicateGenericParameter{g->name.value}, g->location);
        else
            seen.insert(g->name);

        if (g->defaultValue)
            visit(g->defaultValue);
    }

    for (const auto* g : genericPacks)
    {
        if (seen.contains(g->name))
            reportError(DuplicateGenericParameter{g->name.value}, g->location);
        else
            seen.insert(g->name);

        if (g->defaultValue)
            visit(g->defaultValue);
    }
}

void TypeChecker2::visit(AstType* ty)
{
    TypeId* resolvedTy = module->astResolvedTypes.find(ty);
    if (resolvedTy)
        checkForTypeFunctionInhabitance(follow(*resolvedTy), ty->location);

    if (auto t = ty->as<AstTypeReference>())
        return visit(t);
    else if (auto t = ty->as<AstTypeTable>())
        return visit(t);
    else if (auto t = ty->as<AstTypeFunction>())
        return visit(t);
    else if (auto t = ty->as<AstTypeTypeof>())
        return visit(t);
    else if (auto t = ty->as<AstTypeUnion>())
        return visit(t);
    else if (auto t = ty->as<AstTypeIntersection>())
        return visit(t);
    else if (auto t = ty->as<AstTypeGroup>())
        return visit(t->type);
}

void TypeChecker2::visit(AstTypeReference* ty)
{
    // No further validation is necessary in this case. The main logic for
    // _luau_print is contained in lookupAnnotation.
    if (FFlag::DebugLuauMagicTypes && (ty->name == kLuauPrint || ty->name == kLuauForceConstraintSolvingIncomplete || ty->name == kLuauBlockedType))
        return;

    // `class<T>` is routed to a type function by ConstraintGenerator rather than resolved through
    // the `class` alias, which is the zero-parameter top type; checking its arity against that
    // alias would always report a spurious mismatch.
    if (FFlag::LuwuClasses && !ty->prefix.has_value() && ty->name == "class" && ty->hasParameterList)
    {
        Scope* scope = findInnermostScope(ty->location);
        LUAU_ASSERT(scope);

        for (const AstTypeOrPack& param : ty->parameters)
        {
            // `class<List>` names a generic class without its type arguments on purpose: it is the generic class
            bool namesGenericClass = scope && param.type && genericClassValueType(*scope, param.type);
            if (namesGenericClass)
                continue;

            if (param.type)
                visit(param.type);
            else
                visit(param.typePack);
        }
        return;
    }

    // Luwu Traits (rfcs/classes/traits.md): `trait<T>` is resolved by ConstraintGenerator (resolveReferenceType), which
    // reports a misuse; there is no `trait` alias to check it against
    if (FFlag::LuwuTraits && !ty->prefix.has_value() && ty->name == "trait" && ty->hasParameterList)
    {
        for (const AstTypeOrPack& param : ty->parameters)
        {
            if (param.type)
                visit(param.type);
            else
                visit(param.typePack);
        }
        return;
    }

    for (const AstTypeOrPack& param : ty->parameters)
    {
        if (param.type)
            visit(param.type);
        else
            visit(param.typePack);
    }

    Scope* scope = findInnermostScope(ty->location);
    LUAU_ASSERT(scope);

    std::optional<TypeFun> alias = (ty->prefix) ? scope->lookupImportedType(ty->prefix->value, ty->name.value) : scope->lookupType(ty->name.value);

    if (alias.has_value())
    {
        size_t typesRequired = alias->typeParams.size();
        size_t packsRequired = alias->typePackParams.size();

        size_t typesProvided = 0;
        size_t extraTypes = 0;
        size_t packsProvided = 0;

        for (const AstTypeOrPack& p : ty->parameters)
        {
            if (p.type)
            {
                if (packsProvided != 0)
                {
                    reportError(GenericError{"Type parameters must come before type pack parameters"}, ty->location);
                    continue;
                }

                if (typesProvided < typesRequired)
                {
                    typesProvided += 1;
                }
                else
                {
                    extraTypes += 1;
                }
            }
            else if (p.typePack)
            {
                std::optional<TypePackId> tp = lookupPackAnnotation(p.typePack);
                if (!tp.has_value())
                    continue;

                if (typesProvided < typesRequired && size(*tp) == 1 && finite(*tp) && first(*tp))
                {
                    typesProvided += 1;
                }
                else
                {
                    packsProvided += 1;
                }
            }
        }

        // If we require type parameters, but no typeArguments are provided and only packs are provided, we report an error.
        if (typesRequired != 0 && typesProvided == 0 && packsProvided != 0)
        {
            reportError(GenericError{"Type parameters must come before type pack parameters"}, ty->location);
        }

        if (extraTypes != 0 && packsProvided == 0)
        {
            // Extra typeArguments are only collected into a pack if a pack is expected
            if (packsRequired != 0)
                packsProvided += 1;
            else
                typesProvided += extraTypes;
        }

        for (size_t i = typesProvided; i < typesRequired; ++i)
        {
            if (alias->typeParams[i].defaultValue)
            {
                typesProvided += 1;
            }
        }

        for (size_t i = packsProvided; i < packsRequired; ++i)
        {
            if (alias->typePackParams[i].defaultValue)
            {
                packsProvided += 1;
            }
        }

        // If the type parameter list is explicitly provided, allow an empty type pack to satisfy the expected pack count.
        if (extraTypes == 0 && packsProvided + 1 == packsRequired && ty->hasParameterList)
            packsProvided += 1;

        if (typesProvided != typesRequired || packsProvided != packsRequired)
        {
            reportError(
                IncorrectGenericParameterCount{
                    /* name */ ty->name.value,
                    /* typeFun */ *alias,
                    /* actualParameters */ typesProvided,
                    /* actualPackParameters */ packsProvided,
                },
                ty->location
            );
        }
    }
    else
    {
        if (scope->lookupPack(ty->name.value))
        {
            reportError(
                SwappedGenericTypeParameter{
                    ty->name.value,
                    SwappedGenericTypeParameter::Kind::Type,
                },
                ty->location
            );
        }
        else
        {
            std::string symbol = "";
            if (ty->prefix)
            {
                symbol += (*(ty->prefix)).value;
                symbol += ".";
            }
            symbol += ty->name.value;

            reportError(UnknownSymbol{std::move(symbol), UnknownSymbol::Context::Type}, ty->location);
        }
    }
}

void TypeChecker2::visit(AstTypeTable* table)
{
    // TODO!

    for (const AstTableProp& prop : table->props)
        visit(prop.type);

    if (table->indexer)
    {
        visit(table->indexer->indexType);
        visit(table->indexer->resultType);
    }
}

void TypeChecker2::visit(AstTypeFunction* ty)
{
    visitGenerics(ty->generics, ty->genericPacks);
    visit(ty->argTypes);
    visit(ty->returnTypes);
}

// The source spelling of a name or a chain of field reads (`Cat`, `a.Node`).
static std::optional<std::string> spellNameExpr(const AstExpr* expr)
{
    if (const AstExprLocal* local = expr->as<AstExprLocal>())
        return std::string(local->local->name.value);

    if (const AstExprGlobal* global = expr->as<AstExprGlobal>())
        return std::string(global->name.value);

    if (const AstExprIndexName* index = expr->as<AstExprIndexName>())
    {
        if (std::optional<std::string> prefix = spellNameExpr(index->expr))
            return *prefix + "." + index->index.value;
    }

    return std::nullopt;
}

// `valueKind` is "class", or "trait" for a trait's value
void TypeChecker2::reportClassTypeofSpelling(AstTypeTypeof* ty, const std::string& className, TypeId objectTy, const char* valueKind)
{
    // The suggestion names the class's type only where that name resolves to it: an imported class
    // or a shadowed name has no bare spelling here.
    const Scope* scope = findInnermostScope(ty->location);
    std::optional<TypeFun> named = scope ? scope->lookupType(className) : std::nullopt;
    const bool nameable = named && follow(named->type) == follow(objectTy);

    const std::string typeofText = "'typeof(" + spellNameExpr(ty->expr).value_or("...") + ")'";
    const std::string kind = valueKind;
    const std::string classText =
        nameable ? "'" + kind + "<" + className + ">'" : "'" + kind + "<T>', where T is the type of '" + className + "' objects,";

    const std::string target = kind == "trait" ? "the trait '" + className + "'" : "the class of '" + className + "'";
    reportError(GenericError{"Use " + classText + " instead of " + typeofText + " to get " + target}, ty->location);
}

void TypeChecker2::visit(AstTypeTypeof* ty)
{
    visit(ty->expr, ValueContext::RValue);

    // `typeof(Cat)` and `class<Cat>` name the same type, but only the latter says so at a glance:
    // an ExternType stringifies as its bare name, so `typeof(Cat)` reads as the object type in
    // every hover and error message. Steer users to the spelling that doesn't.
    if (FFlag::LuwuClasses)
    {
        if (auto resolved = module->astResolvedTypes.find(ty))
        {
            const ExternType* klass = get<ExternType>(follow(*resolved));
            const bool isClassValue = klass && klass->root == builtinTypes->classType;
            const bool isTraitValue = klass && klass->root == builtinTypes->traitType;
            if ((isClassValue || isTraitValue) && klass->relation)
            {
                if (const Obj* obj = klass->relation->get_if<Obj>())
                    reportClassTypeofSpelling(ty, klass->name, obj->ty, isTraitValue ? "trait" : "class");
            }
        }
    }
}

void TypeChecker2::visit(AstTypeUnion* ty)
{
    // TODO!
    for (AstType* type : ty->types)
        visit(type);
}

void TypeChecker2::visit(AstTypeIntersection* ty)
{
    // TODO!
    for (AstType* type : ty->types)
        visit(type);
}

void TypeChecker2::visit(AstTypePack* pack)
{
    if (auto p = pack->as<AstTypePackExplicit>())
        return visit(p);
    else if (auto p = pack->as<AstTypePackVariadic>())
        return visit(p);
    else if (auto p = pack->as<AstTypePackGeneric>())
        return visit(p);
}

void TypeChecker2::visit(AstTypePackExplicit* tp)
{
    // TODO!
    for (AstType* type : tp->typeList.types)
        visit(type);

    if (tp->typeList.tailType)
        visit(tp->typeList.tailType);
}

void TypeChecker2::visit(AstTypePackVariadic* tp)
{
    // TODO!
    visit(tp->variadicType);
}

void TypeChecker2::visit(AstTypePackGeneric* tp)
{
    Scope* scope = findInnermostScope(tp->location);
    LUAU_ASSERT(scope);

    if (std::optional<TypePackId> alias = scope->lookupPack(tp->genericName.value))
        return;

    if (scope->lookupType(tp->genericName.value))
        return reportError(
            SwappedGenericTypeParameter{
                tp->genericName.value,
                SwappedGenericTypeParameter::Kind::Pack,
            },
            tp->location
        );

    reportError(UnknownSymbol{tp->genericName.value, UnknownSymbol::Context::Type}, tp->location);
}

// Returns true if `prefix`'s components are a (possibly empty, possibly complete) prefix of
// `whole`'s components. Used to detect when a subPath/superPath pair share a common lead-in
// (e.g. both drill into "the 1st entry in the type pack") so that lead-in doesn't get printed
// twice back-to-back in a mismatch explanation.
static bool isPrefixPath(const TypePath::Path& prefix, const TypePath::Path& whole)
{
    if (prefix.components.size() > whole.components.size())
        return false;

    TypePath::Path wholePrefix{std::vector<TypePath::Component>(whole.components.begin(), whole.components.begin() + prefix.components.size())};
    return prefix == wholePrefix;
}

// If every non-empty subPath/superPath among `reasonings` begins with the same recognized
// PackField (Returns or Arguments), returns it. This tells us the whole mismatch is fundamentally
// about one specific, nameable aspect of the type (e.g. "its return type"), which we can fold
// into a short preamble instead of repeating jargon like "it returns" inside every reason.
static std::optional<TypePath::PackField> commonLeadingPackField(const SubtypingReasonings& reasonings)
{
    std::optional<TypePath::PackField> common;

    auto consider = [&](const TypePath::Path& path) -> bool
    {
        if (path.empty())
            return true;

        const TypePath::PackField* pf = get_if<TypePath::PackField>(&path.components[0]);
        if (!pf || (*pf != TypePath::PackField::Returns && *pf != TypePath::PackField::Arguments))
            return false;

        if (!common)
            common = *pf;
        return *pf == *common;
    };

    for (const SubtypingReasoning& reasoning : reasonings)
    {
        if (!consider(reasoning.subPath) || !consider(reasoning.superPath))
            return std::nullopt;
    }

    return common;
}

static std::optional<std::string> contextVerbForPackField(TypePath::PackField field)
{
    switch (field)
    {
    case TypePath::PackField::Returns:
        return mismatchContextFunctionReturns;
    case TypePath::PackField::Arguments:
        return mismatchContextFunctionTakes;
    default:
        return std::nullopt;
    }
}

// Builds a trimmed-down path for human-readable narration: drops indices into packs/unions/
// intersections (e.g. "the 1st component of the union"), since which member differs is already
// obvious from comparing the printed wanted/got types side by side, and drops a leading PackField
// component that matches `commonContext`, since that's already conveyed by the enclosing
// preamble (e.g. "Expected this function to return..."). Property names and other structural
// markers are preserved, since those aren't recoverable just by reading the printed types.
static TypePath::Path narrationPath(const TypePath::Path& path, std::optional<TypePath::PackField> commonContext)
{
    std::vector<TypePath::Component> result;
    for (size_t i = 0; i < path.components.size(); ++i)
    {
        const TypePath::Component& c = path.components[i];

        if (i == 0 && commonContext)
        {
            if (const TypePath::PackField* pf = get_if<TypePath::PackField>(&c); pf && *pf == *commonContext)
                continue;
        }

        if (get_if<TypePath::Index>(&c))
            continue;

        result.push_back(c);
    }
    return TypePath::Path{std::move(result)};
}

// Luwu Traits (rfcs/classes/traits.md): why `given` doesn't fit a trait function's `Self`
static std::string traitSelfMismatchReason(const std::string& given)
{
    return "`Self` is the class this is called on, and a `" + given +
           "` may be a different class, so this has to be `Self` too: `self`, or `class.of(self)(...)` for a new object of its class";
}

template<typename TID>
Reasonings TypeChecker2::explainReasonings_(TID subTy, TID superTy, Location location, const SubtypingResult& r)
{
    if (r.reasoning.empty())
        return {};

    std::optional<TypePath::PackField> commonContext = commonLeadingPackField(r.reasoning);

    std::vector<std::string> reasons;
    bool suppressed = true;
    for (const SubtypingReasoning& reasoning : r.reasoning)
    {
        if (reasoning.subPath.empty() && reasoning.superPath.empty())
            continue;

        std::optional<TypeOrPack> optSubLeaf = traverse(subTy, reasoning.subPath, builtinTypes, subtyping->arena);

        std::optional<TypeOrPack> optSuperLeaf = traverse(superTy, reasoning.superPath, builtinTypes, subtyping->arena);

        if (!optSubLeaf || !optSuperLeaf)
        {
            reportError(InternalError{"Subtyping test returned a reasoning with an invalid path"}, location);
            return {};
        }

        const TypeOrPack& subLeaf = *optSubLeaf;
        const TypeOrPack& superLeaf = *optSuperLeaf;

        auto subLeafTy = get<TypeId>(subLeaf);
        auto superLeafTy = get<TypeId>(superLeaf);

        auto subLeafTp = get<TypePackId>(subLeaf);
        auto superLeafTp = get<TypePackId>(superLeaf);

        if (!subLeafTy && !superLeafTy && !subLeafTp && !superLeafTp)
        {
            reportError(InternalError{"Subtyping test returned a reasoning where one path ends at a type and the other ends at a pack."}, location);
            return {};
        }

        std::string relation = "a subtype of";
        if (reasoning.variance == SubtypingVariance::Invariant)
            relation = "exactly";
        else if (reasoning.variance == SubtypingVariance::Contravariant)
            relation = "a supertype of";

        std::string subLeafAsString = toString(subLeaf);
        // if the string is empty, it must be an empty type pack
        if (subLeafAsString.empty())
            subLeafAsString = "()";

        std::string superLeafAsString = toString(superLeaf);
        // if the string is empty, it must be an empty type pack
        if (superLeafAsString.empty())
            superLeafAsString = "()";

        // When the only thing wrong is that the given type might be `nil`, the subtyping test fails
        // on the `nil` member of a union, and naming that member ("`nil` is not a subtype of
        // `number`") makes the reader work backwards to figure out which type it came out of. Name
        // the optional type they actually wrote instead, and say what is wrong with it.
        std::optional<std::string> optionalSubLeaf;
        if (subLeafTy && superLeafTy && isNil(*subLeafTy) && !isNil(*superLeafTy))
        {
            TypePath::Path enclosingPath = reasoning.subPath;
            if (!enclosingPath.components.empty() && get_if<TypePath::Index>(&enclosingPath.components.back()))
            {
                enclosingPath.components.pop_back();

                if (std::optional<TypeOrPack> optEnclosing = traverse(subTy, enclosingPath, builtinTypes, subtyping->arena))
                {
                    if (auto enclosingTy = get<TypeId>(*optEnclosing); enclosingTy && isOptional(*enclosingTy))
                    {
                        optionalSubLeaf = toString(*enclosingTy);
                        subLeafAsString = *optionalSubLeaf;
                    }
                }
            }
        }

        // Luwu Traits (rfcs/classes/traits.md): `Self` is the class of whatever the trait function is called on, which a
        // value of the trait type (or any other class) may not be. Saying so beats "`X` is not a subtype of `Self`".
        const GenericType* superGeneric = superLeafTy ? get<GenericType>(follow(*superLeafTy)) : nullptr;
        const bool expectsTraitSelf = !optionalSubLeaf && superGeneric && superGeneric->traitSelf;

        std::stringstream baseReasonBuilder;
        if (optionalSubLeaf)
            baseReasonBuilder << "`" << subLeafAsString << "` could be `nil`";
        else if (expectsTraitSelf)
            baseReasonBuilder << traitSelfMismatchReason(subLeafAsString);
        else
            baseReasonBuilder << "`" << subLeafAsString << "` is not " << relation << " `" << superLeafAsString << "`";
        std::string baseReason = baseReasonBuilder.str();

        // The same comparison as `baseReason`, phrased to follow a narrated path ("accessing `n`
        // results in ...").
        std::string subLeafNotSuper = optionalSubLeaf ? ("`" + subLeafAsString + "`, which could be `nil`")
                                                      : ("`" + subLeafAsString + "`, which is not " + relation + " `" + superLeafAsString + "`");
        std::string superLeafAndSubNot = optionalSubLeaf
                                             ? ("`" + superLeafAsString + "`, and `" + subLeafAsString + "` could be `nil`")
                                             : ("`" + superLeafAsString + "`, and `" + subLeafAsString + "` is not " + relation + " it");
        if (expectsTraitSelf)
        {
            subLeafNotSuper = "`" + subLeafAsString + "`, which may be a different class than `Self`";
            superLeafAndSubNot = "`Self`, and `" + subLeafAsString + "` may be a different class";
        }

        std::stringstream reason;

        TypePath::Path subNarration = narrationPath(reasoning.subPath, commonContext);
        TypePath::Path superNarration = narrationPath(reasoning.superPath, commonContext);

        if ((FFlag::LuauPropertyModifierMismatchErrors || FFlag::LuauIndexerModifierMismatchErrors) && reasoning.isAccessModifierViolation)
        {
            if (FFlag::LuauPropertyModifierMismatchErrors && FFlag::LuauIndexerModifierMismatchErrors)
            {
                // The leaf types at the end of the paths are the same type, so a
                // plain "X is not a subtype of X" message would be misleading.
                // Instead, explain that the mismatch is about the access modifier.
                auto last = reasoning.subPath.last();
                bool isReadOnly = true;
                std::string path = "something";

                if (last)
                {
                    if (auto* prop = get_if<TypePath::Property>(&*last))
                    {
                        path = "`" + prop->name + "`";
                        isReadOnly = prop->isRead;
                    }
                    else if (auto* field = get_if<TypePath::TypeField>(&*last); field && *field == TypePath::TypeField::IndexResult)
                        path = "the indexer";
                }

                if (isReadOnly)
                    reason << path << " is read-only in the latter type, but the former type requires it to be read-write";
                else
                    reason << path << " is write-only in the latter type, but the former type requires it to be read-write";
            }
            else
            {
                // The leaf types at the end of the paths are the same type, so a
                // plain "X is not a subtype of X" message would be misleading.
                // Instead, explain that the mismatch is about the property modifier.
                std::string propName = "a property";
                bool isReadOnly = true;
                auto last = reasoning.subPath.last();
                LUAU_ASSERT(last && get_if<TypePath::Property>(&*last));
                if (last)
                {
                    if (auto* prop = get_if<TypePath::Property>(&*last))
                    {
                        propName = "`" + prop->name + "`";
                        isReadOnly = prop->isRead;
                    }
                }

                if (isReadOnly)
                    reason << propName << " is a read-only property in the latter type, but the former type requires a read-write property";
                else
                    reason << propName << " is a write-only property in the latter type, but the former type requires a read-write property";
            }
        }
        // If, once the shared context (e.g. "this function returns") and union/pack indices are
        // stripped out, there's nothing left worth narrating (the common case -- most mismatches
        // are just "the type here doesn't match the type there", and which union/pack slot is
        // already obvious from comparing the printed wanted/got types), skip narration entirely
        // and just state the comparison.
        else if (subNarration.empty() && superNarration.empty())
            reason << baseReason;
        else if (subNarration == superNarration)
            reason << toStringHuman(subNarration) << "`" << subLeafAsString << "` in the latter type and `" << superLeafAsString
                   << "` in the former type, and " << baseReason;
        // If one non-empty path is a strict prefix of the other, they share a common lead-in --
        // printing both paths in full repeats that lead-in verbatim, which reads as a confusing
        // double explanation. Describe only the more specific (longer) path in that case.
        else if (!subNarration.empty() && !superNarration.empty() && isPrefixPath(superNarration, subNarration))
            reason << toStringHuman(subNarration) << subLeafNotSuper;
        else if (!subNarration.empty() && !superNarration.empty() && isPrefixPath(subNarration, superNarration))
            reason << toStringHuman(superNarration) << superLeafAndSubNot;
        else if (!subNarration.empty() && !superNarration.empty())
            reason << toStringHuman(subNarration) << "`" << subLeafAsString << "` and " << toStringHuman(superNarration) << "`"
                   << superLeafAsString << "`, and " << baseReason;
        else if (!subNarration.empty())
            reason << toStringHuman(subNarration) << subLeafNotSuper;
        else
            reason << toStringHuman(superNarration) << "`" << superLeafAsString << "`, and " << baseReason;

        if (FFlag::LuauBetterPackAndVariadicMismatchErrors && reasoning.variance == SubtypingVariance::Contravariant && subLeafTp && superLeafTp)
        {
            const VariadicTypePack* vtp = get<VariadicTypePack>(*subLeafTp);
            const GenericTypePack* gtp = get<GenericTypePack>(*superLeafTp);

            if (gtp && vtp && gtp->explicitName && is<GenericType>(vtp->ty))
            {
                reason << "; the former type has a generic pack, and the latter type has a variadic, ";
                reason << "consider changing the former generic to `" << gtp->name << "` or the latter generic to ";
                reason << "`" << toString(vtp->ty) << "...`";
            }
        }

        reasons.push_back(reason.str());

        // if we haven't already proved this isn't suppressing, we have to keep checking.
        if (suppressed)
        {
            if (subLeafTy && superLeafTy)
                suppressed &= isErrorSuppressing(location, *subLeafTy) || isErrorSuppressing(location, *superLeafTy);
            else
                suppressed &= isErrorSuppressing(location, *subLeafTp) || isErrorSuppressing(location, *superLeafTp);
        }
    }

    std::optional<std::string> contextVerb = commonContext ? contextVerbForPackField(*commonContext) : std::nullopt;

    // The preamble built from `contextVerb` promises one part of the type ("Expected this function
    // to return"), so hand the caller that part's stringification to print after it, rather than
    // the whole function type.
    std::optional<std::string> wantedDisplay;
    std::optional<std::string> givenDisplay;
    if (commonContext)
    {
        TypePath::Path contextPath{std::vector<TypePath::Component>{TypePath::Component{*commonContext}}};

        std::optional<TypeOrPack> wantedPart = traverse(superTy, contextPath, builtinTypes, subtyping->arena);
        std::optional<TypeOrPack> givenPart = traverse(subTy, contextPath, builtinTypes, subtyping->arena);

        if (wantedPart && givenPart)
        {
            wantedDisplay = toString(*wantedPart);
            givenDisplay = toString(*givenPart);

            // An empty type pack stringifies to nothing at all, which would leave the message with
            // a bare pair of quotes.
            if (wantedDisplay->empty())
                wantedDisplay = "()";
            if (givenDisplay->empty())
                givenDisplay = "()";
        }
    }

    return {std::move(reasons), suppressed, std::move(contextVerb), std::move(wantedDisplay), std::move(givenDisplay)};
}

Reasonings TypeChecker2::explainReasonings(TypeId subTy, TypeId superTy, Location location, const SubtypingResult& r)
{
    return explainReasonings_(subTy, superTy, location, r);
}

Reasonings TypeChecker2::explainReasonings(TypePackId subTp, TypePackId superTp, Location location, const SubtypingResult& r)
{
    return explainReasonings_(subTp, superTp, location, r);
}

// True when the *only* reason `subTy` fails to be a subtype of `superTy` is that `subTy` is (or contains, as a
// union member) `none` and `superTy` doesn't account for it. This happens when an optional value (`T | none`)
// is used somewhere that expects a plain `T` without first being narrowed/checked.
static bool isUncheckedNoneMismatch(TypeId subTy, TypeId superTy, const SubtypingResult& result, NotNull<BuiltinTypes> builtinTypes, NotNull<TypeArena> arena)
{
    const SubtypingReasoning* onlyReasoning = nullptr;
    for (const SubtypingReasoning& reasoning : result.reasoning)
    {
        if (onlyReasoning)
            return false;
        onlyReasoning = &reasoning;
    }

    if (!onlyReasoning || !onlyReasoning->superPath.empty())
        return false;

    std::optional<TypeOrPack> subLeaf = traverse(subTy, onlyReasoning->subPath, builtinTypes, arena);
    if (!subLeaf)
        return false;

    auto subLeafTy = get<TypeId>(*subLeaf);
    return subLeafTy && Luau::isNone(*subLeafTy);
}

// True when `subTy` failed to be a subtype of `superTy` because `superTy` is a negated union
// (e.g. `~(number | string)`) and `subTy` itself overlaps with one of that union's members.
// The generic reasoning text for this case reads as "`number` is not a subtype of `number`",
// which is confusing since disjointness, not subtyping, is what actually failed.
static bool isSimpleNegatedUnionMismatch(TypeId subTy, TypeId superTy, const SubtypingResult& result)
{
    const NegationType* negation = get<NegationType>(follow(superTy));
    if (!negation || !get<UnionType>(follow(negation->ty)))
        return false;

    const SubtypingReasoning* onlyReasoning = nullptr;
    for (const SubtypingReasoning& reasoning : result.reasoning)
    {
        if (onlyReasoning)
            return false;
        onlyReasoning = &reasoning;
    }

    if (!onlyReasoning || !onlyReasoning->subPath.empty())
        return false;

    const std::vector<TypePath::Component>& components = onlyReasoning->superPath.components;
    if (components.size() != 2)
        return false;

    const TypePath::TypeField* negated = get_if<TypePath::TypeField>(&components[0]);
    return negated && *negated == TypePath::TypeField::Negated && get_if<TypePath::Index>(&components[1]);
}

// Returns the sole reasoning behind a failed subtyping test, or nullptr when the test failed for
// more than one reason. The bespoke explanations below each describe one specific failure, so
// speaking for a result that has several would hide the rest.
static const SubtypingReasoning* soleReasoning(const SubtypingResult& result)
{
    const SubtypingReasoning* only = nullptr;
    for (const SubtypingReasoning& reasoning : result.reasoning)
    {
        if (only)
            return nullptr;
        only = &reasoning;
    }
    return only;
}

// Luwu (helpful subtyping errors): the shape a read/write mismatch was found in. An array and a map differ
// only in nouns and in the annotation that fixes them; a property is a different sentence entirely.
enum class ReadWriteShape
{
    Array,
    Map,
    Property,
};

// Luwu (helpful subtyping errors): the explanation for a read/write mismatch, plus the one-line version that
// goes on the parameter the value was passed to.
struct ReadWriteExplanation
{
    std::string message;
    std::string parameterHelp;

    // The failing path on the expected side, which is the parameter's own type, so it lines up with what
    // the user wrote there: `annotatedMemberLocation` follows it to the indexer or property to mark `read`.
    TypePath::Path parameterHelpPath;
};

// Luwu (helpful subtyping errors): both sides' types at the end of their paths, when both paths lead to a
// type rather than a pack. Returned as {sub, super}.
static std::optional<std::pair<TypeId, TypeId>> typesAtPaths(
    TypeId subTy,
    const TypePath::Path& subPath,
    TypeId superTy,
    const TypePath::Path& superPath,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<TypeArena> arena
)
{
    std::optional<TypeOrPack> subPart = traverse(subTy, subPath, builtinTypes, arena);
    std::optional<TypeOrPack> superPart = traverse(superTy, superPath, builtinTypes, arena);
    if (!subPart || !superPart)
        return std::nullopt;

    const TypeId* subPartTy = get<TypeId>(*subPart);
    const TypeId* superPartTy = get<TypeId>(*superPart);
    if (!subPartTy || !superPartTy)
        return std::nullopt;

    return std::make_pair(*subPartTy, *superPartTy);
}

static AstType* withoutGroups(AstType* ty)
{
    while (AstTypeGroup* group = ty ? ty->as<AstTypeGroup>() : nullptr)
        ty = group->type;

    return ty;
}

// Luwu (helpful subtyping errors): what a written type resolves to for the purpose of finding a property or
// an indexer in it. A parameter is as likely to be annotated `Box<Item>` as `{ value: Item }`, and
// `read` is written in the alias either way, so a reference is followed to the type it names. Generic
// arguments are ignored: only the place a modifier is typed matters, not what it is instantiated to.
static AstType* resolveForMembers(AstType* ty, const TypeChecker2::DeclarationIndex& declarations)
{
    // Aliases can name each other; this is far more hops than real code needs, and stops a cyclic
    // alias from spinning here.
    constexpr int maxAliasHops = 8;

    for (int hops = 0; hops < maxAliasHops; ++hops)
    {
        ty = withoutGroups(ty);

        AstTypeReference* reference = ty ? ty->as<AstTypeReference>() : nullptr;
        if (!reference || reference->prefix)
            return ty;

        AstStatTypeAlias* alias = declarations.aliasNamed(reference->name);
        if (!alias)
            return ty;

        ty = alias->type;
    }

    return ty;
}

// Luwu (helpful subtyping errors): the part of a written type a read/write mismatch is about -- the indexer
// of `{ [string]: T }` or the property of `{ x: T }` -- found by walking the subtyping path that failed
// into the annotation the user typed. A path component with no counterpart in the annotation gives up
// rather than guessing, since the whole point is to point at the exact place `read` is written.
static std::optional<Location> annotatedMemberLocation(
    AstType* annotation,
    const TypePath::Path& path,
    const TypeChecker2::DeclarationIndex& declarations
)
{
    AstType* current = resolveForMembers(annotation, declarations);
    std::optional<Location> found;

    for (const TypePath::Component& component : path.components)
    {
        // A trailing index into a union is which member failed, not a place in the annotation; the
        // indexer or property named just before it is still the thing to mark `read`.
        if (get_if<TypePath::Index>(&component))
            break;

        AstTypeTable* table = current ? current->as<AstTypeTable>() : nullptr;
        if (!table)
            return std::nullopt;

        if (const TypePath::Property* property = get_if<TypePath::Property>(&component))
        {
            const AstTableProp* match = nullptr;
            for (const AstTableProp& prop : table->props)
            {
                if (prop.name.value && property->name == prop.name.value)
                {
                    match = &prop;
                    break;
                }
            }

            if (!match)
                return std::nullopt;

            found = match->location;
            current = resolveForMembers(match->type, declarations);
        }
        else if (const TypePath::TypeField* field = get_if<TypePath::TypeField>(&component);
                 field && *field == TypePath::TypeField::IndexResult)
        {
            if (!table->indexer)
                return std::nullopt;

            found = table->indexer->location;
            current = resolveForMembers(table->indexer->resultType, declarations);
        }
        else
            return std::nullopt;
    }

    return found;
}

// Luwu (helpful subtyping errors): what a read/write mismatch is about, found by `findReadWriteMismatch` and
// put into words by `wordReadWriteMismatch`. Type names are unquoted.
struct ReadWriteMismatch
{
    ReadWriteShape shape = ReadWriteShape::Property;
    std::string propertyName;
    std::string keyType;

    std::string wantedLeaf;
    std::string givenLeaf;
    std::string wantedContainer;
    std::string givenContainer;

    // The member of an expected union that the given side lacks, when that is the whole difference.
    std::optional<std::string> widenedMember;
    bool widenedWithNil = false;

    bool isArgument = false;

    // The callee, when the value is an argument and the callee has a name.
    std::string functionName;

    // See ReadWriteExplanation::parameterHelpPath.
    TypePath::Path parameterHelpPath;
};

// Luwu (helpful subtyping errors): a property or indexer that is read *and* written has to match exactly, so
// a perfectly good subtype is rejected wherever one is expected -- passing a `{Button}` to a `{Widget}`
// parameter, say. Upstream reports this as "`Button` is not exactly `Widget`", which a reader can't act
// on: the reason a smaller type is refused is that the callee can *replace* an element with one. So the
// message spells out that mechanism and both ways out of it.
//
// The covariant direction is re-tested here rather than recorded during subtyping: this only runs
// once an error is already being reported, and `{number}` against `{string}` fails invariantly too
// without `read` helping it in the slightest.
static std::optional<ReadWriteMismatch> findReadWriteMismatch(
    TypeId subTy,
    TypeId superTy,
    const SubtypingResult& result,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<TypeArena> arena
)
{
    const SubtypingReasoning* reasoning = soleReasoning(result);
    if (!reasoning || reasoning->variance != SubtypingVariance::Invariant || reasoning->isAccessModifierViolation)
        return std::nullopt;

    // Both sides have to fail in the same place for the message to be able to point at one. When the
    // expected side is wider -- `{string}` against `{string?}`, the single most common way to meet
    // this error -- subtyping fails against one member of that union and the super path carries a
    // trailing index into it. Compare against the whole union instead, or the message ends up
    // reading "`string` is not exactly `nil`", which means nothing to anybody.
    const TypePath::Path& path = reasoning->subPath;
    if (path.empty())
        return std::nullopt;

    const bool superPathIsWider = path != reasoning->superPath;
    if (superPathIsWider)
    {
        const std::vector<TypePath::Component>& superComponents = reasoning->superPath.components;
        if (superComponents.size() != path.components.size() + 1)
            return std::nullopt;

        const TypePath::Index* widened = get_if<TypePath::Index>(&superComponents.back());
        if (!widened || widened->variant == TypePath::Index::Variant::Pack)
            return std::nullopt;

        if (!std::equal(path.components.begin(), path.components.end(), superComponents.begin()))
            return std::nullopt;
    }

    // Inside a function's arguments the roles are reversed, and `read` belongs on whichever side the
    // reader didn't write. The wording assumes the ordinary direction, so leave those alone.
    for (const TypePath::Component& component : path.components)
    {
        if (const TypePath::PackField* pf = get_if<TypePath::PackField>(&component); pf && *pf == TypePath::PackField::Arguments)
            return std::nullopt;
    }

    const TypePath::Component& last = path.components.back();

    const TypePath::Property* property = get_if<TypePath::Property>(&last);
    const TypePath::TypeField* typeField = get_if<TypePath::TypeField>(&last);
    const bool isIndexer = typeField && *typeField == TypePath::TypeField::IndexResult;

    if (!isIndexer && !(property && property->isRead))
        return std::nullopt;

    std::optional<std::pair<TypeId, TypeId>> leaves = typesAtPaths(subTy, path, superTy, path, builtinTypes, arena);
    if (!leaves)
        return std::nullopt;

    const auto [subLeafTy, superLeafTy] = *leaves;

    // Reading is the half that works, or `read` has nothing to offer here.
    if (!subtyping->isSubtype(subLeafTy, superLeafTy, scope).isSubtype)
        return std::nullopt;

    // The container that actually holds the read-write slot, which for an argument passed straight
    // to a parameter is the whole type, and for a nested one is the inner table that owns it.
    TypePath::Path containerPath{std::vector<TypePath::Component>(path.components.begin(), path.components.end() - 1)};

    std::optional<std::pair<TypeId, TypeId>> containers = typesAtPaths(subTy, containerPath, superTy, containerPath, builtinTypes, arena);
    if (!containers)
        return std::nullopt;

    const auto [subContainerTy, superContainerTy] = *containers;

    ReadWriteMismatch mismatch;
    mismatch.wantedLeaf = toString(superLeafTy);
    mismatch.givenLeaf = toString(subLeafTy);

    // Both leaf types are printed three or four times over, so a long expansion turns the whole
    // explanation into a wall. Fall back to the terse form rather than print that.
    constexpr size_t maxLeafLength = 40;
    if (mismatch.wantedLeaf.length() > maxLeafLength || mismatch.givenLeaf.length() > maxLeafLength)
        return std::nullopt;

    if (isIndexer)
    {
        mismatch.shape = ReadWriteShape::Array;
        if (const TableType* superTable = get<TableType>(follow(superContainerTy)); superTable && superTable->indexer)
        {
            mismatch.keyType = toString(superTable->indexer->indexType);
            if (mismatch.keyType != "number")
                mismatch.shape = ReadWriteShape::Map;
        }
    }
    else
        mismatch.propertyName = property->name;

    mismatch.wantedContainer = toString(superContainerTy);
    mismatch.givenContainer = toString(subContainerTy);

    // The member of an expected union that the given side lacks names the difference concretely.
    if (superPathIsWider)
    {
        if (std::optional<TypeOrPack> widenedMember = traverse(superTy, reasoning->superPath, builtinTypes, arena))
        {
            if (const TypeId* memberTy = get<TypeId>(*widenedMember))
            {
                mismatch.widenedWithNil = isNil(follow(*memberTy));
                mismatch.widenedMember = toString(*memberTy);
            }
        }
    }

    mismatch.parameterHelpPath = reasoning->superPath;
    return mismatch;
}

static ReadWriteExplanation wordReadWriteMismatch(const ReadWriteMismatch& mismatch)
{
    auto quote = [](const std::string& s)
    {
        return "'" + s + "'";
    };

    const bool isProperty = mismatch.shape == ReadWriteShape::Property;
    const bool isMap = mismatch.shape == ReadWriteShape::Map;
    const bool hasFunction = !mismatch.functionName.empty();
    const std::string noun = isMap ? "map" : "array";
    const std::string plural = isMap ? "values" : "elements";
    const std::string property = quote(mismatch.propertyName);
    const std::string wantedContainer = quote(mismatch.wantedContainer);
    const std::string givenContainer = quote(mismatch.givenContainer);
    const std::string wanted = quote(mismatch.wantedLeaf);
    const std::string given = quote(mismatch.givenLeaf);
    const std::string function = "the function " + quote(mismatch.functionName);

    // The help goes on the parameter, where "it" would otherwise read as the indexer or the property
    // being marked rather than as the thing doing the reading. This line is only ever reported for a
    // call argument, so the reader is always the callee.
    const std::string reader = hasFunction ? function : "the function";
    std::string parameterHelp = isProperty
                                    ? "Help (read/write mismatch): consider marking " + property + " as 'read' if " + reader + " only reads from it."
                                    : "Help (read/write mismatch): consider marking this as 'read' if " + reader + " only reads from the " + noun + ".";

    // A union member the given type never admitted -- `nil` most often, but any of them. Nothing is lost,
    // one disallowed value could simply be written in, so the help is to mark it `read` or cast.
    if (mismatch.widenedMember)
    {
        const std::string member = quote(*mismatch.widenedMember);
        const std::string writtenValue = mismatch.widenedWithNil ? member : "a " + member;

        std::string message;
        if (!mismatch.widenedWithNil)
        {
            // Both types on their own lines, as every other expected/got mismatch prints them, so the
            // one member that differs can be found by eye.
            const std::string target = isProperty ? property : ("your " + noun);
            message = "Expected this to be\n    " + wantedContainer + "\nbut got\n    " + givenContainer + "\nThis incorrectly allows a " + member +
                      " to be written to " + target + ".";
        }
        else
        {
            // "passed" is right for an argument; an assignment or a return is merely a use.
            const std::string verb = mismatch.isArgument ? "passed" : "used";
            if (isProperty)
                message = "Property " + property + " is non-optional, so this cannot be " + verb +
                          " where it is allowed to be optional; doing so would allow 'nil' to be written into it.";
            else
                message = "This " + noun + " with non-optional " + plural + " cannot be " + verb + " where optional " + plural +
                          " are allowed; doing so would allow 'nil' to be written into it.";
        }

        message += "\n\nHelp (read/write mismatch):\n  - annotate " + (isProperty ? property : std::string("the expected indexer")) +
                   " as 'read' if nothing writes to it\n  - cast this " + (isProperty ? std::string() : noun + " ") + "to " + wantedContainer +
                   " if you know " + writtenValue + " will not be written to it";

        return ReadWriteExplanation{std::move(message), std::move(parameterHelp), mismatch.parameterHelpPath};
    }

    // Only a wider *shape* reaches here -- storing one really does drop fields. The sentences differ only in
    // whether there is a function to name as the one doing the writing.
    const std::string target = isProperty ? property : "the " + noun;
    const std::string nothingWrites = hasFunction ? "the function doesn't actually write to " + target : "nothing writes to " + target;
    const std::string passTail = hasFunction ? " to pass to the function" : "";

    std::string message;
    if (isProperty)
    {
        const std::string annotation = quote("read " + mismatch.propertyName + ": " + mismatch.wantedLeaf);
        const std::string canReadWrite = hasFunction ? function + " can read and write to " + property : property + " can be read and written";
        const std::string replaced = hasFunction ? "it could silently replace it with" : "it could silently be replaced with";

        message = "Expected property " + property + " to allow reading and writing as " + wanted + ", but in " + givenContainer + " it is a " +
                  given + ", which can only be read as " + wanted + ". Because " + canReadWrite + ", " + replaced +
                  " a value of a smaller type, causing data loss.\n\n"
                  "Help (read/write mismatch):\n  - if " +
                  nothingWrites + ", mark it as " + annotation + " in " + wantedContainer + "\n  - if it reads and writes, make a " + wanted +
                  " version of your data" + passTail + " or mark the additional fields as optional";
    }
    else
    {
        const std::string annotation =
            isMap ? quote("{ read [" + mismatch.keyType + "]: " + mismatch.wantedLeaf + " }") : quote("{ read " + mismatch.wantedLeaf + " }");
        const std::string article = isMap ? "a " : "an ";
        const std::string canReadWrite = hasFunction ? function + " can read and write to your " + noun : wantedContainer + " can be read and written";
        const std::string annotated = hasFunction ? "the parameter" : "it";

        message = "Expected this to be " + wantedContainer + ", " + article + noun + " that can read and write " + wanted + ", but got " +
                  givenContainer + ", " + article + noun + " that is only allowed to read " + wanted + " through its " + given + " " + plural +
                  ". Because " + canReadWrite + ", it can silently replace its " + plural +
                  " with those of a smaller type, causing data loss.\n\n"
                  "Help (read/write mismatch):\n  - if " +
                  nothingWrites + ", annotate " + annotated + " as " + annotation + "\n  - if it reads and writes, make " + wanted +
                  " versions of your data" + passTail + " or mark the additional fields as optional";
    }

    return ReadWriteExplanation{std::move(message), std::move(parameterHelp), mismatch.parameterHelpPath};
}

static std::optional<ReadWriteExplanation> explainReadOnlyWouldSatisfy(
    TypeId subTy,
    TypeId superTy,
    const SubtypingResult& result,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<TypeArena> arena,
    bool isArgument,
    const std::string& functionName
)
{
    std::optional<ReadWriteMismatch> mismatch = findReadWriteMismatch(subTy, superTy, result, subtyping, scope, builtinTypes, arena);
    if (!mismatch)
        return std::nullopt;

    mismatch->isArgument = isArgument;
    mismatch->functionName = functionName;
    return wordReadWriteMismatch(*mismatch);
}

// Luwu (helpful subtyping errors): a callback is passed somewhere that will call it with any `Widget`, and it
// only accepts `Button`. Upstream reports that as "`Button` is not a supertype of `Widget`", which is the
// definition of contravariance and no help to a reader looking at a `Button` that plainly is a `Widget`.
// Say what the callback will actually be handed instead.
static std::optional<std::string> explainContravariantArgument(
    TypeId subTy,
    TypeId superTy,
    const SubtypingResult& result,
    NotNull<Subtyping> subtyping,
    NotNull<Scope> scope,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<TypeArena> arena
)
{
    const SubtypingReasoning* reasoning = soleReasoning(result);
    if (!reasoning || reasoning->variance != SubtypingVariance::Contravariant)
        return std::nullopt;

    if (reasoning->subPath != reasoning->superPath)
        return std::nullopt;

    const std::vector<TypePath::Component>& components = reasoning->subPath.components;
    if (components.size() != 2)
        return std::nullopt;

    const TypePath::PackField* arguments = get_if<TypePath::PackField>(&components[0]);
    const TypePath::Index* index = get_if<TypePath::Index>(&components[1]);
    const bool isArgumentsField = arguments && *arguments == TypePath::PackField::Arguments;
    const bool isPackIndex = index && index->variant == TypePath::Index::Variant::Pack;
    if (!isArgumentsField || !isPackIndex)
        return std::nullopt;

    std::optional<std::pair<TypeId, TypeId>> leaves = typesAtPaths(subTy, reasoning->subPath, superTy, reasoning->superPath, builtinTypes, arena);
    if (!leaves)
        return std::nullopt;

    const auto [subLeafTy, superLeafTy] = *leaves;

    // "Not just 'Button'" says the callback handles part of what it will be handed. When the two types
    // don't nest (a `string` parameter handed a `number`), that is false.
    if (!subtyping->isSubtype(subLeafTy, superLeafTy, scope).isSubtype)
        return std::nullopt;

    std::string message = "Expected this to be callable with any '" + toString(superLeafTy) + "'";

    // Naming the parameter is only worth the words when there is more than one to tell apart, and
    // the name the reader recognizes is the one on the function they wrote -- the side being passed.
    // Every function defined in Luwu carries an implicit `...any` tail, so only the head counts as
    // parameters the reader wrote and could tell apart by name.
    const FunctionType* subFunction = get<FunctionType>(follow(subTy));
    size_t parameterCount = 0;
    if (subFunction)
        parameterCount = flatten(subFunction->argTypes).first.size();

    if (parameterCount > 1)
    {
        std::optional<std::string> name;
        if (subFunction && index->index < subFunction->argNames.size())
        {
            if (const std::optional<FunctionArgument>& argument = subFunction->argNames[index->index])
                name = argument->name;
        }

        if (name)
            message += " as parameter '" + *name + "'";
        else
            message += " as parameter " + std::to_string(index->index + 1);
    }

    message += ", not just '" + toString(subLeafTy) + "'.";

    return message;
}

// Luwu (helpful subtyping errors): where the value at `location` is being put, when it's a name: the target of
// `sprite.tint = { ... }` or the local of `local cfg: Config = { ... }`. A literal has no name of its
// own, so this names the root of its paths in place of the generic `given`.
static std::optional<std::string> destinationName(const SourceModule& sourceModule, Location location)
{
    std::vector<AstNode*> ancestry = findAstAncestryOfPosition(sourceModule, location.begin);
    for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it)
    {
        if (AstStatAssign* assign = (*it)->as<AstStatAssign>())
        {
            for (size_t i = 0; i < assign->values.size && i < assign->vars.size; ++i)
            {
                if (assign->values.data[i]->location == location)
                    return bindingName(assign->vars.data[i]);
            }
            return std::nullopt;
        }
        if (AstStatLocal* local = (*it)->as<AstStatLocal>())
        {
            for (size_t i = 0; i < local->values.size && i < local->vars.size; ++i)
            {
                if (local->values.data[i]->location == location)
                    return std::string{local->vars.data[i]->name.value};
            }
            return std::nullopt;
        }
        if ((*it)->asStat())
            return std::nullopt;
    }
    return std::nullopt;
}

void TypeChecker2::explainError(TypeId subTy, TypeId superTy, Location location, const SubtypingResult& result)
{
    if (result.isErrorSuppressing)
        return;

    switch (shouldSuppressErrors(NotNull{&normalizer}, subTy).orElse(shouldSuppressErrors(NotNull{&normalizer}, superTy)))
    {
    case ErrorSuppression::Suppress:
        return;
    case ErrorSuppression::NormalizationFailed:
        reportError(NormalizationTooComplex{}, location);
        break;
    case ErrorSuppression::DoNotSuppress:
        break;
    }

    if (isUncheckedNoneMismatch(subTy, superTy, result, builtinTypes, subtyping->arena))
    {
        reportError(
            GenericError{"this '" + toString(superTy) + "' value is optional and may be 'none', use an if condition to check"},
            location
        );
        return;
    }

    // Still a TypeMismatch, so `wantedType` and `givenType` survive for anything reading the error rather
    // than the message; only the rendering is replaced.
    auto reportWithMessage = [&](std::string message)
    {
        TypeMismatch tm{superTy, subTy};
        tm.overrideMessage = std::move(message);
        reportError(std::move(tm), location);
    };

    NotNull<Scope> scope{findInnermostScope(location)};
    const std::string calleeName = argumentContext ? calleeDisplayName(argumentContext->callee) : std::string{};

    if (std::optional<ReadWriteExplanation> readWrite = explainReadOnlyWouldSatisfy(
            subTy, superTy, result, subtyping, scope, builtinTypes, subtyping->arena, argumentContext.has_value(), calleeName
        ))
    {
        reportWithMessage(std::move(readWrite->message));

        // The value is at the call, but the annotation to change is at the parameter, which can be pages
        // away. Say so where the edit actually happens, or nowhere if that place can't be found.
        if (argumentContext)
        {
            const DeclarationIndex& declarations = getDeclarationIndex();
            AstType* annotation = parameterAnnotation(argumentContext->calleeType, argumentContext->parameterIndex, module->name, declarations);
            if (std::optional<Location> helpLocation = annotatedMemberLocation(annotation, readWrite->parameterHelpPath, declarations))
                reportError(GenericError{std::move(readWrite->parameterHelp)}, *helpLocation);
        }

        return;
    }

    if (std::optional<std::string> contravariant = explainContravariantArgument(subTy, superTy, result, subtyping, scope, builtinTypes, subtyping->arena))
    {
        reportWithMessage(std::move(*contravariant));
        return;
    }

    if (isSimpleNegatedUnionMismatch(subTy, superTy, result))
    {
        const NegationType* negation = get<NegationType>(follow(superTy));
        reportError(
            GenericError{"Expected this to be anything but '" + toString(negation->ty) + "', got '" + toString(subTy) + "'"},
            location
        );
        return;
    }

    // Luwu (helpful subtyping errors): a structural diff of the two types, with a path to each place they
    // disagree, instead of a sentence per subtyping reasoning. It declines types it can't take apart,
    // which then fall through to the reasonings below.
    std::optional<MismatchExplanation> explanation;
    if (FFlag::LuwuHelpfulSubtypingErrors)
    {
        std::optional<std::string> callee;
        if (!calleeName.empty())
            callee = calleeName;
        const char* arrival = argumentContext ? "passed" : checkingReturnStatement ? "returned" : "given";

        // The value's own name, when the mismatched expression is a variable or a chain of plain
        // property reads off one (`registry.recipes`), so paths read `leaving.kind`, not `given.kind`.
        // The expression spanning exactly the error: the innermost one at its start (`req` of
        // `req.user`) is usually a piece of it.
        std::optional<std::string> variable;
        for (AstNode* node : findAstAncestryOfPosition(*sourceModule, location.begin))
        {
            if (AstExpr* expr = node->asExpr(); expr && expr->location == location)
            {
                variable = bindingName(expr);
                break;
            }
        }
        if (!variable)
            variable = destinationName(*sourceModule, location);

        explanation = explainMismatch(subTy, superTy, subtyping, scope, builtinTypes, callee, arrival, variable);
    }

    if (explanation)
    {
        TypeMismatch tm{superTy, subTy, explanation->standalone ? std::string() : std::move(explanation->reason)};
        if (explanation->standalone)
            tm.overrideMessage = std::move(explanation->reason);
        tm.contextVerb = std::move(explanation->contextVerb);
        tm.contextWantedDisplay = std::move(explanation->contextWantedDisplay);
        tm.contextGivenDisplay = std::move(explanation->contextGivenDisplay);
        tm.wantedName = std::move(explanation->wantedName);
        tm.givenName = std::move(explanation->givenName);
        tm.wantedExpansion = std::move(explanation->wantedExpansion);
        tm.givenExpansion = std::move(explanation->givenExpansion);
        tm.luwuExplanation = true;
        tm.notation = explanation->notation;
        reportError(std::move(tm), location);
        return;
    }

    Reasonings reasonings = explainReasonings(subTy, superTy, location, result);

    if (!reasonings.suppressed)
    {
        TypeMismatch tm{superTy, subTy, reasonings.toString()};
        tm.contextVerb = reasonings.contextVerb;
        tm.contextWantedDisplay = reasonings.contextWantedDisplay;
        tm.contextGivenDisplay = reasonings.contextGivenDisplay;
        reportError(std::move(tm), location);
    }
}

void TypeChecker2::explainError(TypePackId subTy, TypePackId superTy, Location location, const SubtypingResult& result)
{
    if (result.isErrorSuppressing)
        return;

    switch (shouldSuppressErrors(NotNull{&normalizer}, subTy).orElse(shouldSuppressErrors(NotNull{&normalizer}, superTy)))
    {
    case ErrorSuppression::Suppress:
        return;
    case ErrorSuppression::NormalizationFailed:
        reportError(NormalizationTooComplex{}, location);
        break;
    case ErrorSuppression::DoNotSuppress:
        break;
    }

    if (checkingReturnStatement)
    {
        if (std::optional<std::string> explanation = explainReturnCountMismatch(subTy, superTy))
        {
            TypePackMismatch tpm{superTy, subTy};
            tpm.overrideMessage = std::move(*explanation);
            reportError(std::move(tpm), location);
            return;
        }
    }

    // Luwu (helpful subtyping errors): one value against one value (`return x` where one is declared) is a
    // type mismatch in all but name, and gets the same structural explanation.
    if (FFlag::LuwuHelpfulSubtypingErrors)
    {
        auto [subHead, subTail] = flatten(subTy);
        auto [superHead, superTail] = flatten(superTy);
        const bool isOneValueEach = subHead.size() == 1 && superHead.size() == 1 && !subTail && !superTail;
        if (isOneValueEach)
        {
            SubtypingResult single = subtyping->isSubtype(subHead[0], superHead[0], NotNull<Scope>{findInnermostScope(location)});
            if (!single.isSubtype)
            {
                explainError(subHead[0], superHead[0], location, single);
                return;
            }
        }
    }

    Reasonings reasonings = explainReasonings(subTy, superTy, location, result);

    if (!reasonings.suppressed)
        reportError(TypePackMismatch{superTy, subTy, reasonings.toString()}, location);
}

bool TypeChecker2::testLiteralOrAstTypeIsSubtype(AstExpr* expr, TypeId expectedType)
{
    NotNull<Scope> scope{findInnermostScope(expr->location)};
    auto exprTy = lookupType(expr);

    SubtypingResult r;

    if (FFlag::LuauImproveUniqueTableWidthSubtyping && !FFlag::LuauBidirectionalInferenceSimplifyTables)
    {
        DenseHashSet<TypeId> uniqueTypes{nullptr};
        findUniqueTypes(NotNull{&uniqueTypes}, std::vector{expr}, NotNull{&module->astTypes});

        // We create a separate `Subtyping` instance here because, in this
        // particular context, we have knowledge that any table literals are
        // unique references to their types.  Because we know that no other
        // references to those values can exist, we can safely test those table
        // types covariantly.
        //
        // These same TypeIds must _not_ be considered to be unique references
        // if they occur in any other context, and so we need to separate the
        // caches.

        Subtyping st{builtinTypes, NotNull{module->internalTypes.get()}, NotNull{&normalizer}, typeFunctionRuntime, ice};
        st.uniqueTypes = &uniqueTypes;

        r = st.isSubtype(exprTy, expectedType, scope);
    }
    else
    {
        r = subtyping->isSubtype(exprTy, expectedType, scope);
    }

    if (r.isSubtype)
        return true;

    return testPotentialLiteralIsSubtype(expr, expectedType);
}

// Luwu (helpful subtyping errors): the name a union was written under, or, for an optional one
// (`GameEvent?`), the name of the one union inside it.
static std::optional<std::string> unionDisplayName(const UnionType* utv)
{
    if (utv->name)
        return utv->name;

    const UnionType* inner = nullptr;
    size_t nonNilCount = 0;
    for (TypeId option : utv->options)
    {
        if (isNil(follow(option)))
            continue;

        ++nonNilCount;
        inner = get<UnionType>(follow(option));
    }

    if (nonNilCount == 1 && inner)
        return inner->name;

    return std::nullopt;
}

bool TypeChecker2::testPotentialLiteralIsSubtype(AstExpr* expr, TypeId expectedType)
{
    auto exprType = follow(lookupType(expr));
    expectedType = follow(expectedType);

    if (auto group = expr->as<AstExprGroup>())
    {
        return testPotentialLiteralIsSubtype(group->expr, expectedType);
    }
    else if (auto ifElse = expr->as<AstExprIfElse>())
    {
        bool passes = testPotentialLiteralIsSubtype(ifElse->trueExpr, expectedType);
        passes &= testPotentialLiteralIsSubtype(ifElse->falseExpr, expectedType);
        return passes;
    }
    else if (auto binExpr = expr->as<AstExprBinary>(); binExpr && binExpr->op == AstExprBinary::Or)
    {
        // In this case: `{ ... } or { ... }` is literal _enough_ that
        // we should do this covariant check.
        auto relaxedExpectedLhs = module->internalTypes->addType(UnionType{{builtinTypes->falsyType, expectedType}});
        bool passes = testPotentialLiteralIsSubtype(binExpr->left, relaxedExpectedLhs);
        passes &= testPotentialLiteralIsSubtype(binExpr->right, expectedType);
        return passes;
    }
    // FIXME: We probably should do a check for `and` here.

    auto exprTable = expr->as<AstExprTable>();
    auto exprTableType = get<TableType>(exprType);
    auto expectedTableType = get<TableType>(expectedType);

    // If we don't have a table or the type of the expression isn't a
    // table, then do a normal subtype test.
    if (!exprTableType || !exprTable)
        return testIsSubtype(exprType, expectedType, expr->location);

    // At this point we *know* that the expression is a table and has a specific
    // table type, but if there isn't an expected table type we should do something
    // slightly different.
    if (!expectedTableType)
    {
        if (auto utv = get<UnionType>(expectedType))
        {
            std::optional<TypeId> matching;
            if (FFlag::LuauBidirectionalInferenceSimplifyTables)
                matching = extractMatchingTableType(utv, exprType, builtinTypes, NotNull{module->internalTypes.get()});
            else
                matching = extractMatchingTableType_DEPRECATED(utv, exprType, builtinTypes);

            if (matching)
            {
                ScopedMemberValue<std::optional<NarrowedLiteral>> narrowed{narrowedLiteralUnion, NarrowedLiteral{expr, utv}};
                return testLiteralOrAstTypeIsSubtype(expr, *matching);
            }
        }

        if (auto itv = get<IntersectionType>(expectedType))
        {
            // If we _happen_ to have an intersection of tables, let's try to
            // construct it and use it as the input to this algorithm.
            TypeIds parts;
            parts.insert(begin(itv), end(itv));
            TypeId simplified = simplifyIntersection(builtinTypes, NotNull{module->internalTypes.get()}, std::move(parts)).result;
            if (is<TableType>(simplified))
                return testPotentialLiteralIsSubtype(expr, simplified);
        }

        return testIsSubtype(exprType, expectedType, expr->location);
    }

    Set<std::optional<std::string>> missingKeys{{}};
    for (const auto& [name, prop] : expectedTableType->props)
    {
        if (prop.readTy)
        {
            if (!isOptional(*prop.readTy))
                missingKeys.insert(name);
        }
    }

    bool isArrayLike = false;
    if (expectedTableType->indexer)
    {
        NotNull<Scope> scope{findInnermostScope(expr->location)};

        auto result = subtyping->isSubtype(/* subTy */ builtinTypes->numberType, /* superTy */ expectedTableType->indexer->indexType, scope);
        isArrayLike = result.isSubtype || isErrorSuppressing(expr->location, expectedTableType->indexer->indexType);
    }

    bool isSubtype = true;

    for (const auto& item : exprTable->items)
    {
        if (isRecord(item))
        {
            const AstArray<char>& s = item.key->as<AstExprConstantString>()->value;
            std::string keyStr{s.data, s.data + s.size};

            missingKeys.erase(keyStr);
            auto expectedIt = expectedTableType->props.find(keyStr);
            if (expectedIt == expectedTableType->props.end())
            {
                if (expectedTableType->indexer)
                {
                    module->astExpectedTypes[item.key] = expectedTableType->indexer->indexType;
                    module->astExpectedTypes[item.value] = expectedTableType->indexer->indexResultType;
                    auto inferredKeyType = module->internalTypes->addType(SingletonType{StringSingleton{keyStr}});
                    isSubtype &= testIsSubtype(inferredKeyType, expectedTableType->indexer->indexType, item.key->location);
                    isSubtype &= testPotentialLiteralIsSubtype(item.value, expectedTableType->indexer->indexResultType);
                }
                // If there's not an indexer, then by width subtyping we can just do nothing :)
            }
            else
            {
                // If the type has a read type, then we have an expected type for it, otherwise, we actually don't
                // care what's assigned to it because the only allowed behavior is writing to that property.

                if (expectedIt->second.readTy)
                {
                    module->astExpectedTypes[item.value] = *expectedIt->second.readTy;
                    isSubtype &= testPotentialLiteralIsSubtype(item.value, *expectedIt->second.readTy);
                }
            }
        }
        else if (item.kind == AstExprTable::Item::Kind::List)
        {
            if (!isArrayLike)
            {
                isSubtype = false;
                reportError(UnexpectedArrayLikeTableItem{}, item.value->location);
            }
            // if the indexer index type is not exactly `number`.
            if (expectedTableType->indexer)
            {
                module->astExpectedTypes[item.value] = expectedTableType->indexer->indexResultType;
                isSubtype &= testPotentialLiteralIsSubtype(item.value, expectedTableType->indexer->indexResultType);
            }
        }
        else if (item.kind == AstExprTable::Item::Kind::General && expectedTableType->indexer)
        {
            module->astExpectedTypes[item.key] = expectedTableType->indexer->indexType;
            module->astExpectedTypes[item.value] = expectedTableType->indexer->indexResultType;
            isSubtype &= testPotentialLiteralIsSubtype(item.key, expectedTableType->indexer->indexType);
            isSubtype &= testPotentialLiteralIsSubtype(item.value, expectedTableType->indexer->indexResultType);
        }
    }

    if (!missingKeys.empty())
    {
        std::vector<Name> temp;
        temp.reserve(missingKeys.size());
        for (const auto& key : missingKeys)
            if (key)
                temp.push_back(*key);
        if (FFlag::LuwuHelpfulSubtypingErrors)
        {
            // In the order they were declared, which is how the reader will go looking for them; the set
            // holding them is unordered.
            auto declaredAt = [&](const Name& name) -> std::optional<Position>
            {
                auto it = expectedTableType->props.find(name);
                if (it == expectedTableType->props.end())
                    return std::nullopt;
                if (it->second.typeLocation)
                    return it->second.typeLocation->begin;
                if (it->second.location)
                    return it->second.location->begin;
                return std::nullopt;
            };
            std::sort(temp.begin(), temp.end());
            std::stable_sort(
                temp.begin(),
                temp.end(),
                [&](const Name& a, const Name& b)
                {
                    std::optional<Position> pa = declaredAt(a);
                    std::optional<Position> pb = declaredAt(b);
                    if (pa && pb)
                        return *pa < *pb;
                    return pa.has_value() && !pb.has_value();
                }
            );
        }
        MissingProperties missing{expectedType, exprType, std::move(temp)};
        if (FFlag::LuwuHelpfulSubtypingErrors)
        {
            missing.luwuExplanation = true;
            missing.givenName = destinationName(*sourceModule, expr->location);
            const bool isNarrowedFromUnion = narrowedLiteralUnion && narrowedLiteralUnion->literal == expr;
            const bool isUninstantiatedAlias = expectedTableType->name && expectedTableType->instantiatedTypeParams.empty() &&
                                               expectedTableType->instantiatedTypePackParams.empty();
            if (isNarrowedFromUnion)
                missing.wantedName = unionDisplayName(narrowedLiteralUnion->narrowedFrom);
            if (!missing.wantedName && isUninstantiatedAlias)
                missing.wantedName = expectedTableType->name;
        }
        reportError(std::move(missing), expr->location);
        return false;
    }

    return isSubtype;
}

bool TypeChecker2::testIsSubtype(TypeId subTy, TypeId superTy, Location location)
{
    NotNull<Scope> scope{findInnermostScope(location)};
    SubtypingResult r = subtyping->isSubtype(subTy, superTy, scope);

    if (r.isErrorSuppressing)
        return r.isSubtype;

    for (auto& e : r.errors)
        e.location = location;

    reportErrors(std::move(r.errors));
    if (r.normalizationTooComplex)
        reportError(NormalizationTooComplex{}, location);

    if (!r.isSubtype)
        explainError(subTy, superTy, location, r);

    return r.isSubtype;
}

bool TypeChecker2::testIsSubtype(TypePackId subTy, TypePackId superTy, Location location)
{
    NotNull<Scope> scope{findInnermostScope(location)};
    SubtypingResult r = subtyping->isSubtype(subTy, superTy, scope, {});

    if (!isErrorSuppressing(location, subTy))
    {
        for (auto& e : r.errors)
            e.location = location;
    }
    reportErrors(std::move(r.errors));
    if (r.normalizationTooComplex)
        reportError(NormalizationTooComplex{}, location);

    if (!r.isSubtype)
        explainError(subTy, superTy, location, r);

    return r.isSubtype;
}

void TypeChecker2::maybeReportSubtypingError(const TypeId subTy, const TypeId superTy, const Location& location)
{
    switch (shouldSuppressErrors(NotNull{&normalizer}, subTy).orElse(shouldSuppressErrors(NotNull{&normalizer}, superTy)))
    {
    case ErrorSuppression::Suppress:
        return;
    case ErrorSuppression::NormalizationFailed:
        reportError(NormalizationTooComplex{}, location);
        break;
    case ErrorSuppression::DoNotSuppress:
        break;
    default:
        break;
    }

    reportError(TypeMismatch{superTy, subTy}, location);
}

void TypeChecker2::testIsSubtypeForInStat(const TypeId iterFunc, const TypeId prospectiveFunc, const AstStatForIn& forInStat)
{
    LUAU_ASSERT(get<FunctionType>(follow(iterFunc)));
    LUAU_ASSERT(get<FunctionType>(follow(prospectiveFunc)));

    const Location& iterFuncLocation = forInStat.values.data[0]->location;

    const NotNull<Scope> scope{findInnermostScope(iterFuncLocation)};
    SubtypingResult r = subtyping->isSubtype(iterFunc, prospectiveFunc, scope);

    if (!isErrorSuppressing(iterFuncLocation, iterFunc))
    {
        for (auto& e : r.errors)
            e.location = iterFuncLocation;
    }
    reportErrors(std::move(r.errors));

    if (r.normalizationTooComplex)
        reportError(NormalizationTooComplex{}, iterFuncLocation);

    if (r.isSubtype)
        return;

    // TODO, CLI-177651: We do not get amazing errors from this, we probably
    // want to do something more bidirectional here.
    explainError(iterFunc, prospectiveFunc, iterFuncLocation, r);
}

void TypeChecker2::reportError(TypeErrorData data, const Location& location)
{
    if (auto utk = get_if<UnknownProperty>(&data))
        diagnoseMissingTableKey(utk, data);

    module->errors.emplace_back(location, module->name, std::move(data));

    if (logger)
        logger->captureTypeCheckError(module->errors.back());
}

void TypeChecker2::reportError(TypeError e)
{
    reportError(std::move(e.data), e.location);
}

void TypeChecker2::reportErrors(ErrorVec errors)
{
    for (TypeError e : errors)
        reportError(std::move(e));
}

/* A helper for checkIndexTypeFromType.
 *
 * Returns a pair:
 * * A boolean indicating that at least one of the constituent typeArguments
 *     contains the prop, and
 * * A vector of typeArguments that do not contain the prop.
 */
PropertyTypes TypeChecker2::lookupProp(
    const NormalizedType* norm,
    const std::string& prop,
    ValueContext context,
    const Location& location,
    TypeId astIndexExprType,
    std::vector<TypeError>& errors
)
{
    std::vector<TypeId> typesOfProp;
    std::vector<TypeId> typesMissingTheProp;

    // this is `false` if we ever hit the resource limits during any of our uses of `fetch`.
    bool normValid = true;

    auto fetch = [&](TypeId ty)
    {
        NormalizationResult result = normalizer.isInhabited(ty);
        if (result == NormalizationResult::HitLimits)
            normValid = false;
        if (result != NormalizationResult::True)
            return;

        DenseHashSet<TypeId> seen{nullptr};
        PropertyType res = hasIndexTypeFromType(ty, prop, context, location, seen, astIndexExprType, errors);

        if (res.present == NormalizationResult::HitLimits)
        {
            normValid = false;
            return;
        }

        if (res.present == NormalizationResult::True && res.result)
            typesOfProp.emplace_back(*res.result);

        if (res.present == NormalizationResult::False)
            typesMissingTheProp.push_back(ty);
    };

    if (normValid)
        fetch(norm->tops);
    if (normValid)
        fetch(norm->booleans);

    if (FFlag::LuwuClasses)
    {
        // Luwu Classes (rfcs/classes): upstream checks each extern type part on its own here, so that a union of
        // objects needs the property on every member. It skips the table shapes intersected with those parts, so
        // upstream reports `Instance` as missing `brushes` in `Instance & { brushes: Instance }`. A shape that has the
        // property gives it to every part; the shape-aware lookup below adds its type.
        bool shapeHasProp = false;
        for (TypeId shape : norm->externTypes.shapeExtensions)
        {
            DenseHashSet<TypeId> seen{nullptr};
            // The shape-aware lookup below reports this shape's errors; don't report them twice.
            std::vector<TypeError> shapeErrors;
            PropertyType res = hasIndexTypeFromType(shape, prop, context, location, seen, astIndexExprType, shapeErrors);

            if (res.present == NormalizationResult::HitLimits)
                normValid = false;

            if (res.present == NormalizationResult::True)
            {
                shapeHasProp = true;
                break;
            }
        }

        if (normValid && !shapeHasProp)
        {
            for (const auto& partTy : norm->externTypes.ordering)
            {
                fetch(partTy);
                if (!normValid)
                    break;
            }
        }
    }

    // TODO: the subsequent code here is basically proof that this broader approach to doing indexing isn't quite right.
    // we _should_ be leveraging one unified implementation of indexing here, shared with e.g. the `index` type function.
    if (normValid)
    {
        // each individual extern type consists of a collection of extern types in a normal form, and a collection of table types describing the
        // shapes further. extern types and tables are both open to extension in general, and therefore, we need to consider the possibility that a
        // subset of these types might not be contributing to the type of the index, but that the index should nevertheless be valid still. towards
        // that end, we want to look through all of the components to see if any of them have the index before making a judgment if the extern types
        // portion as a whole has the index.

        std::vector<TypeId> localTypesOfProp;

        for (const auto& [ty, _negations] : norm->externTypes.externTypes)
        {
            NormalizationResult result = normalizer.isInhabited(ty);
            if (result == NormalizationResult::HitLimits)
                normValid = false;
            if (result != NormalizationResult::True)
                continue;

            DenseHashSet<TypeId> seen{nullptr};
            PropertyType res = hasIndexTypeFromType(ty, prop, context, location, seen, astIndexExprType, errors);

            if (res.present == NormalizationResult::HitLimits)
            {
                normValid = false;
                continue;
            }

            if (res.present == NormalizationResult::True && res.result)
                localTypesOfProp.emplace_back(*res.result);
        }

        for (TypeId ty : norm->externTypes.shapeExtensions)
        {
            NormalizationResult result = normalizer.isInhabited(ty);
            if (result == NormalizationResult::HitLimits)
                normValid = false;
            if (result != NormalizationResult::True)
                continue;

            DenseHashSet<TypeId> seen{nullptr};
            PropertyType res = hasIndexTypeFromType(ty, prop, context, location, seen, astIndexExprType, errors);

            if (res.present == NormalizationResult::HitLimits)
            {
                normValid = false;
                continue;
            }

            if (res.present == NormalizationResult::True && res.result)
                localTypesOfProp.emplace_back(*res.result);
        }

        if (!localTypesOfProp.empty())
            typesOfProp.insert(typesOfProp.end(), localTypesOfProp.begin(), localTypesOfProp.end());
        else
        {
            typesMissingTheProp.insert(typesMissingTheProp.end(), norm->externTypes.ordering.begin(), norm->externTypes.ordering.end());
            typesMissingTheProp.insert(typesMissingTheProp.end(), norm->externTypes.shapeExtensions.begin(), norm->externTypes.shapeExtensions.end());
        }
    }
    else if (normValid)
    {
        for (const auto& [ty, _negations] : norm->externTypes.externTypes)
        {
            fetch(ty);

            if (!normValid)
                break;
        }
    }

    if (normValid)
        fetch(norm->errors);
    if (normValid)
        fetch(norm->nils);
    if (normValid)
        fetch(norm->numbers);
    if (normValid && !norm->strings.isNever())
        fetch(builtinTypes->stringType);
    if (normValid)
        fetch(norm->threads);
    if (normValid)
        fetch(norm->buffers);

    if (normValid)
    {
        for (TypeId ty : norm->tables)
        {
            fetch(ty);

            if (!normValid)
                break;
        }
    }

    if (normValid && norm->functions.isTop)
        fetch(builtinTypes->functionType);
    else if (normValid && !norm->functions.isNever())
    {
        if (norm->functions.parts.size() == 1)
            fetch(norm->functions.parts.front());
        else
        {
            std::vector<TypeId> parts;
            parts.insert(parts.end(), norm->functions.parts.begin(), norm->functions.parts.end());
            fetch(module->internalTypes->addType(IntersectionType{std::move(parts)}));
        }
    }

    if (normValid)
    {
        for (const auto& [tyvar, intersect] : norm->tyvars)
        {
            if (get<NeverType>(intersect->tops))
            {
                TypeId ty = normalizer.typeFromNormal(*intersect);
                fetch(module->internalTypes->addType(IntersectionType{{tyvar, ty}}));
            }
            else
                fetch(follow(tyvar));

            if (!normValid)
                break;
        }
    }

    return {std::move(typesOfProp), std::move(typesMissingTheProp)};
}


void TypeChecker2::checkIndexTypeFromType(
    TypeId tableTy,
    const std::string& prop,
    ValueContext context,
    const Location& location,
    TypeId astIndexExprType
)
{
    std::shared_ptr<const NormalizedType> norm = normalizer.normalize(tableTy);
    if (!norm)
    {
        reportError(NormalizationTooComplex{}, location);
        return;
    }

    // if the type is error suppressing, we don't actually have any work left to do.
    if (norm->shouldSuppressErrors())
        return;

    std::vector<TypeError> dummy;
    const auto propTypes = lookupProp(norm.get(), prop, context, location, astIndexExprType, module->errors);

    if (propTypes.foundMissingProp())
    {
        if (propTypes.foundOneProp())
            reportError(MissingUnionProperty{tableTy, propTypes.missingProp, prop}, location);
        // For class LValues, we don't want to report an extension error,
        // because extern typeArguments come into being with full knowledge of their
        // shape. We instead want to report the unknown property error of
        // the `else` branch.
        else if (context == ValueContext::LValue)
        {
            const auto lvPropTypes = lookupProp(norm.get(), prop, ValueContext::RValue, location, astIndexExprType, dummy);
            if (lvPropTypes.foundOneProp() && lvPropTypes.noneMissingProp())
                reportError(PropertyAccessViolation{tableTy, prop, PropertyAccessViolation::CannotWrite}, location);
            else if (get<PrimitiveType>(tableTy) || get<FunctionType>(tableTy))
                reportError(NotATable{tableTy}, location);
            else if (auto et = get<ExternType>(tableTy))
            {
                // Luwu Classes (rfcs/classes): upstream calls a write to a missing property of an extern type
                // "read-only", since an embedder's type can't grow new properties. For an object, class or trait
                // that hides a misspelled field name, so report the field as not found.
                bool reportAsUnknown = et->indexer || luwuNominalKind(tableTy);
                if (reportAsUnknown)
                    reportError(UnknownProperty{tableTy, prop}, location);
                else
                    reportError(PropertyAccessViolation{tableTy, prop, PropertyAccessViolation::CannotWrite}, location);
            }
            else
                reportError(CannotExtendTable{tableTy, CannotExtendTable::Property, prop}, location);
        }
        else if (context == ValueContext::RValue)
        {
            const auto rvPropTypes = lookupProp(norm.get(), prop, ValueContext::LValue, location, astIndexExprType, dummy);
            if (rvPropTypes.foundOneProp() && rvPropTypes.noneMissingProp())
                reportError(PropertyAccessViolation{tableTy, prop, PropertyAccessViolation::CannotRead}, location);
            else
                reportError(UnknownProperty{tableTy, prop}, location);
        }
        else
            reportError(UnknownProperty{tableTy, prop}, location);
    }
}

PropertyType TypeChecker2::hasIndexTypeFromType(
    TypeId ty,
    const std::string& prop,
    ValueContext context,
    const Location& location,
    DenseHashSet<TypeId>& seen,
    TypeId astIndexExprType,
    std::vector<TypeError>& errors
)
{
    // If we have already encountered this type, we must assume that some
    // other codepath will do the right thing and signal false if the
    // property is not present.
    if (seen.contains(ty))
        return {NormalizationResult::True, {}};
    seen.insert(ty);

    if (get<ErrorType>(ty) || get<AnyType>(ty) || get<NeverType>(ty))
        return {NormalizationResult::True, {ty}};

    if (isString(ty))
    {
        std::optional<TypeId> mtIndex = Luau::findMetatableEntry(builtinTypes, errors, builtinTypes->stringType, "__index", location);
        LUAU_ASSERT(mtIndex);
        ty = *mtIndex;
    }

    if (auto tt = getTableType(ty))
    {
        if (auto resTy = findTablePropertyRespectingMeta(builtinTypes, errors, ty, prop, context, location, /* useNewSolver */ true))
            return {NormalizationResult::True, resTy};

        if (tt->indexer)
        {
            TypeId indexType = follow(tt->indexer->indexType);
            TypeId givenType = module->internalTypes->addType(SingletonType{StringSingleton{prop}});
            bool keyMatches = subtyping->isSubtype(givenType, indexType, NotNull{module->getModuleScope().get()}).isSubtype;

            if (keyMatches)
            {
                if (context == ValueContext::LValue && tt->indexer->isReadOnly)
                    return {NormalizationResult::False, {}};
                return {NormalizationResult::True, {tt->indexer->indexResultType}};
            }
        }

        return {NormalizationResult::False, {builtinTypes->unknownType}};
    }
    else if (const ExternType* cls = get<ExternType>(ty))
    {
        // If the property doesn't exist on the class, we consult the indexer
        // We need to check if the type of the index expression foo (x[foo])
        // is compatible with the indexer's indexType
        // Construct the intersection and test inhabitedness!
        if (auto property = lookupExternTypeProp(cls, prop))
        {
            if ((context == ValueContext::LValue && !property->writeTy) || (context == ValueContext::RValue && !property->readTy))
                return {NormalizationResult::False, {}};
            else
                return {NormalizationResult::True, context == ValueContext::LValue ? property->writeTy : property->readTy};
        }
        if (cls->indexer)
        {
            TypeId inhabitedTestType = module->internalTypes->addType(IntersectionType{{cls->indexer->indexType, astIndexExprType}});
            return {normalizer.isInhabited(inhabitedTestType), {cls->indexer->indexResultType}};
        }

        if (FFlag::LuwuClasses)
        {
            if (cls->metatable)
            {
                // For user-defined classes, the object metatable holds metamethods (e.g. __add)
                // directly in its props rather than under an __index table.
                if (const TableType* mtt = get<TableType>(follow(*cls->metatable)))
                {
                    if (auto mtProp = mtt->props.find(prop); mtProp != mtt->props.end())
                    {
                        if ((context == ValueContext::LValue && !mtProp->second.writeTy) ||
                            (context == ValueContext::RValue && !mtProp->second.readTy))
                            return {NormalizationResult::False, {}};
                        return {NormalizationResult::True, context == ValueContext::LValue ? mtProp->second.writeTy : mtProp->second.readTy};
                    }
                }
            }
        }

        return {NormalizationResult::False, {}};
    }
    else if (const UnionType* utv = get<UnionType>(ty))
    {
        std::vector<TypeId> parts;
        parts.reserve(utv->options.size());

        for (TypeId part : utv)
        {
            PropertyType result = hasIndexTypeFromType(part, prop, context, location, seen, astIndexExprType, errors);

            if (result.present != NormalizationResult::True)
                return {result.present, {}};
            if (result.result)
                parts.emplace_back(*result.result);
        }

        if (parts.size() == 0)
            return {NormalizationResult::False, {}};

        if (parts.size() == 1)
            return {NormalizationResult::True, {parts[0]}};

        TypeId propTy;
        if (context == ValueContext::LValue)
            propTy = module->internalTypes->addType(IntersectionType{std::move(parts)});
        else
            propTy = module->internalTypes->addType(UnionType{std::move(parts)});

        return {NormalizationResult::True, propTy};
    }
    else if (const IntersectionType* itv = get<IntersectionType>(ty))
    {
        for (TypeId part : itv)
        {
            PropertyType result = hasIndexTypeFromType(part, prop, context, location, seen, astIndexExprType, errors);
            if (result.present != NormalizationResult::False)
                return result;
        }

        return {NormalizationResult::False, {}};
    }
    else if (const PrimitiveType* pt = get<PrimitiveType>(ty))
        return {(inConditional(typeContext) && pt->type == PrimitiveType::Table) ? NormalizationResult::True : NormalizationResult::False, {ty}};
    else
        return {NormalizationResult::False, {}};
}

void TypeChecker2::suggestAnnotations(AstExprFunction* expr, TypeId ty)
{
    const FunctionType* inferredFtv = get<FunctionType>(ty);
    LUAU_ASSERT(inferredFtv);

    VecDeque<TypeId> workList;
    DenseHashSet<TypeId> seen{nullptr};

    TypeFunctionReductionGuesser guesser{NotNull{module->internalTypes.get()}, builtinTypes, NotNull{&normalizer}};
    for (TypeId retTy : inferredFtv->retTypes)
        workList.push_back(retTy);

    while (!workList.empty())
    {
        TypeId t = follow(workList.front());
        workList.pop_front();

        if (seen.contains(t))
            continue;
        seen.insert(t);

        if (auto ut = get<UnionType>(t))
        {
            for (TypeId t : ut)
                workList.push_back(t);
        }
        else if (auto it = get<IntersectionType>(t))
        {
            for (TypeId t : it)
                workList.push_back(t);
        }
        else if (get<TypeFunctionInstanceType>(t))
        {
            TypeFunctionReductionGuessResult result = guesser.guessTypeFunctionReductionForFunctionExpr(*expr, inferredFtv, t);
            if (result.shouldRecommendAnnotation && !get<UnknownType>(result.guessedReturnType))
                reportError(
                    ExplicitFunctionAnnotationRecommended{std::move(result.guessedFunctionAnnotations), result.guessedReturnType}, expr->location
                );
        }
    }
}

void TypeChecker2::checkTypeInstantiation(
    AstExpr* baseFunctionExpr,
    TypeId fnType,
    const Location& location,
    const AstArray<AstTypeOrPack>& typeArguments
)
{
    const FunctionType* ftv = get<FunctionType>(follow(fnType));
    if (!ftv)
    {
        InstantiateGenericsOnNonFunction::InterestingEdgeCase interestingEdgeCase = InstantiateGenericsOnNonFunction::InterestingEdgeCase::None;

        if (findMetatableEntry(builtinTypes, module->errors, fnType, "__call", location).has_value())
        {
            interestingEdgeCase = InstantiateGenericsOnNonFunction::InterestingEdgeCase::MetatableCall;
        }
        else if (get<IntersectionType>(follow(fnType)))
        {
            interestingEdgeCase = InstantiateGenericsOnNonFunction::InterestingEdgeCase::Intersection;
        }

        reportError(
            InstantiateGenericsOnNonFunction{
                interestingEdgeCase,
            },
            location
        );

        return;
    }

    size_t typeCount = 0;
    size_t typePackCount = 0;

    for (const AstTypeOrPack& typeOrPack : typeArguments)
    {
        if (typeOrPack.type)
        {
            visit(typeOrPack.type);
            ++typeCount;
        }
        else
        {
            LUAU_ASSERT(typeOrPack.typePack);
            visit(typeOrPack.typePack);
            ++typePackCount;
        }
    }

    if (ftv->generics.size() < typeCount || ftv->genericPacks.size() < typePackCount)
    {
        reportError(
            TypeInstantiationCountMismatch{
                getIdentifierOfBaseVar(baseFunctionExpr), fnType, typeCount, ftv->generics.size(), typePackCount, ftv->genericPacks.size()
            },
            location
        );
    }
}


void TypeChecker2::diagnoseMissingTableKey(UnknownProperty* utk, TypeErrorData& data) const
{
    std::string_view sv(utk->key);
    std::set<Name> candidates;

    auto accumulate = [&](const TableType::Props& props)
    {
        for (const auto& [name, ty] : props)
        {
            if (sv != name && equalsLower(sv, name))
                candidates.insert(name);
        }
    };

    if (auto ttv = getTableType(utk->table))
        accumulate(ttv->props);
    else if (auto etv = get<ExternType>(follow(utk->table)))
    {
        while (etv)
        {
            accumulate(etv->props());

            if (!etv->parent)
                break;

            etv = get<ExternType>(*etv->parent);
            LUAU_ASSERT(etv);
        }
    }

    if (!candidates.empty())
        data = TypeErrorData(UnknownPropButFoundLikeProp{utk->table, utk->key, std::move(candidates)});
}

bool TypeChecker2::isErrorSuppressing(Location loc, TypeId ty)
{
    switch (shouldSuppressErrors(NotNull{&normalizer}, ty))
    {
    case ErrorSuppression::DoNotSuppress:
        return false;
    case ErrorSuppression::Suppress:
        return true;
    case ErrorSuppression::NormalizationFailed:
        reportError(NormalizationTooComplex{}, loc);
        return false;
    };

    LUAU_ASSERT(false);
    return false; // UNREACHABLE
}

bool TypeChecker2::isErrorSuppressing(Location loc1, TypeId ty1, Location loc2, TypeId ty2)
{
    return isErrorSuppressing(loc1, ty1) || isErrorSuppressing(loc2, ty2);
}

bool TypeChecker2::isErrorSuppressing(Location loc, TypePackId tp)
{
    switch (shouldSuppressErrors(NotNull{&normalizer}, tp))
    {
    case ErrorSuppression::DoNotSuppress:
        return false;
    case ErrorSuppression::Suppress:
        return true;
    case ErrorSuppression::NormalizationFailed:
        reportError(NormalizationTooComplex{}, loc);
        return false;
    };

    LUAU_ASSERT(false);
    return false; // UNREACHABLE
}

bool TypeChecker2::isErrorSuppressing(Location loc1, TypePackId tp1, Location loc2, TypePackId tp2)
{
    return isErrorSuppressing(loc1, tp1) || isErrorSuppressing(loc2, tp2);
}

bool TypeChecker2::reportNonviableOverloadErrors(
    std::vector<std::pair<TypeId, ErrorVec>> nonviableOverloads,
    Location callFuncLocation,
    size_t argHeadSize,
    Location callLocation
)
{
    // If multiple overloads report errors, we want to return an error reporting that multiple overloads have errors.
    // If only one overload has errors, we want to report those errors.
    std::optional<ErrorVec> reportedErrors;
    bool multipleOverloadsHaveErrors = false;
    for (auto& [ty, errs] : nonviableOverloads)
    {
        if (!isErrorSuppressing(callFuncLocation, ty) && !errs.empty())
        {
            if (reportedErrors)
            {
                multipleOverloadsHaveErrors = true;
                break;
            }
            reportedErrors.emplace(errs);
        }
    }
    if (multipleOverloadsHaveErrors)
    {
        reportError(MultipleNonviableOverloads{argHeadSize}, callLocation);
        return true;
    }
    else if (reportedErrors)
    {
        reportErrors(std::move(*reportedErrors));
        return true;
    }

    return false;
}


} // namespace Luau
