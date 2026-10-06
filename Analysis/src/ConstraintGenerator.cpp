// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ConstraintGenerator.h"

#include "Luau/ApplyTypeFunction.h"
#include "Luau/Ast.h"
#include "Luau/AstUtils.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/BuiltinTypeFunctions.h"
#include "Luau/Common.h"
#include "Luau/Constraint.h"
#include "Luau/ControlFlow.h"
#include "Luau/ControlFlowGraph.h"
#include "Luau/DcrLogger.h"
#include "Luau/Def.h"
#include "Luau/DenseHash.h"
#include "Luau/IterativeTypeVisitor.h"
#include "Luau/ModuleResolver.h"
#include "Luau/Normalize.h"
#include "Luau/NotNull.h"
#include "Luau/RecursionCounter.h"
#include "Luau/Refinement.h"
#include "Luau/Scope.h"
#include "Luau/Simplify.h"
#include "Luau/StringUtils.h"
#include "Luau/Subtyping.h"
#include "Luau/TimeTrace.h"
#include "Luau/Type.h"
#include "Luau/TypeFunction.h"
#include "Luau/TypeFunctionError.h"
#include "Luau/TypePack.h"
#include "Luau/TypeStateMap.h"
#include "Luau/TypeUtils.h"
#include "Luau/Unifier2.h"
#include "Luau/VisitType.h"

#include <algorithm>
#include <functional>
#include <memory>

LUAU_DYNAMIC_FASTINTVARIABLE(LuauConstraintGeneratorRecursionLimit, 300)

LUAU_FASTINT(LuauCheckRecursionLimit)
LUAU_FASTFLAG(DebugLuauLogSolverToJson)
LUAU_FASTFLAG(DebugLuauMagicTypes)
LUAU_FASTINTVARIABLE(LuauPrimitiveInferenceInTableLimit, 500)
LUAU_FASTFLAGVARIABLE(LuauDisallowRedefiningBuiltinTypes)
LUAU_FASTFLAG(LuauTypeFunctionStructuredErrors)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAGVARIABLE(LuauTidyTypePrototyping)
LUAU_FASTFLAGVARIABLE(LuauDoNotEmplaceAnnotatedType)
LUAU_FASTFLAGVARIABLE(LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier)
LUAU_FLAGVERSION(LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier, 2)
LUAU_FASTFLAGVARIABLE(LuauDeprecatedAttributeOnAnonymousFunctions)
LUAU_FASTFLAGVARIABLE(DebugLuauCFG)
LUAU_FASTFLAG(LuwuDefaultArguments)
LUAU_FASTFLAGVARIABLE(LuwuExternTypeUseDefinitionScope)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(DebugLuauCyclicRequireTypeInference)
LUAU_FASTFLAG(DebugLuwuUserDefinedRefinements)

namespace Luau
{

bool doesCallError(const AstExprCall* call);        // TypeInfer.cpp
const AstStat* getFallthrough(const AstStat* node); // TypeInfer.cpp

static bool isValidClassMetamethod(const Name& name)
{
    return name == "__call" || name == "__concat" || name == "__unm" || name == "__add" || name == "__sub" || name == "__mul" || name == "__div" ||
           name == "__mod" || name == "__pow" || name == "__tostring" || name == "__eq" || name == "__lt" || name == "__le" || name == "__iter" ||
           name == "__len" || name == "__idiv";
}

static std::optional<AstExpr*> matchRequire(const AstExprCall& call)
{
    const char* require = "require";

    if (call.args.size != 1)
        return std::nullopt;

    const AstExprGlobal* funcAsGlobal = call.func->as<AstExprGlobal>();
    if (!funcAsGlobal || funcAsGlobal->name != require)
        return std::nullopt;

    if (call.args.size != 1)
        return std::nullopt;

    return call.args.data[0];
}


namespace
{

Checkpoint checkpoint(const ConstraintGenerator* cg)
{
    if (FFlag::DebugLuauCyclicRequireTypeInference)
        return Checkpoint{cg->cgraph->constraints.size()};
    return Checkpoint{cg->constraints.size()};
}

template<typename F>
void forEachConstraint(const Checkpoint& start, const Checkpoint& end, const ConstraintGenerator* cg, F f)
{
    if (FFlag::DebugLuauCyclicRequireTypeInference)
    {
        for (size_t i = start.offset; i < end.offset; ++i)
        {
            f(cg->cgraph->constraints[i]);
        }
    }
    else
    {
        for (size_t i = start.offset; i < end.offset; ++i)
        {
            f(cg->constraints[i]);
        }
    }
}

/**
 * For all constraints [C] from [start] to [end], block dispatching
 * [target] on [C].
 */
LUAU_NOINLINE void addAllAsDependencies(const Checkpoint& start, const Checkpoint& end, const ConstraintGenerator* cg, NotNull<Constraint> target)
{
    forEachConstraint(
        start,
        end,
        cg,
        [cg, target](const ConstraintPtr& ptr)
        {
            cg->cgraph->addDependencyOf(ptr.get(), target);
        }
    );
}

/**
 * For all constraints [C] from [start] to [end], block dispatching
 * [C] on [target].
 */
LUAU_NOINLINE void addAllAsReverseDependencies(
    const Checkpoint& start,
    const Checkpoint& end,
    const ConstraintGenerator* cg,
    NotNull<Constraint> target
)
{
    forEachConstraint(
        start,
        end,
        cg,
        [cg, target](const ConstraintPtr& ptr)
        {
            cg->cgraph->addDependencyOf(target, ptr.get());
        }
    );
}

/**
 * For all constraints [C] from [start] to [end], block dispatching
 * [target] on [C].
 *
 * HACK: Additionally, chain `PackSubtypeConstraint`s that are tied to return
 * statements in order to preserve some behavior from the old solver.
 */
LUAU_NOINLINE void addAllAsDependenciesAndChainReturns(
    const Checkpoint& start,
    const Checkpoint& end,
    const ConstraintGenerator* cg,
    NotNull<Constraint> target
)
{
    Constraint* previous = nullptr;
    forEachConstraint(
        start,
        end,
        cg,
        [cg, target, &previous](const ConstraintPtr& constraint)
        {
            cg->cgraph->addDependencyOf(constraint.get(), target);
            if (auto psc = get<PackSubtypeConstraint>(*constraint); psc && psc->returns)
            {
                if (previous)
                    cg->cgraph->addDependencyOf(previous, constraint.get());

                previous = constraint.get();
            }
        }
    );
}

struct HasFreeType : TypeOnceVisitor
{
    bool result = false;

    HasFreeType()
        : TypeOnceVisitor("TypeOnceVisitor", /* skipBoundTypes */ true)
    {
    }

    bool visit(TypeId ty) override
    {
        if (result || ty->persistent)
            return false;
        return true;
    }

    bool visit(TypePackId tp) override
    {
        if (result)
            return false;
        return true;
    }

    bool visit(TypeId ty, const ExternType&) override
    {
        return false;
    }

    bool visit(TypeId ty, const FreeType&) override
    {
        result = true;
        return false;
    }

    bool visit(TypePackId ty, const FreeTypePack&) override
    {
        result = true;
        return false;
    }
};

bool hasFreeType(TypeId ty)
{
    HasFreeType hft{};
    hft.traverse(ty);
    return hft.result;
}

struct GlobalNameCollector : public AstVisitor
{
    DenseHashSet<AstName> names;

    GlobalNameCollector()
        : names(AstName())
    {
    }

    bool visit(AstExprGlobal* node) override
    {
        names.insert(node->name);
        return true;
    }
};

} // namespace

ConstraintGenerator::ConstraintGenerator(
    ModulePtr module,
    NotNull<Normalizer> normalizer,
    NotNull<TypeFunctionRuntime> typeFunctionRuntime,
    NotNull<ModuleResolver> moduleResolver,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<InternalErrorReporter> ice,
    ScopePtr globalScope,
    ScopePtr typeFunctionScope,
    std::function<void(const ModuleName&, const ScopePtr&)> prepareModuleScope,
    DcrLogger* logger,
    NotNull<DataFlowGraph> dfg,
    std::vector<RequireCycle> requireCycles,
    NotNull<ConstraintGraph> cgraph,
    CFG::TypeStateMap* typestate
)
    : module(module)
    , builtinTypes(builtinTypes)
    , arena(normalizer->arena)
    , rootScope(nullptr)
    , dfg(dfg)
    , normalizer(normalizer)
    , typeFunctionRuntime(typeFunctionRuntime)
    , moduleResolver(moduleResolver)
    , ice(ice)
    , globalScope(std::move(globalScope))
    , typeFunctionScope(std::move(typeFunctionScope))
    , prepareModuleScope(std::move(prepareModuleScope))
    , requireCycles(std::move(requireCycles))
    , logger(logger)
    , cgraph(cgraph)
    , typestate(typestate)
{
    LUAU_ASSERT(module);
}

ConstraintSet ConstraintGenerator::run(AstStatBlock* block)
{
    visitModuleRoot(block);

    if (FFlag::DebugLuauCyclicRequireTypeInference)
        return ConstraintSet{NotNull{rootScope}, {}, {}, DenseHashMap<Scope*, TypeId>{nullptr}, std::move(errors)};
    return ConstraintSet{NotNull{rootScope}, std::move(constraints), std::move(freeTypes), std::move(scopeToFunction), std::move(errors)};
}

ConstraintSet ConstraintGenerator::runOnFragment(const ScopePtr& resumeScope, AstStatBlock* block)
{
    visitFragmentRoot(resumeScope, block);

    if (FFlag::DebugLuauCyclicRequireTypeInference)
        return ConstraintSet{NotNull{rootScope}, {}, {}, DenseHashMap<Scope*, TypeId>{nullptr}, std::move(errors)};
    return ConstraintSet{NotNull{rootScope}, std::move(constraints), std::move(freeTypes), std::move(scopeToFunction), std::move(errors)};
}

void ConstraintGenerator::visitModuleRoot(AstStatBlock* block)
{
    LUAU_TIMETRACE_SCOPE("ConstraintGenerator::visitModuleRoot", "Typechecking");

    LUAU_ASSERT(scopes.empty());
    LUAU_ASSERT(rootScope == nullptr);
    ScopePtr scope = std::make_shared<Scope>(globalScope);
    rootScope = scope.get();
    scopes.emplace_back(block->location, scope);
    rootScope->location = block->location;
    module->astScopes[block] = NotNull{scope.get()};

    interiorFreeTypes.emplace_back();

    // Create module-local scope for the type function environment
    ScopePtr localTypeFunctionScope = std::make_shared<Scope>(typeFunctionScope);
    localTypeFunctionScope->location = block->location;
    typeFunctionRuntime->rootScope = localTypeFunctionScope;

    rootScope->returnType = freshTypePack(scope, Polarity::Positive);
    TypeId moduleFnTy = arena->addType(FunctionType{TypeLevel{}, builtinTypes->anyTypePack, rootScope->returnType});

    if (declaresFileGlobals())
        hoistDeclarations(block);

    prepopulateGlobalScope(scope, block);

    // A declared global has its declared type, so an assignment to it must not become its type the way an
    // assignment to an undeclared global does.
    for (const auto& [name, _] : hoistedDeclarations)
        uninitializedGlobals.erase(name);

    Checkpoint start = checkpoint(this);

    ControlFlow cf = visitBlockWithoutChildScope(scope, block);
    if (cf == ControlFlow::None)
        addConstraint(scope, block->location, PackSubtypeConstraint{builtinTypes->emptyTypePack, rootScope->returnType});

    Checkpoint end = checkpoint(this);

    TypeId result = arena->addType(BlockedType{});
    NotNull<Constraint> genConstraint = addConstraint(
        scope,
        block->location,
        GeneralizationConstraint{
            result,
            moduleFnTy,
            /*interiorTypes*/ std::vector<TypeId>{},
            /*hasDeprecatedAttribute*/ false,
            /*deprecatedInfo*/ {},
            /*noGenerics*/ true
        }
    );

    scope->interiorFreeTypes = std::move(interiorFreeTypes.back().types);
    scope->interiorFreeTypePacks = std::move(interiorFreeTypes.back().typePacks);

    getMutable<BlockedType>(result)->setOwner(genConstraint);

    addAllAsDependencies(start, end, this, genConstraint);

    interiorFreeTypes.pop_back();

    if (!FFlag::DebugLuauCFG)
        fillInInferredBindings(scope, block);

    if (logger)
        logger->captureGenerationModule(module);

    for (const auto& [ty, domain] : localTypes)
    {
        // FIXME: This isn't the most efficient thing.
        TypeId domainTy = builtinTypes->neverType;
        for (TypeId d : domain)
        {
            d = follow(d);
            if (d == ty)
                continue;
            domainTy = simplifyUnion(scope, Location{}, domainTy, d);
        }

        LUAU_ASSERT(get<BlockedType>(ty));
        asMutable(ty)->ty.emplace<BoundType>(domainTy);
    }

    for (TypeId ty : unionsToSimplify)
        addConstraint(scope, block->location, SimplifyConstraint{ty});
}

void ConstraintGenerator::visitFragmentRoot(const ScopePtr& resumeScope, AstStatBlock* block)
{
    // We prepopulate global data in the resumeScope to avoid writing data into the old modules scopes
    prepopulateGlobalScopeForFragmentTypecheck(globalScope, resumeScope, block);
    // Pre
    interiorFreeTypes.emplace_back();
    visitBlockWithoutChildScope(resumeScope, block);
    // Post
    interiorFreeTypes.pop_back();

    if (!FFlag::DebugLuauCFG)
        fillInInferredBindings(resumeScope, block);

    if (logger)
        logger->captureGenerationModule(module);

    for (const auto& [ty, domain] : localTypes)
    {
        // FIXME: This isn't the most efficient thing.
        TypeId domainTy = builtinTypes->neverType;
        for (TypeId d : domain)
        {
            d = follow(d);
            if (d == ty)
                continue;
            domainTy = simplifyUnion(resumeScope, resumeScope->location, domainTy, d);
        }

        LUAU_ASSERT(get<BlockedType>(ty));
        asMutable(ty)->ty.emplace<BoundType>(domainTy);
    }
}


TypeId ConstraintGenerator::freshType(const ScopePtr& scope, Polarity polarity)
{
    const TypeId ft = Luau::freshType(arena, builtinTypes, scope.get(), polarity);
    interiorFreeTypes.back().types.push_back(ft);
    if (FFlag::DebugLuauCyclicRequireTypeInference)
        cgraph->freeTypes.insert(ft);
    else
        freeTypes.insert(ft);
    return ft;
}

TypePackId ConstraintGenerator::freshTypePack(const ScopePtr& scope, Polarity polarity)
{
    FreeTypePack f{scope.get(), polarity};
    TypePackId result = arena->addTypePack(TypePackVar{std::move(f)});
    interiorFreeTypes.back().typePacks.push_back(result);
    return result;
}

TypePackId ConstraintGenerator::addTypePack(std::vector<TypeId> head, std::optional<TypePackId> tail)
{
    if (head.empty())
    {
        if (tail)
            return *tail;
        else
            return builtinTypes->emptyTypePack;
    }
    else
        return arena->addTypePack(TypePack{std::move(head), tail});
}

ScopePtr ConstraintGenerator::childScope(AstNode* node, const ScopePtr& parent)
{
    auto scope = std::make_shared<Scope>(parent);
    scopes.emplace_back(node->location, scope);
    scope->location = node->location;

    scope->returnType = parent->returnType;
    scope->varargPack = parent->varargPack;

    parent->children.emplace_back(scope.get());
    module->astScopes[node] = scope.get();

    return scope;
}

std::optional<TypeId> ConstraintGenerator::lookup(const ScopePtr& scope, Location location, DefId def, bool prototype)
{
    if (get<Cell>(def))
        return scope->lookup(def);
    if (auto phi = get<Phi>(def))
    {
        if (auto found = scope->lookup(def))
            return *found;
        else if (!prototype && phi->operands.size() == 1)
            return lookup(scope, location, phi->operands.at(0), prototype);
        else if (!prototype)
            return std::nullopt;

        TypeId res = builtinTypes->neverType;

        for (DefId operand : phi->operands)
        {
            // `scope->lookup(operand)` may return nothing because we only bind a type to that operand
            // once we've seen that particular `DefId`. In this case, we need to prototype those typeArguments
            // and use those at a later time.
            std::optional<TypeId> ty = lookup(scope, location, operand, /*prototype*/ false);
            if (!ty)
            {
                ty = arena->addType(BlockedType{});
                localTypes.try_insert(*ty, {});
                rootScope->lvalueTypes[operand] = *ty;
            }

            res = makeUnion(scope, location, res, *ty);
        }

        scope->lvalueTypes[def] = res;
        return res;
    }
    else
        ice->ice("ConstraintGenerator::lookup is inexhaustive?");
}

TypeId ConstraintGenerator::resolveRHSType(const ScopePtr& scope, Location location, AstExpr* expr)
{
    LUAU_ASSERT(FFlag::DebugLuauCFG);
    TypeId ty = typestate->getRHSType(expr);
    LUAU_ASSERT(ty);
    ty = follow(ty);
    if (auto c = typestate->getOptionalConstraint(ty))
    {
        auto oc = addConstraint(scope, location, std::move(*c));
        if (auto bt = getMutable<BlockedType>(ty))
            bt->setOwner(oc.get());
    }

    return ty;
}

TypeId ConstraintGenerator::resolveLHSType(const ScopePtr& scope, Location location, const CFG::LValue& lv)
{
    LUAU_ASSERT(FFlag::DebugLuauCFG);
    TypeId ty = typestate->getLHSType(lv);
    LUAU_ASSERT(ty);
    ty = follow(ty);
    if (auto c = typestate->getOptionalConstraint(ty))
    {
        auto oc = addConstraint(scope, location, std::move(*c));
        if (auto bt = getMutable<BlockedType>(ty))
            bt->setOwner(oc.get());
    }

    return ty;
}

NotNull<Constraint> ConstraintGenerator::addConstraint(const ScopePtr& scope, const Location& location, ConstraintV cv)
{
    if (FFlag::DebugLuauCyclicRequireTypeInference)
        return NotNull{cgraph->constraints.emplace_back(new Constraint{NotNull{scope.get()}, location, std::move(cv)}).get()};
    return NotNull{constraints.emplace_back(new Constraint{NotNull{scope.get()}, location, std::move(cv)}).get()};
}

NotNull<Constraint> ConstraintGenerator::addConstraint(const ScopePtr& scope, std::unique_ptr<Constraint> c)
{
    if (FFlag::DebugLuauCyclicRequireTypeInference)
        return NotNull{cgraph->constraints.emplace_back(std::move(c)).get()};
    return NotNull{constraints.emplace_back(std::move(c)).get()};
}

void ConstraintGenerator::unionRefinements(
    const ScopePtr& scope,
    Location location,
    const RefinementContext& lhs,
    const RefinementContext& rhs,
    RefinementContext& dest,
    std::vector<ConstraintV>* constraints
)
{
    const auto intersect = [&](const std::vector<TypeId>& types)
    {
        if (1 == types.size())
            return types[0];
        else if (2 == types.size())
            return makeIntersect(scope, location, types[0], types[1]);

        return arena->addType(IntersectionType{types});
    };

    for (auto& [def, partition] : lhs)
    {
        auto rhsIt = rhs.find(def);
        if (rhsIt == rhs.end())
            continue;

        LUAU_ASSERT(!partition.discriminantTypes.empty());
        LUAU_ASSERT(!rhsIt->second.discriminantTypes.empty());

        TypeId leftDiscriminantTy = partition.discriminantTypes.size() == 1 ? partition.discriminantTypes[0] : intersect(partition.discriminantTypes);

        TypeId rightDiscriminantTy =
            rhsIt->second.discriminantTypes.size() == 1 ? rhsIt->second.discriminantTypes[0] : intersect(rhsIt->second.discriminantTypes);

        dest.insert(def, {});
        dest.get(def)->discriminantTypes.push_back(makeUnion(scope, location, leftDiscriminantTy, rightDiscriminantTy));
        dest.get(def)->shouldAppendNilType |= partition.shouldAppendNilType || rhsIt->second.shouldAppendNilType;
    }
}

void ConstraintGenerator::computeRefinement(
    const ScopePtr& scope,
    Location location,
    RefinementId refinement,
    RefinementContext* refis,
    bool sense,
    bool eq,
    std::vector<ConstraintV>* constraints
)
{
    if (!refinement)
        return;
    else if (auto variadic = get<Variadic>(refinement))
    {
        for (RefinementId refi : variadic->refinements)
            computeRefinement(scope, location, refi, refis, sense, eq, constraints);
    }
    else if (auto negation = get<Negation>(refinement))
        return computeRefinement(scope, location, negation->refinement, refis, !sense, eq, constraints);
    else if (auto conjunction = get<Conjunction>(refinement))
    {
        RefinementContext lhsRefis;
        RefinementContext rhsRefis;

        computeRefinement(scope, location, conjunction->lhs, sense ? refis : &lhsRefis, sense, eq, constraints);
        computeRefinement(scope, location, conjunction->rhs, sense ? refis : &rhsRefis, sense, eq, constraints);

        if (!sense)
            unionRefinements(scope, location, lhsRefis, rhsRefis, *refis, constraints);
    }
    else if (auto disjunction = get<Disjunction>(refinement))
    {
        RefinementContext lhsRefis;
        RefinementContext rhsRefis;

        computeRefinement(scope, location, disjunction->lhs, sense ? &lhsRefis : refis, sense, eq, constraints);
        computeRefinement(scope, location, disjunction->rhs, sense ? &rhsRefis : refis, sense, eq, constraints);

        if (sense)
            unionRefinements(scope, location, lhsRefis, rhsRefis, *refis, constraints);
    }
    else if (auto equivalence = get<Equivalence>(refinement))
    {
        computeRefinement(scope, location, equivalence->lhs, refis, sense, true, constraints);
        computeRefinement(scope, location, equivalence->rhs, refis, sense, true, constraints);
    }
    else if (auto proposition = get<Proposition>(refinement))
    {
        TypeId discriminantTy = proposition->discriminantTy;

        // if we have a negative sense, then we need to negate the discriminant
        if (!sense && proposition->negativeDiscriminantTy)
        {
            // Luwu user-defined refinements: the solver decides it (see Proposition)
            discriminantTy = proposition->negativeDiscriminantTy;
        }
        else if (!sense)
        {
            if (auto nt = get<NegationType>(follow(discriminantTy)))
                discriminantTy = nt->ty;
            else
                discriminantTy = arena->addType(NegationType{discriminantTy});
        }

        if (eq)
            discriminantTy = createTypeFunctionInstance(builtinTypes->typeFunctions->singletonFunc, {discriminantTy}, {}, scope, location);

        for (const RefinementKey* key = proposition->key; key; key = key->parent)
        {
            refis->insert(key->def, {});
            refis->get(key->def)->discriminantTypes.push_back(discriminantTy);

            // Reached leaf node
            if (!key->propName)
                break;

            TypeId nextDiscriminantTy = arena->addType(TableType{});
            NotNull<TableType> table{getMutable<TableType>(nextDiscriminantTy)};
            table->props[*key->propName] = Property::readonly(discriminantTy);
            table->scope = scope.get();
            table->state = TableState::Sealed;

            discriminantTy = nextDiscriminantTy;
        }

        // When the top-level expression is `t[x]`, we want to refine it into `nil`, not `never`.
        LUAU_ASSERT(refis->get(proposition->key->def));
        refis->get(proposition->key->def)->shouldAppendNilType =
            (sense || !eq) && containsSubscriptedDefinition(proposition->key->def) && !proposition->implicitFromCall;
    }
}

namespace
{

/*
 * Constraint generation may be called upon to simplify an intersection or union
 * of typeArguments that are not sufficiently solved yet.  We use
 * FindSimplificationBlockers to recognize these typeArguments and defer the
 * simplification until constraint solution.
 */
struct FindSimplificationBlockers : IterativeTypeVisitor
{
    bool found = false;

    FindSimplificationBlockers()
        : IterativeTypeVisitor("FindSimplificationBlockers", /* skipBoundTypes */ true)
    {
    }

    bool visit(TypeId) override
    {
        return !found;
    }

    bool visit(TypeId, const BlockedType&) override
    {
        found = true;
        return false;
    }

    bool visit(TypeId, const FreeType&) override
    {
        found = true;
        return false;
    }

    bool visit(TypeId, const PendingExpansionType&) override
    {
        found = true;
        return false;
    }

    // We do not need to know anything at all about a function's argument or
    // return typeArguments in order to simplify it in an intersection or union.
    bool visit(TypeId, const FunctionType&) override
    {
        return false;
    }

