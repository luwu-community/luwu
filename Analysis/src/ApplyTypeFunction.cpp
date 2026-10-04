// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/ApplyTypeFunction.h"

#include "Luau/Common.h"
#include "Luau/Instantiation.h"
#include "Luau/VisitType.h"

LUAU_FASTFLAG(LuwuGenericNominals)

namespace Luau
{

bool ApplyTypeFunction::isDirty(TypeId ty)
{
    if (typeArguments.count(ty))
        return true;
    else if (const FreeType* ftv = get<FreeType>(ty))
    {
        if (ftv->forwardedTypeAlias)
            encounteredForwardedType = true;
        return false;
    }
    else
        return false;
}

bool ApplyTypeFunction::isDirty(TypePackId tp)
{
    if (typePackArguments.count(tp))
        return true;
    else
        return false;
}

bool ApplyTypeFunction::ignoreChildren(TypeId ty)
{
    if (get<GenericType>(ty))
        return true;
    else if (const ExternType* etv = get<ExternType>(ty))
    {
        // Luwu Generic Nominals (rfcs/generics-on-extern-types.md): an instantiation of a generic class (`List<T>` inside a member), or a template
        // (its own instantiation), changes through its type arguments, which Substitution walks for it; every other
        // extern type is opaque
        return !(FFlag::LuwuGenericNominals && etv->isGenericNominal());
    }
    else
        return false;
}

bool ApplyTypeFunction::ignoreChildren(TypePackId tp)
{
    if (get<GenericTypePack>(tp))
        return true;
    else
        return false;
}

TypeId ApplyTypeFunction::clean(TypeId ty)
{
    TypeId& arg = typeArguments[ty];
    LUAU_ASSERT(arg);
    return arg;
}

TypePackId ApplyTypeFunction::clean(TypePackId tp)
{
    TypePackId& arg = typePackArguments[tp];
    LUAU_ASSERT(arg);
    return arg;
}

std::vector<TypeId> copiedPendingExpansions(const Substitution& substitution)
{
    std::vector<TypeId> copies;

    // The record also holds the replacements themselves (`T` -> its argument), which can be a reference too
    for (const auto& [original, copy] : substitution.newTypes)
    {
        if (get<PendingExpansionType>(original) && get<PendingExpansionType>(copy))
            copies.push_back(copy);
    }

    return copies;
}

bool isIdentityInstantiation(const TypeFun& typeFun, const std::vector<TypeId>& typeArguments, const std::vector<TypePackId>& packArguments)
{
    bool sameTypes = std::equal(
        typeArguments.begin(),
        typeArguments.end(),
        typeFun.typeParams.begin(),
        typeFun.typeParams.end(),
        [](TypeId argument, const GenericTypeDefinition& param)
        {
            return follow(argument) == follow(param.ty);
        }
    );

    bool samePacks = std::equal(
        packArguments.begin(),
        packArguments.end(),
        typeFun.typePackParams.begin(),
        typeFun.typePackParams.end(),
        [](TypePackId argument, const GenericTypePackDefinition& param)
        {
            return follow(argument) == follow(param.tp);
        }
    );

    return sameTypes && samePacks;
}

// Luwu Generic Nominals (rfcs/generics-on-extern-types.md): maps a template's generics to an instantiation's type arguments, and the template to the
// instantiation (a member's `self: List<T>` is the template)
static void mapTemplateOntoInstantiation(ApplyTypeFunction& substitution, const ExternType& genericTemplate, TypeId instantiation)
{
    const ExternType* etv = get<ExternType>(instantiation);
    LUAU_ASSERT(etv && etv->genericTemplate);

    for (size_t i = 0; i < genericTemplate.instantiatedTypeParams.size() && i < etv->instantiatedTypeParams.size(); ++i)
        substitution.typeArguments[follow(genericTemplate.instantiatedTypeParams[i])] = etv->instantiatedTypeParams[i];

    for (size_t i = 0; i < genericTemplate.instantiatedTypePackParams.size() && i < etv->instantiatedTypePackParams.size(); ++i)
        substitution.typePackArguments[follow(genericTemplate.instantiatedTypePackParams[i])] = etv->instantiatedTypePackParams[i];

    substitution.typeArguments[follow(*etv->genericTemplate)] = instantiation;
}

// Substitutes a template's child into an instantiation; the instantiation itself is never rewritten. The alias
// references the substitution copied without expanding (`Listable<T>` before `Listable` is declared) go to
// `copiedReferences`: nothing expands a copy unless its caller does.
static TypeId applyTemplate(ApplyTypeFunction& substitution, TypeId instantiation, TypeId ty, std::vector<TypeId>& copiedReferences)
{
    substitution.dontTraverseInto(instantiation);
    TypeId result = substitution.substitute(ty).value_or(ty);

    std::vector<TypeId> copied = copiedPendingExpansions(substitution);
    copiedReferences.insert(copiedReferences.end(), copied.begin(), copied.end());

    return result;
}

TypeId instantiateGenericNominal(
    TypeArena* arena,
    TypeId genericTemplate,
    std::vector<TypeId> typeArguments,
    std::vector<TypePackId> packArguments,
    std::vector<TypeId>& copiedReferences
)
{
    genericTemplate = follow(genericTemplate);
    const ExternType* templateEtv = get<ExternType>(genericTemplate);
    LUAU_ASSERT(templateEtv);

    ExternType instantiation{
        templateEtv->name,
        {},
        templateEtv->parent,
        templateEtv->metatable,
        templateEtv->tags,
        templateEtv->userData,
        templateEtv->definitionModuleName,
        templateEtv->definitionLocation,
        templateEtv->indexer
    };
    instantiation.root = templateEtv->root;
    // The class value stays the generic class's: `class<List<number>>` is `List`
    instantiation.relation = templateEtv->relation;
    instantiation.initLocation = templateEtv->initLocation;
    instantiation.implementedTraits = templateEtv->implementedTraits;
    instantiation.traitInfo = templateEtv->traitInfo;
    instantiation.traitIntersection = templateEtv->traitIntersection;
    instantiation.genericTemplate = genericTemplate;
    instantiation.instantiatedTypeParams = std::move(typeArguments);
    instantiation.instantiatedTypePackParams = std::move(packArguments);

    GenericTypeFinder finder;
    for (TypeId argument : instantiation.instantiatedTypeParams)
        finder.traverse(argument);
    for (TypePackId argument : instantiation.instantiatedTypePackParams)
        finder.traverse(argument);
    instantiation.hasUnresolvedGenerics = finder.found;

    TypeId result = arena->addType(std::move(instantiation));
    resetNominalMembers(result, arena, /* sharedArena */ nullptr);

    // The members are built on first read. The rest is a handful of types, substituted now.
    ApplyTypeFunction substitution{arena};
    mapTemplateOntoInstantiation(substitution, *templateEtv, result);

    ExternType* etv = getMutable<ExternType>(result);
    if (etv->metatable)
        etv->metatable = applyTemplate(substitution, result, *etv->metatable, copiedReferences);

    if (etv->indexer)
    {
        etv->indexer->indexType = applyTemplate(substitution, result, etv->indexer->indexType, copiedReferences);
        etv->indexer->indexResultType = applyTemplate(substitution, result, etv->indexer->indexResultType, copiedReferences);
    }

    for (TypeId& trait : etv->implementedTraits)
        trait = applyTemplate(substitution, result, trait, copiedReferences);

    return result;
}

TypeId traitTemplateOf(TypeId trait)
{
    trait = follow(trait);
    const ExternType* traitType = get<ExternType>(trait);
    return traitType && traitType->genericTemplate ? follow(*traitType->genericTemplate) : trait;
}

std::optional<std::vector<TypeId>> neededTraitTypeArguments(
    TypeArena* arena,
    const ExternType& needingTemplate,
    TypeId needed,
    const std::vector<TypeId>& needingArguments,
    std::vector<TypeId>& copiedReferences
)
{
    if (!needingTemplate.traitInfo)
        return std::nullopt;

    needed = follow(needed);

    for (const auto& [trait, arguments] : needingTemplate.traitInfo->neededTypeArguments)
    {
        if (follow(trait) != needed)
            continue;

        ApplyTypeFunction substitution{arena};
        for (size_t i = 0; i < needingTemplate.instantiatedTypeParams.size() && i < needingArguments.size(); ++i)
            substitution.typeArguments[follow(needingTemplate.instantiatedTypeParams[i])] = needingArguments[i];

        std::vector<TypeId> result;
        for (TypeId argument : arguments)
            result.push_back(substitution.substitute(argument).value_or(argument));

        std::vector<TypeId> copied = copiedPendingExpansions(substitution);
        copiedReferences.insert(copiedReferences.end(), copied.begin(), copied.end());

        return result;
    }

    return std::nullopt;
}

void resetNominalMembers(TypeId instantiation, TypeArena* arena, std::shared_ptr<SharedNominalArena> sharedArena)
{
    ExternType* etv = getMutable<ExternType>(instantiation);
    LUAU_ASSERT(etv && etv->genericTemplate);

    auto lazy = std::make_shared<LazyNominalMembers>();
    lazy->instantiation = instantiation;
    lazy->arena = arena;
    lazy->sharedArena = std::move(sharedArena);

    etv->memberStorage.clear();
    etv->lazyMembers = std::move(lazy);
}

// A template's member that is still being solved: a blocked type, an alias reference not expanded yet, or a type
// function not reduced yet. Substituting into it now would copy something the solver is about to replace.
struct PendingMemberFinder : TypeOnceVisitor
{
    std::optional<TypeId> found;

