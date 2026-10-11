// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "CstNodes.gen.inl"

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushCstNode, checkCstNode, cstNodeDtor, CstNodeData, const Luau::CstNode*, TagCstNode, "CstNode")

LUAU_REFLECT_GET_NODE_KIND(getCstNodeKind, const Luau::CstNode*, s_cstClassTable, "CstNode")
LUAU_REFLECT_GET_NODE_CATEGORY(getCstNodeCategory, const Luau::CstNode*, s_cstClassTable, "generic")

static void initializeCstDispatchTables()
{
    static const bool initialized = []() {
        registerCstNodeClasses();
        return true;
    }();
    (void)initialized;
}

static int cstNodeProperties(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    if (!handle.node)
    {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, 0, 6);

    lua_pushlightuserdatatagged(L, (void*)handle.node, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, getCstNodeCategory(handle.node));
    lua_setfield(L, -2, "category");

    int idx = handle.node->classIndex;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].propCollector)
    {
        s_cstClassTable[idx].propCollector(L, handle);
    }

    return 1;
}

static int dispatchCstNodeMethod(lua_State* L, CstNodeData& handle, ReflectAtom atom, const char* str, size_t len)
{
    if (atom == ReflectAtom::Properties)
        return cstNodeProperties(L);

    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].methodHandler)
    {
        if (s_cstClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }
    luaL_error(L, "%.*s is not a valid method of CstNode", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(cstNodeMethodTrampoline, checkCstNode, dispatchCstNodeMethod)
LUAU_REFLECT_NAMECALL(cstNodeNamecall, checkCstNode, dispatchCstNodeMethod)
LUAU_REFLECT_INDEX(cstNodeIndex, checkCstNode, getCstNodeKind, getCstNodeCategory, TagCstNode, cstNodeMethodTrampoline)

static int cstNodeToString(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    lua_pushfstring(L, "CstNode(%s)", getCstNodeKind(handle.node));
    return 1;
}

LUAU_REFLECT_DEFINE_EQ(cstNodeEq, TagCstNode, checkCstNode, a.node == b.node)

void registerCstNode(lua_State* L)
{
    initializeCstDispatchTables();
    registerUserdataType(L, TagCstNode, "CstNode", cstNodeDtor, cstNodeIndex, cstNodeToString, cstNodeEq, cstNodeNamecall);
}

} // namespace Luau