    bool visit(TypeId, const ExternType&) override
    {
        return false;
    }
};

bool mustDeferIntersection(TypeId ty)
{
    FindSimplificationBlockers bts;
    bts.run(ty);
    return bts.found;
}
} // namespace

enum RefinementsOpKind
{
    Intersect,
    Refine,
    None
};

void ConstraintGenerator::applyRefinements(const ScopePtr& scope, Location location, RefinementId refinement)
{
    if (!refinement)
        return;

    RefinementContext refinements;
    std::vector<ConstraintV> constraints;
    computeRefinement(scope, location, refinement, &refinements, /*sense*/ true, /*eq*/ false, &constraints);
    auto flushConstraints = [this, &scope, &location](RefinementsOpKind kind, TypeId ty, std::vector<TypeId>& discriminants)
    {
        if (discriminants.empty())
            return ty;
        if (kind == RefinementsOpKind::None)
        {
            LUAU_ASSERT(false);
            return ty;
        }
        std::vector<TypeId> args = {ty};
        const TypeFunction& func =
            kind == RefinementsOpKind::Intersect ? builtinTypes->typeFunctions->intersectFunc : builtinTypes->typeFunctions->refineFunc;
        LUAU_ASSERT(!func.name.empty());
        args.insert(args.end(), discriminants.begin(), discriminants.end());
        TypeId resultType = createTypeFunctionInstance(func, std::move(args), {}, scope, location);
        discriminants.clear();
        return resultType;
    };

    for (auto& [def, partition] : refinements)
    {
        if (std::optional<TypeId> defTy = lookup(scope, location, def))
        {
            TypeId ty = *defTy;
            // Intersect ty with every discriminant type. If either type is not
            // sufficiently solved, we queue the intersection up via an
            // IntersectConstraint.
            // For each discriminant ty, we accumulated it onto ty, creating a longer and longer
            // sequence of refine constraints. On every loop of this we called mustDeferIntersection.
            // For sufficiently large typeArguments, we would blow the stack.
            // Instead, we record all the discriminant typeArguments in sequence
            // and then dispatch a single refine constraint with multiple arguments. This helps us avoid
            // the potentially expensive check on mustDeferIntersection
            std::vector<TypeId> discriminants;
            RefinementsOpKind kind = RefinementsOpKind::None;
            bool mustDefer = mustDeferIntersection(ty);
            for (TypeId dt : partition.discriminantTypes)
            {
                mustDefer = mustDefer || mustDeferIntersection(dt);
                if (mustDefer)
                {
                    if (kind == RefinementsOpKind::Intersect)
                        ty = flushConstraints(kind, ty, discriminants);
                    kind = RefinementsOpKind::Refine;

                    discriminants.push_back(dt);
                }
                else
                {
                    ErrorSuppression status = shouldSuppressErrors(normalizer, ty);
                    if (status == ErrorSuppression::NormalizationFailed)
                        reportError(location, NormalizationTooComplex{});
                    if (kind == RefinementsOpKind::Refine)
                        ty = flushConstraints(kind, ty, discriminants);
                    kind = RefinementsOpKind::Intersect;

                    discriminants.push_back(dt);

                    if (status == ErrorSuppression::Suppress)
                    {
                        ty = flushConstraints(kind, ty, discriminants);
                        ty = makeUnion(scope, location, ty, builtinTypes->errorType);
                    }
                }
            }

            // Finalize - if there are any discriminants left, make one big constraint for refining them
            if (kind != RefinementsOpKind::None)
                ty = flushConstraints(kind, ty, discriminants);

            if (partition.shouldAppendNilType)
                ty = createTypeFunctionInstance(builtinTypes->typeFunctions->weakoptionalFunc, {ty}, {}, scope, location);
            updateRValueRefinements(scope, def, ty);
        }
    }

    for (auto& c : constraints)
        addConstraint(scope, location, c);
}

/*
 * To support things like recursive and corecursive type aliases, we handle them
 * in two passes. First, we do a surface scan where we count generic arguments
 * and stub types in with BlockedTypes.  Later, we'll process the bodies of
 * these statements and actually work out how to expand them.  In the case of
 * class definitions, we'll run type inference on class methods during that
 * second pass.
 *
 * This function implements the early prototyping pass.  The main execution flow
 * of ConstraintGenerator handles the second pass.
 */
// Luwu Classes (rfcs/classes): the types of a class, created before any statement of its block is checked so that code
// above the class can refer to it. `declared` is a `declare class` (rfcs/declare-statements.md), which gets the same
// types and no value.
void ConstraintGenerator::prototypeClass(
    const ScopePtr& scope,
    AstStatClass* classDecl,
    DenseHashMap<Name, Location>& typeNameLocations,
    bool declared
)
{
    LUAU_ASSERT(FFlag::LuwuClasses);

    Name declName = classDecl->name->name.value;

    if (Location* loc = typeNameLocations.find(declName))
    {
        reportError(classDecl->location, DuplicateTypeDefinition{declName, *loc});
        if (!declared)
        {
            scope->bindings[classDecl->name->name] = Binding{builtinTypes->errorType, classDecl->location};
            scope->lvalueTypes[dfg->getDef(classDecl->name)] = builtinTypes->errorType;
        }
        return;
    }
    typeNameLocations[declName] = classDecl->location;

    // Luwu Declare Statements (rfcs/declare-statements.md): a declared class is a type and nothing else; its class
    // value is reached through `class<T>` (a cast, or a module's type).
    TypeId theTy = nullptr;
    if (!declared)
    {
        theTy = arena->addType(BlockedType{});
        scope->bindings[classDecl->name->name] = Binding{theTy, classDecl->name->location};
        scope->lvalueTypes[dfg->getDef(classDecl->name)] = theTy;
        classGlobalNames.insert(classDecl->name->name);
    }

    // Under LuwuGenericNominals, property and method type annotations are resolved
    // against the class's own definition scope, so that references to the class's own
    // generics (e.g. the `T` in `class Box<T> ... end`) resolve correctly. See the
    // equivalent handling for `declare extern type` above.
    ScopePtr defnScope = scope;
    if (FFlag::LuwuGenericNominals)
    {
        defnScope = childScope(classDecl, scope);
        astClassDefiningScopes[classDecl] = defnScope;
    }

    // Objects are ExternTypes, where the metatable field represents the metamethods associated with the instance, ** not ** the class itself.
    // Class: ExternType { props, parent: top class type, metatable: {__call -- this lets it be called as a constructor } }
    // Object: ExternType { props, parent: top object type for now, metatable: instance metamethods }
    // TODO: we should add a direct reference to the `class` on the `object` type (probably useful for class.of)
    TableType::Props staticProps;
    ExternType::Props props;
    TableType::Props instanceMetatableProps;
    DenseHashMap<AstName, TypeId> memberTypes{AstName{""}};
    DenseHashMap<AstName, TypeId> classValueMethodTypes{AstName{""}};
    const bool isGenericClass = FFlag::LuwuGenericNominals &&
                                (classDecl->generics.size != 0 || classDecl->genericPacks.size != 0);
    // Names of `props` entries that are actual fields (AstClassProperty), not methods.
    // See ClassFieldUserData's doc comment for why this needs tracking separately.
    std::set<Name> instanceFieldNames;

    TypeId ctorArgTy = arena->addType(TableType{TableType::Props{}, std::nullopt, TypeLevel{}, scope.get(), TableState::Sealed});
    TableType* ctorArgTable = getMutable<TableType>(ctorArgTy);
    LUAU_ASSERT(ctorArgTable);

    // Whether the whole constructor argument table can be omitted (`Class()`), i.e. every
    // property either has a default value or there are no properties at all.
    bool anyRequiredCtorArg = false;

    // Luwu Classes (rfcs/classes): a primary constructor's parameters each declare a field
    // (public and mutable unless qualified), unless the class body restates the parameter -- in which case the restatement is
    // the declaration, and carries the access specifier and modifiers. The constructor's own
    // type is built from the parameters in the second pass, once their annotations can be
    // resolved; see visit(AstStatClass*).
    auto restatedInBody = [&](const AstName& name)
    {
        for (const AstClassMember& member : classDecl->members)
            if (const AstClassProperty* prop = member.get_if<AstClassProperty>(); prop && prop->name == name)
                return true;

        return false;
    };

    for (const auto& member : classDecl->members)
    {
        Luau::visit(
            overloaded{
                [&](const AstClassProperty& classProp)
                {
                    if (memberTypes.contains(classProp.name))
                        return;

                    auto [propertyType, _] = memberTypes.try_insert(classProp.name, arena->addType(BlockedType{}));
                    auto& p = props[classProp.name.value];
                    instanceFieldNames.insert(classProp.name.value);

                    // This needs to be blocked initially: if this
                    // type refers to a type that contains a typeof
                    // or an alias that we have yet to define, then
                    // we'll ICE or misbehave.
                    p = Property::rw(propertyType);
                    p.location = classProp.nameLocation;

                    applyDeprecatedAttribute(p, classProp.attributes);
                    if (FFlag::LuwuClasses)
                    {
                        p.isPrivate = classProp.visibility == AstClassMemberVisibility::Private;
                        p.isFinal = classDecl->isTrait && classProp.finalLocation.has_value();
                        p.isConst = classProp.isConst || p.isFinal;
                    }

                    // We make the constructor take read-only args.
                    // This is true, in that we do not write to the
                    // table you pass for constructing an object.
                    //
                    // A property with a default value doesn't need to be provided by the
                    // caller (the default fills it in), so its key in the constructor's
                    // argument table is optional.
                    // A declared class's `name = T` has an `=` and no defaultValue.
                    if (classProp.defaultValue || classProp.equalsLocation)
                        ctorArgTable->props[classProp.name.value] =
                            Property::readonly(makeOption(builtinTypes, *arena, propertyType));
                    else
                    {
                        anyRequiredCtorArg = true;
                        ctorArgTable->props[classProp.name.value] = Property::readonly(propertyType);
                    }
                },
                [&](const AstClassMethod& method)
                {
                    if (memberTypes.contains(method.functionName))
                        return;

                    auto [propertyType, _] = memberTypes.try_insert(method.functionName, arena->addType(BlockedType{}));

                    auto prop = Property::readonly(propertyType);
                    prop.location = method.nameLocation;
                    if (FFlag::LuwuClasses)
                        prop.isPrivate = method.visibility == AstClassMemberVisibility::Private;
                    // Luwu Classes (rfcs/classes): an instance method is also readable through the
                    // class value, with the same type (`self` is the object type): `Cls.method(obj)`
                    // and `Cls.method` as a value are how it is called without method-call syntax.
                    // A metamethod stays on the instance metatable only.
                    const bool takesSelf = method.function->args.size >= 1 && method.function->args.data[0]->name == "self";
                    const bool isMetamethod = isValidClassMetamethod(method.functionName.value);
                    // Luwu Traits (rfcs/classes/traits.md): a trait's expected function only exists in implementing classes
                    const bool readableThroughClass = (!takesSelf || method.functionName == "__init" || !isMetamethod) && !method.expectLocation;
                    // Luwu Generic Nominals (rfcs/generics-on-extern-types.md): nothing instantiates a generic class's generics
                    // when a method is read through the class value, so it gets a type of its own that is generic over them
                    // (quantifyOverClassGenerics). A static (`function make(v: T): Box<T>`) needs it as much as a method does.
                    const bool needsOwnClassValueType = readableThroughClass && isGenericClass && method.functionName != "__init";
                    if (needsOwnClassValueType)
                    {
                        Property classValueProp = prop;
                        TypeId classValueTy = arena->addType(BlockedType{});
                        classValueProp.readTy = classValueTy;
                        classValueMethodTypes[method.functionName] = classValueTy;
                        staticProps[method.functionName.value] = classValueProp;
                    }
                    else if (readableThroughClass)
                        staticProps[method.functionName.value] = prop;
                    // The parser will report an error for classes that define disallowed metamethods.
                    // The RFC also requires that it is a syntax error for methods to have __ in their name whos name is not in the
                    // validClassMetamethod set.
                    if (isMetamethod)
                        instanceMetatableProps[method.functionName.value] = prop;
                    else
                        props[method.functionName.value] = prop;
                }
            },
            member
        );
    }

    if (classDecl->primaryConstructor)
    {
        for (size_t i = 0; i < classDecl->primaryConstructor->args.size; ++i)
        {
            AstLocal* param = classDecl->primaryConstructor->args.data[i];

            if (memberTypes.contains(param->name) || restatedInBody(param->name))
                continue;

            auto [propertyType, _] = memberTypes.try_insert(param->name, arena->addType(BlockedType{}));
            instanceFieldNames.insert(param->name.value);

            // a parameter's field is public and non-const unless the parameter says otherwise
            // (`class SshKey(private const key: string)`); restating it in the class body is
            // the other way to say the same thing
            auto& p = props[param->name.value];
            p = Property::rw(propertyType);
            p.location = param->location;

            if (FFlag::LuwuClasses &&
                classDecl->primaryConstructor->argsQualifiers.size == classDecl->primaryConstructor->args.size)
            {
                const AstClassPrimaryConstructorParamQualifiers& qualifiers = classDecl->primaryConstructor->argsQualifiers.data[i];
                p.isPrivate = qualifiers.visibility == AstClassMemberVisibility::Private;
                p.isConst = qualifiers.isConst;
            }
        }
    }

    TypeId instanceMetatable = arena->addType(TableType{instanceMetatableProps, std::nullopt, TypeLevel{}, scope.get(), TableState::Sealed});

    auto classFieldUserData = std::make_shared<ClassFieldUserData>();
    classFieldUserData->fieldNames = std::move(instanceFieldNames);

    TypeId classInstanceTy = arena->addType(
        ExternType{
            declName,
            std::move(props),
            builtinTypes->objectType,
            instanceMetatable,
            Tags{},
            std::move(classFieldUserData),
            module->name,
            classDecl->location
        }
    );

    // Luwu Traits (rfcs/classes/traits.md): what implementing classes need to know about the trait (see implementTraits)
    if (classDecl->isTrait)
    {
        ExternType::TraitInfo info;
        info.hasParameters = classDecl->primaryConstructor && classDecl->primaryConstructor->args.size > 0;

        if (const AstClassPrimaryConstructor* params = classDecl->primaryConstructor)
        {
            for (size_t i = 0; i < params->args.size; ++i)
                info.parameters.push_back({params->args.data[i]->name.value, params->argsDefaults.data[i] != nullptr});
        }

        for (const AstClassMember& member : classDecl->members)
        {
            if (const AstClassProperty* prop = member.get_if<AstClassProperty>(); prop && prop->expectLocation)
                info.expectations[prop->name.value] = false;
            else if (const AstClassMethod* method = member.get_if<AstClassMethod>(); method && method->expectLocation)
                info.expectations[method->functionName.value] = method->isOptional;
            else if (method && method->finalLocation)
                info.finals.insert(method->functionName.value);
        }

        getMutable<ExternType>(classInstanceTy)->traitInfo = std::move(info);
    }

    std::vector<GenericTypeDefinition> classTypeParams;
    std::vector<GenericTypePackDefinition> classTypePackParams;
    if (FFlag::LuwuGenericNominals)
    {
        // Not from the cache: it is keyed by name in the enclosing scope, so `class Box<T>` would get the `T` of an earlier
        // `trait Holder<T>` or `class List<T>`. That `T`'s scope is the other declaration's, and subtyping only treats a
        // generic as itself inside its own scope, so `T <: T?` would fail in Box's body.
        for (const auto& [name, gen] : createGenerics(defnScope, classDecl->generics, /* useCache */ false, /* addTypes */ false))
            classTypeParams.push_back(gen);
        for (const auto& [name, genPack] : createGenericPacks(defnScope, classDecl->genericPacks, /* useCache */ false, /* addTypes */ false))
            classTypePackParams.push_back(genPack);

        // Methods implicitly take `self`, typed as the bare object type
        // (classInstanceTy). For a generic class, `self` needs to be `Box<T>` (applied to
        // the class's own generics), not the bare, unparameterized `Box` -- otherwise
        // calling a method on `Box<number>` fails to match against `self`, since neither
        // side would look like the other nominally. See the equivalent handling for
        // `declare extern type` above.
        ExternType* classInstanceEtv = getMutable<ExternType>(classInstanceTy);
        for (const GenericTypeDefinition& param : classTypeParams)
            classInstanceEtv->instantiatedTypeParams.push_back(param.ty);
        for (const GenericTypePackDefinition& param : classTypePackParams)
            classInstanceEtv->instantiatedTypePackParams.push_back(param.tp);
        classInstanceEtv->hasUnresolvedGenerics =
            !classInstanceEtv->instantiatedTypeParams.empty() || !classInstanceEtv->instantiatedTypePackParams.empty();
    }

    // A primary constructor counts here too: its `__init` is synthesized from the parameter
    // list, so neither it nor the constructor is the POD table constructor below.
    bool hasCustomInit = classDecl->primaryConstructor != nullptr;
    for (const auto& member : classDecl->members)
    {
        if (const auto* method = member.get_if<AstClassMethod>(); method && method->functionName == "__init")
        {
            hasCustomInit = true;
            break;
        }
    }

    // Blocked alongside ctorTy, and filled in with it once the parameters resolve.
    TypeId primaryInitTy = nullptr;

    // Luwu Traits (rfcs/classes/traits.md): a trait's parameters are its `implements` arguments, not a constructor
    if (classDecl->primaryConstructor && !classDecl->isTrait)
    {
        primaryInitTy = arena->addType(BlockedType{});

        Property initProp = Property::readonly(primaryInitTy);
        initProp.location = classDecl->primaryConstructor->argLocation;
        initProp.isPrivate = classDecl->primaryConstructor->visibility == AstClassMemberVisibility::Private;

        if (ExternType* classInstanceEtv = getMutable<ExternType>(classInstanceTy))
            classInstanceEtv->props()["__init"] = initProp;
        staticProps["__init"] = initProp;
    }

    // If the class defines a custom `__init` or a primary constructor,
    // the constructor's real signature isn't known until `__init`'s
    // signature or the parameters' annotations have been resolved (see
    // visit(AstStatClass*)), so we leave this blocked for now.
    // Luwu Traits (rfcs/classes/traits.md): a trait is only callable when it defines `__create`, a factory whose type is known once its signature
    // is (visitClass)
    bool traitHasCreate = false;
    if (classDecl->isTrait)
    {
        for (const auto& member : classDecl->members)
        {
            if (const auto* method = member.get_if<AstClassMethod>(); method && method->functionName == "__create" && !method->expectLocation)
                traitHasCreate = true;
        }
    }

    TypeId ctorTy = nullptr;
    if (classDecl->isTrait)
    {
        if (traitHasCreate)
            ctorTy = arena->addType(BlockedType{});
    }
    else if (hasCustomInit)
    {
        ctorTy = arena->addType(BlockedType{});
    }
    else
    {
        std::vector<TypeId> podCtorGenerics;
        std::vector<TypePackId> podCtorGenericPacks;
        for (const GenericTypeDefinition& param : classTypeParams)
            podCtorGenerics.push_back(param.ty);
        for (const GenericTypePackDefinition& param : classTypePackParams)
            podCtorGenericPacks.push_back(param.tp);

        // Classes with no members, or where every property has a default value, can be
        // constructed either with no arguments (`Empty()`) or with an argument table
        // (`Empty {}`); make the argument optional so both call shapes typecheck.
        TypeId ctorArgTyForCall = !anyRequiredCtorArg ? makeOption(builtinTypes, *arena, ctorArgTy) : ctorArgTy;
        TypePackId ctorArgsPack = arena->addTypePack({builtinTypes->unknownType, ctorArgTyForCall});

        ctorTy = arena->addType(FunctionType{
            podCtorGenerics,
            podCtorGenericPacks,
            ctorArgsPack,
            arena->addTypePack({classInstanceTy}),
            /* defn */ std::nullopt,
            /* hasSelf */ true
        });

        // The default POD constructor is a real function like any other, so it should be
        // directly callable as `object:__init(...)`/`Class.__init(...)`, just like a
        // user-defined `__init` is.
        TypeId initTy = arena->addType(FunctionType{
            std::move(podCtorGenerics),
            std::move(podCtorGenericPacks),
            arena->addTypePack({classInstanceTy, ctorArgTyForCall}),
            arena->addTypePack({}),
            /* defn */ std::nullopt,
            /* hasSelf */ true
        });
        Property initProp = Property::readonly(initTy);
        initProp.location = classDecl->location;
        if (ExternType* classInstanceEtv = getMutable<ExternType>(classInstanceTy))
            classInstanceEtv->props()["__init"] = initProp;
        staticProps["__init"] = initProp;
    }

    TableType::Props classValueMetatableProps;
    if (ctorTy)
        classValueMetatableProps["__call"] = Property::readonly(ctorTy);

    TypeId metatableTy =
        arena->addType(TableType{std::move(classValueMetatableProps), std::nullopt, TypeLevel{}, scope.get(), TableState::Sealed});

    // Luwu Traits (rfcs/classes/traits.md): a trait's value is a `trait`, not a `class`
    TypeId valueRoot = classDecl->isTrait ? builtinTypes->traitType : builtinTypes->classType;
    TypeId externTy =
        arena->addType(ExternType{declName, staticProps, valueRoot, metatableTy, Tags{}, nullptr, module->name, classDecl->location});

    // Setup a bidirectional relationship between classes and objects
    getMutable<ExternType>(externTy)->relation.emplace(Obj{classInstanceTy});
    getMutable<ExternType>(classInstanceTy)->relation.emplace(Klass{externTy});

    // Luwu Traits (rfcs/classes/traits.md): `class<Trait>`, the class value of any class implementing the trait (see TraitInfo). It has the trait's
    // functions, expected ones included since every implementing class defines them, and is callable when the trait
    // expects `__init` (typed in visitClass, once that signature is resolved). Its name differs from the trait value's,
    // which is also a class-rooted extern type declared at the same place, so the two are never the same nominal type.
    if (classDecl->isTrait)
    {
        ExternType::Props implementorProps;
        for (const auto& [name, prop] : staticProps)
        {
            if (name != "__init" && name != "__create")
                implementorProps[name] = prop;
        }

        bool expectsInit = false;
        for (const AstClassMember& member : classDecl->members)
        {
            const AstClassMethod* method = member.get_if<AstClassMethod>();
            if (!method || !method->expectLocation)
                continue;

            if (method->functionName == "__init")
            {
                expectsInit = true;
                continue;
            }

            if (TypeId* methodTy = memberTypes.find(method->functionName))
            {
                Property prop = Property::readonly(*methodTy);
                prop.location = method->nameLocation;
                prop.isPrivate = method->visibility == AstClassMemberVisibility::Private;
                implementorProps[method->functionName.value] = prop;
            }
        }

        TableType::Props implementorMetatableProps;
        if (expectsInit)
            implementorMetatableProps["__call"] = Property::readonly(arena->addType(BlockedType{}));

        TypeId implementorMetatable =
            arena->addType(TableType{std::move(implementorMetatableProps), std::nullopt, TypeLevel{}, scope.get(), TableState::Sealed});

        TypeId implementorTy = arena->addType(
            ExternType{
                "class<" + declName + ">",
                std::move(implementorProps),
                builtinTypes->classType,
                implementorMetatable,
                Tags{},
                nullptr,
                module->name,
                classDecl->location
            }
        );

        getMutable<ExternType>(classInstanceTy)->traitInfo->implementorClass = implementorTy;
    }

    if (theTy)
    {
        LUAU_ASSERT(!is<BoundType>(theTy));
        [[maybe_unused]] const BlockedType* bt = get<BlockedType>(theTy);
        LUAU_ASSERT(bt);
        LUAU_ASSERT(bt->getOwner() == nullptr);

        emplaceType<BoundType>(asMutable(theTy), externTy);
    }

    // A class in a definition file is a global type, like everything else declared there.
    bool exported = classDecl->exported || (declared && !declaresFileGlobals());
    if (exported)
        scope->exportedTypeBindings[classDecl->name->name.value] =
            TypeFun{classTypeParams, classTypePackParams, classInstanceTy, classDecl->location};
    else
        scope->privateTypeBindings[classDecl->name->name.value] =
            TypeFun{classTypeParams, classTypePackParams, classInstanceTy, classDecl->location};

    classDeclRecords[classDecl->name] = std::make_unique<ClassDeclRecord>(
        ClassDeclRecord{
            classInstanceTy,
            std::move(memberTypes),
            ctorTy,
            primaryInitTy,
            std::move(classTypeParams),
            std::move(classTypePackParams),
            std::move(classValueMethodTypes)
        }
    );
}

// A trait named by an `implements` or `needs` entry, through the type it declares: `Trait` in this module, or
// `mod.Trait` from a module this one requires. Returns the trait's object type, or nullptr when the entry names
// nothing known to be a trait.
std::optional<ConstraintGenerator::TraitInstantiation> ConstraintGenerator::instantiateTraitRef(const ScopePtr& scope, const AstClassTraitRef& ref)
{
    if (!FFlag::LuwuGenericNominals || ref.typeArguments.size == 0)
        return std::nullopt;

    std::optional<TypeFun> traitFun = scope->lookupTraitRef(ref);
    if (!traitFun || traitFun->typeParams.empty())
        return std::nullopt;

    // the type reference `ref` spells, as `resolveType` would read it
    std::optional<AstName> prefix;
    AstName name;
    if (AstExprGlobal* global = ref.trait->as<AstExprGlobal>())
        name = global->name;
    else if (AstExprIndexName* index = ref.trait->as<AstExprIndexName>())
    {
        name = index->index;
        if (AstExprLocal* moduleLocal = index->expr->as<AstExprLocal>())
            prefix = moduleLocal->local->name;
        else if (AstExprGlobal* moduleGlobal = index->expr->as<AstExprGlobal>())
            prefix = moduleGlobal->name;
        else
            return std::nullopt;
    }
    else
        return std::nullopt;

    TraitInstantiation result;
    for (const AstTypeOrPack& arg : ref.typeArguments)
    {
        if (!arg.type)
            return std::nullopt;

        result.args.push_back(resolveType_(scope, arg.type, /* inTypeArguments */ true));
    }

    // missing arguments take the trait's defaults
    for (size_t i = 0; i < traitFun->typeParams.size(); ++i)
    {
        result.params.push_back(traitFun->typeParams[i].ty);

        if (i >= result.args.size())
            result.args.push_back(traitFun->typeParams[i].defaultValue.value_or(builtinTypes->unknownType));
    }

    result.args.resize(result.params.size());

    result.instantiated = arena->addType(PendingExpansionType{prefix, name, result.args, {}});
    addConstraint(scope, ref.location, TypeAliasExpansionConstraint{/* target */ result.instantiated});
    return result;
}

ConstraintGenerator::TraitInstantiation ConstraintGenerator::instantiateImpliedTrait(
    const ScopePtr& scope,
    Location location,
    TypeId trait,
    std::vector<TypeId> args,
    std::vector<TypeId> copiedReferences
)
{
    const ExternType* traitType = get<ExternType>(follow(trait));
    LUAU_ASSERT(traitType);

    TraitInstantiation result;
    result.params = traitType->instantiatedTypeParams;
    result.args = args;
    result.instantiated = instantiateGenericNominal(arena, trait, std::move(args), {}, copiedReferences);

    for (TypeId reference : copiedReferences)
        addConstraint(scope, location, TypeAliasExpansionConstraint{reference});

    return result;
}

TypeId ConstraintGenerator::instantiateTraitMember(
    const ScopePtr& scope,
    Location location,
    TypeId trait,
    const TraitInstantiation& instantiation,
    TypeId ty
)
{
    TypeId placeholder = arena->addType(BlockedType{});
    NotNull<Constraint> constraint = addConstraint(
        scope,
        location,
        InstantiateNominalPropConstraint{ty, placeholder, trait, instantiation.instantiated, instantiation.params, instantiation.args, {}, {}}
    );
    getMutable<BlockedType>(placeholder)->setOwner(constraint);
    return placeholder;
}

Property ConstraintGenerator::instantiateTraitProperty(
    const ScopePtr& scope,
    Location location,
    TypeId trait,
    const TraitInstantiation& instantiation,
    const Property& prop
)
{
    Property result = prop;
    if (prop.isShared())
    {
        result.readTy = instantiateTraitMember(scope, location, trait, instantiation, *prop.readTy);
        result.writeTy = result.readTy;
        return result;
    }

    if (prop.readTy)
        result.readTy = instantiateTraitMember(scope, location, trait, instantiation, *prop.readTy);
    if (prop.writeTy)
        result.writeTy = instantiateTraitMember(scope, location, trait, instantiation, *prop.writeTy);
    return result;
}

TypeId ConstraintGenerator::resolveTraitRef(const ScopePtr& scope, const AstClassTraitRef& ref)
{
    std::optional<TypeFun> traitFun = scope->lookupTraitRef(ref);
    if (!traitFun)
        return nullptr;

    TypeId traitTy = follow(traitFun->type);
    const ExternType* traitType = get<ExternType>(traitTy);
    return traitType && traitType->traitInfo ? traitTy : nullptr;
}

// Luwu Traits (rfcs/classes/traits.md): "Traits 'A', 'B' and 'C' are codependent. ..." for traits that need each
// other, in the order the cycle goes. luaR_checkneedscycles words the runtime's error the same way.
static std::string codependentTraitsMessage(const std::vector<TypeId>& cycle)
{
    std::string names;
    for (size_t i = 0; i < cycle.size(); ++i)
    {
        if (i + 1 == cycle.size())
            names += " and ";
        else if (i > 0)
            names += ", ";

        // only traits are linked by `needs` (linkTraitNeeds)
        const ExternType* traitType = get<ExternType>(follow(cycle[i]));
        LUAU_ASSERT(traitType);
        names += "'" + traitType->name + "'";
    }

    return "Traits " + names +
           " are codependent. This is an unhealthy relationship; consider merging these traits or factoring out common members "
           "into a new trait";
}

void ConstraintGenerator::recordNeededTypeArguments(
    AstStatClass* trait,
    const AstClassTraitRef& ref,
    TypeId needed,
    ExternType* traitType
)
{
    std::unique_ptr<ClassDeclRecord>* record = classDeclRecords.find(trait->name);
    ScopePtr* defnScope = astClassDefiningScopes.find(trait);
    if (!record || !defnScope)
        return;

    // The type arguments are written in the trait's own generics. The trait's body binds those only after resolving their
    // defaults, so this scope binds them for the entry alone. It is registered under the entry's location, so it encloses
    // nothing else.
    ScopePtr refScope = std::make_shared<Scope>(*defnScope);
    refScope->location = ref.location;
    scopes.emplace_back(ref.location, refScope);
    (*defnScope)->children.emplace_back(refScope.get());

    const std::vector<GenericTypeDefinition>& typeParams = (*record)->typeParams;
    for (size_t i = 0; i < trait->generics.size && i < typeParams.size(); ++i)
        refScope->privateTypeBindings[trait->generics.data[i]->name.value] = TypeFun{typeParams[i].ty};

    const std::vector<GenericTypePackDefinition>& typePackParams = (*record)->typePackParams;
    for (size_t i = 0; i < trait->genericPacks.size && i < typePackParams.size(); ++i)
        refScope->privateTypePackBindings[trait->genericPacks.data[i]->name.value] = typePackParams[i].tp;

    if (std::optional<TraitInstantiation> instantiation = instantiateTraitRef(refScope, ref))
        traitType->traitInfo->neededTypeArguments.emplace_back(needed, std::move(instantiation->args));
}

void ConstraintGenerator::linkTraitNeeds(const ScopePtr& scope, const AstArray<AstStat*>& statements)
{
    for (AstStat* stat : statements)
    {
        AstStatClass* trait = stat->as<AstStatClass>();
        if (!trait || !trait->isTrait || trait->needs.size == 0)
            continue;

        std::unique_ptr<ClassDeclRecord>* record = classDeclRecords.find(trait->name);
        ExternType* traitType = record ? getMutable<ExternType>(follow((*record)->ty)) : nullptr;
        if (!traitType)
            continue;

        for (const AstClassTraitRef& ref : trait->needs)
        {
            TypeId needed = resolveTraitRef(scope, ref);
            if (!needed)
                continue;

            traitType->implementedTraits.push_back(needed);

            if (traitType->traitInfo && ref.typeArguments.size > 0)
                recordNeededTypeArguments(trait, ref, needed, traitType);
        }
    }

    // A trait lists a trait it needs with type arguments as an instantiation in its own generics (`Base<T>` for
    // `needs Base<T>`), so its own instantiations are subtypes of the needed trait's (`Mut<string>` of `Base<string>`). An
    // instantiation copies its template's list when it is made, so a needed trait's list is instantiated before the list
    // of a trait needing it. Traits from elsewhere were linked when their own block was.
    DenseHashMap<const ExternType*, AstStatClass*> traitDecls{nullptr};
    for (AstStat* stat : statements)
    {
        AstStatClass* trait = stat->as<AstStatClass>();
        std::unique_ptr<ClassDeclRecord>* record = trait && trait->isTrait ? classDeclRecords.find(trait->name) : nullptr;
        if (const ExternType* traitType = record ? get<ExternType>(follow((*record)->ty)) : nullptr)
            traitDecls[traitType] = trait;
    }

    DenseHashSet<const ExternType*> needsInstantiated{nullptr};
    std::function<void(ExternType*)> instantiateNeeds = [&](ExternType* traitType)
    {
        AstStatClass** trait = traitType ? traitDecls.find(traitType) : nullptr;
        if (!trait || needsInstantiated.contains(traitType))
            return;

        needsInstantiated.insert(traitType);

        for (TypeId& needed : traitType->implementedTraits)
        {
            instantiateNeeds(getMutable<ExternType>(follow(needed)));

            std::vector<TypeId> copiedReferences;
            std::optional<std::vector<TypeId>> args = neededTraitTypeArguments(arena, *traitType, needed, {}, copiedReferences);
            if (args)
            {
                Location location = (*trait)->name->location;
                needed = instantiateImpliedTrait(scope, location, needed, std::move(*args), std::move(copiedReferences)).instantiated;
            }
        }
    };

    for (AstStat* stat : statements)
    {
        AstStatClass* trait = stat->as<AstStatClass>();
        std::unique_ptr<ClassDeclRecord>* record = trait && trait->isTrait ? classDeclRecords.find(trait->name) : nullptr;
        if (record)
            instantiateNeeds(getMutable<ExternType>(follow((*record)->ty)));
    }

    // A trait-typed value has the members of the traits it needs, metamethods included, which every implementing class
    // implements too. Each needed trait is visited once, however the `needs` graph branches or loops.
    for (AstStat* stat : statements)
    {
        AstStatClass* trait = stat->as<AstStatClass>();
        std::unique_ptr<ClassDeclRecord>* record = trait && trait->isTrait ? classDeclRecords.find(trait->name) : nullptr;
        ExternType* traitType = record ? getMutable<ExternType>(follow((*record)->ty)) : nullptr;
        if (!traitType || !traitType->traitInfo || traitType->implementedTraits.empty())
            continue;

        TableType* traitMetatable = traitType->metatable ? getMutable<TableType>(follow(*traitType->metatable)) : nullptr;
        Location location = trait->name->location;

        // A needed trait, with the type arguments its `needs` entry gives it, in this trait's generics (`needs Base<T>`)
        struct Needed
        {
            TypeId ty;
            std::optional<std::vector<TypeId>> args;
            std::vector<TypeId> copiedReferences;
        };

        std::vector<Needed> pending;
        DenseHashSet<const ExternType*> visited{nullptr};
        visited.insert(traitType);

        // `from`'s needs. `fromArgs` are `from`'s own type arguments in this trait's generics, substituted into its entries.
        auto pushNeeds = [&](const ExternType* from, const std::optional<std::vector<TypeId>>& fromArgs)
        {
            for (TypeId listed : from->implementedTraits)
            {
                TypeId neededTy = traitTemplateOf(listed);
                const ExternType* neededType = get<ExternType>(neededTy);
                if (!neededType || visited.contains(neededType))
                    continue;

                visited.insert(neededType);

                Needed needed{neededTy, std::nullopt, {}};
                needed.args = neededTraitTypeArguments(arena, *from, neededTy, fromArgs.value_or(std::vector<TypeId>{}), needed.copiedReferences);
                pending.push_back(std::move(needed));
            }
        };

        pushNeeds(traitType, std::nullopt);

        while (!pending.empty())
        {
            Needed needed = std::move(pending.back());
            pending.pop_back();

            const ExternType* neededType = get<ExternType>(needed.ty);
            LUAU_ASSERT(neededType);

            // `trait Mut<T> needs Base<T>`: Mut's copy of Base's `first(self): T?` returns Mut's `T`
            std::optional<TraitInstantiation> instantiation;
            if (needed.args)
                instantiation = instantiateImpliedTrait(scope, location, needed.ty, *needed.args, std::move(needed.copiedReferences));

            auto asSeenHere = [&](const Property& prop)
            {
                return instantiation ? instantiateTraitProperty(scope, location, needed.ty, *instantiation, prop) : prop;
            };

            for (const auto& [name, prop] : neededType->props())
            {
                bool copyable = name != "__init" && name != "__create" && !traitType->props().count(name);
                if (copyable)
                {
                    traitType->props()[name] = asSeenHere(prop);
                    traitType->traitInfo->fromNeeds.insert(name);
                }
            }

            const TableType* neededMetatable = neededType->metatable ? get<TableType>(follow(*neededType->metatable)) : nullptr;
            if (traitMetatable && neededMetatable)
            {
                for (const auto& [name, prop] : neededMetatable->props)
                {
                    if (traitMetatable->props.count(name))
                        continue;

                    traitMetatable->props[name] = asSeenHere(prop);
                    traitType->traitInfo->fromNeeds.insert(name);
                }
            }

            pushNeeds(neededType, needed.args);
        }
    }

    // Traits that need each other are always implemented together, so they should be one trait (the runtime raises too).
    // The traits on the shortest path of `needs` from `from` to `target`, both included; empty when there is none. A
    // breadth-first search with a visited set, so cycles elsewhere in the graph end it.
    auto needsPath = [](TypeId from, TypeId target)
    {
        // each trait reached, with the index of the trait it was reached from
        const size_t noParent = ~size_t(0);
        std::vector<std::pair<TypeId, size_t>> reached{{follow(from), noParent}};
        DenseHashSet<TypeId> visited{nullptr};
        visited.insert(follow(from));

        for (size_t i = 0; i < reached.size(); ++i)
        {
            TypeId current = reached[i].first;

            if (current == target)
            {
                std::vector<TypeId> path;
                for (size_t at = i; at != noParent; at = reached[at].second)
                    path.push_back(reached[at].first);
                std::reverse(path.begin(), path.end());
                return path;
            }

            const ExternType* currentType = get<ExternType>(current);
            if (!currentType)
                continue;

            for (TypeId listed : currentType->implementedTraits)
            {
                TypeId next = traitTemplateOf(listed);
                if (visited.contains(next))
                    continue;

                visited.insert(next);
                reached.emplace_back(next, i);
            }
        }

        return std::vector<TypeId>{};
    };

    for (AstStat* stat : statements)
    {
        AstStatClass* trait = stat->as<AstStatClass>();
        if (!trait || !trait->isTrait || trait->needs.size == 0)
            continue;

        std::unique_ptr<ClassDeclRecord>* record = classDeclRecords.find(trait->name);
        if (!record)
            continue;

        TypeId traitTy = follow((*record)->ty);

        for (const AstClassTraitRef& ref : trait->needs)
        {
            TypeId needed = resolveTraitRef(scope, ref);
            if (!needed)
                continue;

            if (needed == traitTy)
            {
                reportError(ref.trait->location, GenericError{format("Trait '%s' needs itself", trait->name->name.value)});
                continue;
            }

            // this trait, then the traits from `needed` back around to it
            std::vector<TypeId> cycle = needsPath(needed, traitTy);
            if (cycle.empty())
                continue;

            cycle.pop_back();
            cycle.insert(cycle.begin(), traitTy);

            reportError(ref.trait->location, GenericError{codependentTraitsMessage(cycle)});
        }
    }
}

void ConstraintGenerator::bindTraitImplementorConstructor(ClassDeclRecord* traitRecord, TypeId initSignature)
{
    const ExternType* traitType = get<ExternType>(follow(traitRecord->ty));
    const FunctionType* init = get<FunctionType>(follow(initSignature));
    if (!traitType || !traitType->traitInfo || !traitType->traitInfo->implementorClass || !init)
        return;

    const ExternType* implementor = get<ExternType>(follow(*traitType->traitInfo->implementorClass));
    const TableType* metatable = implementor && implementor->metatable ? get<TableType>(follow(*implementor->metatable)) : nullptr;
    auto call = metatable ? metatable->props.find("__call") : TableType::Props::const_iterator{};
    bool callIsBlocked = metatable && call != metatable->props.end() && call->second.readTy && is<BlockedType>(follow(*call->second.readTy));
    if (!callIsBlocked)
        return;

    // `__init(self, ...)` becomes `(class, ...) -> Trait`, like a class's own constructor
    auto [argHead, argTail] = flatten(init->argTypes);
    std::vector<TypeId> ctorArgs{builtinTypes->unknownType};
    if (argHead.size() > 1)
        ctorArgs.insert(ctorArgs.end(), argHead.begin() + 1, argHead.end());

    TypePackId ctorArgsPack = argTail ? arena->addTypePack(std::move(ctorArgs), *argTail) : arena->addTypePack(std::move(ctorArgs));
    TypeId ctor = arena->addType(FunctionType{ctorArgsPack, arena->addTypePack({traitRecord->ty}), /* defn */ std::nullopt, /* hasSelf */ true});

    if (FunctionType* ctorFtv = getMutable<FunctionType>(ctor))
        ctorFtv->argNames = init->argNames;

    emplaceType<BoundType>(asMutable(follow(*call->second.readTy)), ctor);
}

// The checks mirror luaR_implementtraits's, which raise at runtime; each error underlines the `implements` entry of the
// trait it is about (for a trait implied through `needs`, the entry that implied it).
void ConstraintGenerator::checkTraitArguments(const ScopePtr& initializerScope, AstStatClass* cls)
{
    for (const AstClassTraitRef& ref : cls->implements)
    {
        TypeId trait = resolveTraitRef(initializerScope, ref);
        const ExternType* traitType = trait ? get<ExternType>(trait) : nullptr;
        const ExternType::TraitInfo* info = traitType ? &*traitType->traitInfo : nullptr;

        if (!info)
        {
            for (AstExpr* arg : ref.args)
                check(initializerScope, arg);
            continue;
        }

        const std::vector<ExternType::TraitInfo::Parameter>& params = info->parameters;

        size_t required = 0;
        for (const ExternType::TraitInfo::Parameter& param : params)
            if (!param.hasDefault)
                required++;

        if (!ref.hasArgs && required > 0)
        {
            std::string signature = traitType->name + "(";
            for (size_t i = 0; i < params.size(); ++i)
                signature += (i > 0 ? ", " : "") + params[i].name;
            signature += ")";

            reportError(ref.trait->location, GenericError{format("This trait must be called: '%s'", signature.c_str())});
        }
        else if (ref.hasArgs && (ref.args.size < required || ref.args.size > params.size()))
        {
            std::string expected = required == params.size() ? std::to_string(required) : format("%zu to %zu", required, params.size());
            reportError(
                ref.location,
                GenericError{format(
                    "Trait '%s' takes %s argument%s, but %zu %s given",
                    traitType->name.c_str(),
                    expected.c_str(),
                    params.size() == 1 ? "" : "s",
                    ref.args.size,
                    ref.args.size == 1 ? "was" : "were"
                )}
            );
        }

        // Each argument is the value of the parameter's field, and is checked against its type. A generic trait's field
        // types mention its own generic parameters, which the arguments aren't checked against.
        std::optional<TypeFun> traitFun = initializerScope->lookupTraitRef(ref);
        bool isGeneric = traitFun && (!traitFun->typeParams.empty() || !traitFun->typePackParams.empty());

        for (size_t i = 0; i < ref.args.size; ++i)
        {
            AstExpr* arg = ref.args.data[i];
            auto field = i < params.size() && !isGeneric ? traitType->props().find(params[i].name) : traitType->props().end();
            std::optional<TypeId> expectedType = field != traitType->props().end() ? field->second.readTy : std::nullopt;

            Inference found = check(initializerScope, arg, expectedType);
            if (expectedType)
                addConstraint(initializerScope, arg->location, SubtypeConstraint{found.ty, *expectedType});
        }
    }
}

std::map<Name, TypeId> ConstraintGenerator::implementTraits(const ScopePtr& scope, AstStatClass* cls, ClassDeclRecord* record)
{
    std::map<Name, TypeId> expectedFieldTypes;

    ExternType* classType = getMutable<ExternType>(follow(record->ty));
    if (!classType)
        return expectedFieldTypes;

    const std::string className = cls->name->name.value;

    // The listed traits, then the traits without parameters they need, each once (the runtime's rule)
    std::vector<std::pair<TypeId, Location>> traits;
    DenseHashSet<TypeId> seen{nullptr};

    // A generic trait listed with type arguments (`implements Listable<string>`): the class implements that
    // instantiation, and what it gets from the trait has the arguments substituted for the trait's generics
    std::map<TypeId, TraitInstantiation> instantiations;

    for (const AstClassTraitRef& ref : cls->implements)
    {
        TypeId trait = resolveTraitRef(scope, ref);
        if (!trait || seen.contains(trait))
            continue;

        seen.insert(trait);
        traits.emplace_back(trait, ref.trait->location);

        if (std::optional<TraitInstantiation> instantiation = instantiateTraitRef(scope, ref))
            instantiations.emplace(trait, std::move(*instantiation));
    }

    // `ty`, a type from `trait`'s declaration, as the class sees it
    auto instantiateMember = [&](TypeId trait, TypeId ty, Location location) -> TypeId
    {
        auto it = instantiations.find(trait);
        if (it == instantiations.end())
            return ty;

        return instantiateTraitMember(scope, location, trait, it->second, ty);
    };

    auto instantiateProperty = [&](TypeId trait, const Property& prop, Location location)
    {
        auto it = instantiations.find(trait);
        if (it == instantiations.end())
            return prop;

        return instantiateTraitProperty(scope, location, trait, it->second, prop);
    };

    for (size_t i = 0; i < traits.size(); ++i)
    {
        const ExternType* traitType = get<ExternType>(traits[i].first);
        LUAU_ASSERT(traitType);

        for (TypeId listed : traitType->implementedTraits)
        {
            TypeId needed = traitTemplateOf(listed);
            const ExternType* neededType = get<ExternType>(needed);
            bool implied = neededType && neededType->traitInfo && !neededType->traitInfo->hasParameters;

            if (!implied || seen.contains(needed))
                continue;

            seen.insert(needed);
            traits.emplace_back(needed, traits[i].second);

            // `implements Mut<string>`, where `Mut<T>` needs `Base<T>`, implements `Base<string>`. A mismatch with a
            // listed `Base<...>` is reported by TypeChecker2, once the arguments are solved.
            auto needing = instantiations.find(traits[i].first);
            std::vector<TypeId> needingArguments = needing != instantiations.end() ? needing->second.args : std::vector<TypeId>{};
            std::vector<TypeId> copiedReferences;
            std::optional<std::vector<TypeId>> arguments =
                neededTraitTypeArguments(arena, *traitType, needed, needingArguments, copiedReferences);
            if (!arguments)
                continue;

            Location location = traits[i].second;
            instantiations.emplace(needed, instantiateImpliedTrait(scope, location, needed, std::move(*arguments), std::move(copiedReferences)));
        }
    }

    auto classValueTypeOf = [](const ExternType* objectType) -> ExternType*
    {
        if (!objectType->relation)
            return nullptr;

        const Klass* klass = get_if<Klass>(&*objectType->relation);
        return klass ? getMutable<ExternType>(follow(klass->ty)) : nullptr;
    };

    auto fieldNamesOf = [](const ExternType* objectType) -> std::set<Name>*
    {
        ClassFieldUserData* data = dynamic_cast<ClassFieldUserData*>(objectType->userData.get());
        return data ? &data->fieldNames : nullptr;
    };

    ExternType* classValueType = classValueTypeOf(classType);
    TableType* classMetatable = classType->metatable ? getMutable<TableType>(follow(*classType->metatable)) : nullptr;
    std::set<Name>* classFields = fieldNamesOf(classType);

    // What the class declares itself, before any trait adds to it, and which trait added each name since
    std::set<Name> own;
    for (const auto& [name, _] : classType->props())
        own.insert(name);
    if (classMetatable)
    {
        for (const auto& [name, _] : classMetatable->props)
            own.insert(name);
    }

    // The class's own member `name`, as it declares it
    auto ownProperty = [&](const Name& name) -> const Property*
    {
        if (auto it = classType->props().find(name); it != classType->props().end())
            return &it->second;
        if (classMetatable)
        {
            if (auto it = classMetatable->props.find(name); it != classMetatable->props.end())
                return &it->second;
        }
        return nullptr;
    };

    // the class's own statics, before any trait adds to its value
    std::set<Name> ownStatics;
    if (classValueType)
    {
        for (const auto& [name, _] : classValueType->props())
            ownStatics.insert(name);
    }

    // A class may define a function a trait also defines, replacing the trait's, but not change whether it is public:
    // that is part of what the trait promises about every class implementing it. A method is both on the class's value
    // and in its metatable, so it is met twice.
    std::set<Name> accessReported;

    // Where the class declares its own member `name`: a field, a function, or a primary constructor parameter
    auto ownMemberLocation = [&](const Name& name) -> std::optional<Location>
    {
        for (const AstClassMember& member : cls->members)
        {
            if (const AstClassProperty* prop = member.get_if<AstClassProperty>(); prop && name == prop->name.value)
                return prop->nameLocation;
            if (const AstClassMethod* method = member.get_if<AstClassMethod>(); method && name == method->functionName.value)
                return method->nameLocation;
        }

        if (cls->primaryConstructor)
        {
            for (AstLocal* param : cls->primaryConstructor->args)
                if (name == param->name.value)
                    return param->location;
        }

        return std::nullopt;
    };

    // An error about the class's own member `name` goes on the trait it breaks and on the member itself
    auto reportOnMember = [&](const Name& name, Location traitLocation, const std::string& message)
    {
        reportError(traitLocation, GenericError{message});

        if (std::optional<Location> memberLocation = ownMemberLocation(name))
            reportError(*memberLocation, GenericError{message});
    };

    // "'x' must be public for 'C' to implement 'T'"
    auto reportRequirement = [&](const Name& name, const char* requirement, const ExternType* traitType, Location location)
    {
        reportOnMember(
            name, location, format("'%s' must be %s for '%s' to implement '%s'", name.c_str(), requirement, className.c_str(), traitType->name.c_str())
        );
    };

    auto checkOverrideAccess = [&](
        const Name& name,
        const Property& traitProp,
        const Property* ownProp,
        const ExternType* traitType,
        Location location
    )
    {
        bool changesAccess = ownProp && ownProp->isPrivate != traitProp.isPrivate;
        if (!changesAccess || !accessReported.insert(name).second)
            return;

        reportRequirement(name, traitProp.isPrivate ? "private" : "public", traitType, location);
    };

    // An expected `__init` asks for a constructor; the POD table constructor's `__init` doesn't count
    bool hasConstructor = cls->primaryConstructor != nullptr;
    for (const AstClassMember& member : cls->members)
    {
        if (const AstClassMethod* method = member.get_if<AstClassMethod>(); method && method->functionName == "__init")
            hasConstructor = true;
    }

    // Every trait that provides each name the class doesn't define itself
    std::map<Name, std::vector<const ExternType*>> providers;
    for (const auto& traitEntry : traits)
    {
        const ExternType* traitType = get<ExternType>(traitEntry.first);
        const ExternType::TraitInfo& info = *traitType->traitInfo;

        // A static (on the trait's value) is overridden by the class's own static, an instance member by its own member
        auto addProvider = [&](const Name& name, bool isStatic)
        {
            bool provided = name != "__init" && name != "__create" && !info.expectations.count(name) && !info.fromNeeds.count(name);
            bool classDefines = isStatic ? ownStatics.count(name) > 0 : own.count(name) > 0;
            if (!provided || classDefines)
                return;

            std::vector<const ExternType*>& list = providers[name];
            if (std::find(list.begin(), list.end(), traitType) == list.end())
                list.push_back(traitType);
        };

        for (const auto& [name, _] : traitType->props())
            addProvider(name, /* isStatic */ false);

        const TableType* traitMetatable = traitType->metatable ? get<TableType>(follow(*traitType->metatable)) : nullptr;
        if (traitMetatable)
        {
            for (const auto& [name, _] : traitMetatable->props)
                addProvider(name, /* isStatic */ false);
        }

        if (const ExternType* traitValueType = classValueTypeOf(traitType))
        {
            for (const auto& [name, _] : traitValueType->props())
                addProvider(name, /* isStatic */ true);
        }
    }

    // The trait whose member the class gets for each name: a trait's function overrides the function of a trait it
    // needs, so the provider that needs every other one wins (luaR_overridingtrait's rule). Absent when none does.
    std::map<Name, const ExternType*> winners;
    for (const auto& [name, list] : providers)
    {
        for (const ExternType* candidate : list)
        {
            bool needsAll = std::all_of(
                list.begin(),
                list.end(),
                [&](const ExternType* other)
                {
                    return other == candidate || isSubclass(candidate, other);
                }
            );

            if (needsAll)
            {
                winners[name] = candidate;
                break;
            }
        }
    }

    // For a name no provider wins, the first two providers neither of which needs the other, as the runtime reports them.
    // A name can be both a property and a metamethod of one trait, so each clash is reported once.
    std::map<Name, std::pair<const ExternType*, const ExternType*>> clashes;
    std::set<Name> clashReported;
    for (const auto& [name, list] : providers)
    {
        if (winners.count(name))
            continue;

        for (size_t i = 0; i < list.size() && !clashes.count(name); ++i)
        {
            for (size_t j = i + 1; j < list.size() && !clashes.count(name); ++j)
            {
                bool related = isSubclass(list[i], list[j]) || isSubclass(list[j], list[i]);
                if (!related)
                    clashes[name] = {list[i], list[j]};
            }
        }
    }

    for (const auto& traitEntry : traits)
    {
        TypeId trait = traitEntry.first;
        // a named local rather than a structured binding: the lambdas below capture it (C++17)
        Location location = traitEntry.second;

        auto instantiation = instantiations.find(trait);
        classType->implementedTraits.push_back(instantiation != instantiations.end() ? instantiation->second.instantiated : trait);

        const ExternType* traitType = get<ExternType>(trait);
        const ExternType::TraitInfo& info = *traitType->traitInfo;
        std::set<Name>* traitFields = fieldNamesOf(traitType);

        // the class's value is a `class<Trait>`
        if (classValueType && info.implementorClass)
            classValueType->implementedTraits.push_back(*info.implementorClass);

        auto isTraitField = [&](const Name& name)
        {
            return traitFields && traitFields->count(name) > 0;
        };

        // Whether the trait's member `name` is added to the class, reporting a clash when it can't be
        auto provide = [&](const Name& name, const Property& traitProp)
        {
            // a factory's `__create` belongs to the trait alone, and a needed trait's members to that trait
            if (name == "__init" || name == "__create" || info.expectations.count(name) || info.fromNeeds.count(name))
                return false;

            if (own.count(name))
            {
                bool clashes = isTraitField(name) || (classFields && classFields->count(name));
                if (clashes)
                    reportOnMember(name, location, format("'%s' is already provided by trait '%s'", name.c_str(), traitType->name.c_str()));
                else if (info.finals.count(name))
                    reportOnMember(name, location, format("'%s' is final in trait '%s' and can't be overridden", name.c_str(), traitType->name.c_str()));
                else
                    checkOverrideAccess(name, traitProp, ownProperty(name), traitType, location);

                return false;
            }

            if (auto winner = winners.find(name); winner != winners.end())
                return winner->second == traitType;

            // No provider needs every other one: reported once, on the entry of the second of the clashing pair
            auto clash = clashes.find(name);
            bool reportsHere = clash != clashes.end() && clash->second.second == traitType;
            if (!reportsHere || !clashReported.insert(name).second)
                return false;

            const ExternType* first = clash->second.first;
            std::string message = format("Traits '%s' and '%s' both provide '%s'", first->name.c_str(), traitType->name.c_str(), name.c_str());
            if (!isTraitField(name))
                message += format(" and neither needs the other; define '%s' in class '%s' to choose", name.c_str(), className.c_str());

            reportError(location, GenericError{std::move(message)});
            return false;
        };

        for (const auto& [name, prop] : traitType->props())
        {
            if (!provide(name, prop))
                continue;

            classType->props()[name] = instantiateProperty(trait, prop, location);

            if (classFields && isTraitField(name))
                classFields->insert(name);
        }

        ExternType* traitValueType = classValueTypeOf(traitType);
        if (traitValueType && classValueType)
        {
            for (const auto& [name, prop] : traitValueType->props())
            {
                bool provided = name != "__init" && name != "__create" && info.expectations.count(name) == 0;
                if (provided && ownStatics.count(name))
                {
                    auto own = classValueType->props().find(name);
                    checkOverrideAccess(name, prop, own != classValueType->props().end() ? &own->second : nullptr, traitType, location);
                    continue;
                }

                // the winning provider's static (a method is on the value too; its winner is the same)
                auto winner = winners.find(name);
                bool isWinner = winner == winners.end() || winner->second == traitType;
                if (provided && isWinner && !classValueType->props().count(name))
                    classValueType->props()[name] = instantiateProperty(trait, prop, location);
            }
        }

        const TableType* traitMetatable = traitType->metatable ? get<TableType>(follow(*traitType->metatable)) : nullptr;
        if (traitMetatable && classMetatable)
        {
            for (const auto& [name, prop] : traitMetatable->props)
            {
                if (provide(name, prop))
                    classMetatable->props[name] = instantiateProperty(trait, prop, location);
            }
        }
    }

    // Every expectation has to be met by the finished class, by the class itself or by another trait
    for (const auto& [trait, location] : traits)
    {
        const ExternType* traitType = get<ExternType>(trait);

        for (const auto& [name, optional] : traitType->traitInfo->expectations)
        {
            // An expected `__init` asks for a constructor, of any access (the trait may call a private one). Its
            // parameters are compared with the constructor's by TypeChecker2.
            if (name == "__init")
            {
                if (!hasConstructor)
                    reportError(
                        location,
                        GenericError{format("Missing constructor required for '%s' to implement '%s'", className.c_str(), traitType->name.c_str())}
                    );
                continue;
            }

            auto expectedProp = traitType->props().find(name);
            auto found = classType->props().find(name);
            bool inMetatable = classMetatable && classMetatable->props.count(name);

            if (found == classType->props().end() && !inMetatable)
            {
                // an optional function the class leaves out reads as nil
                if (optional && expectedProp != traitType->props().end() && expectedProp->second.readTy)
                {
                    Property nilable = instantiateProperty(trait, expectedProp->second, location);
                    nilable.readTy = makeOption(builtinTypes, *arena, *nilable.readTy);
                    classType->props()[name] = nilable;
                    continue;
                }

                // TypeChecker2 reports it, once the member's type is known (checkTraitFieldExpectations)
                continue;
            }

            if (found == classType->props().end() || expectedProp == traitType->props().end())
                continue;

            if (found->second.isPrivate != expectedProp->second.isPrivate)
                reportRequirement(name, expectedProp->second.isPrivate ? "private" : "public", traitType, location);
            else if (found->second.isConst != expectedProp->second.isConst)
                reportRequirement(name, expectedProp->second.isConst ? "const" : "non-const", traitType, location);
            // A field's type is compared with the trait's by TypeChecker2, once both are known. An unannotated field's
            // default is inferred against it, so `const kind = "Bolt"` fits `expect const kind: "Las" | "Bolt"`.
            std::set<Name>* traitFields = fieldNamesOf(traitType);
            if (traitFields && traitFields->count(name) && expectedProp->second.readTy)
                expectedFieldTypes.try_emplace(name, instantiateMember(trait, *expectedProp->second.readTy, location));
        }
    }

    return expectedFieldTypes;
}

void ConstraintGenerator::prototypeTypeDefinitions(const ScopePtr& scope, AstStatBlock* block)
{
    DenseHashMap<Name, Location> typeNameLocations{Name{}};

    bool hasTypeFunction = false;
    ScopePtr typeFunctionEnvScope;

    // Luwu Declare Statements (rfcs/declare-statements.md): an extern type or class declared in a `do` block applies to
    // the whole file, like every declaration, so the file's own block prototypes them too.
    AstArray<AstStat*> statements = block->body;
    std::vector<AstStat*> withNestedDeclarations;
    if (declaresFileGlobals() && scope.get() == rootScope)
    {
        withNestedDeclarations.assign(block->body.begin(), block->body.end());
        for (AstStat* stat : block->body)
        {
            if (AstStatBlock* inner = stat->as<AstStatBlock>())
                collectNestedTypeDeclarations(inner, withNestedDeclarations);
        }
        statements = AstArray<AstStat*>{withNestedDeclarations.data(), withNestedDeclarations.size()};
    }

    // In order to enable mutually-recursive type aliases, we need to
    // populate the type bindings before we actually check any of the
    // alias statements.
    for (AstStat* stat : statements)
    {
        if (auto alias = stat->as<AstStatTypeAlias>())
        {
            if (FFlag::LuauDisallowRedefiningBuiltinTypes && globalScope->builtinTypeNames.contains(alias->name.value))
            {
                reportError(alias->location, DuplicateTypeDefinition{alias->name.value});
                continue;
            }

            // A type alias might have no name if the code is syntactically
            // illegal. We mustn't prepopulate anything in this case.
            if (alias->name == kParseNameError || alias->name == "typeof")
                continue;

            if (const Location* loc = typeNameLocations.find(alias->name.value))
            {
                reportError(alias->location, DuplicateTypeDefinition{alias->name.value, *loc});
                continue;
            }

            ScopePtr defnScope = childScope(alias, scope);

            TypeId initialType = arena->addType(BlockedType{});
            TypeFun initialFun{initialType};

            /* The boolean toggle `addTypes` decides whether or not to introduce the generic type/pack param into the privateType/Pack bindings.
               This map is used by resolveType(Pack) to determine whether or not to produce an error for `F<T... = ...T>`. Because we are delaying
               the the initialization of the generic default to the point at which we check the type alias, we need to ensure that we don't
               prematurely add `T` as this will cause us to allow the above example (T is in the bindings so we return that as the resolved type).
               Done this way, we can evaluate the default safely and then introduce the variable into the map again once the default has been
               evaluated. Note, only generic type aliases support default generic parameters.
             */

            for (const auto& [name, gen] : createGenerics(defnScope, alias->generics, /* useCache */ true, /* addTypes */ false))
            {
                initialFun.typeParams.push_back(gen);
            }

            for (const auto& [name, genPack] : createGenericPacks(defnScope, alias->genericPacks, /* useCache */ true, /* addTypes */ false))
            {
                initialFun.typePackParams.push_back(genPack);
            }
            initialFun.definitionLocation = alias->location;

            if (alias->exported)
                scope->exportedTypeBindings[alias->name.value] = std::move(initialFun);
            else
                scope->privateTypeBindings[alias->name.value] = std::move(initialFun);

            astTypeAliasDefiningScopes[alias] = defnScope;
            typeNameLocations[alias->name.value] = alias->location;
        }
        else if (auto function = stat->as<AstStatTypeFunction>())
        {
            hasTypeFunction = true;

            // If a type function w/ same name has already been defined, error for having duplicates
            if (const Location* loc = typeNameLocations.find(function->name.value))
            {
                reportError(function->location, DuplicateTypeDefinition{function->name.value, *loc});
                continue;
            }

            // Create TypeFunctionInstanceType
            std::vector<TypeId> typeParams;
            typeParams.reserve(function->body->args.size);

            std::vector<GenericTypeDefinition> quantifiedTypeParams;
            quantifiedTypeParams.reserve(function->body->args.size);

            for (size_t i = 0; i < function->body->args.size; i++)
            {
                std::string name = format("T%zu", i);
                TypeId ty = arena->addType(GenericType{name, Polarity::Unknown});
                typeParams.push_back(ty);

                GenericTypeDefinition genericTy{ty};
                quantifiedTypeParams.push_back(genericTy);
            }

            if (FFlag::LuauTypeFunctionStructuredErrors)
            {
                if (std::optional<TypeFunctionError> error = typeFunctionRuntime->registerFunction(function))
                    reportError(function->location, BuiltInTypeFunctionError{*error});
            }
            else
            {
                if (std::optional<std::string> error = typeFunctionRuntime->registerFunction_DEPRECATED(function))
                    reportError(function->location, GenericError{*error});
            }

            UserDefinedFunctionData udtfData;

            udtfData.owner = module;
            udtfData.definition = function;

            TypeId typeFunctionTy = arena->addType(
                TypeFunctionInstanceType{NotNull{&builtinTypes->typeFunctions->userFunc}, std::move(typeParams), {}, function->name, udtfData}
            );

            TypeFun typeFunction{std::move(quantifiedTypeParams), typeFunctionTy};

            typeFunction.definitionLocation = function->location;

            // Set type bindings and definition locations for this user-defined type function
            if (function->exported)
                scope->exportedTypeBindings[function->name.value] = std::move(typeFunction);
            else
                scope->privateTypeBindings[function->name.value] = std::move(typeFunction);

            typeNameLocations[function->name.value] = function->location;
        }
        else if (auto classDeclaration = stat->as<AstStatDeclareExternType>())
        {
            // A class might have no name if the code is syntactically
            // illegal. We mustn't prepopulate anything in this case.
            if (classDeclaration->name == kParseNameError)
                continue;

            // Luwu Declare Statements (rfcs/declare-statements.md): the file's own block has prototyped it already.
            if (declaresFileGlobals() && scope.get() != rootScope)
                continue;

            if (const Location* loc = typeNameLocations.find(classDeclaration->name.value))
            {
                // Luwu: at the name; the declaration spans its whole body
                reportError(classDeclaration->nameLocation, DuplicateTypeDefinition{classDeclaration->name.value, *loc});
                continue;
            }

            ScopePtr defnScope = childScope(classDeclaration, scope);

            if (FFlag::LuwuExternTypeUseDefinitionScope || FFlag::LuwuGenericNominals)
                astExternTypeDefiningScopes[classDeclaration] = defnScope;

            TypeId initialType = arena->addType(BlockedType{});
            TypeFun initialFun{initialType};
            initialFun.definitionLocation = classDeclaration->location;

            if (FFlag::LuwuGenericNominals)
            {
                for (const auto& [name, gen] : createGenerics(defnScope, classDeclaration->generics, /* useCache */ true, /* addTypes */ false))
                    initialFun.typeParams.push_back(gen);

                for (const auto& [name, genPack] :
                     createGenericPacks(defnScope, classDeclaration->genericPacks, /* useCache */ true, /* addTypes */ false))
                    initialFun.typePackParams.push_back(genPack);
            }

            externTypeBindings(*scope, classDeclaration)[classDeclaration->name.value] = std::move(initialFun);

            typeNameLocations[classDeclaration->name.value] = classDeclaration->location;
        }
        else if (auto classDecl = stat->as<AstStatClass>())
        {
            prototypeClass(scope, classDecl, typeNameLocations, /* declared= */ false);
        }
        else if (auto declaredClass = stat->as<AstStatDeclareClass>())
        {
            // Luwu Declare Statements (rfcs/declare-statements.md): the file's own block has prototyped it already.
            if (declaresFileGlobals() && scope.get() != rootScope)
                continue;

            prototypeClass(scope, declaredClass->shape, typeNameLocations, /* declared= */ true);
        }
    }

    if (FFlag::LuwuTraits)
        linkTraitNeeds(scope, statements);

    if (hasTypeFunction)
        typeFunctionEnvScope = std::make_shared<Scope>(typeFunctionRuntime->rootScope);

    std::vector<TypeFunctionInstanceType*> createdTypeFunctions;
    DenseHashMap<AstStatTypeFunction*, const TypeFunctionInstanceType*> referencedTypeFunctions{nullptr};

    // Additional pass for user-defined type functions to fill in their environments completely
    for (AstStat* stat : block->body)
    {
        if (auto function = stat->as<AstStatTypeFunction>())
        {
            // Similar to global pre-population, create a binding for each type function in the scope upfront
            TypeId bt = arena->addType(BlockedType{});
            typeFunctionEnvScope->bindings[function->name] = Binding{bt, function->location};
            astTypeFunctionEnvironmentScopes[function] = typeFunctionEnvScope;

            // Find the type function we have already created
            TypeFunctionInstanceType* mainTypeFun = nullptr;

            if (auto it = scope->privateTypeBindings.find(function->name.value); it != scope->privateTypeBindings.end())
                mainTypeFun = getMutable<TypeFunctionInstanceType>(it->second.type);

            if (!mainTypeFun)
            {
                if (auto it = scope->exportedTypeBindings.find(function->name.value); it != scope->exportedTypeBindings.end())
                    mainTypeFun = getMutable<TypeFunctionInstanceType>(it->second.type);
            }

            // Fill it with all visible type functions and referenced type aliases
            if (mainTypeFun)
            {
                createdTypeFunctions.push_back(mainTypeFun);

                GlobalNameCollector globalNameCollector;
                stat->visit(&globalNameCollector);

                UserDefinedFunctionData& userFuncData = mainTypeFun->userFuncData;
                size_t level = 0;

                auto addToEnvironment = [this, &globalNameCollector, &referencedTypeFunctions](
                                            UserDefinedFunctionData& userFuncData, ScopePtr scope, const Name& name, TypeFun tf, size_t level
                                        )
                {
                    if (auto ty = get<TypeFunctionInstanceType>(follow(tf.type)); ty && ty->userFuncData.definition)
                    {
                        if (userFuncData.environmentFunction.find(name))
                            return;

                        userFuncData.environmentFunction[name] = std::make_pair(ty->userFuncData.definition, level);

                        referencedTypeFunctions[ty->userFuncData.definition] = ty;

                        if (auto it = astTypeFunctionEnvironmentScopes.find(ty->userFuncData.definition))
                        {
                            if (auto existing = (*it)->linearSearchForBinding(name, /* traverseScopeChain */ false))
                                scope->bindings[ty->userFuncData.definition->name] = Binding{existing->typeId, ty->userFuncData.definition->location};
                        }
                    }
                    else if (!get<TypeFunctionInstanceType>(follow(tf.type)))
                    {
                        if (userFuncData.environmentAlias.find(name))
                            return;

                        AstName astName = module->names->get(name.c_str());

                        // Only register globals that we have detected to be used
                        if (!globalNameCollector.names.find(astName))
                            return;

                        // Function evaluation environment needs a stable reference to the alias
                        module->typeFunctionAliases.push_back(std::make_unique<TypeFun>(tf));

                        userFuncData.environmentAlias[name] = std::make_pair(module->typeFunctionAliases.back().get(), level);

                        // TODO: create a specific type alias type
                        scope->bindings[astName] = Binding{builtinTypes->anyType, tf.definitionLocation.value_or(Location())};
                    }
                };

                // Go up the scopes to register type functions and aliases, but without reaching into the global scope
                for (Scope* curr = scope.get(); curr && curr != globalScope.get(); curr = curr->parent.get())
                {
                    for (auto& [name, tf] : curr->privateTypeBindings)
                        addToEnvironment(userFuncData, typeFunctionEnvScope, name, tf, level);

                    for (auto& [name, tf] : curr->exportedTypeBindings)
                        addToEnvironment(userFuncData, typeFunctionEnvScope, name, tf, level);

                    level++;
                }
            }
        }
    }

    // Finally, we need to include aliases from functions we might call
    for (TypeFunctionInstanceType* type : createdTypeFunctions)
    {
        UserDefinedFunctionData& sourceFuncData = type->userFuncData;

        // Go over all functions in our environment
        for (const auto& [targetFuncName, definitionAndLevel] : sourceFuncData.environmentFunction)
        {
            if (const TypeFunctionInstanceType** it = referencedTypeFunctions.find(definitionAndLevel.first))
            {
                const UserDefinedFunctionData& targetFuncData = (*it)->userFuncData;

                for (const auto& [aliasName, typeAndLevel] : targetFuncData.environmentAlias)
                {
                    if (!sourceFuncData.environmentAlias.find(aliasName))
                    {
                        // Combine definition levels because we are viewing target function aliases from the perspective of the target function
                        sourceFuncData.environmentAlias[aliasName] = {typeAndLevel.first, typeAndLevel.second + definitionAndLevel.second};
                    }
                }
            }
        }
    }
}

ControlFlow ConstraintGenerator::visitBlockWithoutChildScope(const ScopePtr& scope, AstStatBlock* block)
{
    RecursionCounter counter{&recursionCount};

    if (recursionCount >= DFInt::LuauConstraintGeneratorRecursionLimit)
    {
        reportCodeTooComplex(block->location);
        return ControlFlow::None;
    }

    prototypeTypeDefinitions(scope, block);

    std::optional<ControlFlow> firstControlFlow;
    for (AstStat* stat : block->body)
    {
        ControlFlow cf = visit(scope, stat);
        if (cf != ControlFlow::None && !firstControlFlow)
            firstControlFlow = cf;
    }

    return firstControlFlow.value_or(ControlFlow::None);
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStat* stat)
{
    RecursionCounter counter{&recursionCount};
    std::optional<RecursionLimiter> limiter;

    if (recursionCount >= DFInt::LuauConstraintGeneratorRecursionLimit)
    {
        reportCodeTooComplex(stat->location);
        return ControlFlow::None;
    }

    if (auto s = stat->as<AstStatBlock>())
        return visit(scope, s);
    else if (auto i = stat->as<AstStatIf>())
        return visit(scope, i);
    else if (auto s = stat->as<AstStatWhile>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatRepeat>())
        return visit(scope, s);
    else if (stat->is<AstStatBreak>())
        return ControlFlow::Breaks;
    else if (stat->is<AstStatContinue>())
        return ControlFlow::Continues;
    else if (auto r = stat->as<AstStatReturn>())
        return visit(scope, r);
    else if (auto e = stat->as<AstStatExpr>())
    {
        checkPack(scope, e->expr);

        if (auto call = e->expr->as<AstExprCall>(); call && doesCallError(call))
            return ControlFlow::Throws;

        return ControlFlow::None;
    }
    else if (auto s = stat->as<AstStatLocal>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatFor>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatForIn>())
        return visit(scope, s);
    else if (auto a = stat->as<AstStatAssign>())
        return visit(scope, a);
    else if (auto a = stat->as<AstStatCompoundAssign>())
        return visit(scope, a);
    else if (auto f = stat->as<AstStatFunction>())
        return visit(scope, f);
    else if (auto f = stat->as<AstStatLocalFunction>())
        return visit(scope, f);
    else if (auto a = stat->as<AstStatTypeAlias>())
        return visit(scope, a);
    else if (auto f = stat->as<AstStatTypeFunction>())
        return visit(scope, f);
    else if (auto s = stat->as<AstStatDeclareGlobal>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatDeclareFunction>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatDeclareExternType>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatDeclareClass>())
        return visit(scope, s);
    else if (auto s = stat->as<AstStatClass>())
    {
        LUAU_ASSERT(FFlag::LuwuClasses);
        return visit(scope, s);
    }
    else if (auto s = stat->as<AstStatError>())
        return visit(scope, s);
    else
    {
        LUAU_ASSERT(0 && "Internal error: Unknown AstStat type");
        return ControlFlow::None;
    }
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatLocal* statLocal)
{
    std::vector<TypeId> annotatedTypes;
    annotatedTypes.reserve(statLocal->vars.size);
    bool hasAnnotation = false;

    std::vector<std::optional<TypeId>> expectedTypes;
    expectedTypes.reserve(statLocal->vars.size);

    std::vector<TypeId> assignees;
    assignees.reserve(statLocal->vars.size);

    // Used to name the first value type, even if it's not placed in varTypes,
    // for the purpose of synthetic name attribution.
    std::optional<TypeId> firstValueType;

    for (AstLocal* local : statLocal->vars)
    {
        const Location location = local->location;

        TypeId assignee;
        if (FFlag::DebugLuauCFG)
        {
            assignee = resolveLHSType(scope, location, CFG::LValue{Symbol{local}});
        }
        else
        {
            assignee = arena->addType(BlockedType{});
        }

        localTypes.try_insert(assignee, {});
        assignees.push_back(assignee);

        if (!firstValueType)
            firstValueType = assignee;

        if (local->annotation)
        {
            hasAnnotation = true;
            TypeId annotationTy = resolveType(scope, local->annotation, /* inTypeArguments */ false);
            annotatedTypes.push_back(annotationTy);
            expectedTypes.emplace_back(annotationTy);
            scope->bindings[local] = Binding{annotationTy, location};
        }
        else
        {
            // annotatedTypes must contain one type per local.  If a particular
            // local has no annotation at, assume the most conservative thing.
            annotatedTypes.push_back(builtinTypes->unknownType);

            expectedTypes.emplace_back(std::nullopt);
            scope->bindings[local] = Binding{builtinTypes->unknownType, location};

            inferredBindings[local] = {scope.get(), location, {assignee}};
        }

        if (!FFlag::DebugLuauCFG)
        {
            DefId def = dfg->getDef(local);
            scope->lvalueTypes[def] = assignee;
        }
    }

    Checkpoint start = checkpoint(this);
    TypePackId rvaluePack = checkPack(scope, statLocal->values, expectedTypes).tp;
    Checkpoint end = checkpoint(this);

    std::vector<TypeId> deferredTypes;
    auto [head, tail] = flatten(rvaluePack);

    DenseHashSet<BlockedType*> freshBlockedTypes{nullptr};

    for (size_t i = 0; i < statLocal->vars.size; ++i)
    {
        LUAU_ASSERT(get<BlockedType>(assignees[i]));
        TypeIds* localDomain = localTypes.find(assignees[i]);
        LUAU_ASSERT(localDomain);

        if (statLocal->vars.data[i]->annotation)
        {
            localDomain->insert(annotatedTypes[i]);
            if (i >= head.size() && tail)
            {
                if (FFlag::LuauDoNotEmplaceAnnotatedType)
                {
                    deferredTypes.push_back(arena->addType(BlockedType{}));
                    freshBlockedTypes.insert(getMutable<BlockedType>(deferredTypes.back()));
                }
                else
                {
                    deferredTypes.emplace_back(annotatedTypes[i]);
                }
            }
        }
        else
        {
            if (i < head.size())
            {
                localDomain->insert(head[i]);
            }
            else if (tail)
            {
                deferredTypes.push_back(arena->addType(BlockedType{}));
                localDomain->insert(deferredTypes.back());
                freshBlockedTypes.insert(getMutable<BlockedType>(deferredTypes.back()));
            }
            else
            {
                localDomain->insert(builtinTypes->nilType);
            }
        }
    }

    if (hasAnnotation)
    {
        TypePackId annotatedPack = arena->addTypePack(std::move(annotatedTypes));
        addConstraint(scope, statLocal->location, PackSubtypeConstraint{rvaluePack, annotatedPack});
    }

    if (!deferredTypes.empty())
    {
        LUAU_ASSERT(tail);
        NotNull<Constraint> uc = addConstraint(scope, statLocal->location, UnpackConstraint{deferredTypes, *tail});

        addAllAsDependencies(start, end, this, uc);
        // This is a separate set from `deferredTypes` to
        // distinguish between blocked types we just minted
        // and blocked types that correspond to annotations.
        for (BlockedType* bt : freshBlockedTypes)
            bt->setOwner(uc);
    }

    if (statLocal->vars.size == 1 && statLocal->values.size == 1 && firstValueType && scope.get() == rootScope && !hasAnnotation)
    {
        AstLocal* var = statLocal->vars.data[0];
        AstExpr* value = statLocal->values.data[0];

        if (value->is<AstExprTable>())
            addConstraint(scope, value->location, NameConstraint{*firstValueType, var->name.value, /*synthetic*/ true});
        else if (const AstExprCall* call = value->as<AstExprCall>())
        {
            if (matchSetMetatable(*call))
                addConstraint(scope, value->location, NameConstraint{*firstValueType, var->name.value, /*synthetic*/ true});
        }
    }

    if (statLocal->values.size > 0)
    {
        // To correctly handle 'require', we need to import the exported type bindings into the variable 'namespace'.
        for (size_t i = 0; i < statLocal->values.size && i < statLocal->vars.size; ++i)
        {
            const AstExprCall* call = statLocal->values.data[i]->as<AstExprCall>();
            if (!call)
                continue;

            auto maybeRequire = matchRequire(*call);
            if (!maybeRequire)
                continue;

            AstExpr* require = *maybeRequire;

            auto moduleInfo = moduleResolver->resolveModuleInfo(module->name, *require);
            if (!moduleInfo)
                continue;

            ModulePtr module = moduleResolver->getModule(moduleInfo->name);
            if (!module)
                continue;

            const Name name{statLocal->vars.data[i]->name.value};
            scope->importedTypeBindings[name] = module->exportedTypeBindings;
            scope->importedModules[name] = moduleInfo->name;

            // Imported typeArguments of requires that transitively refer to current module have to be replaced with 'any'
            for (const auto& [location, path] : requireCycles)
            {
                if (path.empty() || path.front() != moduleInfo->name)
                    continue;

                for (auto& [name, tf] : scope->importedTypeBindings[name])
                    tf = TypeFun{{}, {}, builtinTypes->anyType};
            }
        }
    }

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatFor* for_)
{
    TypeId annotationTy = builtinTypes->numberType;
    if (for_->var->annotation)
        annotationTy = resolveType(scope, for_->var->annotation, /* inTypeArguments */ false);

    auto inferNumber = [&](AstExpr* expr)
    {
        if (!expr)
            return;

        TypeId t = check(scope, expr).ty;
        addConstraint(scope, expr->location, SubtypeConstraint{t, builtinTypes->numberType});
    };

    inferNumber(for_->from);
    inferNumber(for_->to);
    inferNumber(for_->step);

    ScopePtr forScope = childScope(for_, scope);
    forScope->bindings[for_->var] = Binding{annotationTy, for_->var->location};

    DefId def = dfg->getDef(for_->var);
    forScope->lvalueTypes[def] = annotationTy;
    updateRValueRefinements(forScope, def, annotationTy);

    visit(forScope, for_->body);

    scope->inheritAssignments(forScope);

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatForIn* forIn)
{
    ScopePtr loopScope = childScope(forIn, scope);
    TypePackId iterator = checkPack(scope, forIn->values).tp;

    std::vector<TypeId> variableTypes;
    variableTypes.reserve(forIn->vars.size);

    for (AstLocal* var : forIn->vars)
    {
        TypeId loopVar = arena->addType(BlockedType{});
        variableTypes.push_back(loopVar);

        DefId def = dfg->getDef(var);

        if (var->annotation)
        {
            TypeId annotationTy = resolveType(loopScope, var->annotation, /*inTypeArguments*/ false);
            loopScope->bindings[var] = Binding{annotationTy, var->location};
            addConstraint(scope, var->location, SubtypeConstraint{loopVar, annotationTy});
            loopScope->lvalueTypes[def] = annotationTy;
        }
        else
        {
            loopScope->bindings[var] = Binding{loopVar, var->location};
            loopScope->lvalueTypes[def] = loopVar;
        }
    }

    auto iterable = addConstraint(
        loopScope, getLocation(forIn->values), IterableConstraint{iterator, variableTypes, forIn->values.data[0], &module->astForInNextTypes}
    );

    // Add an intersection ReduceConstraint for the key variable to denote that it can't be nil
    AstLocal* keyVar = *forIn->vars.begin();
    const DefId keyDef = dfg->getDef(keyVar);
    const TypeId loopVar = loopScope->lvalueTypes[keyDef];

    // Luwu: `refine` rather than `intersect`, like the value in ConstraintSolver::tryDispatchIterableTable (from upstream 0.734)
    const TypeId intersectionTy =
        createTypeFunctionInstance(builtinTypes->typeFunctions->refineFunc, {loopVar, builtinTypes->notNilType}, {}, loopScope, keyVar->location);

    loopScope->bindings[keyVar] = Binding{intersectionTy, keyVar->location};
    loopScope->lvalueTypes[keyDef] = intersectionTy;

    auto c = addConstraint(loopScope, keyVar->location, ReduceConstraint{intersectionTy});
    cgraph->addDependencyOf(iterable, c);
    for (TypeId var : variableTypes)
    {
        auto bt = getMutable<BlockedType>(var);
        LUAU_ASSERT(bt);
        bt->setOwner(iterable);
    }

    Checkpoint start = checkpoint(this);
    visit(loopScope, forIn->body);
    Checkpoint end = checkpoint(this);

    scope->inheritAssignments(loopScope);

    // This iter constraint must dispatch first.
    addAllAsReverseDependencies(start, end, this, iterable);
    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatWhile* while_)
{
    if (FFlag::DebugLuauCFG)
    {
        check(scope, while_->condition);
        ScopePtr whileScope = childScope(while_->body, scope);
        visit(whileScope, while_->body);
        return ControlFlow::None;
    }

    RefinementId refinement = check(scope, while_->condition).refinement;

    ScopePtr whileScope = childScope(while_, scope);
    applyRefinements(whileScope, while_->condition->location, refinement);

    visit(whileScope, while_->body);

    scope->inheritAssignments(whileScope);

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatRepeat* repeat)
{
    ScopePtr repeatScope = childScope(repeat, scope);

    visitBlockWithoutChildScope(repeatScope, repeat->body);

    check(repeatScope, repeat->condition);

    scope->inheritAssignments(repeatScope);

    return ControlFlow::None;
}

static void propagateDeprecatedAttributeToConstraint(ConstraintV& c, const AstExprFunction* func)
{
    if (GeneralizationConstraint* genConstraint = c.get_if<GeneralizationConstraint>())
    {
        AstAttr* deprecatedAttribute = func->getAttribute(AstAttr::Type::Deprecated);
        genConstraint->hasDeprecatedAttribute = deprecatedAttribute != nullptr;
        if (deprecatedAttribute)
        {
            genConstraint->deprecatedInfo = deprecatedAttribute->deprecatedInfo();
        }
    }
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatLocalFunction* function)
{
    // Local
    // Global
    // Dotted path
    // Self?

    TypeId functionType = nullptr;
    auto ty = scope->lookup(function->name);
    LUAU_ASSERT(!ty.has_value()); // The parser ensures that every local function has a distinct Symbol for its name.

    functionType = arena->addType(BlockedType{});
    scope->bindings[function->name] = Binding{functionType, function->name->location};

    FunctionSignature sig = checkFunctionSignature(scope, nullptr, function->func, /* expectedType */ std::nullopt, function->name->location);
    sig.bodyScope->bindings[function->name] = Binding{sig.signature, function->name->location};

    DefId def = dfg->getDef(function->name);
    scope->lvalueTypes[def] = functionType;
    updateRValueRefinements(scope, def, functionType);
    sig.bodyScope->lvalueTypes[def] = sig.signature;
    updateRValueRefinements(sig.bodyScope, def, sig.signature);

    Checkpoint start = checkpoint(this);
    checkFunctionBody(sig.bodyScope, function->func);
    Checkpoint end = checkpoint(this);

    NotNull<Scope> constraintScope{sig.signatureScope ? sig.signatureScope.get() : sig.bodyScope.get()};
    std::unique_ptr<Constraint> c =
        std::make_unique<Constraint>(constraintScope, function->name->location, GeneralizationConstraint{functionType, sig.signature});

    propagateDeprecatedAttributeToConstraint(c->c, function->func);

    addAllAsDependenciesAndChainReturns(start, end, this, NotNull{c.get()});
    getMutable<BlockedType>(functionType)->setOwner(addConstraint(scope, std::move(c)));
    module->astTypes[function->func] = functionType;

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatFunction* function)
{
    // Name could be AstStatLocal, AstStatGlobal, AstStatIndexName.
    // With or without self

    Checkpoint start = checkpoint(this);
    FunctionSignature sig = checkFunctionSignature(scope, nullptr, function->func, /* expectedType */ std::nullopt, function->name->location);

    DefId def = dfg->getDef(function->name);

    if (AstExprLocal* localName = function->name->as<AstExprLocal>())
    {
        sig.bodyScope->bindings[localName->local] = Binding{sig.signature, localName->location};
        sig.bodyScope->lvalueTypes[def] = sig.signature;
        updateRValueRefinements(sig.bodyScope, def, sig.signature);
    }
    else if (AstExprGlobal* globalName = function->name->as<AstExprGlobal>())
    {
        sig.bodyScope->bindings[globalName->name] = Binding{sig.signature, globalName->location};
        sig.bodyScope->lvalueTypes[def] = sig.signature;
        updateRValueRefinements(sig.bodyScope, def, sig.signature);
    }
    else if (function->name->is<AstExprIndexName>())
    {
        updateRValueRefinements(sig.bodyScope, def, sig.signature);
    }

    if (auto indexName = function->name->as<AstExprIndexName>())
    {
        auto beginProp = checkpoint(this);
        auto [fn, _] = check(scope, indexName);
        auto endProp = checkpoint(this);
        auto pftc = addConstraint(
            sig.signatureScope,
            function->func->location,
            PushFunctionTypeConstraint{
                fn,
                sig.signature,
                NotNull{function->func},
                /* isSelf */ indexName->op == ':',
            }
        );

        addAllAsDependencies(beginProp, endProp, this, pftc);

        auto beginBody = checkpoint(this);
        checkFunctionBody(sig.bodyScope, function->func);
        auto endBody = checkpoint(this);

        addAllAsReverseDependencies(beginBody, endBody, this, pftc);
    }
    else
    {
        checkFunctionBody(sig.bodyScope, function->func);
    }

    Checkpoint end = checkpoint(this);

    TypeId generalizedType = arena->addType(BlockedType{});
    const ScopePtr& constraintScope = sig.signatureScope ? sig.signatureScope : sig.bodyScope;

    NotNull<Constraint> c = addConstraint(constraintScope, function->name->location, GeneralizationConstraint{generalizedType, sig.signature});
    getMutable<BlockedType>(generalizedType)->setOwner(c);

    propagateDeprecatedAttributeToConstraint(c->c, function->func);

    addAllAsDependenciesAndChainReturns(start, end, this, c);
    std::optional<TypeId> existingFunctionTy = follow(lookup(scope, function->name->location, def));

    if (AstExprLocal* localName = function->name->as<AstExprLocal>())
    {
        visitLValue(scope, localName, generalizedType);

        scope->bindings[localName->local] = Binding{sig.signature, localName->location};
        scope->lvalueTypes[def] = sig.signature;
    }
    else if (AstExprGlobal* globalName = function->name->as<AstExprGlobal>())
    {
        if (!existingFunctionTy)
            ice->ice("prepopulateGlobalScope did not populate a global name", globalName->location);

        if (auto bt = get<BlockedType>(*existingFunctionTy); bt && uninitializedGlobals.contains(globalName->name))
        {
            LUAU_ASSERT(bt->getOwner() == nullptr);
            uninitializedGlobals.erase(globalName->name);
            emplaceType<BoundType>(asMutable(*existingFunctionTy), generalizedType);
        }


        scope->bindings[globalName->name] = Binding{sig.signature, globalName->location};
        scope->lvalueTypes[def] = sig.signature;
    }
    else if (AstExprIndexName* indexName = function->name->as<AstExprIndexName>())
    {
        visitLValue(scope, indexName, generalizedType);
    }
    else if (function->name->is<AstExprError>())
    {
        generalizedType = builtinTypes->errorType;
    }

    if (generalizedType == nullptr)
        ice->ice("generalizedType == nullptr", function->location);

    updateRValueRefinements(scope, def, generalizedType);

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatReturn* ret)
{
    // At this point, the only way scope->returnType should have anything
    // interesting in it is if the function has an explicit return annotation.
    // If this is the case, then we can expect that the return expression
    // conforms to that.
    std::vector<std::optional<TypeId>> expectedTypes;
    for (TypeId ty : scope->returnType)
        expectedTypes.emplace_back(ty);
    TypePackId exprTypes = checkPack(scope, ret->list, expectedTypes).tp;
    addConstraint(scope, ret->location, PackSubtypeConstraint{exprTypes, scope->returnType, /*returns*/ true});

    return ControlFlow::Returns;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatBlock* block)
{
    ScopePtr innerScope = childScope(block, scope);

    ControlFlow flow = visitBlockWithoutChildScope(innerScope, block);

    // An AstStatBlock has linear control flow, i.e. one entry and one exit, so we can inherit
    // all the changes to the environment occurred by the statements in that block.
    if (!FFlag::DebugLuauCFG)
    {
        scope->inheritRefinements(innerScope);
        scope->inheritAssignments(innerScope);
    }


    return flow;
}

// TODO Clip?
static void bindFreeType(TypeId a, TypeId b)
{
    FreeType* af = getMutable<FreeType>(a);
    FreeType* bf = getMutable<FreeType>(b);

    LUAU_ASSERT(af || bf);

    if (!bf)
        emplaceType<BoundType>(asMutable(a), b);
    else if (!af)
        emplaceType<BoundType>(asMutable(b), a);
    else if (subsumes(bf->scope, af->scope))
        emplaceType<BoundType>(asMutable(a), b);
    else if (subsumes(af->scope, bf->scope))
        emplaceType<BoundType>(asMutable(b), a);
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatAssign* assign)
{
    TypePackId resultPack = checkPack(scope, assign->values).tp;

    std::vector<TypeId> valueTypes;
    valueTypes.reserve(assign->vars.size);

    auto [head, tail] = flatten(resultPack);
    if (head.size() >= assign->vars.size)
    {
        // If the resultPack is definitely long enough for each variable, we can
        // skip the UnpackConstraint and use the result typeArguments directly.

        for (size_t i = 0; i < assign->vars.size; ++i)
            valueTypes.push_back(head[i]);
    }
    else
    {
        // We're not sure how many typeArguments are produced by the right-side
        // expressions.  We'll use an UnpackConstraint to defer this until
        // later.
        for (size_t i = 0; i < assign->vars.size; ++i)
            valueTypes.push_back(arena->addType(BlockedType{}));

        auto uc = addConstraint(scope, assign->location, UnpackConstraint{valueTypes, resultPack});

        for (TypeId t : valueTypes)
            getMutable<BlockedType>(t)->setOwner(uc);
    }

    for (size_t i = 0; i < assign->vars.size; ++i)
    {
        visitLValue(scope, assign->vars.data[i], valueTypes[i]);
    }

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatCompoundAssign* assign)
{
    TypeId resultTy = checkAstExprBinary(scope, assign->location, assign->op, assign->var, assign->value, std::nullopt).ty;
    module->astCompoundAssignResultTypes[assign] = resultTy;
    // NOTE: We do not update lvalues for compound assignments. This is
    // intentional.
    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatIf* ifStatement)
{
    if (FFlag::DebugLuauCFG)
    {
        check(scope, ifStatement->condition, std::nullopt);
        ScopePtr thenScope = childScope(ifStatement->thenbody, scope);
        visit(thenScope, ifStatement->thenbody);
        if (ifStatement->elsebody)
        {
            ScopePtr elseScope = childScope(ifStatement->elsebody ? ifStatement->elsebody : ifStatement, scope);
            visit(elseScope, ifStatement->elsebody);
        }
        return ControlFlow::None;
    }
    else
    {
        RefinementId refinement = [&]()
        {
            InConditionalContext flipper{&typeContext};
            return check(scope, ifStatement->condition, std::nullopt).refinement;
        }();

        ScopePtr thenScope = childScope(ifStatement->thenbody, scope);
        applyRefinements(thenScope, ifStatement->condition->location, refinement);

        ScopePtr elseScope = childScope(ifStatement->elsebody ? ifStatement->elsebody : ifStatement, scope);
        applyRefinements(elseScope, ifStatement->elseLocation.value_or(ifStatement->condition->location), refinementArena.negation(refinement));

        ControlFlow thencf = visit(thenScope, ifStatement->thenbody);
        ControlFlow elsecf = ControlFlow::None;
        if (ifStatement->elsebody)
            elsecf = visit(elseScope, ifStatement->elsebody);

        if (thencf != ControlFlow::None && elsecf == ControlFlow::None)
            scope->inheritRefinements(elseScope);
        else if (thencf == ControlFlow::None && elsecf != ControlFlow::None)
            scope->inheritRefinements(thenScope);

        if (thencf == ControlFlow::None)
            scope->inheritAssignments(thenScope);
        if (elsecf == ControlFlow::None)
            scope->inheritAssignments(elseScope);

        if (thencf == elsecf)
            return thencf;
        else if (matches(thencf, ControlFlow::Returns | ControlFlow::Throws) && matches(elsecf, ControlFlow::Returns | ControlFlow::Throws))
            return ControlFlow::Returns;
        else
            return ControlFlow::None;
    }
}

void ConstraintGenerator::resolveGenericDefaultParameters(const ScopePtr& defnScope, AstStatTypeAlias* alias, const TypeFun& fun)
{
    resolveGenericDefaultParameters(defnScope, alias->generics, alias->genericPacks, fun.typeParams, fun.typePackParams);
}

void ConstraintGenerator::resolveGenericDefaultParameters(
    const ScopePtr& defnScope,
    AstArray<AstGenericType*> generics,
    AstArray<AstGenericTypePack*> genericPacks,
    const std::vector<GenericTypeDefinition>& typeParams,
    const std::vector<GenericTypePackDefinition>& typePackParams
)
{
    LUAU_ASSERT(generics.size == typeParams.size());
    for (size_t i = 0; i < generics.size; i++)
    {
        auto astTy = generics.data[i];
        auto param = typeParams[i];
        if (param.defaultValue && astTy->defaultValue != nullptr)
        {
            auto resolvesTo = astTy->defaultValue;
            auto toUnblock = *param.defaultValue;
            emplaceType<BoundType>(asMutable(toUnblock), resolveType(defnScope, resolvesTo, /*  inTypeArguments */ false));
        }
        defnScope->privateTypeBindings[astTy->name.value] = TypeFun{param.ty};
    }

    LUAU_ASSERT(genericPacks.size == typePackParams.size());
    for (size_t i = 0; i < genericPacks.size; i++)
    {
        auto astPack = genericPacks.data[i];
        auto param = typePackParams[i];
        if (param.defaultValue && astPack->defaultValue != nullptr)
        {
            auto resolvesTo = astPack->defaultValue;
            auto toUnblock = *param.defaultValue;
            emplaceTypePack<BoundTypePack>(asMutable(toUnblock), resolveTypePack(defnScope, resolvesTo, /*  inTypeArguments */ false));
        }
        defnScope->privateTypePackBindings[astPack->name.value] = param.tp;
    }
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatTypeAlias* alias)
{
    if (alias->name == kParseNameError)
        return ControlFlow::None;

    if (alias->name == "typeof")
    {
        reportError(alias->location, ReservedIdentifier{"typeof"});
        return ControlFlow::None;
    }

    scope->typeAliasLocations[alias->name.value] = alias->location;
    scope->typeAliasNameLocations[alias->name.value] = alias->nameLocation;

    ScopePtr* defnScopePtr = astTypeAliasDefiningScopes.find(alias);

    std::unordered_map<Name, TypeFun>* typeBindings;
    if (alias->exported)
        typeBindings = &scope->exportedTypeBindings;
    else
        typeBindings = &scope->privateTypeBindings;

    // These will be undefined if the alias was a duplicate definition, in which
    // case we just skip over it.
    auto bindingIt = typeBindings->find(alias->name.value);
    if (bindingIt == typeBindings->end() || defnScopePtr == nullptr)
        return ControlFlow::None;

    ScopePtr defnScope = *defnScopePtr;
    resolveGenericDefaultParameters(defnScope, alias, bindingIt->second);

    TypeId ty = resolveType(
        defnScope,
        alias->type,
        /* inTypeArguments */ false,
        /* replaceErrorWithFresh */ false
    );

    TypeId aliasTy = bindingIt->second.type;
    LUAU_ASSERT(get<BlockedType>(aliasTy));
    if (occursCheck(aliasTy, ty))
    {
        emplaceType<BoundType>(asMutable(aliasTy), builtinTypes->anyType);
        reportError(alias->nameLocation, OccursCheckFailed{});
    }
    else
        emplaceType<BoundType>(asMutable(aliasTy), ty);

    std::vector<TypeId> typeParams;
    for (const auto& tyParam : createGenerics(defnScope, alias->generics, /* useCache */ true, /* addTypes */ false))
        typeParams.push_back(tyParam.second.ty);

    std::vector<TypePackId> typePackParams;
    for (const auto& tpParam : createGenericPacks(defnScope, alias->genericPacks, /* useCache */ true, /* addTypes */ false))
        typePackParams.push_back(tpParam.second.tp);

    addConstraint(
        scope,
        alias->type->location,
        NameConstraint{
            ty,
            alias->name.value,
            /*synthetic=*/false,
            std::move(typeParams),
            std::move(typePackParams),
        }
    );

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatTypeFunction* function)
{
    if (function->name == "typeof")
    {
        reportError(function->location, ReservedIdentifier{"typeof"});
    }

    auto scopeIt = astTypeFunctionEnvironmentScopes.find(function);
    LUAU_ASSERT(scopeIt);

    ScopePtr environmentScope = *scopeIt;

    Checkpoint startCheckpoint = checkpoint(this);
    FunctionSignature sig = checkFunctionSignature(environmentScope, nullptr, function->body, /* expectedType */ std::nullopt);

    // Place this function as a child of the non-type function scope
    scope->children.emplace_back(sig.signatureScope.get());
    interiorFreeTypes.emplace_back();
    checkFunctionBody(sig.bodyScope, function->body);
    Checkpoint endCheckpoint = checkpoint(this);

    TypeId generalizedTy = arena->addType(BlockedType{});
    NotNull<Constraint> gc = addConstraint(
        sig.signatureScope,
        function->location,
        GeneralizationConstraint{
            generalizedTy,
            sig.signature,
            std::vector<TypeId>{},
        }
    );

    sig.signatureScope->interiorFreeTypes = std::move(interiorFreeTypes.back().types);
    sig.signatureScope->interiorFreeTypePacks = std::move(interiorFreeTypes.back().typePacks);

    getMutable<BlockedType>(generalizedTy)->setOwner(gc);
    interiorFreeTypes.pop_back();

    addAllAsDependenciesAndChainReturns(startCheckpoint, endCheckpoint, this, gc);
    std::optional<TypeId> existingFunctionTy = environmentScope->lookup(function->name);

    if (!existingFunctionTy)
        ice->ice("checkAliases did not populate type function name", function->nameLocation);

    TypeId unpackedTy = follow(*existingFunctionTy);

    if (auto bt = get<BlockedType>(unpackedTy); bt && nullptr == bt->getOwner())
        emplaceType<BoundType>(asMutable(unpackedTy), generalizedTy);

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatDeclareGlobal* global)
{
    // Luwu Declare Statements (rfcs/declare-statements.md): `declare name` says the global exists and keeps the type
    // the environment gives it, or `any` where the environment doesn't declare it. A script can then declare what its
    // host's definitions already describe without losing their types where they are loaded.
    TypeId globalTy = builtinTypes->anyType;
    if (global->type)
        globalTy = resolveType(scope, global->type, /* inTypeArguments */ false);
    else if (std::optional<TypeId> environmentTy = rootScope->parent ? rootScope->parent->lookup(Symbol{global->name}) : std::nullopt)
        globalTy = *environmentTy;
    Name globalName(global->name.value);

    if (declaresFileGlobals())
        bindDeclaration(global->name, globalTy);
    else
        module->declaredGlobals[globalName] = globalTy;
    rootScope->bindings[global->name] = Binding{globalTy, global->location};

    DefId def = dfg->getDef(global);
    rootScope->lvalueTypes[def] = globalTy;
    updateRValueRefinements(rootScope, def, globalTy);

    return ControlFlow::None;
}

static bool isMetamethod(const Name& name)
{
    return name == "__index" || name == "__newindex" || name == "__call" || name == "__concat" || name == "__unm" || name == "__add" ||
           name == "__sub" || name == "__mul" || name == "__div" || name == "__mod" || name == "__pow" || name == "__tostring" ||
           name == "__metatable" || name == "__eq" || name == "__lt" || name == "__le" || name == "__mode" || name == "__iter" || name == "__len" ||
           name == "__idiv";
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatDeclareExternType* declaredExternType)
{
    // If a class with the same name was already defined, we skip over
    // Luwu Declare Statements (rfcs/declare-statements.md): outside definition files, the file's own scope holds it.
    Scope& bindingScope = declaresFileGlobals() ? *rootScope : *scope;
    std::unordered_map<Name, TypeFun>& bindings = externTypeBindings(bindingScope, declaredExternType);
    auto bindingIt = bindings.find(declaredExternType->name.value);
    if (bindingIt == bindings.end())
        return ControlFlow::None;

    std::optional<TypeId> superTy = std::make_optional(builtinTypes->externType);
    if (declaredExternType->superName)
    {
        // Luwu: an error about the supertype underlines its name; upstream underlines the whole declaration.
        Location superNameLocation = declaredExternType->superNameLocation.value_or(declaredExternType->location);
        Name superName = Name(declaredExternType->superName->value);
        std::optional<TypeFun> lookupType = scope->lookupType(superName);

        if (!lookupType)
        {
            reportError(superNameLocation, UnknownSymbol{std::move(superName), UnknownSymbol::Type});
            return ControlFlow::None;
        }

        if (FFlag::LuwuGenericNominals && (lookupType->typeParams.size() != 0 || lookupType->typePackParams.size() != 0))
        {
            // `extends Base<T>` isn't supported yet -- the parser doesn't even accept type
            // arguments after a supertype name -- so the only way to reach this is a generic
            // supertype referenced without any arguments, which we don't allow.
            reportError(
                superNameLocation,
                GenericError{format("Generic extern type '%s' cannot be used as a supertype without type arguments", superName.c_str())}
            );

            emplaceType<BoundType>(asMutable(bindingIt->second.type), builtinTypes->errorType);

            return ControlFlow::None;
        }
        else if (!FFlag::LuwuGenericNominals)
        {
            // We don't have generic extern typeArguments, so this assertion _should_ never be hit.
            LUAU_ASSERT(lookupType->typeParams.size() == 0 && lookupType->typePackParams.size() == 0);
        }

        superTy = follow(lookupType->type);

        if (!get<ExternType>(follow(*superTy)))
        {
            reportError(
                superNameLocation,
                GenericError{
                    // Luwu: upstream calls these classes, from the legacy `declare class` spelling; Luwu classes have no
                    // superclasses, so the message says extern type
                    format("Cannot use non-extern type '%s' as the supertype of extern type '%s'", superName.c_str(), declaredExternType->name.value)
                }
            );

            // If we don't emplace an error type here, then later we'll be
            // exposing a blocked type in this file's type interface. This
            // is _normally_ harmless.
            emplaceType<BoundType>(asMutable(bindingIt->second.type), builtinTypes->errorType);

            return ControlFlow::None;
        }
    }

    Name className(declaredExternType->name.value);

    TypeId externTy =
        arena->addType(ExternType(std::move(className), {}, superTy, std::nullopt, {}, {}, module->name, declaredExternType->location));
    ExternType* etv = getMutable<ExternType>(externTy);

    TypeId metaTy = arena->addType(TableType{TableState::Sealed, scope->level, scope.get()});
    TableType* metatable = getMutable<TableType>(metaTy);

    etv->metatable = metaTy;

    TypeId classBindTy = bindingIt->second.type;
    emplaceType<BoundType>(asMutable(classBindTy), externTy);

    // Indexer and property types are resolved against the extern type's own definition scope
    // (rather than the enclosing scope) so that per-property type references - e.g. a generic
    // method's own type parameters - nest under it instead of becoming sibling scopes that
    // TypeChecker2's location-based scope lookup can never find. See LuwuExternTypeUseDefinitionScope.
    ScopePtr bodyScope = scope;
    if (FFlag::LuwuExternTypeUseDefinitionScope || FFlag::LuwuGenericNominals)
    {
        if (ScopePtr* defnScopePtr = astExternTypeDefiningScopes.find(declaredExternType))
            bodyScope = *defnScopePtr;
    }

    // Bind the extern type's own generics (e.g. the `T` in `declare extern type Box<T> with ... end`)
    // into its definition scope, so that references to `T` inside the indexer, properties, and
    // methods resolve to these type-level generics rather than erroring or (worse) accidentally
    // resolving to an unrelated same-named generic elsewhere in the file.
    if (FFlag::LuwuGenericNominals)
    {
        // This also resolves any defaults the parameter list was written with
        // (`declare extern type Box<T = string> with ... end`).
        resolveGenericDefaultParameters(
            bodyScope,
            declaredExternType->generics,
            declaredExternType->genericPacks,
            bindingIt->second.typeParams,
            bindingIt->second.typePackParams
        );

        // Methods implicitly take `self`, typed below as the bare extern type (externTy). For a
        // generic extern type, `self` needs to be `Box<T>` (applied to the type's own generics),
        // not the bare, unparameterized `Box` -- otherwise calling a method on `Box<number>`
        // fails to match against `self`, since neither side would look like the other nominally.
        for (const GenericTypeDefinition& param : bindingIt->second.typeParams)
            etv->instantiatedTypeParams.push_back(param.ty);
        for (const GenericTypePackDefinition& param : bindingIt->second.typePackParams)
            etv->instantiatedTypePackParams.push_back(param.tp);
        etv->hasUnresolvedGenerics = !etv->instantiatedTypeParams.empty() || !etv->instantiatedTypePackParams.empty();
    }

    if (declaredExternType->indexer)
    {
        if (recursionCount >= DFInt::LuauConstraintGeneratorRecursionLimit)
        {
            reportCodeTooComplex(declaredExternType->indexer->location);
        }
        else
        {
            // I don't think extern types can *be* generic, but if they
            // have an indexer over those generics, the polarity is
            // mixed.
            //
            // mluau fork note (deviaze): extern types can now be generic, behind
            // LuwuGenericNominals. When enabled, this indexer's index/result types
            // may reference the extern type's own generics, bound into bodyScope above.
            etv->indexer = TableIndexer{
                resolveType(
                    bodyScope,
                    declaredExternType->indexer->indexType,
                    /* inTypeArguments */ false,
                    /* replaceErrorWithFresh */ false,
                    /* initialPolarity */ Polarity::Mixed
                ),
                resolveType(
                    bodyScope,
                    declaredExternType->indexer->resultType,
                    /* inTypeArguments */ false,
                    /* replaceErrorWithFresh */ false,
                    /* initialPolarity */ Polarity::Mixed
                ),
            };
        }
    }

    for (const AstDeclaredExternTypeProperty& externProp : declaredExternType->props)
    {
        Name propName(externProp.name.value);
        TypeId propTy = resolveType(
            bodyScope, externProp.ty, /* inTypeArguments */ false, /* replaceErrorWithFresh */ false, /* initialPolarity */ Polarity::Mixed
        );

        bool assignToMetatable = isMetamethod(propName);

        // Function typeArguments always take 'self', but this isn't reflected in the
        // parsed annotation. Add it here.
        if (externProp.isMethod)
        {
            if (FunctionType* ftv = getMutable<FunctionType>(propTy))
            {
                ftv->argNames.insert(ftv->argNames.begin(), FunctionArgument{"self", {}});
                ftv->argTypes = addTypePack({externTy}, ftv->argTypes);

                ftv->hasSelf = true;

                FunctionDefinition defn;

                defn.definitionModuleName = module->name;
                defn.definitionLocation = externProp.location;
                // No data is preserved for varargLocation
                defn.originalNameLocation = externProp.nameLocation;

                ftv->definition = defn;
            }
        }

        TableType::Props& props = assignToMetatable ? metatable->props : etv->props();

        if (props.count(propName) == 0)
        {
            Property tableProp;

            if (externProp.access == AstTableAccess::Read)
                tableProp = Property::readonly(propTy);
            else if (externProp.access == AstTableAccess::Write)
                tableProp = Property::writeonly(propTy);
            else
                tableProp = Property::rw(propTy);

            tableProp.location = externProp.location;

            props[propName] = tableProp;
        }
        else
        {
            Luau::Property& prop = props[propName];
            bool addedWriteTypeByOverload = false;

            if (auto readTy = prop.readTy)
            {
                // We special-case this logic to keep the intersection flat; otherwise we
                // would create a ton of nested intersection typeArguments.
                if (const IntersectionType* itv = get<IntersectionType>(*readTy))
                {
                    std::vector<TypeId> options = itv->parts;
                    options.push_back(propTy);
                    TypeId newItv = arena->addType(IntersectionType{std::move(options)});

                    prop.readTy = newItv;
                }
                else if (get<FunctionType>(*readTy))
                {
                    TypeId intersection = arena->addType(IntersectionType{{*readTy, propTy}});

                    prop.readTy = intersection;
                }
                else if (externProp.access == AstTableAccess::Write && !prop.writeTy.has_value())
                {
                    prop.writeTy = propTy;
                    addedWriteTypeByOverload = true;
                }
                else
                    reportError(
                        externProp.location,
                        GenericError{format("Cannot overload read type of non-function extern type member '%s'", propName.c_str())}
                    );
            }

            if (auto writeTy = prop.writeTy; writeTy && !addedWriteTypeByOverload)
            {
                // We special-case this logic to keep the intersection flat; otherwise we
                // would create a ton of nested intersection typeArguments.
                if (const IntersectionType* itv = get<IntersectionType>(*writeTy))
                {
                    std::vector<TypeId> options = itv->parts;
                    options.push_back(propTy);
                    TypeId newItv = arena->addType(IntersectionType{std::move(options)});

                    prop.writeTy = newItv;
                }
                else if (get<FunctionType>(*writeTy))
                {
                    TypeId intersection = arena->addType(IntersectionType{{*writeTy, propTy}});

                    prop.writeTy = intersection;
                }
                else if (externProp.access == AstTableAccess::Read && !prop.readTy.has_value())
                    prop.readTy = propTy;
                else
                    reportError(
                        externProp.location,
                        GenericError{format("Cannot overload write type of non-function extern type member '%s'", propName.c_str())}
                    );
            }
        }
    }

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatDeclareFunction* global)
{
    std::vector<std::pair<Name, GenericTypeDefinition>> generics = createGenerics(scope, global->generics);
    std::vector<std::pair<Name, GenericTypePackDefinition>> genericPacks = createGenericPacks(scope, global->genericPacks);

    std::vector<TypeId> genericTys;
    genericTys.reserve(generics.size());
    for (auto& [name, generic] : generics)
    {
        genericTys.push_back(generic.ty);
    }

    std::vector<TypePackId> genericTps;
    genericTps.reserve(genericPacks.size());
    for (auto& [name, generic] : genericPacks)
    {
        genericTps.push_back(generic.tp);
    }

    ScopePtr funScope = scope;
    if (!generics.empty() || !genericPacks.empty())
        funScope = childScope(global, scope);

    TypePackId paramPack = resolveTypePack(
        funScope, global->params, /* inTypeArguments */ false, /* replaceErrorWithFresh */ false, /* initialPolarity */ Polarity::Negative
    );
    TypePackId retPack = resolveTypePack(
        funScope, global->retTypes, /* inTypeArguments */ false, /* replaceErrorWithFresh */ false, /* initialPolarity */ Polarity::Positive
    );

    FunctionDefinition defn;

    defn.definitionModuleName = module->name;
    defn.definitionLocation = global->location;
    defn.varargLocation = global->vararg ? std::make_optional(global->varargLocation) : std::nullopt;
    defn.originalNameLocation = global->nameLocation;

    TypeId fnType = arena->addType(FunctionType{TypeLevel{}, std::move(genericTys), std::move(genericTps), paramPack, retPack, defn});

    FunctionType* ftv = getMutable<FunctionType>(fnType);
    ftv->isCheckedFunction = global->isCheckedFunction();
    AstAttr* deprecatedAttr = global->getAttribute(AstAttr::Type::Deprecated);
    ftv->isDeprecatedFunction = deprecatedAttr != nullptr;
    if (deprecatedAttr)
    {
        ftv->deprecatedInfo = std::make_shared<AstAttr::DeprecatedInfo>(deprecatedAttr->deprecatedInfo());
    }

    ftv->argNames.reserve(global->paramNames.size);
    for (const auto& el : global->paramNames)
        ftv->argNames.emplace_back(FunctionArgument{el.first.value, el.second});
    Name fnName(global->name.value);

    if (declaresFileGlobals())
    {
        bindDeclaration(global->name, fnType);
        rootScope->bindings[global->name] = Binding{fnType, global->location};
    }
    else
    {
        module->declaredGlobals[fnName] = fnType;
        scope->bindings[global->name] = Binding{fnType, global->location};
    }

    DefId def = dfg->getDef(global);
    rootScope->lvalueTypes[def] = fnType;
    updateRValueRefinements(rootScope, def, fnType);

    return ControlFlow::None;
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatClass* statClass)
{
    LUAU_ASSERT(FFlag::LuwuClasses);
    visitClass(scope, statClass, /* declared= */ false);
    return ControlFlow::None;
}

// Luwu Declare Statements (rfcs/declare-statements.md): a declared class's members get the types a class's would, from
// its annotations alone.
ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatDeclareClass* declaredClass)
{
    visitClass(scope, declaredClass->shape, /* declared= */ true);
    return ControlFlow::None;
}

