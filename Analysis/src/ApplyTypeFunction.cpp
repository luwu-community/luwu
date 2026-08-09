// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/ApplyTypeFunction.h"

#include "Luau/Common.h"

LUAU_FASTFLAG(LuauGenericNominals)

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
    else if (get<ExternType>(ty))
    {
        if (FFlag::LuauGenericNominals)
        {
            // replaceChildren() re-checks ignoreChildren on the freshly cloned type, so the
            // root check must also match the clone produced for genericNominalRoot, not just
            // the template itself.
            TypeId* clonedRoot = genericNominalRoot ? newTypes.find(genericNominalRoot) : nullptr;
            if (ty == genericNominalRoot || (clonedRoot && ty == *clonedRoot) || get<ExternType>(ty)->hasUnresolvedGenerics)
                return false;
        }

        return true;
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

} // namespace Luau
