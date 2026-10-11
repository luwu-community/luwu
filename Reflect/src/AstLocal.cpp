// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "AstLocal.gen.inl"

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushAstLocal, checkAstLocal, astLocalDtor, AstLocalData, Luau::AstLocal*, TagLocal, "AstLocal")

static int astLocalProperties(lua_State* L, AstLocalData& handle)
{
    lua_createtable(L, 0, 8);
    lua_pushlightuserdatatagged(L, (void*)handle.local, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, "AstLocal");
    lua_setfield(L, -2, "kind");

    collectAstLocalProps(L, handle);

    return 1;
}

static int dispatchAstLocalMethod(lua_State* L, AstLocalData& handle, ReflectAtom atom, const char* str, size_t len)
{
    if (atom == ReflectAtom::Properties)
        return astLocalProperties(L, handle);

    if (handleAstLocalMethods(L, handle, atom))
        return 1;

    luaL_error(L, "%.*s is not a valid method of AstLocal", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astLocalMethodTrampoline, checkAstLocal, dispatchAstLocalMethod)
LUAU_REFLECT_NAMECALL(astLocalNamecall, checkAstLocal, dispatchAstLocalMethod)

static int astLocalIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstLocal);

    switch (atom)
    {
    case ReflectAtom::Id:
        lua_pushlightuserdatatagged(L, (void*)handle.local, TagId);
        return 1;

    case ReflectAtom::Kind:
        lua_pushstring(L, "AstLocal");
        return 1;

    default:
        break;
    }

    if (atom != ReflectAtom::Unknown)
        return pushCachedUserdataMethod(L, TagLocal, keyStr, astLocalMethodTrampoline);

    lua_pushnil(L);
    return 1;
}

static int astLocalToString(lua_State* L)
{
    auto& handle = checkAstLocal(L, 1);
    if (handle.local && handle.local->name.value)
        lua_pushfstring(L, "AstLocal(%s)", handle.local->name.value);
    else
        lua_pushstring(L, "AstLocal");
    return 1;
}

LUAU_REFLECT_DEFINE_EQ(astLocalEq, TagLocal, checkAstLocal, a.local == b.local)

void registerAstLocal(lua_State* L)
{
    registerUserdataType(L, TagLocal, "AstLocal", astLocalDtor, astLocalIndex, astLocalToString, astLocalEq, astLocalNamecall);
}

} // namespace Luau