void ConstraintGenerator::visitClass(const ScopePtr& scope, AstStatClass* statClass, bool declared)
{
    auto* classDeclRecordPtr = classDeclRecords.find(statClass->name);
    // TODO CLI-199124: This is unpopulated in fragment autocomplete.
    if (classDeclRecordPtr == nullptr)
        return;

    auto classDeclRecord = classDeclRecordPtr->get();

    // Property and method type annotations are resolved against the class's own definition scope
    // (rather than the enclosing scope), so that references to the class's own generics (e.g. the
    // `T` in `class Box<T> ... end`) resolve to these type-level generics.
    ScopePtr bodyScope = scope;
    if (FFlag::LuwuGenericNominals)
    {
        if (ScopePtr* defnScopePtr = astClassDefiningScopes.find(statClass))
            bodyScope = *defnScopePtr;

        // This also resolves any defaults the parameter list was written with (`class Box<T = string>`).
        resolveGenericDefaultParameters(
            bodyScope, statClass->generics, statClass->genericPacks, classDeclRecord->typeParams, classDeclRecord->typePackParams
        );
    }

    // Luwu Traits (rfcs/classes/traits.md): before anything uses the class's members. This is the second pass, so a trait
    // from a required module is resolvable now, which it isn't while prototyping.
    std::map<Name, TypeId> traitFieldTypes;
    if (FFlag::LuwuTraits && statClass->implements.size > 0)
        traitFieldTypes = implementTraits(bodyScope, statClass, classDeclRecord);

    // Luwu Classes (rfcs/classes): a primary constructor's parameters are in scope for the class's
    // field initializer expressions and nowhere else, so they are bound in a scope of their own that
    // the methods below are deliberately not checked in.
    ScopePtr initializerScope = bodyScope;
    std::vector<TypeId> primaryCtorParamTypes;

    if (const AstClassPrimaryConstructor* primaryConstructor = statClass->primaryConstructor)
    {
        // Deliberately not childScope(): that would register the scope under the *class's* location,
        // and TypeChecker2's findInnermostScope would then hand it to everything inside the class,
        // method bodies included -- which loses their return types. Registering it under the
        // parameter list's own location keeps it owned (so types referring to it stay valid) without
        // it enclosing anything it has no business enclosing.
        initializerScope = std::make_shared<Scope>(bodyScope);
        initializerScope->location = primaryConstructor->argLocation;
        initializerScope->returnType = bodyScope->returnType;
        initializerScope->varargPack = bodyScope->varargPack;
        bodyScope->children.emplace_back(initializerScope.get());
        scopes.emplace_back(primaryConstructor->argLocation, initializerScope);

        for (size_t i = 0; i < primaryConstructor->args.size; ++i)
        {
            AstLocal* param = primaryConstructor->args.data[i];
            AstExpr* paramDefault = primaryConstructor->argsDefaults.data[i];

            TypeId paramTy;

            if (param->annotation)
            {
                paramTy = resolveType(bodyScope, param->annotation, /* inTypeArguments */ false);

                // as with a default function argument, the default has to fit the annotation
                if (paramDefault)
                {
                    Inference found = check(bodyScope, paramDefault, paramTy);
                    addConstraint(bodyScope, paramDefault->location, SubtypeConstraint{found.ty, paramTy});
                }
            }
            else if (paramDefault)
                paramTy = check(bodyScope, paramDefault).ty;
            else
                paramTy = builtinTypes->anyType;

            primaryCtorParamTypes.push_back(paramTy);

            initializerScope->bindings[param] = Binding{paramTy, param->location};

            if (!FFlag::DebugLuauCFG)
                initializerScope->lvalueTypes[dfg->getDef(param)] = paramTy;
        }
    }

    if (FFlag::LuwuTraits && statClass->implements.size > 0)
        checkTraitArguments(initializerScope, statClass);

    // Which parameter, if any, a class member restates -- `class Card(hash: string) private const hash
    // end` names one field, declared by the parameter and given its access by the restatement.
    auto primaryCtorParamIndex = [&](const AstName& name) -> std::optional<size_t>
    {
        if (!statClass->primaryConstructor)
            return std::nullopt;

        for (size_t i = 0; i < statClass->primaryConstructor->args.size; ++i)
            if (statClass->primaryConstructor->args.data[i]->name == name)
                return i;

        return std::nullopt;
    };

    for (const auto& member : statClass->members)
    {
        Luau::visit(
            overloaded{
                [&](const AstClassProperty& classProp)
                {
                    auto entry = classDeclRecord->memberTypes.find(classProp.name);
                    if (entry == nullptr)
                    {
                        LUAU_ASSERT(!"Unexpected missing class property type");
                        return;
                    }

                    auto blockedTy = follow(*entry);
                    if (!is<BlockedType>(blockedTy))
                        return;

                    // With no explicit type annotation, infer the property's type from its default
                    // value expression (if any) rather than falling back to `any`. When there IS
                    // an annotation, the property keeps that type, but the default value must
                    // still be checked against it (mirrors default function argument checking
                    // in checkFunctionSignature).
                    TypeId target;
                    if (classProp.ty)
                    {
                        target = resolveType(bodyScope, classProp.ty, false);
                        if (classProp.defaultValue)
                        {
                            Inference found = check(initializerScope, classProp.defaultValue, target);
                            addConstraint(initializerScope, classProp.defaultValue->location, SubtypeConstraint{found.ty, target});
                        }
                        else if (std::optional<size_t> paramIndex = primaryCtorParamIndex(classProp.name))
                        {
                            // a bare restatement (`private breed: number`) is still initialized from the
                            // parameter it names, so the parameter's type has to fit the annotation the
                            // restatement gives the field -- there just isn't an expression to blame
                            addConstraint(
                                initializerScope, classProp.nameLocation, SubtypeConstraint{primaryCtorParamTypes[*paramIndex], target}
                            );
                        }
                    }
                    else if (classProp.defaultValue)
                    {
                        // Luwu Traits (rfcs/classes/traits.md): a field a trait expects is inferred against the trait's type
                        auto traitFieldType = traitFieldTypes.find(classProp.name.value);
                        std::optional<TypeId> expectedType;
                        if (traitFieldType != traitFieldTypes.end())
                            expectedType = traitFieldType->second;

                        target = check(initializerScope, classProp.defaultValue, expectedType).ty;
                    }
                    else if (std::optional<size_t> paramIndex = primaryCtorParamIndex(classProp.name))
                        // a bare restatement (`private const hash`) is initialized from the parameter
                        // it names, so it has that parameter's type
                        target = primaryCtorParamTypes[*paramIndex];
                    else
                        target = builtinTypes->anyType;
                    emplaceType<BoundType>(asMutable(blockedTy), target);
                },
                [&](const AstClassMethod& method)
                {
                    auto entry = classDeclRecord->memberTypes.find(method.functionName);
                    if (entry == nullptr)
                    {
                        LUAU_ASSERT(!"Unexpected missing class method type");
                        return;
                    }

                    auto functionType = follow(*entry);

                    // TODO: This might have strange behavior if you ever
                    // copy a method.
                    if (!is<BlockedType>(functionType))
                        return;

                    // Luwu Traits (rfcs/classes/traits.md): an unannotated `__create` returns the trait, so its returns are
                    // checked against it; an annotation can make it return anything
                    std::optional<TypeId> expectedType;
                    bool unannotatedTraitCreate = statClass->isTrait && method.functionName == "__create" && !method.function->returnAnnotation;
                    if (unannotatedTraitCreate)
                        expectedType = arena->addType(FunctionType{arena->addTypePack({}), arena->addTypePack({classDeclRecord->ty})});

                    FunctionSignature sig =
                        checkFunctionSignature(bodyScope, classDeclRecord, method.function, expectedType, method.function->location);

                    Checkpoint start = checkpoint(this);
                    // a declared method, and a trait's expected function (Luwu Traits (rfcs/classes/traits.md)), are only a signature
                    if (!declared && !method.expectLocation)
                        checkFunctionBody(sig.bodyScope, method.function);
                    Checkpoint end = checkpoint(this);

                    // Luwu Traits (rfcs/classes/traits.md): calling the trait calls `__create` with the same arguments; unannotated, it returns
                    // the trait (TypeChecker2 checks the body against that)
                    bool isTraitCreate = statClass->isTrait && method.functionName == "__create" && !method.expectLocation;
                    const FunctionType* createSig = isTraitCreate ? get<FunctionType>(follow(sig.signature)) : nullptr;
                    if (createSig && classDeclRecord->ctorTy && is<BlockedType>(follow(classDeclRecord->ctorTy)))
                    {
                        auto [argHead, argTail] = flatten(createSig->argTypes);

                        std::vector<TypeId> ctorArgs{builtinTypes->unknownType};
                        ctorArgs.insert(ctorArgs.end(), argHead.begin(), argHead.end());

                        TypePackId ctorArgsPack = argTail ? arena->addTypePack(std::move(ctorArgs), *argTail) : arena->addTypePack(std::move(ctorArgs));
                        TypePackId ctorRets = method.function->returnAnnotation ? createSig->retTypes : arena->addTypePack({classDeclRecord->ty});

                        TypeId ctor = arena->addType(FunctionType{
                            createSig->generics, createSig->genericPacks, ctorArgsPack, ctorRets, /* defn */ std::nullopt, /* hasSelf */ true
                        });

                        if (FunctionType* ctorFtv = getMutable<FunctionType>(ctor))
                        {
                            ctorFtv->argNames.push_back(std::nullopt);
                            ctorFtv->argNames.insert(ctorFtv->argNames.end(), createSig->argNames.begin(), createSig->argNames.end());
                        }

                        emplaceType<BoundType>(asMutable(follow(classDeclRecord->ctorTy)), ctor);
                    }

                    // Luwu Traits (rfcs/classes/traits.md): a trait's expected `__init` is how `class<Trait>` is called: with its arguments after
                    // `self`, returning the trait
                    if (statClass->isTrait && method.functionName == "__init")
                        bindTraitImplementorConstructor(classDeclRecord, sig.signature);

                    if (method.functionName == "__init" && !statClass->isTrait)
                    {
                        if (FFlag::LuwuClasses)
                        {
                            if (ExternType* classInstanceEtv = getMutable<ExternType>(follow(classDeclRecord->ty)))
                                classInstanceEtv->initLocation = method.function->location;
                        }

                        if (const FunctionType* initSig = get<FunctionType>(follow(sig.signature)); initSig && is<BlockedType>(follow(classDeclRecord->ctorTy)))
                        {
                            auto [argHead, argTail] = flatten(initSig->argTypes);

                            std::vector<TypeId> ctorArgs;
                            ctorArgs.push_back(builtinTypes->unknownType);
                            if (argHead.size() > 1)
                                ctorArgs.insert(ctorArgs.end(), argHead.begin() + 1, argHead.end());

                            TypePackId ctorArgsPack =
                                argTail ? arena->addTypePack(std::move(ctorArgs), *argTail) : arena->addTypePack(std::move(ctorArgs));

                            std::vector<TypeId> ctorGenerics;
                            std::vector<TypePackId> ctorGenericPacks;
                            for (const GenericTypeDefinition& param : classDeclRecord->typeParams)
                                ctorGenerics.push_back(param.ty);
                            for (const GenericTypePackDefinition& param : classDeclRecord->typePackParams)
                                ctorGenericPacks.push_back(param.tp);

                            TypeId newCtorTy = arena->addType(FunctionType{
                                std::move(ctorGenerics),
                                std::move(ctorGenericPacks),
                                ctorArgsPack,
                                arena->addTypePack({classDeclRecord->ty}),
                                /* defn */ std::nullopt,
                                /* hasSelf */ true
                            });

                            // Preserve `__init`'s parameter names (e.g. `name`, `age`) on the
                            // synthesized constructor type, so tooling that prints the
                            // constructor's signature (e.g. hover) shows `Cat(name: string, age:
                            // number)` rather than unnamed parameters.
                            if (FunctionType* newCtorFtv = getMutable<FunctionType>(newCtorTy))
                                newCtorFtv->argNames = initSig->argNames;

                            emplaceType<BoundType>(asMutable(follow(classDeclRecord->ctorTy)), newCtorTy);
                        }
                    }

                    NotNull<Scope> constraintScope{sig.signatureScope ? sig.signatureScope.get() : sig.bodyScope.get()};
                    std::unique_ptr<Constraint> c = std::make_unique<Constraint>(
                        constraintScope, method.function->location, GeneralizationConstraint{functionType, sig.signature}
                    );

                    propagateDeprecatedAttributeToConstraint(c->c, method.function);

                    TypeId* classValueMethodTy = classDeclRecord->classValueMethodTypes.find(method.functionName);
                    if (classValueMethodTy)
                    {
                        GeneralizationConstraint* gc = get_if<GeneralizationConstraint>(&c->c);
                        LUAU_ASSERT(gc);
                        gc->classValueMethodType = *classValueMethodTy;
                        for (const GenericTypeDefinition& param : classDeclRecord->typeParams)
                            gc->classGenerics.push_back(param.ty);
                        for (const GenericTypePackDefinition& param : classDeclRecord->typePackParams)
                            gc->classGenericPacks.push_back(param.tp);
                    }

                    addAllAsDependenciesAndChainReturns(start, end, this, NotNull{c.get()});

                    NotNull<Constraint> generalization = addConstraint(scope, std::move(c));
                    getMutable<BlockedType>(functionType)->setOwner(generalization);
                    if (classValueMethodTy)
                        getMutable<BlockedType>(*classValueMethodTy)->setOwner(generalization);
                }
            },
            member
        );
    }

    // Luwu Classes (rfcs/classes): with the parameters' types resolved, the constructor a primary
    // constructor implies can be built -- `Cat(name: string, age: number)` -- along with the `__init`
    // the RFC says it defines, and the field each parameter the class body didn't restate declares.
    if (const AstClassPrimaryConstructor* primaryConstructor = statClass->primaryConstructor)
    {
        std::vector<TypeId> ctorArgs;
        std::vector<TypeId> initArgs;
        std::vector<std::optional<FunctionArgument>> argNames;

        // the constructor is reached as the class value's `__call`, so its first argument is the
        // class itself; `__init` takes the instance instead
        ctorArgs.push_back(builtinTypes->unknownType);
        initArgs.push_back(classDeclRecord->ty);
        argNames.push_back(std::nullopt);

        for (size_t i = 0; i < primaryConstructor->args.size; ++i)
        {
            AstLocal* param = primaryConstructor->args.data[i];
            TypeId paramTy = primaryCtorParamTypes[i];

            // An argument for a parameter with a default may be left out; what the field ends up with
            // is the default rather than nil, so only the *signature* is optional here, not the type
            // the parameter has inside the class.
            // A declared class writes a default as `name = T`: no expression, just the `=`.
            bool declaredDefault = primaryConstructor->argsQualifiers.size == primaryConstructor->args.size &&
                                   primaryConstructor->argsQualifiers.data[i].declaredDefaultLocation;
            bool hasDefault = primaryConstructor->argsDefaults.data[i] || declaredDefault;
            TypeId argTy = hasDefault ? makeOption(builtinTypes, *arena, paramTy) : paramTy;

            ctorArgs.push_back(argTy);
            initArgs.push_back(argTy);
            argNames.push_back(FunctionArgument{param->name.value, param->location});

            // the parameter's own field, when the class body doesn't restate it (a restatement
            // resolves the member type itself, above)
            if (TypeId* memberTy = classDeclRecord->memberTypes.find(param->name))
            {
                TypeId blockedTy = follow(*memberTy);
                if (is<BlockedType>(blockedTy))
                    emplaceType<BoundType>(asMutable(blockedTy), paramTy);
            }
        }

        std::vector<TypeId> ctorGenerics;
        std::vector<TypePackId> ctorGenericPacks;
        for (const GenericTypeDefinition& param : classDeclRecord->typeParams)
            ctorGenerics.push_back(param.ty);
        for (const GenericTypePackDefinition& param : classDeclRecord->typePackParams)
            ctorGenericPacks.push_back(param.tp);

        TypeId newCtorTy = arena->addType(FunctionType{
            ctorGenerics,
            ctorGenericPacks,
            arena->addTypePack(std::move(ctorArgs)),
            arena->addTypePack({classDeclRecord->ty}),
            /* defn */ std::nullopt,
            /* hasSelf */ true
        });

        // keep the parameter names, so tooling prints `Cat(name: string, age: number)`
        if (FunctionType* newCtorFtv = getMutable<FunctionType>(newCtorTy))
            newCtorFtv->argNames = argNames;

        if (classDeclRecord->ctorTy && is<BlockedType>(follow(classDeclRecord->ctorTy)))
            emplaceType<BoundType>(asMutable(follow(classDeclRecord->ctorTy)), newCtorTy);

        TypeId newInitTy = arena->addType(FunctionType{
            std::move(ctorGenerics),
            std::move(ctorGenericPacks),
            arena->addTypePack(std::move(initArgs)),
            arena->addTypePack({}),
            /* defn */ std::nullopt,
            /* hasSelf */ true
        });

        if (FunctionType* newInitFtv = getMutable<FunctionType>(newInitTy))
            newInitFtv->argNames = std::move(argNames);

        if (classDeclRecord->primaryInitTy && is<BlockedType>(follow(classDeclRecord->primaryInitTy)))
            emplaceType<BoundType>(asMutable(follow(classDeclRecord->primaryInitTy)), newInitTy);

        if (ExternType* classInstanceEtv = getMutable<ExternType>(follow(classDeclRecord->ty)))
            classInstanceEtv->initLocation = primaryConstructor->argLocation;
    }
}