    PendingMemberFinder()
        : TypeOnceVisitor("PendingMemberFinder", /* skipBoundTypes */ true)
    {
    }

    bool visit(TypeId ty) override
    {
        return !found;
    }

    bool visit(TypeId ty, const BlockedType&) override
    {
        found = ty;
        return false;
    }

    bool visit(TypeId ty, const PendingExpansionType&) override
    {
        found = ty;
        return false;
    }

    bool visit(TypeId ty, const TypeFunctionInstanceType& tfit) override
    {
        if (tfit.state == TypeFunctionInstanceState::Unsolved)
            found = ty;
        return !found;
    }

    // A table keeps the arguments it was instantiated with for display (`R<A<T>>`), which visitors don't reach
    bool visit(TypeId ty, const TableType& ttv) override
    {
        for (TypeId argument : ttv.instantiatedTypeParams)
            traverse(argument);
        return !found;
    }
};

static std::optional<TypeId> findPendingMember(const Property& prop)
{
    PendingMemberFinder finder;
    if (prop.readTy)
        finder.traverse(*prop.readTy);
    if (prop.writeTy && !prop.isShared())
        finder.traverse(*prop.writeTy);
    return finder.found;
}

std::optional<TypeId> pendingNominalMember(const ExternType& instantiation, const Name& name)
{
    if (!instantiation.hasUnbuiltMembers() || !instantiation.genericTemplate)
        return std::nullopt;

    const ExternType* templateEtv = get<ExternType>(follow(*instantiation.genericTemplate));
    if (!templateEtv)
        return std::nullopt;

    auto it = templateEtv->builtProps().find(name);
    if (it == templateEtv->builtProps().end())
        return std::nullopt;

    return findPendingMember(it->second);
}

void buildNominalMembers(const ExternType& etv)
{
    const std::shared_ptr<LazyNominalMembers>& lazy = etv.lazyMembers;
    if (!lazy)
        return;

    // Another thread may have built them while this one waited
    std::lock_guard<std::mutex> guard(lazy->mutex);
    if (lazy->built.load(std::memory_order_relaxed))
        return;

    // An imported instantiation's template is final, so whatever it holds is built now: one left pending would be
    // rewritten on later reads, under other threads reading it
    const bool templateIsFinal = lazy->sharedArena != nullptr;

    const ExternType* templateEtv = get<ExternType>(follow(*etv.genericTemplate));
    LUAU_ASSERT(templateEtv);

    TypeArena* arena = lazy->arena;
    std::unique_lock<std::mutex> arenaGuard;
    if (lazy->sharedArena)
    {
        arena = &lazy->sharedArena->arena;
        arenaGuard = std::unique_lock<std::mutex>{lazy->sharedArena->mutex};
    }

    ApplyTypeFunction substitution{arena};
    mapTemplateOntoInstantiation(substitution, *templateEtv, lazy->instantiation);

    // A member the solver hasn't finished is left as the template's and built on a later read, once it is done.
    // pendingNominalMember tells the solver what to wait for before reading it.
    bool anyPending = false;
    for (const auto& [name, prop] : templateEtv->builtProps())
    {
        if (lazy->builtNames.count(name))
            continue;

        if (!templateIsFinal && findPendingMember(prop))
        {
            anyPending = true;
            etv.memberStorage[name] = prop;
            continue;
        }

        // A copied reference would never be expanded here, so the member waits until the template's is
        std::vector<TypeId> copiedReferences;
        Property built = prop;
        if (prop.readTy)
            built.readTy = applyTemplate(substitution, lazy->instantiation, *prop.readTy, copiedReferences);
        if (prop.isShared())
            built.writeTy = built.readTy;
        else if (prop.writeTy)
            built.writeTy = applyTemplate(substitution, lazy->instantiation, *prop.writeTy, copiedReferences);

        if (!templateIsFinal && !copiedReferences.empty())
        {
            anyPending = true;
            etv.memberStorage[name] = prop;
            continue;
        }

        etv.memberStorage[name] = std::move(built);
        lazy->builtNames.insert(name);
    }

    if (!anyPending)
        lazy->built.store(true, std::memory_order_release);
}

} // namespace Luau
