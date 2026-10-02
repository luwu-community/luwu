// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/ApplyTypeFunction.h"

#include "Luau/Common.h"
#include "Luau/VisitType.h"

LUAU_FASTFLAG(LuwuGenericNominals)

namespace Luau
{

// Whether a generic nominal instantiation's type arguments mention a type this substitution replaces. An instantiation is
// built from its template and its type arguments, so its arguments are all it can mention. A nested instantiation inside
// an argument (`List<List<T>>`) is checked by its own arguments, not walked: its members can reach every other
// instantiation of the class.
struct SubstitutedTypeFinder : TypeOnceVisitor
{
    using TypeOnceVisitor::visit;

    const ApplyTypeFunction& substitution;
    bool found = false;

    explicit SubstitutedTypeFinder(const ApplyTypeFunction& substitution)
        : TypeOnceVisitor("SubstitutedTypeFinder", /* skipBoundTypes */ true)
        , substitution(substitution)
    {
    }

    void checkArguments(const ExternType& etv)
    {
        for (TypeId arg : etv.instantiatedTypeParams)
            traverse(arg);
        for (TypePackId arg : etv.instantiatedTypePackParams)
            traverse(arg);
    }

    bool visit(TypeId ty) override
    {
        if (substitution.typeArguments.count(ty))
            found = true;
        return !found;
    }

    bool visit(TypePackId tp) override
    {
        if (substitution.typePackArguments.count(tp))
            found = true;
        return !found;
    }

    bool visit(TypeId ty, const ExternType& etv) override
    {
        if (substitution.typeArguments.count(ty))
            found = true;
        else
            checkArguments(etv);
        return false;
    }
};

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
    else if (get<ExternType>(ty))
    {
        if (FFlag::LuwuGenericNominals)
        {
            // replaceChildren() re-checks ignoreChildren on the freshly cloned type, so the
            // root check must also match the clone produced for genericNominalRoot, not just
            // the template itself.
            TypeId* clonedRoot = genericNominalRoot ? newTypes.find(genericNominalRoot) : nullptr;
            if (ty == genericNominalRoot || (clonedRoot && ty == *clonedRoot))
                return false;

            // Luwu Classes (rfcs/classes): an instantiation with unresolved generics is walked only when its type
            // arguments mention something this substitution replaces. `List<U>` in `map<U>(...): List<U>` is left
            // alone when instantiating `List<number>`. Walking it anyway clones every instantiation reachable through
            // its members, which grows with each expansion until the substitution limit reports "too complex".
            const ExternType* etv = get<ExternType>(ty);
            if (etv->hasUnresolvedGenerics)
                return !mentionsSubstitutedType(*etv);
        }

        return true;
    }
    else
        return false;
}

bool ApplyTypeFunction::mentionsSubstitutedType(const ExternType& etv) const
{
    // An instantiation without type arguments has nothing to check, so it is walked
    if (etv.instantiatedTypeParams.empty() && etv.instantiatedTypePackParams.empty())
        return true;

    SubstitutedTypeFinder finder{*this};
    finder.checkArguments(etv);
    return finder.found;
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

} // namespace Luau