ControlFlow ConstraintGenerator::visit(const ScopePtr& scope, AstStatError* error)
{
    for (AstStat* stat : error->statements)
        visit(scope, stat);
    for (AstExpr* expr : error->expressions)
        check(scope, expr);

    return ControlFlow::None;
}

InferencePack ConstraintGenerator::checkPack(const ScopePtr& scope, AstArray<AstExpr*> exprs, const std::vector<std::optional<TypeId>>& expectedTypes)
{
    std::vector<TypeId> head;
    std::optional<TypePackId> tail;

    for (size_t i = 0; i < exprs.size; ++i)
    {
        AstExpr* expr = exprs.data[i];
        if (i < exprs.size - 1)
        {
            std::optional<TypeId> expectedType;
            if (i < expectedTypes.size())
                expectedType = expectedTypes[i];
            head.push_back(check(scope, expr, expectedType).ty);
        }
        else
        {
            std::vector<std::optional<TypeId>> expectedTailTypes;
            if (i < expectedTypes.size())
                expectedTailTypes.assign(begin(expectedTypes) + i, end(expectedTypes));
            tail = checkPack(scope, expr, expectedTailTypes).tp;
        }
    }

    return InferencePack{addTypePack(std::move(head), tail)};
}

InferencePack ConstraintGenerator::checkPack(
    const ScopePtr& scope,
    AstExpr* expr,
    const std::vector<std::optional<TypeId>>& expectedTypes,
    bool generalize
)
{
    RecursionCounter counter{&recursionCount};

    if (recursionCount >= DFInt::LuauConstraintGeneratorRecursionLimit)
    {
        reportCodeTooComplex(expr->location);
        return InferencePack{builtinTypes->errorTypePack};
    }

    InferencePack result;

    if (AstExprCall* call = expr->as<AstExprCall>())
    {
        std::optional<TypeId> expectedType;
        if (FFlag::LuwuGenericNominals && !expectedTypes.empty())
            expectedType = expectedTypes[0];
        result = checkPack(scope, call, expectedType);
    }
    else if (expr->is<AstExprVarargs>())
    {
        if (scope->varargPack)
            result = InferencePack{*scope->varargPack};
        else
            result = InferencePack{builtinTypes->errorTypePack};
    }
    else
    {
        std::optional<TypeId> expectedType;
        if (!expectedTypes.empty())
            expectedType = expectedTypes[0];
        TypeId t = check(scope, expr, expectedType, /*forceSingletons*/ false, generalize).ty;
        result = InferencePack{arena->addTypePack({t})};
    }

    LUAU_ASSERT(result.tp);
    module->astTypePacks[expr] = result.tp;
    return result;
}

