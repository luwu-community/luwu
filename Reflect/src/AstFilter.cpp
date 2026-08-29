// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

bool AstFilterData::addCategory(std::string_view category)
{
    NodeCategory cat = categoryFromString(category);
    if (cat != NodeCategory::Unknown)
    {
        categoryMask |= uint8_t(cat);
        return true;
    }
    return false;
}

bool AstFilterData::addKind(std::string_view kind)
{
    if (addCategory(kind))
        return true;

    int idx = getNodeClassIndexByKind(kind);
    if (idx >= 0 && idx < 128)
    {
        // (idx / 64): which integer holds the flag (c++ guarantees integer division so this is safe)
        // (idx % 64): gives us the position of the bit we need to look for.
        classMask[idx / 64] |= (1ULL << (idx % 64));
        return true;
    }
    return false;
}

bool AstFilterData::matches(Luau::AstNode* node) const
{
    if (!node)
        return false;

    if (categoryMask != 0)
    {
        NodeCategory cat = getNodeCategory(node);
        if ((uint8_t(cat) & categoryMask) != 0)
            return true;
    }

    int idx = node->classIndex;
    if (idx >= 0 && idx < 128)
    {
        // (idx / 64): which integer holds the flag (c++ guarantees integer division so this is safe)
        // (idx % 64): gives us the position of the bit we need to look for.
        if ((classMask[idx / 64] & (1ULL << (idx % 64))) != 0)
            return true;
    }

    return false;
}

void pushAstFilter(lua_State* L, const AstFilterData& filter)
{
    AstFilterData* data = static_cast<AstFilterData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstFilterData), TagFilter));
    new (data) AstFilterData(filter);
}

LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkAstFilter, astFilterDtor, AstFilterData, TagFilter, "AstFilter")

AstFilterData extractAstFilter(lua_State* L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return AstFilterData{};

    return checkAstFilter(L, idx);
}

static int astFilterMatchesMethod(lua_State* L)
{
    auto& handle = checkAstFilter(L, 1);
    auto& nodeHandle = checkAstNode(L, 2);
    lua_pushboolean(L, handle.matches(nodeHandle.node));
    return 1;
}

static int astFilterMatchesTrampoline(lua_State* L)
{
    return astFilterMatchesMethod(L);
}

static int astFilterIndex(lua_State* L)
{
    auto& handle = checkAstFilter(L, 1);
    LUAU_REFLECT_RESOLVE_INDEX_ATOM();

    switch (atom)
    {
    case ReflectAtom::Id:
        lua_pushlightuserdatatagged(L, (void*)&handle, TagId);
        return 1;

    case ReflectAtom::Kind:
        lua_pushstring(L, "AstFilter");
        return 1;

    case ReflectAtom::Matches:
        return pushCachedUserdataMethod(L, TagFilter, "matches", astFilterMatchesTrampoline);

    default:
        break;
    }

    lua_pushnil(L);
    return 1;
}

static int astFilterNamecall(lua_State* L)
{
    LUAU_REFLECT_RESOLVE_NAMECALL_ATOM();

    switch (atom)
    {
    case ReflectAtom::Matches:
        return astFilterMatchesMethod(L);

    default:
        break;
    }

    luaL_error(L, "attempt to call non-existent method '%s'", str);
}

static int astFilterToString(lua_State* L)
{
    checkAstFilter(L, 1);
    lua_pushstring(L, "AstFilter");
    return 1;
}

static int astFilterEq(lua_State* L)
{
    auto& a = checkAstFilter(L, 1);
    auto& b = checkAstFilter(L, 2);
    lua_pushboolean(L, a.classMask[0] == b.classMask[0] && a.classMask[1] == b.classMask[1] && a.categoryMask == b.categoryMask);
    return 1;
}

void registerAstFilter(lua_State* L)
{
    registerUserdataType(L, TagFilter, "AstFilter", astFilterDtor, astFilterIndex, astFilterToString, astFilterEq, astFilterNamecall);
}

int reflectFilter(lua_State* L)
{
    AstFilterData filter;
    int top = lua_gettop(L);
    for (int i = 1; i <= top; i++)
    {
        if (AstFilterData* existing = static_cast<AstFilterData*>(lua_touserdatatagged(L, i, TagFilter)))
        {
            filter.classMask[0] |= existing->classMask[0];
            filter.classMask[1] |= existing->classMask[1];
            filter.categoryMask |= existing->categoryMask;
        }
        else if (lua_isstring(L, i))
        {
            size_t len = 0;
            const char* str = lua_tolstring(L, i, &len);
            if (!filter.addKind(std::string_view(str, len)))
                luaL_error(L, "unknown node kind or category '%.*s'", int(len), str);
        }
        else if (lua_istable(L, i))
        {
            int len = lua_objlen(L, i);
            for (int j = 1; j <= len; j++)
            {
                lua_rawgeti(L, i, j);
                if (lua_isstring(L, -1))
                {
                    size_t strLen = 0;
                    const char* str = lua_tolstring(L, -1, &strLen);
                    if (!filter.addKind(std::string_view(str, strLen)))
                    {
                        lua_pop(L, 1);
                        luaL_error(L, "unknown node kind or category '%.*s'", int(strLen), str);
                    }
                }
                else
                {
                    lua_pop(L, 1);
                    luaL_error(L, "expected string in filter table at index %d", j);
                }
                lua_pop(L, 1);
            }
        }
        else
        {
            luaL_typeerror(L, i, "string, table, or AstFilter");
        }
    }

    pushAstFilter(L, filter);
    return 1;
}

} // namespace Luau
