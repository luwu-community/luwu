// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "AstItems.gen.inl"

namespace Luau
{

static AstItem& checkAnyAstItem(lua_State* L, int idx)
{
    AstItem* item = static_cast<AstItem*>(lua_touserdatatagged(L, idx, TagItem));
    if (!item)
        luaL_typeerrorL(L, idx, "AstItem");
    return *item;
}

static void astItemDtor(lua_State* L, void* userdata)
{
    reflectUserdataDtor<AstItem>(L, userdata);
}

static int plainItemProperties(lua_State* L, AstItem& handle)
{
    lua_createtable(L, 0, 8);

    lua_pushlightuserdatatagged(L, handle.target, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, handle.cls->kind);
    lua_setfield(L, -2, "kind");

    handle.cls->propCollector(L, handle);

    return 1;
}

static int dispatchAstItemMethod(lua_State* L, AstItem& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (handle.cls->family)
    {
    case ItemFamily::Node: return dispatchAstNodeMethod(L, handle, atom, str, len);
    case ItemFamily::Cst:  return dispatchCstNodeMethod(L, handle, atom, str, len);
    case ItemFamily::Plain:
        if (atom == ReflectAtom::Properties)
            return plainItemProperties(L, handle);
        if (handle.cls->methodHandler(L, handle, atom))
            return 1;
        break;
    }

    luaL_error(L, "%.*s is not a valid method of %s", int(len), str, handle.cls->kind);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astItemMethodTrampoline, checkAnyAstItem, dispatchAstItemMethod)
LUAU_REFLECT_NAMECALL(astItemNamecall, checkAnyAstItem, dispatchAstItemMethod)

static int astItemIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAnyAstItem);

    switch (atom)
    {
    case ReflectAtom::Kind:
        lua_pushstring(L, handle.cls->kind);
        return 1;

    case ReflectAtom::Category:
        lua_pushstring(L, handle.cls->category);
        return 1;

    case ReflectAtom::Id:
        lua_pushlightuserdatatagged(L, handle.target, TagId);
        return 1;

    default:
        break;
    }

    if (atom != ReflectAtom::Unknown)
        return pushCachedUserdataMethod(L, TagItem, keyStr, astItemMethodTrampoline);

    lua_pushnil(L);
    return 1;
}

static int astItemToString(lua_State* L)
{
    auto& handle = checkAnyAstItem(L, 1);

    switch (handle.cls->family)
    {
    case ItemFamily::Node:
        lua_pushfstring(L, "AstNode(%s)", handle.cls->kind);
        break;
    case ItemFamily::Cst:
        lua_pushfstring(L, "CstNode(%s)", handle.cls->kind);
        break;
    case ItemFamily::Plain:
        if (handle.cls->label)
            handle.cls->label(L, handle);
        else
            lua_pushstring(L, handle.cls->kind);
        break;
    }

    return 1;
}

static int astItemEq(lua_State* L)
{
    AstItem* a = static_cast<AstItem*>(lua_touserdatatagged(L, 1, TagItem));
    AstItem* b = static_cast<AstItem*>(lua_touserdatatagged(L, 2, TagItem));
    lua_pushboolean(L, a && b && a->target == b->target && a->cls == b->cls);
    return 1;
}

void registerAstItem(lua_State* L)
{
    initializeAstNodeClasses();
    initializeCstNodeClasses();
    registerUserdataType(L, TagItem, "AstItem", astItemDtor, astItemIndex, astItemToString, astItemEq, astItemNamecall);
}

} // namespace Luau