// If `declaredParamTy` is exactly `genericTy`, the concrete argument type at that position
// resolves the generic directly. If `declaredParamTy` is `{genericTy}` (an array of the
// generic), the concrete argument's own array element type resolves the generic. This covers
// the common `f<V>(container: {V}, value: V)` shape (e.g. `table.insert`) so that a later
// argument's expected type can be refined using an earlier, already-checked sibling argument.
static std::optional<TypeId> tryResolveGenericFromArrayArg(TypeId genericTy, TypeId declaredParamTy, TypeId concreteArgTy)
{
    declaredParamTy = follow(declaredParamTy);
    concreteArgTy = follow(concreteArgTy);

    if (declaredParamTy == genericTy)
        return concreteArgTy;

    if (const TableType* declaredTable = get<TableType>(declaredParamTy))
    {
        if (declaredTable->indexer && follow(declaredTable->indexer->indexResultType) == genericTy)
        {
            if (const TableType* concreteTable = get<TableType>(concreteArgTy))
            {
                if (concreteTable->indexer)
                    return concreteTable->indexer->indexResultType;
            }
        }
    }

    return std::nullopt;
}

// True if `ty` is itself a bare generic, or a union containing one. Used to detect when the
// coarse, syntax-directed expected type for a call argument (computed without knowledge of
// sibling arguments) is uninformative because it still mentions an unresolved generic.
static bool isOrContainsBareGeneric(TypeId ty)
{
    ty = follow(ty);
    if (get<GenericType>(ty))
        return true;
    if (auto utv = get<UnionType>(ty))
    {
        for (TypeId opt : utv)
            if (get<GenericType>(follow(opt)))
                return true;
    }
    return false;
}

