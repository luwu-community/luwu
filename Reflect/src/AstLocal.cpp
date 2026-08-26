// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

// Because the embedder may have themselves set useratom, we cannot use Luau's builtin atom system here, instead define the atoms separately using a hashmap + enum
enum AstLocalAtom : uint8_t
{
    Atom_Unknown = 0,
    Atom_Name,
    Atom_Location,
    Atom_Shadow,
    Atom_IsConst,
    Atom_Depth,
    Atom_Annotation,
};

static AstLocalAtom getAstLocalAtom(std::string_view key)
{
    static const std::unordered_map<std::string_view, AstLocalAtom> s_atomMap = {
        {"name", Atom_Name},
        {"location", Atom_Location},
        {"shadow", Atom_Shadow},
        {"isConst", Atom_IsConst},
        {"depth", Atom_Depth},
        {"annotation", Atom_Annotation},
    };

    if (auto it = s_atomMap.find(key); it != s_atomMap.end())
        return it->second;

    return Atom_Unknown;
}

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
    size_t keyLen = 0;
    const char* keyStr = luaL_checklstring(L, 2, &keyLen);
    AstLocalAtom atom = getAstLocalAtom(std::string_view(keyStr, keyLen));
    Luau::AstLocal* local = handle.local;

    switch (atom)
    {
    case Atom_Name:       { lua_pushstring(L, local->name.value); return 1; }
    case Atom_Location:   { pushLocation(L, handle.doc, local->location); return 1; }
    case Atom_Shadow:     { pushAstLocal(L, handle.doc, local->shadow); return 1; }
    case Atom_IsConst:    { lua_pushboolean(L, local->isConst); return 1; }
    case Atom_Depth:      { lua_pushinteger(L, int(local->functionDepth)); return 1; }
    case Atom_Annotation: { pushAstNode(L, handle.doc, local->annotation); return 1; }
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
