// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

// Because the embedder may have themselves set useratom, we cannot use Luau's builtin atom system here, instead define the atoms separately using a hashmap + enum
enum AstPositionAtom : uint8_t
{
    Atom_Unknown = 0,
    Atom_Line,
    Atom_Column,
    Atom_ComputedOffset,
};

static AstPositionAtom getAstPositionAtom(std::string_view key)
{
    static const std::unordered_map<std::string_view, AstPositionAtom> s_atomMap = {
        {"line", Atom_Line},
        {"column", Atom_Column},
        {"computedOffset", Atom_ComputedOffset},
    };

    if (auto it = s_atomMap.find(key); it != s_atomMap.end())
        return it->second;

    return Atom_Unknown;
}

void pushPosition(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Position& pos)
{
    if (pos == Luau::Position::missing())
    {
        lua_pushnil(L);
        return;
    }
    AstPositionData* data = static_cast<AstPositionData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstPositionData), TagPosition));
    new (data) AstPositionData{doc, pos};
}

void pushPositionArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::Position>& array)
{
    lua_createtable(L, int(array.size), 0);
    for (size_t i = 0; i < array.size; i++)
    {
        pushPosition(L, doc, array.data[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
}

AstPositionData& checkAstPosition(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagPosition)
        luaL_typeerrorL(L, idx, "AstPosition");
    return *static_cast<AstPositionData*>(lua_touserdata(L, idx));
}

static void astPositionDtor(lua_State* L, void* userdata)
{
    static_cast<AstPositionData*>(userdata)->~AstPositionData();
}

static int astPositionIndex(lua_State* L)
{
    auto& handle = checkAstPosition(L, 1);
    size_t keyLen = 0;
    const char* keyStr = luaL_checklstring(L, 2, &keyLen);
    AstPositionAtom atom = getAstPositionAtom(std::string_view(keyStr, keyLen));
    const auto& pos = handle.position;
    const auto& doc = handle.doc;

    switch (atom)
    {
    case Atom_Line:   { lua_pushinteger(L, pos.line + 1); return 1; }
    case Atom_Column: { lua_pushinteger(L, pos.column + 1); return 1; }
    case Atom_ComputedOffset:
    {
        LUAU_ASSERT(doc);
        size_t off = positionToOffset(doc->lineOffsets, doc->source.size(), pos);
        lua_pushinteger(L, int(off));
        return 1;
    }
    default:
        lua_pushnil(L);
        return 1;
    }
}

static int astPositionToString(lua_State* L)
{
    auto& handle = checkAstPosition(L, 1);
    const auto& pos = handle.position;
    lua_pushfstring(L, "AstPosition(%d:%d)", pos.line + 1, pos.column + 1);
    return 1;
}

static int astPositionEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagPosition || lua_userdatatag(L, 2) != TagPosition)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstPosition(L, 1);
    auto& b = checkAstPosition(L, 2);
    lua_pushboolean(L, a.position == b.position && a.doc == b.doc);
    return 1;
}

static void dfgLine(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstPositionData*>(ud);
    lua_userdatadirectfield_setnumber(res, double(data->position.line + 1));
}

static void dfgColumn(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstPositionData*>(ud);
    lua_userdatadirectfield_setnumber(res, double(data->position.column + 1));
}

static void dfgComputedOffset(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstPositionData*>(ud);
    LUAU_ASSERT(data->doc);
    size_t off = positionToOffset(data->doc->lineOffsets, data->doc->source.size(), data->position);
    lua_userdatadirectfield_setnumber(res, double(off));
}

void registerAstPosition(lua_State* L)
{
    registerUserdataType(L, TagPosition, "AstPosition", astPositionDtor, astPositionIndex, astPositionToString, astPositionEq);
    lua_registeruserdatadirectfieldget(L, TagPosition, "line", dfgLine);
    lua_registeruserdatadirectfieldget(L, TagPosition, "column", dfgColumn);
    lua_registeruserdatadirectfieldget(L, TagPosition, "computedOffset", dfgComputedOffset);
}

} // namespace Luau