InferencePack ConstraintGenerator::checkPack(const ScopePtr& scope, AstExprCall* call, std::optional<TypeId> expectedType)
{
    Checkpoint funcBeginCheckpoint = checkpoint(this);

    TypeId fnType = nullptr;
    {
        InConditionalContext icc2{&typeContext, TypeContext::Default};
        fnType = check(scope, call->func).ty;
    }

    Checkpoint funcEndCheckpoint = checkpoint(this);

    return checkExprCall(scope, call, fnType, funcBeginCheckpoint, funcEndCheckpoint, expectedType);
}

InferencePack ConstraintGenerator::checkExprCall(
    const ScopePtr& scope,
    AstExprCall* call,
    TypeId fnType,
    Checkpoint funcBeginCheckpoint,
    Checkpoint funcEndCheckpoint,
    std::optional<TypeId> expectedType
)
{
    std::vector<AstExpr*> exprArgs;

    std::vector<RefinementId> returnRefinements;
    std::vector<std::optional<TypeId>> discriminantTypes;
    std::vector<std::optional<TypeId>> negativeDiscriminantTypes;

    // Each argument the call could refine gets a discriminant the solver binds once it knows the function.
    // Luwu user-defined refinements: and, with DebugLuwuUserDefinedRefinements on, one for when the call is falsy.
    auto addArgument = [&](AstExpr* arg)
    {
        exprArgs.push_back(arg);

        const RefinementKey* key = dfg->getRefinementKey(arg);
        if (!key)
        {
            discriminantTypes.emplace_back(std::nullopt);
            negativeDiscriminantTypes.emplace_back(std::nullopt);
            return;
        }

        TypeId discriminantTy = arena->addType(BlockedType{});
        TypeId negativeDiscriminantTy = FFlag::DebugLuwuUserDefinedRefinements ? arena->addType(BlockedType{}) : nullptr;
        returnRefinements.push_back(refinementArena.implicitProposition(key, discriminantTy, negativeDiscriminantTy));
        discriminantTypes.emplace_back(discriminantTy);
        negativeDiscriminantTypes.emplace_back(negativeDiscriminantTy ? std::optional<TypeId>{negativeDiscriminantTy} : std::nullopt);
    };

    if (call->self)
    {
        AstExprIndexName* indexExpr = call->func->as<AstExprIndexName>();
        if (!indexExpr)
            ice->ice("method call expression has no 'self'");

        addArgument(indexExpr->expr);
    }

    for (AstExpr* arg : call->args)
        addArgument(arg);

    std::vector<std::optional<TypeId>> expectedTypesForCall = getExpectedCallTypesForFunctionOverloads(fnType);

    module->astOriginalCallTypes[call->func] = fnType;

    Checkpoint argBeginCheckpoint = checkpoint(this);

    std::vector<TypeId> args;
    std::optional<TypePackId> argTail;
    std::vector<RefinementId> argumentRefinements;

    for (size_t i = 0; i < exprArgs.size(); ++i)
    {
        AstExpr* arg = exprArgs[i];

        if (i == 0 && call->self)
        {
            // The self type has already been computed as a side effect of
            // computing fnType.  If computing that did not cause us to exceed a
            // recursion limit, we can fetch it from astTypes rather than
            // recomputing it.
            TypeId* selfTy = module->astTypes.find(exprArgs[0]);
            if (selfTy)
                args.push_back(*selfTy);
            else
                args.push_back(freshType(scope, Polarity::Negative));
        }
        else if (i < exprArgs.size() - 1 || !(arg->is<AstExprCall>() || arg->is<AstExprVarargs>()))
        {
            std::optional<TypeId> expectedType = std::nullopt;
            if (i < expectedTypesForCall.size())
            {
                expectedType = expectedTypesForCall[i];
            }

            // Example: `table.insert<V>(t: {V}, value: V)`. `expectedType` for `value` is just
            // the bare generic `V` here, since it was computed before any argument was checked.
            // `t` (arg 0) has already been checked by this point though, so if it's a `{Foo}`,
            // we can plug `Foo` in for `V` and get a real expected type for `value`.
            if (i > 0 && expectedType && isOrContainsBareGeneric(*expectedType))
            {
                std::vector<TypeId> candidateOverloads;
                TypeId followedFn = follow(fnType);
                if (auto itv = get<IntersectionType>(followedFn))
                {
                    for (TypeId part : itv)
                        candidateOverloads.push_back(part);
                }
                else
                    candidateOverloads.push_back(followedFn);

                std::vector<TypeId> resolvedOptions;
                for (TypeId overload : candidateOverloads)
                {
                    const FunctionType* ov = get<FunctionType>(follow(overload));
                    if (!ov || ov->generics.empty())
                        continue;

                    // `exprArgs` (and `args`) already start with the `self` argument of a `:` call,
                    // so argument `i` lines up with parameter `i` whether or not `ov` has `self`.
                    auto [ovArgsHead, ovArgsTail] = flatten(ov->argTypes);
                    if (i >= ovArgsHead.size())
                        continue;

                    TypeId declaredParamTy = follow(ovArgsHead[i]);

                    bool isBareGenericParam = false;
                    for (TypeId g : ov->generics)
                    {
                        if (follow(g) == declaredParamTy)
                        {
                            isBareGenericParam = true;
                            break;
                        }
                    }
                    if (!isBareGenericParam)
                        continue;

                    for (size_t j = 0; j < i && j < args.size(); ++j)
                    {
                        if (auto resolvedTy = tryResolveGenericFromArrayArg(declaredParamTy, follow(ovArgsHead[j]), follow(args[j])))
                        {
                            resolvedOptions.push_back(*resolvedTy);
                            break;
                        }
                    }
                }

                if (!resolvedOptions.empty())
                {
                    std::vector<TypeId> reduced = reduceUnion(resolvedOptions);
                    if (reduced.size() == 1)
                        expectedType = reduced[0];
                    else if (!reduced.empty())
                        expectedType = makeUnion(std::move(reduced));
                }
            }

            if (i == 0 && matchAssert(*call))
            {
                InConditionalContext flipper{&typeContext};
                auto [ty, refinement] = check(scope, arg, expectedType, /*forceSingleton*/ false, /*generalize*/ false);
                args.push_back(ty);
                argumentRefinements.push_back(refinement);
            }
            else
            {
                auto [ty, refinement] = check(scope, arg, expectedType, /*forceSingleton*/ false, /*generalize*/ false);
                args.push_back(ty);
                argumentRefinements.push_back(refinement);
            }
        }
        else
        {
            std::vector<std::optional<Luau::TypeId>> expectedTypes = {};
            if (i < expectedTypesForCall.size())
            {
                expectedTypes.insert(expectedTypes.end(), expectedTypesForCall.begin() + int(i), expectedTypesForCall.end());
            }
            auto [tp, refis] = checkPack(scope, arg, expectedTypes);
            argTail = tp;
            argumentRefinements.insert(argumentRefinements.end(), refis.begin(), refis.end());
        }
    }

    Checkpoint argEndCheckpoint = checkpoint(this);

    // Luwu: upstream refines `x` in `class.isinstance(x, C)` here, matching the call by its spelling (a global's
    // `.isinstance`). Luwu does it in MagicClassInstanceCheck::refine, by the callee's type, so an alias (`const is =
    // class.isinstance`) refines like the original, as it already compiled like it.
    if (matchSetMetatable(*call))
    {
        TypePack argTailPack;
        if (argTail && args.size() < 2)
            argTailPack = extendTypePack(*arena, builtinTypes, *argTail, 2 - args.size());

        TypeId target = nullptr;
        TypeId mt = nullptr;

        if (args.size() + argTailPack.head.size() == 2)
        {
            target = args.size() > 0 ? args[0] : argTailPack.head[0];
            mt = args.size() > 1 ? args[1] : argTailPack.head[args.size() == 0 ? 1 : 0];
        }
        else
        {
            std::vector<TypeId> unpackedTypes;
            if (args.size() > 0)
                target = follow(args[0]);
            else
            {
                target = arena->addType(BlockedType{});
                unpackedTypes.emplace_back(target);
            }

            mt = arena->addType(BlockedType{});
            unpackedTypes.emplace_back(mt);

            auto c = addConstraint(scope, call->location, UnpackConstraint{std::move(unpackedTypes), *argTail});
            getMutable<BlockedType>(mt)->setOwner(c);
            if (auto b = getMutable<BlockedType>(target); b && b->getOwner() == nullptr)
                b->setOwner(c);
        }

        LUAU_ASSERT(target);
        LUAU_ASSERT(mt);

        target = follow(target);

        // Luwu Classes (rfcs/classes): setmetatable raises on an object, class or trait value, and
        // MagicSetMetatable::typeCheck reports it. Leave the target's binding alone, so later uses don't see a metatable
        // wrapped around an object. A target whose type isn't known yet here (a local initialized from a call) still
        // gets one.
        if (luwuNominalKind(target))
            return InferencePack{arena->addTypePack({target}), {refinementArena.variadic(returnRefinements)}};

        AstExpr* targetExpr = call->args.data[0];

        TypeId resultTy = nullptr;

        if (isTableUnion(target))
        {
            const UnionType* targetUnion = get<UnionType>(target);
            UnionBuilder ub{arena, builtinTypes};

            for (TypeId ty : targetUnion)
                ub.add(arena->addType(MetatableType{ty, mt}));

            resultTy = ub.build();
        }
        else
            resultTy = arena->addType(MetatableType{target, mt});

        if (AstExprLocal* targetLocal = targetExpr->as<AstExprLocal>())
        {
            scope->bindings[targetLocal->local].typeId = resultTy;

            DefId def = dfg->getDef(targetLocal);
            scope->lvalueTypes[def] = resultTy;            // TODO: typestates: track this as an assignment
            updateRValueRefinements(scope, def, resultTy); // TODO: typestates: track this as an assignment

            // HACK: If we have a targetLocal, it has already been added to the
            // inferredBindings table.  We want to replace it so that we don't
            // infer a weird union like tbl | { @metatable something, tbl }
            if (InferredBinding* ib = inferredBindings.find(targetLocal->local))
                ib->types.erase(target);

            recordInferredBinding(targetLocal->local, resultTy);
        }

        return InferencePack{arena->addTypePack({resultTy}), {refinementArena.variadic(returnRefinements)}};
    }

    if (shouldTypestateForFirstArgument(*call) && call->args.size > 0 && isLValue(call->args.data[0]))
    {
        AstExpr* targetExpr = call->args.data[0];
        auto resultTy = arena->addType(BlockedType{});

        if (auto def = dfg->getDefOptional(targetExpr))
        {
            scope->lvalueTypes[*def] = resultTy;
            updateRValueRefinements(scope, *def, resultTy);
        }
    }

    if (matchAssert(*call) && !argumentRefinements.empty())
        applyRefinements(scope, call->args.data[0]->location, argumentRefinements[0]);

    // TODO: How do expectedTypes play into this?  Do they?
    TypePackId rets = arena->addTypePack(BlockedTypePack{});
    TypePackId argPack = addTypePack(std::move(args), argTail);
    FunctionType ftv(TypeLevel{}, argPack, rets, std::nullopt, call->self);

    auto [explicitTypeIds, explicitTypePackIds] =
        call->typeArguments.size ? resolveTypeArguments(scope, call->typeArguments) : std::pair<std::vector<TypeId>, std::vector<TypePackId>>();

    /*
     * To make bidirectional type checking work, we need to solve these constraints in a particular order:
     *
     * 1. Solve the function type
     * 2. Propagate type information from the function type to the argument typeArguments
     * 3. Solve the argument typeArguments
     * 4. Solve the call
     */

    NotNull<Constraint> checkConstraint = addConstraint(
        scope,
        call->func->location,
        FunctionCheckConstraint{
            fnType,
            argPack,
            call,
            NotNull{&module->astTypes},
            NotNull{&module->astExpectedTypes},
            FFlag::LuwuGenericNominals ? expectedType : std::nullopt,
        }
    );

    addAllAsDependencies(funcBeginCheckpoint, funcEndCheckpoint, this, checkConstraint);

    FunctionCallConstraint callConstraintData{
        fnType,
        argPack,
        rets,
        call,
        std::move(discriminantTypes),
        std::move(explicitTypeIds),
        std::move(explicitTypePackIds),
        &module->astOverloadResolvedTypes,
        FFlag::LuwuGenericNominals ? expectedType : std::nullopt,
    };
    if (FFlag::DebugLuwuUserDefinedRefinements)
        callConstraintData.negativeDiscriminantTypes = std::move(negativeDiscriminantTypes);

    NotNull<Constraint> callConstraint = addConstraint(scope, call->func->location, std::move(callConstraintData));

    getMutable<BlockedTypePack>(rets)->owner = callConstraint.get();

    cgraph->addDependencyOf(checkConstraint, callConstraint);
    forEachConstraint(
        argBeginCheckpoint,
        argEndCheckpoint,
        this,
        [this, checkConstraint, callConstraint](const ConstraintPtr& constraint)
        {
            cgraph->addDependencyOf(checkConstraint, constraint.get());
            cgraph->addDependencyOf(constraint.get(), callConstraint);
        }
    );

    return InferencePack{rets, {refinementArena.variadic(returnRefinements)}};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExpr* expr, std::optional<TypeId> expectedType, bool forceSingleton, bool generalize)
{
    RecursionCounter counter{&recursionCount};

    if (recursionCount >= DFInt::LuauConstraintGeneratorRecursionLimit)
    {
        reportCodeTooComplex(expr->location);
        return Inference{builtinTypes->errorType};
    }

    // We may recurse a given expression more than once when checking compound
    // assignment, so we store and cache expressions here s.t. when we generate
    // constraints for something like:
    //
    //   a[b] += c
    //
    // We only solve _one_ set of constraints for `b`.
    if (inferredExprCache.contains(expr))
        return inferredExprCache[expr];

    Inference result;

    if (auto group = expr->as<AstExprGroup>())
        result = check(scope, group->expr, expectedType, forceSingleton, generalize);
    else if (auto stringExpr = expr->as<AstExprConstantString>())
        result = check(scope, stringExpr, expectedType, forceSingleton);
    else if (expr->is<AstExprConstantNumber>())
        result = Inference{builtinTypes->numberType};
    else if (expr->is<AstExprConstantInteger>())
        result = Inference{builtinTypes->integerType};
    else if (auto boolExpr = expr->as<AstExprConstantBool>())
        result = check(scope, boolExpr, expectedType, forceSingleton);
    else if (expr->is<AstExprConstantNil>())
        result = Inference{builtinTypes->nilType};
    else if (auto local = expr->as<AstExprLocal>())
        result = check(scope, local);
    else if (auto global = expr->as<AstExprGlobal>())
        result = check(scope, global);
    else if (expr->is<AstExprVarargs>())
        result = flattenPack(scope, expr->location, checkPack(scope, expr));
    else if (auto call = expr->as<AstExprCall>())
        result = flattenPack(scope, expr->location, checkPack(scope, call, expectedType));
    else if (auto a = expr->as<AstExprFunction>())
        result = check(scope, a, expectedType, generalize);
    else if (auto indexName = expr->as<AstExprIndexName>())
        result = check(scope, indexName);
    else if (auto indexExpr = expr->as<AstExprIndexExpr>())
        result = check(scope, indexExpr);
    else if (auto table = expr->as<AstExprTable>())
        result = check(scope, table, expectedType);
    else if (auto unary = expr->as<AstExprUnary>())
        result = check(scope, unary);
    else if (auto binary = expr->as<AstExprBinary>())
        result = check(scope, binary, expectedType);
    else if (auto ifElse = expr->as<AstExprIfElse>())
        result = check(scope, ifElse, expectedType);
    else if (auto typeAssert = expr->as<AstExprTypeAssertion>())
        result = check(scope, typeAssert);
    else if (auto interpString = expr->as<AstExprInterpString>())
        result = check(scope, interpString);
    else if (auto explicitTypeInstantiation = expr->as<AstExprInstantiate>())
        result = check(scope, explicitTypeInstantiation);
    else if (auto err = expr->as<AstExprError>())
    {
        // Open question: Should we traverse into this?
        for (AstExpr* subExpr : err->expressions)
            check(scope, subExpr);

        result = Inference{builtinTypes->errorType};
    }
    else
    {
        LUAU_ASSERT(0);
        result = Inference{freshType(scope)};
    }

    inferredExprCache[expr] = result;

    LUAU_ASSERT(result.ty);
    module->astTypes[expr] = result.ty;
    if (expectedType)
        module->astExpectedTypes[expr] = *expectedType;
    return result;
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprConstantString* string, std::optional<TypeId> expectedType, bool forceSingleton)
{

    if (forceSingleton)
        return Inference{arena->addType(SingletonType{StringSingleton{std::string{string->value.data, string->value.size}}})};

    // Consider a table like:
    //
    //  local DICTIONARY = { "aback", "abacus", "abandon", --[[ so on and so forth ]] }
    //
    // The intent is (probably) not for this to be an array-like table with a massive
    // union for the value, but instead a `{ string }`.
    if (largeTableDepth > 0)
        return Inference{builtinTypes->stringType};

    TypeId freeTy = freshType(scope, Polarity::Positive);
    FreeType* ft = getMutable<FreeType>(freeTy);
    LUAU_ASSERT(ft);
    ft->lowerBound = arena->addType(SingletonType{StringSingleton{std::string{string->value.data, string->value.size}}});
    ft->upperBound = builtinTypes->stringType;
    if (FFlag::LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier)
    {
        ft->primitiveType = builtinTypes->stringType;
        if (expectedType)
            addConstraint(scope, string->location, SubtypeConstraint{freeTy, *expectedType});
    }
    else
    {
        addConstraint(scope, string->location, DEPRECATED_PrimitiveTypeConstraint{freeTy, expectedType, builtinTypes->stringType});
    }
    return Inference{freeTy};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprConstantBool* boolExpr, std::optional<TypeId> expectedType, bool forceSingleton)
{
    const TypeId singletonType = boolExpr->value ? builtinTypes->trueType : builtinTypes->falseType;
    if (forceSingleton)
        return Inference{singletonType};

    // Consider a table like:
    //
    //  local FLAGS = {
    //      Foo = true,
    //      Bar = false,
    //      Baz = true,
    //      -- so on and so forth
    //  }
    //
    // The intent is (probably) not for this to be a table where each element
    // is potentially `true` or `false` as a singleton, but just `boolean`.
    if (largeTableDepth > 0)
        return Inference{builtinTypes->booleanType};

    TypeId freeTy = freshType(scope, Polarity::Positive);
    FreeType* ft = getMutable<FreeType>(freeTy);
    LUAU_ASSERT(ft);
    ft->lowerBound = singletonType;
    ft->upperBound = builtinTypes->booleanType;
    if (FFlag::LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier)
    {
        ft->primitiveType = builtinTypes->booleanType;
        if (expectedType)
            addConstraint(scope, boolExpr->location, SubtypeConstraint{freeTy, *expectedType});
    }
    else
    {
        addConstraint(scope, boolExpr->location, DEPRECATED_PrimitiveTypeConstraint{freeTy, expectedType, builtinTypes->booleanType});
    }
    return Inference{freeTy};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprLocal* local)
{
    if (FFlag::DebugLuauCFG)
        return Inference{resolveRHSType(scope, local->location, local), nullptr};
    else
    {
        const RefinementKey* key = dfg->getRefinementKey(local);
        LUAU_ASSERT(key);

        std::optional<TypeId> maybeTy;

        // if we have a refinement key, we can look up its type.
        if (key)
            maybeTy = lookup(scope, local->location, key->def);

        if (maybeTy)
        {
            TypeId ty = follow(*maybeTy);

            recordInferredBinding(local->local, ty);

            return Inference{ty, refinementArena.proposition(key, builtinTypes->truthyType)};
        }
        else
            ice->ice("CG: AstExprLocal came before its declaration?");
    }
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprGlobal* global)
{
    const RefinementKey* key = dfg->getRefinementKey(global);
    LUAU_ASSERT(key);

    DefId def = key->def;

    /* prepopulateGlobalScope() has already added all global functions to the environment by this point, so any
     * global that is not already in-scope is definitely an unknown symbol.
     */
    if (auto ty = lookup(scope, global->location, def, /*prototype=*/false))
    {
        return Inference{*ty, refinementArena.proposition(key, builtinTypes->truthyType)};
    }

    // A control-flow join produces a phi def that nothing has bound a type to, and `lookup` can't
    // resolve a phi def. Ordinary globals never need this: `prepopulateGlobalScope` walks every
    // `AstExprGlobal` in the module up front and maps its def, join phis included, to the global's
    // binding.
    // A Luwu class is also referenced as a global, but its binding is only created by the class
    // prepass in visitBlockWithoutChildScope, which runs *after* that walk. So a class referenced
    // after an `if` that also mentions it has no mapping and would get `errorType`. Globals carry no
    // typestate, so the class's binding is exactly what prepopulation would have returned.
    //
    // This applies only to classes; any other global that reaches here keeps `errorType`. For
    // example, a user-defined type function that calls itself inside a loop (`for _, x in f(t) do`)
    // reads its own name through a loop phi. Returning its binding there would give it its own
    // still-unsolved type, which blocks every call made with the result ("outstanding free or
    // blocked type in function call").
    if (FFlag::LuwuClasses && get<Phi>(def) && classGlobalNames.contains(global->name))
    {
        if (auto ty = scope->lookup(global->name))
            return Inference{*ty, refinementArena.proposition(key, builtinTypes->truthyType)};
    }

    return Inference{builtinTypes->errorType};
}

Inference ConstraintGenerator::checkIndexName(
    const ScopePtr& scope,
    const RefinementKey* key,
    AstExpr* indexee,
    const std::string& index,
    Location indexLocation
)
{
    TypeId obj = check(scope, indexee).ty;
    TypeId result = nullptr;

    // We optimize away the HasProp constraint in simple cases so that we can
    // reason about updates to unsealed tables more accurately.

    const TableType* tt = getTableType(obj);

    // This is a little bit iffy but I *believe* it is okay because, if the
    // local's domain is going to be extended at all, it will be someplace after
    // the current lexical position within the script.
    if (!tt)
    {
        if (TypeIds* localDomain = localTypes.find(obj); localDomain && 1 == localDomain->size())
            tt = getTableType(*localDomain->begin());
    }

    if (tt)
    {
        auto it = tt->props.find(index);
        if (it != tt->props.end() && it->second.readTy.has_value())
            result = *it->second.readTy;
    }

    if (auto cachedHasPropResult = propIndexPairsSeen.find({obj, index}))
        result = *cachedHasPropResult;

    if (!result)
    {
        result = arena->addType(BlockedType{});

        auto c = addConstraint(scope, indexee->location, HasPropConstraint{result, obj, index, ValueContext::RValue, inConditional(typeContext)});
        getMutable<BlockedType>(result)->setOwner(c);
        propIndexPairsSeen[{obj, index}] = result;
    }

    if (key)
    {
        if (auto ty = lookup(scope, indexLocation, key->def, false))
            return Inference{*ty, refinementArena.proposition(key, builtinTypes->truthyType)};

        updateRValueRefinements(scope, key->def, result);
    }

    if (key)
        return Inference{result, refinementArena.proposition(key, builtinTypes->truthyType)};
    else
        return Inference{result};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprIndexName* indexName)
{
    const RefinementKey* key = dfg->getRefinementKey(indexName);
    return checkIndexName(scope, key, indexName->expr, indexName->index.value, indexName->indexLocation);
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprIndexExpr* indexExpr)
{
    if (auto constantString = indexExpr->index->as<AstExprConstantString>())
    {
        module->astTypes[indexExpr->index] = builtinTypes->stringType;
        const RefinementKey* key = dfg->getRefinementKey(indexExpr);
        return checkIndexName(scope, key, indexExpr->expr, constantString->value.data, indexExpr->location);
    }

    TypeId obj = check(scope, indexExpr->expr).ty;
    TypeId indexType = check(scope, indexExpr->index).ty;

    TypeId result = arena->addType(BlockedType{});

    const RefinementKey* key = dfg->getRefinementKey(indexExpr);
    if (key)
    {
        if (auto ty = lookup(scope, indexExpr->location, key->def))
            return Inference{*ty, refinementArena.proposition(key, builtinTypes->truthyType)};
        updateRValueRefinements(scope, key->def, result);
    }

    auto c = addConstraint(scope, indexExpr->expr->location, HasIndexerConstraint{result, obj, indexType});
    getMutable<BlockedType>(result)->setOwner(c);

    if (key)
        return Inference{result, refinementArena.proposition(key, builtinTypes->truthyType)};
    else
        return Inference{result};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprFunction* func, std::optional<TypeId> expectedType, bool generalize)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);

    Checkpoint startCheckpoint = checkpoint(this);
    FunctionSignature sig = checkFunctionSignature(scope, nullptr, func, expectedType);

    interiorFreeTypes.emplace_back();
    checkFunctionBody(sig.bodyScope, func);
    Checkpoint endCheckpoint = checkpoint(this);

    TypeId generalizedTy = arena->addType(BlockedType{});
    NotNull<Constraint> gc = addConstraint(
        sig.signatureScope,
        func->location,
        GeneralizationConstraint{
            generalizedTy,
            sig.signature,
            std::vector<TypeId>{},
        }
    );

    if (FFlag::LuauDeprecatedAttributeOnAnonymousFunctions)
        propagateDeprecatedAttributeToConstraint(gc->c, func);

    sig.signatureScope->interiorFreeTypes = std::move(interiorFreeTypes.back().types);
    sig.signatureScope->interiorFreeTypePacks = std::move(interiorFreeTypes.back().typePacks);
    interiorFreeTypes.pop_back();

    getMutable<BlockedType>(generalizedTy)->setOwner(gc);

    addAllAsDependenciesAndChainReturns(startCheckpoint, endCheckpoint, this, gc);
    if (generalize && hasFreeType(sig.signature))
    {
        return Inference{generalizedTy};
    }
    else
    {
        return Inference{sig.signature};
    }
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprUnary* unary)
{
    std::optional<InConditionalContext> inContext;
    if (unary->op != AstExprUnary::Op::Not)
        inContext.emplace(&typeContext, TypeContext::Default);

    auto [operandType, refinement] = check(scope, unary->expr);

    switch (unary->op)
    {
    case AstExprUnary::Op::Not:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->notFunc, {operandType}, {}, scope, unary->location);
        return Inference{resultType, refinementArena.negation(refinement)};
    }
    case AstExprUnary::Op::Len:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->lenFunc, {operandType}, {}, scope, unary->location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprUnary::Op::Minus:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->unmFunc, {operandType}, {}, scope, unary->location);
        return Inference{resultType, std::move(refinement)};
    }
    default: // msvc can't prove that this is exhaustive.
        LUAU_UNREACHABLE();
    }
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprBinary* binary, std::optional<TypeId> expectedType)
{
    return checkAstExprBinary(scope, binary->location, binary->op, binary->left, binary->right, expectedType);
}

Inference ConstraintGenerator::checkAstExprBinary(
    const ScopePtr& scope,
    const Location& location,
    AstExprBinary::Op op,
    AstExpr* left,
    AstExpr* right,
    std::optional<TypeId> expectedType
)
{
    auto [leftType, rightType, refinement] = checkBinary(scope, op, left, right, expectedType);

    switch (op)
    {
    case AstExprBinary::Op::Add:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->addFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Sub:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->subFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Mul:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->mulFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Div:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->divFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::FloorDiv:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->idivFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Pow:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->powFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Mod:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->modFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Concat:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->concatFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::And:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->andFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::Or:
    {
        TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->orFunc, {leftType, rightType}, {}, scope, location);
        return Inference{resultType, std::move(refinement)};
    }
    case AstExprBinary::Op::CompareLt:
    {
        addConstraint(scope, location, EqualityConstraint{leftType, rightType});
        return Inference{builtinTypes->booleanType, std::move(refinement)};
    }
    case AstExprBinary::Op::CompareGe:
    {
        addConstraint(scope, location, EqualityConstraint{leftType, rightType});
        return Inference{builtinTypes->booleanType, std::move(refinement)};
    }
    case AstExprBinary::Op::CompareLe:
    {
        addConstraint(scope, location, EqualityConstraint{leftType, rightType});
        return Inference{builtinTypes->booleanType, std::move(refinement)};
    }
    case AstExprBinary::Op::CompareGt:
    {
        addConstraint(scope, location, EqualityConstraint{leftType, rightType});
        return Inference{builtinTypes->booleanType, std::move(refinement)};
    }
    case AstExprBinary::Op::CompareEq:
    case AstExprBinary::Op::CompareNe:
        return Inference{builtinTypes->booleanType, std::move(refinement)};
    case AstExprBinary::Op::Op__Count:
        ice->ice("Op__Count should never be generated in an AST.");
    default: // msvc can't prove that this is exhaustive.
        LUAU_UNREACHABLE();
    }
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprIfElse* ifElse, std::optional<TypeId> expectedType)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);

    RefinementId refinement = [&]()
    {
        InConditionalContext flipper{&typeContext};
        ScopePtr condScope = childScope(ifElse->condition, scope);
        return check(condScope, ifElse->condition).refinement;
    }();

    ScopePtr thenScope = childScope(ifElse->trueExpr, scope);
    applyRefinements(thenScope, ifElse->trueExpr->location, refinement);
    TypeId thenType = check(thenScope, ifElse->trueExpr, expectedType).ty;

    ScopePtr elseScope = childScope(ifElse->falseExpr, scope);
    applyRefinements(elseScope, ifElse->falseExpr->location, refinementArena.negation(refinement));
    TypeId elseType = check(elseScope, ifElse->falseExpr, expectedType).ty;

    return Inference{makeUnion(scope, ifElse->location, thenType, elseType)};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprTypeAssertion* typeAssert)
{
    check(scope, typeAssert->expr, std::nullopt);
    return Inference{resolveType(scope, typeAssert->annotation, /* inTypeArguments */ false)};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprInterpString* interpString)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);

    for (AstExpr* expr : interpString->expressions)
        check(scope, expr);

    return Inference{builtinTypes->stringType};
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprInstantiate* explicitTypeInstantiation)
{
    TypeId functionType = check(scope, explicitTypeInstantiation->expr, std::nullopt).ty;

    auto [explicitTypeIds, explicitTypePackIds] = resolveTypeArguments(scope, explicitTypeInstantiation->typeArguments);

    TypeId placeholderType = arena->addType(BlockedType{});

    NotNull<Constraint> constraint = addConstraint(
        scope,
        explicitTypeInstantiation->location,
        TypeInstantiationConstraint{functionType, placeholderType, std::move(explicitTypeIds), std::move(explicitTypePackIds)}
    );

    getMutable<BlockedType>(placeholderType)->setOwner(constraint);

    return Inference{placeholderType};
}

