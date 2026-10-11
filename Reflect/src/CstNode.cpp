// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "CstNodes.gen.inl"

namespace Luau
{

static const AstItemClass s_unknownCstClass = {"CstNode", "cst", ItemFamily::Cst};

const AstItemClass* cstNodeClass(const Luau::CstNode* node)
{
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_cstClassTable.size()))
        return &s_cstClassTable[idx];
    return &s_unknownCstClass;
}

void pushCstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::CstNode* node)
{
    if (!node)
    {
        lua_pushnil(L);
        return;
    }
    pushAstItem(L, doc, cstNodeClass(node), const_cast<Luau::CstNode*>(node));
}

void initializeCstNodeClasses()
{
    static const bool initialized = []() {
        registerCstNodeClasses();
        return true;
    }();
    (void)initialized;
}

static int cstNodeProperties(lua_State* L, AstItem& handle)
{
    lua_createtable(L, 0, 6);

    lua_pushlightuserdatatagged(L, handle.target, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, handle.cls->category);
    lua_setfield(L, -2, "category");

    if (handle.cls->propCollector)
        handle.cls->propCollector(L, handle);

    return 1;
}

int dispatchCstNodeMethod(lua_State* L, AstItem& handle, ReflectAtom atom, const char* str, size_t len)
{
    if (atom == ReflectAtom::Properties)
        return cstNodeProperties(L, handle);

    if (handle.cls->methodHandler && handle.cls->methodHandler(L, handle, atom))
        return 1;

    luaL_error(L, "%.*s is not a valid method of CstNode", int(len), str);
}

} // namespace Luau
