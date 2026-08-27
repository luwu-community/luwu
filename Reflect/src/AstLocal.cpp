// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

void pushAstLocal(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* local)
{
    if (!local)
    {
        lua_pushnil(L);
        return;
    }
    AstLocalData* data = static_cast<AstLocalData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstLocalData), TagLocal));
    new (data) AstLocalData{doc, local};
}

AstLocalData& checkAstLocal(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagLocal)
        luaL_typeerrorL(L, idx, "AstLocal");
    return *static_cast<AstLocalData*>(lua_touserdata(L, idx));
}

static void astLocalDtor(lua_State* L, void* userdata)
{
    static_cast<AstLocalData*>(userdata)->~AstLocalData();
}

static int astLocalIndex(lua_State* L)
{
    auto& handle = checkAstLocal(L, 1);
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr);
    if (!keyStr)
    {
        lua_pushnil(L);
        return 1;
    }
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen);
    Luau::AstLocal* local = handle.local;

    switch (atom)
    {
    case ReflectAtom::Name:       { lua_pushstring(L, local->name.value); return 1; }
    case ReflectAtom::Location:   { pushLocation(L, handle.doc, local->location); return 1; }
    case ReflectAtom::Shadow:     { pushAstLocal(L, handle.doc, local->shadow); return 1; }
    case ReflectAtom::IsConst:    { lua_pushboolean(L, local->isConst); return 1; }
    case ReflectAtom::Depth:      { lua_pushinteger(L, int(local->functionDepth)); return 1; }
    case ReflectAtom::Annotation: { pushAstNode(L, handle.doc, local->annotation); return 1; }
    default:
        lua_pushnil(L);
        return 1;
    }
}

static int astLocalToString(lua_State* L)
{
    auto& handle = checkAstLocal(L, 1);
    lua_pushfstring(L, "AstLocal(%s)", handle.local->name.value ? handle.local->name.value : "");
    return 1;
}

static int astLocalEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagLocal || lua_userdatatag(L, 2) != TagLocal)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstLocal(L, 1);
    auto& b = checkAstLocal(L, 2);
    lua_pushboolean(L, a.local == b.local && a.doc == b.doc);
    return 1;
}

void registerAstLocal(lua_State* L)
{
    registerUserdataType(L, TagLocal, "AstLocal", astLocalDtor, astLocalIndex, astLocalToString, astLocalEq);
}

} // namespace Luau