std::pair<std::vector<TypeId>, std::vector<TypePackId>> ConstraintGenerator::resolveTypeArguments(
    const ScopePtr& scope,
    const AstArray<AstTypeOrPack>& typeArguments
)
{
    std::vector<TypeId> resolvedTypeArguments;
    std::vector<TypePackId> resolvedTypePackArguments;

    for (const AstTypeOrPack& typeOrPack : typeArguments)
    {
        if (typeOrPack.type)
        {
            resolvedTypeArguments.push_back(resolveType(
                scope,
                typeOrPack.type,
                /* inTypeArguments = */ false
            ));
        }
        else
        {
            LUAU_ASSERT(typeOrPack.typePack);
            resolvedTypePackArguments.push_back(resolveTypePack(
                scope,
                typeOrPack.typePack,
                /* inTypeArguments = */ false
            ));
        }
    }

    return {std::move(resolvedTypeArguments), std::move(resolvedTypePackArguments)};
}

std::tuple<TypeId, TypeId, RefinementId> ConstraintGenerator::checkBinary(
    const ScopePtr& scope,
    AstExprBinary::Op op,
    AstExpr* left,
    AstExpr* right,
    std::optional<TypeId> expectedType
)
{
    std::optional<InConditionalContext> inContext;
    if (op != AstExprBinary::And && op != AstExprBinary::Or && op != AstExprBinary::CompareEq && op != AstExprBinary::CompareNe)
        inContext.emplace(&typeContext, TypeContext::Default);

    if (op == AstExprBinary::And)
    {
        std::optional<TypeId> relaxedExpectedLhs;

        if (expectedType)
            relaxedExpectedLhs = arena->addType(UnionType{{builtinTypes->falsyType, *expectedType}});

        auto [leftType, leftRefinement] = check(scope, left, relaxedExpectedLhs);

        ScopePtr rightScope = childScope(right, scope);
        applyRefinements(rightScope, right->location, leftRefinement);
        auto [rightType, rightRefinement] = check(rightScope, right, expectedType);

        return {leftType, rightType, refinementArena.conjunction(leftRefinement, rightRefinement)};
    }
    else if (op == AstExprBinary::Or)
    {
        std::optional<TypeId> relaxedExpectedLhs;

        if (expectedType)
            relaxedExpectedLhs = arena->addType(UnionType{{builtinTypes->falsyType, *expectedType}});

        auto [leftType, leftRefinement] = check(scope, left, relaxedExpectedLhs);

        ScopePtr rightScope = childScope(right, scope);
        applyRefinements(rightScope, right->location, refinementArena.negation(leftRefinement));
        auto [rightType, rightRefinement] = check(rightScope, right, expectedType);

        return {leftType, rightType, refinementArena.disjunction(leftRefinement, rightRefinement)};
    }
    else if (auto typeguard = matchTypeGuard(op, left, right))
    {
        TypeId leftType = check(scope, left).ty;
        TypeId rightType = check(scope, right).ty;

        const RefinementKey* key = dfg->getRefinementKey(typeguard->target);
        if (!key)
            return {leftType, rightType, nullptr};

        TypeId discriminantTy = builtinTypes->neverType;
        if (typeguard->type == "nil")
            discriminantTy = builtinTypes->nilType;
        else if (typeguard->type == "string")
            discriminantTy = builtinTypes->stringType;
        else if (typeguard->type == "number")
            discriminantTy = builtinTypes->numberType;
        else if (typeguard->type == "integer")
            discriminantTy = builtinTypes->integerType;
        else if (typeguard->type == "boolean")
            discriminantTy = builtinTypes->booleanType;
        else if (typeguard->type == "thread")
            discriminantTy = builtinTypes->threadType;
        else if (typeguard->type == "buffer")
            discriminantTy = builtinTypes->bufferType;
        else if (typeguard->type == "none")
            discriminantTy = builtinTypes->noneType;
        else if (typeguard->type == "table")
            discriminantTy = builtinTypes->tableType;
        else if (typeguard->type == "function")
            discriminantTy = builtinTypes->functionType;
        else if (typeguard->type == "userdata")
        {
            // For now, we don't really care about being accurate with userdata if the typeguard was using typeof.
            discriminantTy = builtinTypes->externType;
        }
        // Luwu Classes (rfcs/classes): `type` and `typeof` both answer "class", "object" and "trait" (for a trait's value)
        else if (FFlag::LuwuClasses && typeguard->type == "class")
            discriminantTy = builtinTypes->classType;
        else if (FFlag::LuwuClasses && typeguard->type == "object")
            discriminantTy = builtinTypes->objectType;
        else if (FFlag::LuwuClasses && FFlag::LuwuTraits && typeguard->type == "trait")
            discriminantTy = builtinTypes->traitType;
        else if (typeguard->type == "vector" && !typeguard->isTypeof)
        {
            // `vector` is defined in EmbeddedBuiltinDefinitions, not as an actual built-in type
            auto typeFun = globalScope->lookupType("vector");
            if (typeFun)
                discriminantTy = follow(typeFun->type);
        }
        else if (!typeguard->isTypeof)
            discriminantTy = builtinTypes->neverType;
        else if (auto typeFun = globalScope->lookupType(typeguard->type);
                 typeFun && (FFlag::LuwuGenericNominals ? get<ExternType>(follow(typeFun->type)) != nullptr
                                                         : (typeFun->typeParams.empty() && typeFun->typePackParams.empty())))
        {
            TypeId ty = follow(typeFun->type);

            // We're only interested in a type that `typeof` can actually name.
            // For userdata that's a datatype root (a direct child of the
            // `userdata` root, e.g. `Instance`); `typeof` returns those by name.
            // For classes `typeof` returns "object"/"class" uniformly, so the
            // only nameable discriminants there are the `object`/`class` roots
            // themselves -- never an individual class (whose typeof is "object").
            if (auto etv = get<ExternType>(ty); etv &&
                (etv->parent == builtinTypes->externType || ty == builtinTypes->objectType || ty == builtinTypes->classType ||
                 hasTag(ty, kTypeofRootTag)))
                discriminantTy = ty;
        }

        RefinementId proposition = refinementArena.proposition(key, discriminantTy);
        if (op == AstExprBinary::CompareEq)
            return {leftType, rightType, proposition};
        else if (op == AstExprBinary::CompareNe)
            return {leftType, rightType, refinementArena.negation(proposition)};
        else
            ice->ice("matchTypeGuard should only return a Some under `==` or `~=`!");
    }
    else if (op == AstExprBinary::CompareEq || op == AstExprBinary::CompareNe)
    {
        // We are checking a binary expression of the form a op b
        // Just because a op b is expected to return a bool, doesn't mean a, b are expected to be bools too
        TypeId leftType = check(scope, left, {}, true).ty;
        TypeId rightType = check(scope, right, {}, true).ty;

        RefinementId leftRefinement = refinementArena.proposition(dfg->getRefinementKey(left), rightType);
        RefinementId rightRefinement = refinementArena.proposition(dfg->getRefinementKey(right), leftType);

        if (op == AstExprBinary::CompareNe)
        {
            leftRefinement = refinementArena.negation(leftRefinement);
            rightRefinement = refinementArena.negation(rightRefinement);
        }

        return {leftType, rightType, refinementArena.equivalence(leftRefinement, rightRefinement)};
    }
    else
    {
        TypeId leftType = check(scope, left).ty;
        TypeId rightType = check(scope, right).ty;
        return {leftType, rightType, nullptr};
    }
}

void ConstraintGenerator::visitLValue(const ScopePtr& scope, AstExpr* expr, TypeId rhsType)
{
    if (auto e = expr->as<AstExprLocal>())
        visitLValue(scope, e, rhsType);
    else if (auto e = expr->as<AstExprGlobal>())
        visitLValue(scope, e, rhsType);
    else if (auto e = expr->as<AstExprIndexName>())
        visitLValue(scope, e, rhsType);
    else if (auto e = expr->as<AstExprIndexExpr>())
        visitLValue(scope, e, rhsType);
    else if (auto e = expr->as<AstExprError>())
    {
        // If we end up with some sort of error expression in an lvalue
        // position, at least go and check the expressions so that when
        // we visit them later, there aren't any invalid assumptions.
        for (auto subExpr : e->expressions)
        {
            check(scope, subExpr);
        }
    }
    else
        ice->ice("Unexpected lvalue expression", expr->location);
}

void ConstraintGenerator::visitLValue(const ScopePtr& scope, AstExprLocal* local, TypeId rhsType)
{
    if (FFlag::DebugLuauCFG)
    {
        TypeId assignTy = resolveLHSType(scope, local->location, CFG::LValue{static_cast<AstExpr*>(local)});
        localTypes.try_insert(assignTy, {});
        localTypes[assignTy].insert(rhsType);

        std::optional<TypeId> annotatedTy = scope->lookup(local->local);
        if (annotatedTy)
            addConstraint(scope, local->location, SubtypeConstraint{rhsType, *annotatedTy});

        return;
    }

    std::optional<TypeId> annotatedTy = scope->lookup(local->local);
    LUAU_ASSERT(annotatedTy);

    const DefId defId = dfg->getDef(local);
    std::optional<TypeId> ty = scope->lookupUnrefinedType(defId);

    if (ty)
    {
        TypeIds* localDomain = localTypes.find(*ty);
        if (localDomain && !local->upvalue)
            localDomain->insert(rhsType);
    }
    else
    {
        ty = arena->addType(BlockedType{});
        localTypes[*ty].insert(rhsType);

        if (annotatedTy)
        {
            switch (shouldSuppressErrors(normalizer, *annotatedTy))
            {
            case ErrorSuppression::DoNotSuppress:
                break;
            case ErrorSuppression::Suppress:
                ty = simplifyUnion(scope, local->location, *ty, builtinTypes->errorType);
                break;
            case ErrorSuppression::NormalizationFailed:
                reportError(local->local->annotation->location, NormalizationTooComplex{});
                break;
            }
        }

        scope->lvalueTypes[defId] = *ty;
    }

    recordInferredBinding(local->local, *ty);

    if (annotatedTy)
        addConstraint(scope, local->location, SubtypeConstraint{rhsType, *annotatedTy});
}

void ConstraintGenerator::visitLValue(const ScopePtr& scope, AstExprGlobal* global, TypeId rhsType)
{
    std::optional<TypeId> annotatedTy = scope->lookup(Symbol{global->name});
    if (annotatedTy)
    {
        DefId def = dfg->getDef(global);
        rootScope->lvalueTypes[def] = rhsType;

        // Ignore possible self-assignment, it doesn't create a new constraint
        if (annotatedTy == follow(rhsType))
            return;

        auto followedAnnotation = follow(*annotatedTy);
        if (auto bt = get<BlockedType>(followedAnnotation); bt && uninitializedGlobals.contains(global->name))
        {
            LUAU_ASSERT(bt->getOwner() == nullptr);
            uninitializedGlobals.erase(global->name);
            emplaceType<BoundType>(asMutable(followedAnnotation), rhsType);
        }


        addConstraint(scope, global->location, SubtypeConstraint{rhsType, *annotatedTy});
    }
}

void ConstraintGenerator::visitLValue(const ScopePtr& scope, AstExprIndexName* expr, TypeId rhsType)
{
    TypeId lhsTy = check(scope, expr->expr).ty;
    TypeId propTy = arena->addType(BlockedType{});
    module->astTypes[expr] = propTy;

    bool incremented = recordPropertyAssignment(lhsTy);

    auto apc =
        addConstraint(scope, expr->location, AssignPropConstraint{lhsTy, expr->index.value, rhsType, expr->indexLocation, propTy, incremented});
    getMutable<BlockedType>(propTy)->setOwner(apc);
}

void ConstraintGenerator::visitLValue(const ScopePtr& scope, AstExprIndexExpr* expr, TypeId rhsType)
{
    if (auto constantString = expr->index->as<AstExprConstantString>())
    {
        TypeId lhsTy = check(scope, expr->expr).ty;
        TypeId propTy = arena->addType(BlockedType{});
        module->astTypes[expr] = propTy;
        module->astTypes[expr->index] = builtinTypes->stringType; // FIXME? Singleton strings exist.
        std::string propName{constantString->value.data, constantString->value.size};

        bool incremented = recordPropertyAssignment(lhsTy);

        auto apc = addConstraint(
            scope, expr->location, AssignPropConstraint{lhsTy, std::move(propName), rhsType, expr->index->location, propTy, incremented}
        );
        getMutable<BlockedType>(propTy)->setOwner(apc);

        return;
    }

    TypeId lhsTy = check(scope, expr->expr).ty;
    TypeId indexTy = check(scope, expr->index).ty;
    TypeId propTy = arena->addType(BlockedType{});
    module->astTypes[expr] = propTy;
    auto aic = addConstraint(scope, expr->location, AssignIndexConstraint{lhsTy, indexTy, rhsType, propTy});
    getMutable<BlockedType>(propTy)->setOwner(aic);
}

Inference ConstraintGenerator::check(const ScopePtr& scope, AstExprTable* expr, std::optional<TypeId> expectedType)
{
    InConditionalContext inContext(&typeContext, TypeContext::Default);

    TypeId ty = arena->addType(TableType{});
    TableType* ttv = getMutable<TableType>(ty);
    LUAU_ASSERT(ttv);

    ttv->state = TableState::Unsealed;
    ttv->definitionModuleName = module->name;
    ttv->definitionLocation = expr->location;
    ttv->scope = scope.get();

    if (FInt::LuauPrimitiveInferenceInTableLimit > 0 && expr->items.size > size_t(FInt::LuauPrimitiveInferenceInTableLimit))
        largeTableDepth++;

    interiorFreeTypes.back().types.push_back(ty);

    TypeIds indexKeyLowerBound;
    TypeIds indexValueLowerBound;

    auto createIndexer = [&indexKeyLowerBound, &indexValueLowerBound](const Location& location, TypeId currentIndexType, TypeId currentResultType)
    {
        indexKeyLowerBound.insert(follow(currentIndexType));
        indexValueLowerBound.insert(follow(currentResultType));
    };

    TypeIds valuesLowerBound;

    Checkpoint start = checkpoint(this);

    for (const AstExprTable::Item& item : expr->items)
    {
        // Expected typeArguments are threaded through table literals separately via the
        // function matchLiteralType.

        // generalize is false here as we want to be able to push typeArguments into lambdas in a situation like:
        //
        //  type Callback = (string) -> ()
        //
        //  local t: { Callback } = {
        //      function (s)
        //          -- s should have type `string` here
        //      end
        //  }
        TypeId itemTy = check(scope, item.value, /* expectedType */ std::nullopt, /* forceSingleton */ false, /* generalize */ false).ty;

        if (item.key)
        {
            // Even though we don't need to use the type of the item's key if
            // it's a string constant, we still want to check it to populate
            // astTypes.
            TypeId keyTy = check(scope, item.key).ty;

            if (AstExprConstantString* key = item.key->as<AstExprConstantString>())
            {
                std::string propName{key->value.data, key->value.size};
                // `@deprecated` on the entry. The lint checks a Property's own deprecation before it
                // looks at the value's type, which is what gives the RFC's rule that an attribute on
                // the field wins over one on the value bound to it.
                ttv->props[propName] = {itemTy, /*deprecated*/ false, {}, key->location};
                applyDeprecatedAttribute(ttv->props[propName], item.attributes);
            }
            else
            {
                createIndexer(item.key->location, keyTy, itemTy);
            }
        }
        else
        {
            TypeId numberType = builtinTypes->numberType;
            // FIXME?  The location isn't quite right here.  Not sure what is
            // right.
            createIndexer(item.value->location, numberType, itemTy);
        }
    }

    Checkpoint end = checkpoint(this);

    if (!indexKeyLowerBound.empty())
    {
        LUAU_ASSERT(!indexValueLowerBound.empty());

        TypeId indexKey = nullptr;
        TypeId indexValue = nullptr;

        if (indexKeyLowerBound.size() == 1)
        {
            indexKey = *indexKeyLowerBound.begin();
        }
        else
        {
            indexKey = arena->addType(UnionType{std::vector(indexKeyLowerBound.begin(), indexKeyLowerBound.end())});
            unionsToSimplify.push_back(indexKey);
        }

        if (indexValueLowerBound.size() == 1)
        {
            indexValue = *indexValueLowerBound.begin();
        }
        else
        {
            indexValue = arena->addType(UnionType{std::vector(indexValueLowerBound.begin(), indexValueLowerBound.end())});
            unionsToSimplify.push_back(indexValue);
        }

        ttv->indexer = TableIndexer{indexKey, indexValue};
    }

    if (expectedType)
    {
        auto ptc = addConstraint(
            scope,
            expr->location,
            PushTypeConstraint{
                /* expectedType */ *expectedType,
                /* targetType */ ty,
                /* astTypes */ NotNull{&module->astTypes},
                /* astExpectedTypes */ NotNull{&module->astExpectedTypes},
                /* expr */ NotNull{expr},
            }
        );

        addAllAsReverseDependencies(start, end, this, ptc);
    }

    if (FInt::LuauPrimitiveInferenceInTableLimit > 0 && expr->items.size > size_t(FInt::LuauPrimitiveInferenceInTableLimit))
        largeTableDepth--;

    return Inference{ty};
}

ConstraintGenerator::FunctionSignature ConstraintGenerator::checkFunctionSignature(
    const ScopePtr& parent,
    ClassDeclRecord* enclosingClass,
    AstExprFunction* fn,
    std::optional<TypeId> expectedType,
    std::optional<Location> originalName
)
{
    LUAU_ASSERT(FFlag::LuwuClasses || enclosingClass == nullptr);
    ScopePtr signatureScope = nullptr;
    ScopePtr bodyScope = nullptr;
    TypePackId returnType = nullptr;

    std::vector<TypeId> genericTypes;
    std::vector<TypePackId> genericTypePacks;

    if (expectedType)
        expectedType = follow(*expectedType);

    bool hasGenerics = fn->generics.size > 0 || fn->genericPacks.size > 0;

    signatureScope = childScope(fn, parent);

    // We need to assign returnType before creating bodyScope so that the
    // return type gets propagated to bodyScope.
    returnType = freshTypePack(signatureScope, Polarity::Positive);
    signatureScope->returnType = returnType;

    bodyScope = childScope(fn->body, signatureScope);

    if (hasGenerics)
    {
        std::vector<std::pair<Name, GenericTypeDefinition>> genericDefinitions = createGenerics(signatureScope, fn->generics);
        std::vector<std::pair<Name, GenericTypePackDefinition>> genericPackDefinitions = createGenericPacks(signatureScope, fn->genericPacks);

        // We do not support default values on function generics, so we only
        // care about the typeArguments involved.
        for (const auto& [name, g] : genericDefinitions)
        {
            genericTypes.push_back(g.ty);
        }

        for (const auto& [name, g] : genericPackDefinitions)
        {
            genericTypePacks.push_back(g.tp);
        }

        // Local variable works around an odd gcc 11.3 warning: <anonymous> may be used uninitialized
        std::optional<TypeId> none = std::nullopt;
        expectedType = none;
    }

    std::vector<TypeId> argTypes;
    std::vector<std::optional<FunctionArgument>> argNames;
    TypePack expectedArgPack;

    const FunctionType* expectedFunction = expectedType ? get<FunctionType>(*expectedType) : nullptr;
    // This check ensures that expectedType is precisely optional and not any (since any is also an optional type)
    if (expectedType && isOptional(*expectedType) && !get<AnyType>(*expectedType))
    {
        if (auto ut = get<UnionType>(*expectedType))
        {
            for (auto u : ut)
            {
                if (get<FunctionType>(u) && !isNil(u))
                {
                    expectedFunction = get<FunctionType>(u);
                    break;
                }
            }
        }
    }

    if (expectedFunction)
    {
        expectedArgPack = extendTypePack(*arena, builtinTypes, expectedFunction->argTypes, fn->args.size);

        genericTypes = expectedFunction->generics;
        genericTypePacks = expectedFunction->genericPacks;
    }


    bool hasExplicitSelf;
    bool hasSelf;

    if (FFlag::LuwuClasses)
    {
        hasExplicitSelf = enclosingClass != nullptr && fn->args.size > 0 && fn->args.data[0]->name == "self";
        hasSelf = hasExplicitSelf || fn->self != nullptr;

        if (hasSelf)
        {
            TypeId selfType = nullptr;
            if (enclosingClass != nullptr)
                selfType = enclosingClass->ty;
            else
                selfType = freshType(signatureScope, Polarity::Negative);

            AstLocal* selfLocal = fn->self ? fn->self : hasExplicitSelf ? fn->args.data[0] : nullptr;
            LUAU_ASSERT(selfLocal);

            argTypes.push_back(selfType);
            argNames.emplace_back(FunctionArgument{selfLocal->name.value, selfLocal->location});

            signatureScope->bindings[selfLocal] = Binding{selfType, selfLocal->location};

            DefId def = dfg->getDef(selfLocal);
            signatureScope->lvalueTypes[def] = selfType;
            updateRValueRefinements(signatureScope, def, selfType);
        }
    }
    else
    {
        if (fn->self)
        {
            TypeId selfType = freshType(signatureScope, Polarity::Negative);
            argTypes.push_back(selfType);
            argNames.emplace_back(FunctionArgument{fn->self->name.value, fn->self->location});
            signatureScope->bindings[fn->self] = Binding{selfType, fn->self->location};

            DefId def = dfg->getDef(fn->self);
            signatureScope->lvalueTypes[def] = selfType;
            updateRValueRefinements(signatureScope, def, selfType);
        }
    }


    for (size_t i = 0; i < fn->args.size; ++i)
    {
        if (FFlag::LuwuClasses)
        {
            if (hasExplicitSelf && i == 0)
            {
                // It is forbidden to put a type annotation on the self
                // parameter of a class method, but we still need to populate
                // astResolvedTypes for TC2.
                if (AstType* annotation = fn->args.data[0]->annotation)
                    resolveType(signatureScope, annotation, /* inTypeArguments */ false, /* replaceErrorWithFresh */ true, Polarity::Negative);
                continue;
            }
        }

        AstLocal* local = fn->args.data[i];

        TypeId argTy = nullptr;
        bool hasSpecifiedArgTy = false;
        if (local->annotation)
        {
            argTy = resolveType(signatureScope, local->annotation, /* inTypeArguments */ false, /* replaceErrorWithFresh*/ true, Polarity::Negative);
            hasSpecifiedArgTy = true;
        }
        else
        {
            if (i < expectedArgPack.head.size())
            {
                argTy = expectedArgPack.head[i];
                hasSpecifiedArgTy = true;
            }
        }

        if (FFlag::LuwuDefaultArguments)
        {
            AstExpr* argDefault = fn->argsDefaults.data[i];
            if (argDefault)
            {
                std::optional<TypeId> expectedArgTy;
                if (hasSpecifiedArgTy)
                    expectedArgTy = argTy;

                Inference found = check(signatureScope, argDefault, expectedArgTy);

                if (hasSpecifiedArgTy)
                    addConstraint(signatureScope, argDefault->location, SubtypeConstraint{found.ty, argTy});
                else
                    argTy = found.ty;
            }

            if (!argTy)
                argTy = freshType(signatureScope, Polarity::Negative);

            argTypes.push_back(argDefault ? makeUnion(signatureScope, argDefault->location, builtinTypes->nilType, argTy) : argTy);
        }
        else
        {
            if (!argTy)
                argTy = freshType(signatureScope, Polarity::Negative);

            argTypes.push_back(argTy);
        }
        argNames.emplace_back(FunctionArgument{local->name.value, local->location});

        signatureScope->bindings[local] = Binding{argTy, local->location};

        DefId def = dfg->getDef(local);
        signatureScope->lvalueTypes[def] = argTy;
        updateRValueRefinements(signatureScope, def, argTy);
    }

    TypePackId varargPack = nullptr;

    if (fn->vararg)
    {
        if (fn->varargAnnotation)
        {
            TypePackId annotationType =
                resolveTypePack(signatureScope, fn->varargAnnotation, /* inTypeArguments */ false, /* replaceErrorWithFresh */ true);
            varargPack = annotationType;
        }
        else if (expectedArgPack.tail && get<VariadicTypePack>(*expectedArgPack.tail))
            varargPack = *expectedArgPack.tail;
        else
            varargPack = builtinTypes->anyTypePack;

        signatureScope->varargPack = varargPack;
        bodyScope->varargPack = varargPack;
    }
    else
    {
        varargPack = arena->addTypePack(VariadicTypePack{builtinTypes->anyType, /*hidden*/ true});
        // We do not add to signatureScope->varargPack because ... is not valid
        // in functions without an explicit ellipsis.

        signatureScope->varargPack = std::nullopt;
        bodyScope->varargPack = std::nullopt;
    }

    LUAU_ASSERT(nullptr != varargPack);

    // Some of the unannotated parameters in argTypes will eventually be
    // generics, and some will not. The ones that are not generic will be
    // pruned when GeneralizationConstraint dispatches.

    // The self parameter never has an annotation and so could always become generic.
    if (fn->self)
        genericTypes.push_back(argTypes[0]);

    size_t typeIndex = fn->self ? 1 : 0;
    for (auto astArg : fn->args)
    {
        TypeId argTy = argTypes.at(typeIndex);
        if (!astArg->annotation)
            genericTypes.push_back(argTy);

        ++typeIndex;
    }

    varargPack = follow(varargPack);
    returnType = follow(returnType);
    if (!fn->varargAnnotation)
        genericTypePacks.push_back(varargPack);
    if (!fn->returnAnnotation)
        genericTypePacks.push_back(returnType);

    // If there is both an annotation and an expected type, the annotation wins.
    // Type checking will sort out any discrepancies later.
    if (fn->returnAnnotation)
    {
        TypePackId annotatedRetType =
            resolveTypePack(signatureScope, fn->returnAnnotation, /* inTypeArguments */ false, /* replaceErrorWithFresh*/ true);
        // We bind the annotated type directly here so that, when we need to
        // generate constraints for return typeArguments, we have a guarantee that we
        // know the annotated return type already, if one was provided.
        LUAU_ASSERT(get<FreeTypePack>(returnType));
        emplaceTypePack<BoundTypePack>(asMutable(returnType), annotatedRetType);
    }
    else if (expectedFunction)
    {
        emplaceTypePack<BoundTypePack>(asMutable(returnType), expectedFunction->retTypes);
    }

    // TODO: Preserve argument names in the function's type.

    FunctionType actualFunction{TypeLevel{}, arena->addTypePack(std::move(argTypes), varargPack), returnType};
    actualFunction.generics = std::move(genericTypes);
    actualFunction.genericPacks = std::move(genericTypePacks);
    actualFunction.argNames = std::move(argNames);
    actualFunction.hasSelf = FFlag::LuwuClasses ? hasSelf : fn->self != nullptr;

    FunctionDefinition defn;
    defn.definitionModuleName = module->name;
    defn.definitionLocation = fn->location;
    defn.varargLocation = fn->vararg ? std::make_optional(fn->varargLocation) : std::nullopt;
    defn.originalNameLocation = originalName.value_or(Location(fn->location.begin, 0));
    actualFunction.definition = defn;

    if (FFlag::DebugLuwuUserDefinedRefinements)
        actualFunction.truthyRefinement = resolveTruthyRefinement(signatureScope, fn);

    TypeId actualFunctionType = arena->addType(std::move(actualFunction));
    LUAU_ASSERT(actualFunctionType);
    module->astTypes[fn] = actualFunctionType;

    if (expectedType && get<FreeType>(*expectedType))
        bindFreeType(*expectedType, actualFunctionType);

    if (FFlag::DebugLuauCyclicRequireTypeInference)
        cgraph->scopeToFunction[signatureScope.get()] = actualFunctionType;
    else
        scopeToFunction[signatureScope.get()] = actualFunctionType;

    return {
        /* signature */ actualFunctionType,
        /* signatureScope */ std::move(signatureScope),
        /* bodyScope */ std::move(bodyScope),
    };
}

// Luwu user-defined refinements: `@[truthy(param, Type)]` on `fn`, as the index of
// `param` among the function's parameters (`self` first when it has one) and the resolved `Type`
std::optional<FunctionType::TruthyRefinement> ConstraintGenerator::resolveTruthyRefinement(const ScopePtr& signatureScope, AstExprFunction* fn)
{
    AstAttr* truthy = fn->getAttribute(AstAttr::Type::Truthy);
    if (!truthy || !truthy->refinedType)
        return std::nullopt;

    // resolved even when the parameter is wrong, so the type's own errors are reported and it has a resolved type
    TypeId refined = resolveType(signatureScope, truthy->refinedType, /* inTypeArguments */ false);

    std::optional<size_t> argIndex;
    size_t selfOffset = fn->self ? 1 : 0;
    if (fn->self && truthy->refinedParam == fn->self->name)
        argIndex = 0;

    for (size_t i = 0; i < fn->args.size && !argIndex; ++i)
    {
        if (fn->args.data[i]->name == truthy->refinedParam)
            argIndex = selfOffset + i;
    }

    if (!argIndex)
    {
        reportError(
            truthy->refinedParamLocation, GenericError{format("'%s' is not a parameter of this function", truthy->refinedParam.value)}
        );
        return std::nullopt;
    }

    return FunctionType::TruthyRefinement{*argIndex, refined};
}

void ConstraintGenerator::checkFunctionBody(const ScopePtr& scope, AstExprFunction* fn)
{
    // If it is possible for execution to reach the end of the function, the return type must be compatible with ()
    ControlFlow cf = visitBlockWithoutChildScope(scope, fn->body);
    if (cf == ControlFlow::None)
        addConstraint(scope, fn->location, PackSubtypeConstraint{builtinTypes->emptyTypePack, scope->returnType});
}

TypeId ConstraintGenerator::resolveReferenceType(
    const ScopePtr& scope,
    AstType* ty,
    AstTypeReference* ref,
    bool inTypeArguments,
    bool replaceErrorWithFresh
)
{
    TypeId result = nullptr;

    if (FFlag::DebugLuauMagicTypes)
    {
        if (ref->name == "_luau_ice")
            ice->ice("_luau_ice encountered", ty->location);
        else if (ref->name == "_luau_print")
        {
            if (ref->parameters.size != 1 || !ref->parameters.data[0].type)
            {
                reportError(ty->location, GenericError{"_luau_print requires one generic parameter"});
                module->astResolvedTypes[ty] = builtinTypes->errorType;
                return builtinTypes->errorType;
            }
            else
                return resolveType_(scope, ref->parameters.data[0].type, inTypeArguments);
        }
        else if (ref->name == "_luau_blocked_type")
        {
            return arena->addType(BlockedType{});
        }
    }

    // `class` is bound as the zero-parameter top type of all classes (GlobalTypes.cpp), so the
    // applied form `class<Cat>` can't be registered as an ordinary builtin type function without
    // clobbering that binding. Route it to the type function here and leave bare `class` alone.
    if (FFlag::LuwuClasses && !ref->prefix.has_value() && ref->name == "class" && ref->hasParameterList)
    {
        if (ref->parameters.size != 1 || !ref->parameters.data[0].type)
        {
            reportError(ty->location, GenericError{"class<T> requires exactly one type argument, the object type of a class"});
            module->astResolvedTypes[ty] = builtinTypes->errorType;
            return builtinTypes->errorType;
        }

        // Luwu Classes (rfcs/classes): `class<List>` for a generic class `List` is the generic class itself, the type
        // `List`'s own class value has, whose constructor infers `T`. `List` without its type arguments is no object
        // type, so the type function below can't produce it.
        if (std::optional<TypeId> genericClass = genericClassValueType(*scope, ref->parameters.data[0].type))
        {
            module->astResolvedTypes[ty] = *genericClass;
            return *genericClass;
        }

        // Resolved outside a type-argument context on purpose: the argument may itself be a generic
        // alias (`class<List<number>>`), and only the non-type-argument path queues the
        // TypeAliasExpansionConstraint that turns its PendingExpansionType into a real type. Without
        // that, the reducer below is handed a pending type nothing will ever expand.
        TypeId objectTy = resolveType_(scope, ref->parameters.data[0].type, /*inTypeArguments*/ false);

        // Luwu Traits (rfcs/classes/traits.md): `class<Trait>` is the class value of any class implementing the trait
        const ExternType* traitType = get<ExternType>(follow(objectTy));
        if (traitType && traitType->traitInfo && traitType->traitInfo->implementorClass)
        {
            module->astResolvedTypes[ty] = *traitType->traitInfo->implementorClass;
            return *traitType->traitInfo->implementorClass;
        }

        // createTypeFunctionInstance already queues the ReduceConstraint.
        return createTypeFunctionInstance(builtinTypes->typeFunctions->classFunc, {objectTy}, {}, scope, ty->location);
    }

    // Luwu Traits (rfcs/classes/traits.md): `trait<Trait>` is the trait value itself, `typeof(Trait)`
    if (FFlag::LuwuTraits && !ref->prefix.has_value() && ref->name == "trait" && ref->hasParameterList)
    {
        TypeId traitValueTy = builtinTypes->errorType;

        if (ref->parameters.size == 1 && ref->parameters.data[0].type)
        {
            TypeId objectTy = follow(resolveType_(scope, ref->parameters.data[0].type, /*inTypeArguments*/ false));
            const ExternType* traitType = get<ExternType>(objectTy);
            const Klass* klass = traitType && traitType->traitInfo && traitType->relation ? get_if<Klass>(&*traitType->relation) : nullptr;

            if (klass)
                traitValueTy = klass->ty;
        }

        if (traitValueTy == builtinTypes->errorType)
            reportError(ty->location, GenericError{"trait<T> requires exactly one type argument, a trait"});

        module->astResolvedTypes[ty] = traitValueTy;
        return traitValueTy;
    }

    std::optional<TypeFun> alias;

    if (ref->prefix.has_value())
    {
        alias = scope->lookupImportedType(ref->prefix->value, ref->name.value);
    }
    else
    {
        alias = scope->lookupType(ref->name.value);
    }

    if (alias.has_value())
    {
        // If the alias is not generic, we don't need to set up a blocked type and an instantiation constraint
        if (alias.has_value() && alias->typeParams.empty() && alias->typePackParams.empty() && !ref->hasParameterList)
        {
            result = alias->type;
        }
        else
        {
            std::vector<TypeId> parameters;
            std::vector<TypePackId> packParameters;

            for (const AstTypeOrPack& p : ref->parameters)
            {
                // We do not enforce the ordering of typeArguments vs. type packs here;
                // that is done in the parser.
                if (p.type)
                {
                    parameters.push_back(resolveType_(scope, p.type, /* inTypeArguments */ true));
                }
                else if (p.typePack)
                {
                    TypePackId tp = resolveTypePack_(scope, p.typePack, /*inTypeArguments*/ true);

                    // If we need more regular typeArguments, we can use single element type packs to fill those in
                    if (parameters.size() < alias->typeParams.size() && size(tp) == 1 && finite(tp) && first(tp))
                        parameters.push_back(*first(tp));
                    else
                        packParameters.push_back(tp);
                }
                else
                {
                    // This indicates a parser bug: one of these two pointers
                    // should be set.
                    LUAU_ASSERT(false);
                }
            }

            result = arena->addType(PendingExpansionType{ref->prefix, ref->name, std::move(parameters), std::move(packParameters)});

            // If we're not in a type argument context, we need to create a constraint that expands this.
            // The dispatching of the above constraint will queue up additional constraints for nested
            // type function applications.
            if (!inTypeArguments)
                addConstraint(scope, ty->location, TypeAliasExpansionConstraint{/* target */ result});
        }
    }
    else
    {
        result = builtinTypes->errorType;
        if (replaceErrorWithFresh)
            result = freshType(scope, Polarity::Mixed);
    }

    // Luwu: only a reference that names a type function itself is unapplied (`local a: create_table` for `type function
    // create_table()`). Upstream reports any reference that resolves to an instance, which includes an alias of an applied
    // one: `type C = class<Box<number>>` then `local d: C`. Upstream's own type functions never reach that, because
    // `keyof<X>` and friends are expanded through a pending alias reference, while `class<X>` is an instance from the start.
    const TypeFunctionInstanceType* tfit = get<TypeFunctionInstanceType>(follow(result));
    if (tfit)
    {
        const bool namesTypeFunction = tfit->userFuncName && *tfit->userFuncName == ref->name;
        if (namesTypeFunction)
            reportError(ty->location, UnappliedTypeFunction{});
        addConstraint(scope, ty->location, ReduceConstraint{result});
    }

    if (auto genericType = getMutable<GenericType>(follow(result)))
        genericType->polarity = (genericType->polarity & Polarity::Mixed) | polarity;

    return result;
}

namespace
{
Polarity polarityOfAccess(AstTableAccess access, Polarity p)
{
    switch (access)
    {
    case AstTableAccess::Read:
        return p;
    case AstTableAccess::Write:
        return invert(p);
    case AstTableAccess::ReadWrite:
        return Polarity::Mixed;
    default:
        return Polarity::Unknown;
    }
}
} // namespace

TypeId ConstraintGenerator::resolveTableType(const ScopePtr& scope, AstType* ty, AstTypeTable* tab, bool inTypeArguments, bool replaceErrorWithFresh)
{
    TableType::Props props;
    std::optional<TableIndexer> indexer;

    Polarity p = polarity;
    for (const AstTableProp& prop : tab->props)
    {
        Property& propRef = props[prop.name.value];

        // Set the polarity for the inner type
        polarity = polarityOfAccess(prop.access, p);

        TypeId propTy = resolveType_(scope, prop.type, inTypeArguments);

        propRef.typeLocation = prop.location;

        // `@deprecated` on a field of a table type: the DeprecatedApi lint already reports on a
        // Property marked this way, so the attribute only has to reach it.
        applyDeprecatedAttribute(propRef, prop.attributes);

        switch (prop.access)
        {
        case AstTableAccess::ReadWrite:
            propRef.readTy = propTy;
            propRef.writeTy = propTy;
            break;
        case AstTableAccess::Read:
            propRef.readTy = propTy;
            break;
        case AstTableAccess::Write:
            propRef.writeTy = propTy;
            break;
        default:
            ice->ice("Unexpected property access " + std::to_string(int(prop.access)));
            break;
        }
    }

    if (AstTableIndexer* astIndexer = tab->indexer)
    {
        if (astIndexer->access == AstTableAccess::Read)
        {
            polarity = p;
            indexer = TableIndexer{
                resolveType_(scope, astIndexer->indexType, inTypeArguments),
                resolveType_(scope, astIndexer->resultType, inTypeArguments),
                /*isReadOnly*/ true
            };
        }
        else if (astIndexer->access == AstTableAccess::Write)
            reportError(astIndexer->accessLocation.value_or(Location{}), GenericError{"write keyword is illegal here"});
        else if (astIndexer->access == AstTableAccess::ReadWrite)
        {
            polarity = Polarity::Mixed;
            indexer = TableIndexer{
                resolveType_(scope, astIndexer->indexType, inTypeArguments),
                resolveType_(scope, astIndexer->resultType, inTypeArguments),
            };
        }
        else
            ice->ice("Unexpected property access " + std::to_string(int(astIndexer->access)));
    }

    polarity = p;


    TypeId tableTy = arena->addType(TableType{props, indexer, scope->level, scope.get(), TableState::Sealed});
    TableType* ttv = getMutable<TableType>(tableTy);

    ttv->definitionModuleName = module->name;
    ttv->definitionLocation = tab->location;

    return tableTy;
}

TypeId ConstraintGenerator::resolveFunctionType(
    const ScopePtr& scope,
    AstType* ty,
    AstTypeFunction* fn,
    bool inTypeArguments,
    bool replaceErrorWithFresh
)
{
    bool hasGenerics = fn->generics.size > 0 || fn->genericPacks.size > 0;
    ScopePtr signatureScope = nullptr;

    std::vector<TypeId> genericTypes;
    std::vector<TypePackId> genericTypePacks;

    // If we don't have generics, we do not need to generate a child scope
    // for the generic bindings to live on.
    if (hasGenerics)
    {
        signatureScope = childScope(fn, scope);

        std::vector<std::pair<Name, GenericTypeDefinition>> genericDefinitions = createGenerics(signatureScope, fn->generics);
        std::vector<std::pair<Name, GenericTypePackDefinition>> genericPackDefinitions = createGenericPacks(signatureScope, fn->genericPacks);

        for (const auto& [name, g] : genericDefinitions)
        {
            genericTypes.push_back(g.ty);
        }

        for (const auto& [name, g] : genericPackDefinitions)
        {
            genericTypePacks.push_back(g.tp);
        }
    }
    else
    {
        // To eliminate the need to branch on hasGenerics below, we say that
        // the signature scope is the parent scope if we don't have
        // generics.
        signatureScope = scope;
    }

    AstTypePackExplicit tempArgTypes{Location{}, fn->argTypes};

    Polarity p = polarity;
    polarity = invert(polarity);
    TypePackId argTypes = resolveTypePack_(signatureScope, &tempArgTypes, inTypeArguments, replaceErrorWithFresh);
    polarity = p;
    TypePackId returnTypes = resolveTypePack_(signatureScope, fn->returnTypes, inTypeArguments, replaceErrorWithFresh);

    // TODO: FunctionType needs a pointer to the scope so that we know
    // how to quantify/instantiate it.
    FunctionType ftv{TypeLevel{}, {}, {}, argTypes, returnTypes};
    ftv.isCheckedFunction = fn->isCheckedFunction();
    AstAttr* deprecatedAttr = fn->getAttribute(AstAttr::Type::Deprecated);
    ftv.isDeprecatedFunction = deprecatedAttr != nullptr;
    if (deprecatedAttr)
    {
        ftv.deprecatedInfo = std::make_shared<AstAttr::DeprecatedInfo>(deprecatedAttr->deprecatedInfo());
    }


    // This replicates the behavior of the appropriate FunctionType
    // constructors.
    ftv.generics = std::move(genericTypes);
    ftv.genericPacks = std::move(genericTypePacks);

    ftv.argNames.reserve(fn->argNames.size);
    for (const auto& el : fn->argNames)
    {
        if (el)
        {
            const auto& [name, location] = *el;
            ftv.argNames.emplace_back(FunctionArgument{name.value, location});
        }
        else
            ftv.argNames.emplace_back(std::nullopt);
    }

    return arena->addType(std::move(ftv));
}

TypeId ConstraintGenerator::resolveType(
    const ScopePtr& scope,
    AstType* ty,
    bool inTypeArguments,
    bool replaceErrorWithFresh,
    Polarity initialPolarity
)
{
    // Reset the polarity
    polarity = initialPolarity;
    return resolveType_(scope, ty, inTypeArguments, replaceErrorWithFresh);
}

TypeId ConstraintGenerator::resolveType_(const ScopePtr& scope, AstType* ty, bool inTypeArguments, bool replaceErrorWithFresh)
{
    TypeId result = nullptr;

    if (auto ref = ty->as<AstTypeReference>())
    {
        result = resolveReferenceType(scope, ty, ref, inTypeArguments, replaceErrorWithFresh);
    }
    else if (auto tab = ty->as<AstTypeTable>())
    {
        result = resolveTableType(scope, ty, tab, inTypeArguments, replaceErrorWithFresh);
    }
    else if (auto fn = ty->as<AstTypeFunction>())
    {
        result = resolveFunctionType(scope, ty, fn, inTypeArguments, replaceErrorWithFresh);
    }
    else if (auto tof = ty->as<AstTypeTypeof>())
    {
        TypeId exprType = check(scope, tof->expr).ty;
        result = exprType;
    }
    else if (ty->is<AstTypeOptional>())
    {
        result = builtinTypes->nilType;
    }
    else if (auto unionAnnotation = ty->as<AstTypeUnion>())
    {
        if (unionAnnotation->types.size == 1)
            result = resolveType_(scope, unionAnnotation->types.data[0], inTypeArguments);
        else
        {
            std::vector<TypeId> parts;
            for (AstType* part : unionAnnotation->types)
            {
                parts.push_back(resolveType_(scope, part, inTypeArguments));
            }

            result = arena->addType(UnionType{std::move(parts)});
        }
    }
    else if (auto intersectionAnnotation = ty->as<AstTypeIntersection>())
    {
        if (intersectionAnnotation->types.size == 1)
            result = resolveType_(scope, intersectionAnnotation->types.data[0], inTypeArguments);
        else
        {
            std::vector<TypeId> parts;
            for (AstType* part : intersectionAnnotation->types)
            {
                parts.push_back(resolveType_(scope, part, inTypeArguments));
            }

            result = arena->addType(IntersectionType{std::move(parts)});
        }
    }
    else if (auto typeGroupAnnotation = ty->as<AstTypeGroup>())
    {
        result = resolveType_(scope, typeGroupAnnotation->type, inTypeArguments);
    }
    else if (auto boolAnnotation = ty->as<AstTypeSingletonBool>())
    {
        if (boolAnnotation->value)
            result = builtinTypes->trueType;
        else
            result = builtinTypes->falseType;
    }
    else if (auto stringAnnotation = ty->as<AstTypeSingletonString>())
    {
        result = arena->addType(SingletonType(StringSingleton{std::string(stringAnnotation->value.data, stringAnnotation->value.size)}));
    }
    else if (ty->is<AstTypeError>())
    {
        result = builtinTypes->errorType;
        if (replaceErrorWithFresh)
            result = freshType(scope, polarity);
    }
    else
    {
        LUAU_ASSERT(0);
        result = builtinTypes->errorType;
    }

    module->astResolvedTypes[ty] = result;
    return result;
}

TypePackId ConstraintGenerator::resolveTypePack(
    const ScopePtr& scope,
    AstTypePack* tp,
    bool inTypeArgument,
    bool replaceErrorWithFresh,
    Polarity initialPolarity
)
{
    polarity = initialPolarity;
    return resolveTypePack_(scope, tp, inTypeArgument, replaceErrorWithFresh);
}

TypePackId ConstraintGenerator::resolveTypePack_(const ScopePtr& scope, AstTypePack* tp, bool inTypeArgument, bool replaceErrorWithFresh)
{
    TypePackId result;
    if (auto expl = tp->as<AstTypePackExplicit>())
    {
        result = resolveTypePack_(scope, expl->typeList, inTypeArgument, replaceErrorWithFresh);
    }
    else if (auto var = tp->as<AstTypePackVariadic>())
    {
        TypeId ty = resolveType_(scope, var->variadicType, inTypeArgument, replaceErrorWithFresh);
        result = arena->addTypePack(TypePackVar{VariadicTypePack{ty}});
    }
    else if (auto gen = tp->as<AstTypePackGeneric>())
    {
        if (std::optional<TypePackId> lookup = scope->lookupPack(gen->genericName.value))
        {
            result = *lookup;
        }
        else
        {
            reportError(tp->location, UnknownSymbol{gen->genericName.value, UnknownSymbol::Context::Type});
            result = builtinTypes->errorTypePack;
        }
    }
    else
    {
        LUAU_ASSERT(0);
        result = builtinTypes->errorTypePack;
    }

    if (auto gtp = getMutable<GenericTypePack>(follow(result)))
    {
        // The initial polarity is unknown, so we flip that bit off
        // by saying that we are at most Mixed, and then add in the
        // polarity we're currently processing.
        gtp->polarity = (gtp->polarity & Polarity::Mixed) | polarity;
    }

    module->astResolvedTypePacks[tp] = result;
    return result;
}

TypePackId ConstraintGenerator::resolveTypePack_(const ScopePtr& scope, const AstTypeList& list, bool inTypeArguments, bool replaceErrorWithFresh)
{
    std::vector<TypeId> head;

    for (AstType* headTy : list.types)
    {
        head.push_back(resolveType_(scope, headTy, inTypeArguments, replaceErrorWithFresh));
    }

    std::optional<TypePackId> tail = std::nullopt;
    if (list.tailType)
    {
        tail = resolveTypePack_(scope, list.tailType, inTypeArguments, replaceErrorWithFresh);
    }

    return addTypePack(std::move(head), tail);
}

TypePackId ConstraintGenerator::resolveTypePack(
    const ScopePtr& scope,
    const AstTypeList& list,
    bool inTypeArguments,
    bool replaceErrorWithFresh,
    Polarity initialPolarity
)
{
    polarity = initialPolarity;
    return resolveTypePack_(scope, list, inTypeArguments, replaceErrorWithFresh);
}

std::vector<std::pair<Name, GenericTypeDefinition>> ConstraintGenerator::createGenerics(
    const ScopePtr& scope,
    AstArray<AstGenericType*> generics,
    bool useCache,
    bool addTypes
)
{
    std::vector<std::pair<Name, GenericTypeDefinition>> result;
    for (const auto* generic : generics)
    {
        TypeId genericTy = nullptr;

        if (auto it = scope->parent->typeAliasTypeParameters.find(generic->name.value);
            useCache && it != scope->parent->typeAliasTypeParameters.end())
        {
            genericTy = it->second;
        }
        else
        {
            genericTy = arena->addType(GenericType{scope.get(), generic->name.value, Polarity::None});
            scope->parent->typeAliasTypeParameters[generic->name.value] = genericTy;
        }

        std::optional<TypeId> defaultTy = std::nullopt;

        if (generic->defaultValue)
            defaultTy = arena->addType(BlockedType{});

        if (addTypes)
            scope->privateTypeBindings[generic->name.value] = TypeFun{genericTy};

        result.emplace_back(generic->name.value, GenericTypeDefinition{genericTy, defaultTy});
    }

    return result;
}

std::vector<std::pair<Name, GenericTypePackDefinition>> ConstraintGenerator::createGenericPacks(
    const ScopePtr& scope,
    AstArray<AstGenericTypePack*> generics,
    bool useCache,
    bool addTypes
)
{
    std::vector<std::pair<Name, GenericTypePackDefinition>> result;
    for (const auto* generic : generics)
    {
        TypePackId genericTy;

        if (auto it = scope->parent->typeAliasTypePackParameters.find(generic->name.value);
            useCache && it != scope->parent->typeAliasTypePackParameters.end())
            genericTy = it->second;
        else
        {
            genericTy = arena->addTypePack(TypePackVar{GenericTypePack{scope.get(), generic->name.value, Polarity::None}});
            scope->parent->typeAliasTypePackParameters[generic->name.value] = genericTy;
        }

        std::optional<TypePackId> defaultTy = std::nullopt;

        if (generic->defaultValue)
            defaultTy = arena->addTypePack(BlockedTypePack{});

        if (addTypes)
            scope->privateTypePackBindings[generic->name.value] = genericTy;

        result.emplace_back(generic->name.value, GenericTypePackDefinition{genericTy, defaultTy});
    }

    return result;
}

Inference ConstraintGenerator::flattenPack(const ScopePtr& scope, Location location, InferencePack pack)
{
    const auto& [tp, refinements] = pack;
    RefinementId refinement = nullptr;
    if (!refinements.empty())
        refinement = refinements[0];

    if (auto f = first(tp))
        return Inference{*f, refinement};

    TypeId typeResult = arena->addType(BlockedType{});
    auto c = addConstraint(scope, location, UnpackConstraint{{typeResult}, tp});
    getMutable<BlockedType>(typeResult)->setOwner(c);

    return Inference{typeResult, refinement};
}

void ConstraintGenerator::reportError(Location location, TypeErrorData err)
{
    errors.emplace_back(location, module->name, std::move(err));
    if (logger)
        logger->captureGenerationError(errors.back());
}

void ConstraintGenerator::reportCodeTooComplex(Location location)
{
    errors.emplace_back(location, module->name, CodeTooComplex{});
    if (logger)
        logger->captureGenerationError(errors.back());

    recursionLimitMet = true;
}

TypeId ConstraintGenerator::makeUnion(const ScopePtr& scope, Location location, TypeId lhs, TypeId rhs)
{
    if (get<NeverType>(follow(lhs)))
        return rhs;
    if (get<NeverType>(follow(rhs)))
        return lhs;

    TypeId result = simplifyUnion(scope, location, lhs, rhs);
    if (is<UnionType>(follow(result)))
        unionsToSimplify.push_back(result);
    return result;
}

TypeId ConstraintGenerator::makeUnion(std::vector<TypeId> options)
{
    UnionBuilder ub{arena, builtinTypes};
    ub.reserve(options.size());

    for (auto option : options)
        ub.add(option);

    TypeId unionTy = ub.build();

    if (is<UnionType>(unionTy))
        unionsToSimplify.push_back(unionTy);

    return unionTy;
}

TypeId ConstraintGenerator::makeIntersect(const ScopePtr& scope, Location location, TypeId lhs, TypeId rhs)
{
    TypeId resultType = createTypeFunctionInstance(builtinTypes->typeFunctions->intersectFunc, {lhs, rhs}, {}, scope, location);

    return resultType;
}

struct GlobalPrepopulator : AstVisitor
{
    const NotNull<Scope> globalScope;
    const NotNull<TypeArena> arena;
    const NotNull<const DataFlowGraph> dfg;

    DenseHashSet<AstName> uninitializedGlobals{{}};

    GlobalPrepopulator(NotNull<Scope> globalScope, NotNull<TypeArena> arena, NotNull<const DataFlowGraph> dfg)
        : globalScope(globalScope)
        , arena(arena)
        , dfg(dfg)
    {
    }

    bool visit(AstExprGlobal* global) override
    {
        if (auto ty = globalScope->lookup(global->name))
        {
            DefId def = dfg->getDef(global);
            globalScope->lvalueTypes[def] = *ty;
        }

        return true;
    }

    bool visit(AstStatAssign* assign) override
    {
        for (const Luau::AstExpr* expr : assign->vars)
        {
            if (const AstExprGlobal* g = expr->as<AstExprGlobal>())
            {
                if (!globalScope->lookup(g->name))
                    globalScope->globalsToWarn.insert(g->name.value);

                if (globalScope->bindings.find(g->name) == globalScope->bindings.end())
                {
                    TypeId bt = arena->addType(BlockedType{});
                    uninitializedGlobals.insert(g->name);
                    globalScope->bindings[g->name] = Binding{bt, g->location};
                }
            }
        }

        return true;
    }

    bool visit(AstStatFunction* function) override
    {
        if (AstExprGlobal* g = function->name->as<AstExprGlobal>())
        {
            TypeId bt = arena->addType(BlockedType{});
            uninitializedGlobals.insert(g->name);
            globalScope->bindings[g->name] = Binding{bt};
        }

        return true;
    }

    bool visit(AstType*) override
    {
        return true;
    }

    bool visit(class AstTypePack* node) override
    {
        return true;
    }
};

void ConstraintGenerator::prepopulateGlobalScopeForFragmentTypecheck(const ScopePtr& globalScope, const ScopePtr& resumeScope, AstStatBlock* program)
{
    // Handle type function globals as well, without preparing a module scope since they have a separate environment
    GlobalPrepopulator tfgp{NotNull{typeFunctionRuntime->rootScope.get()}, arena, dfg};
    program->visit(&tfgp);

    for (auto name : tfgp.uninitializedGlobals)
        uninitializedGlobals.insert(name);
}

void ConstraintGenerator::prepopulateGlobalScope(const ScopePtr& globalScope, AstStatBlock* program)
{
    GlobalPrepopulator gp{NotNull{globalScope.get()}, arena, dfg};

    if (prepareModuleScope)
        prepareModuleScope(module->name, globalScope);

    program->visit(&gp);

    for (auto name : gp.uninitializedGlobals)
        uninitializedGlobals.insert(name);

    // Handle type function globals as well, without preparing a module scope since they have a separate environment
    GlobalPrepopulator tfgp{NotNull{typeFunctionRuntime->rootScope.get()}, arena, dfg};
    program->visit(&tfgp);

    for (auto name : tfgp.uninitializedGlobals)
        uninitializedGlobals.insert(name);
}

// Luwu Declare Statements (rfcs/declare-statements.md): a declaration outside a definition file declares a global for
// its own file only. Upstream only has definition files, whose declarations become globals for every module
// (`declaredGlobals`, persisted by the Frontend).
bool ConstraintGenerator::declaresFileGlobals() const
{
    return FFlag::LuwuDeclareStatements && module->mode != Mode::Definition;
}

// A declaration applies to the whole file, including code above it, so its name is bound before anything is visited.
// Its type isn't resolved yet (a type alias it names may come later in the file), so it starts as a placeholder that
// bindDeclaration fills in. The parser only accepts declarations at the top level and in `do` blocks there.
void ConstraintGenerator::hoistDeclarations(AstStatBlock* block)
{
    for (AstStat* stat : block->body)
    {
        if (AstStatBlock* inner = stat->as<AstStatBlock>())
        {
            hoistDeclarations(inner);
            continue;
        }

        AstName name;
        Location location;
        if (AstStatDeclareGlobal* global = stat->as<AstStatDeclareGlobal>())
        {
            name = global->name;
            location = global->location;
        }
        else if (AstStatDeclareFunction* function = stat->as<AstStatDeclareFunction>())
        {
            name = function->name;
            location = function->location;
        }
        else
            continue;

        // A second declaration of a name is a parse error; the first one keeps the placeholder.
        if (hoistedDeclarations.contains(name))
            continue;

        TypeId placeholder = arena->addType(BlockedType{});
        hoistedDeclarations[name] = placeholder;
        rootScope->bindings[name] = Binding{placeholder, location};
    }
}

void ConstraintGenerator::collectNestedTypeDeclarations(AstStatBlock* block, std::vector<AstStat*>& out)
{
    for (AstStat* stat : block->body)
    {
        if (AstStatBlock* inner = stat->as<AstStatBlock>())
            collectNestedTypeDeclarations(inner, out);
        else if (stat->is<AstStatDeclareExternType>() || stat->is<AstStatDeclareClass>())
            out.push_back(stat);
    }
}

// Upstream binds every extern type as exported: a definition file's exported types become global types. Outside
// definition files an extern type is scoped like a type alias, private unless it is declared `export`.
std::unordered_map<Name, TypeFun>& ConstraintGenerator::externTypeBindings(Scope& scope, const AstStatDeclareExternType* declaration)
{
    if (declaresFileGlobals() && !declaration->exportLocation)
        return scope.privateTypeBindings;

    return scope.exportedTypeBindings;
}

void ConstraintGenerator::bindDeclaration(AstName name, TypeId declaredTy)
{
    TypeId* placeholder = hoistedDeclarations.find(name);
    if (!placeholder)
        return;

    auto blocked = get<BlockedType>(*placeholder);
    if (!blocked || blocked->getOwner())
        return;

    // `declare x: typeof(x)` resolves to the placeholder itself.
    if (follow(declaredTy) == *placeholder)
        declaredTy = builtinTypes->errorType;

    emplaceType<BoundType>(asMutable(*placeholder), declaredTy);
}

bool ConstraintGenerator::recordPropertyAssignment(TypeId ty)
{
    DenseHashSet<TypeId> seen{nullptr};
    VecDeque<TypeId> queue;

    queue.push_back(ty);

    bool incremented = false;

    while (!queue.empty())
    {
        const TypeId t = follow(queue.front());
        queue.pop_front();

        if (seen.find(t))
            continue;
        seen.insert(t);

        if (auto tt = getMutable<TableType>(t); tt && tt->state == TableState::Unsealed)
        {
            tt->remainingProps += 1;
            incremented = true;
        }
        else if (auto mt = get<MetatableType>(t))
            queue.push_back(mt->table);
        else if (TypeIds* localDomain = localTypes.find(t))
        {
            for (TypeId domainTy : *localDomain)
                queue.push_back(domainTy);
        }
        else if (auto ut = get<UnionType>(t))
        {
            for (TypeId part : ut)
                queue.push_back(part);
        }
    }

    return incremented;
}

void ConstraintGenerator::recordInferredBinding(AstLocal* local, TypeId ty)
{
    if (InferredBinding* ib = inferredBindings.find(local))
        ib->types.insert(ty);
}

void ConstraintGenerator::fillInInferredBindings(const ScopePtr& globalScope, AstStatBlock* block)
{
    for (const auto& [symbol, p] : inferredBindings)
    {
        const auto& [scope, location, types] = p;

        std::vector<TypeId> tys(types.begin(), types.end());
        if (tys.size() == 1)
            scope->bindings[symbol] = Binding{tys.front(), location};
        else
        {
            TypeId ty = makeUnion(std::move(tys));
            scope->bindings[symbol] = Binding{ty, location};
        }
    }
}

std::vector<std::optional<TypeId>> ConstraintGenerator::getExpectedCallTypesForFunctionOverloads(const TypeId fnType)
{
    std::vector<TypeId> funTys;
    if (auto it = get<IntersectionType>(follow(fnType)))
    {
        for (TypeId intersectionComponent : it)
        {
            funTys.push_back(intersectionComponent);
        }
    }

    std::vector<std::optional<TypeId>> expectedTypes;
    // For a list of functions f_0 : e_0 -> r_0, ... f_n : e_n -> r_n,
    // emit a list of arguments that the function could take at each position
    // by unioning the arguments at each place
    auto assignOption = [this, &expectedTypes](size_t index, TypeId ty)
    {
        if (index == expectedTypes.size())
        {
            expectedTypes.emplace_back(ty);
        }
        else if (ty)
        {
            auto& el = expectedTypes[index];

            if (!el)
                el = ty;
            else
            {
                std::vector<TypeId> result = reduceUnion({*el, ty});
                if (result.empty())
                    el = builtinTypes->neverType;
                else if (result.size() == 1)
                    el = result[0];
                else
                    el = makeUnion(std::move(result));
            }
        }
    };

    for (const TypeId overload : funTys)
    {
        if (const FunctionType* ftv = get<FunctionType>(follow(overload)))
        {
            auto [argsHead, argsTail] = flatten(ftv->argTypes);
            size_t start = ftv->hasSelf ? 1 : 0;
            size_t index = 0;
            for (size_t i = start; i < argsHead.size(); ++i)
                assignOption(index++, argsHead[i]);
            if (argsTail)
            {
                argsTail = follow(*argsTail);
                if (const VariadicTypePack* vtp = get<VariadicTypePack>(*argsTail))
                {
                    while (index < funTys.size())
                        assignOption(index++, vtp->ty);
                }
            }
        }
    }

    // TODO vvijay Feb 24, 2023 apparently we have to demote the typeArguments here?

    return expectedTypes;
}

TypeId ConstraintGenerator::createTypeFunctionInstance(
    const TypeFunction& function,
    std::vector<TypeId> typeArguments,
    std::vector<TypePackId> packArguments,
    const ScopePtr& scope,
    Location location
)
{
    TypeId result = arena->addTypeFunction(function, std::move(typeArguments), std::move(packArguments));
    addConstraint(scope, location, ReduceConstraint{result});
    return result;
}

TypeId ConstraintGenerator::simplifyUnion(const ScopePtr& scope, Location location, TypeId left, TypeId right)
{
    return ::Luau::simplifyUnion(builtinTypes, arena, left, right).result;
}

void ConstraintGenerator::updateRValueRefinements(const ScopePtr& scope, DefId def, TypeId ty) const
{
    updateRValueRefinements(scope.get(), def, ty);
}

void ConstraintGenerator::updateRValueRefinements(Scope* scope, DefId def, TypeId ty) const
{
    scope->rvalueRefinements[def] = ty;
    if (auto sym = dfg->getSymbolFromDef(def))
        scope->refinements[*sym] = ty;
}


} // namespace Luau
