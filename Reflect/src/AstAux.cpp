// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

#include "AuxNodes.gen.inl"

namespace Luau
{

LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkAstAux, astAuxDtor, AstAuxData, TagAux, "AstAux")

const char* getAstAuxKind(const AstAuxData& handle)
{
    switch (handle.kind)
    {
    case Aux_TableProp:                  return "AstTableProp";
    case Aux_TableIndexer:               return "AstTableIndexer";
    case Aux_DeclaredExternTypeProperty: return "AstDeclaredExternTypeProperty";
    case Aux_ClassProperty:              return "AstClassProperty";
    case Aux_ClassMethod:                return "AstClassMethod";
    case Aux_Comment:                    return "AstComment";
    case Aux_TableItem:
        if (handle.tableItem.kind == Luau::AstExprTable::Item::Kind::List)
            return "list";
        else if (handle.tableItem.kind == Luau::AstExprTable::Item::Kind::Record)
            return "record";
        return "general";
    case Aux_CstTableItem:               return "CstTableItem";
    case Aux_TypeList:                   return "AstTypeList";
    }
    return "unknown";
}

static int astAuxProperties(lua_State* L, AstAuxData& handle)
{
    lua_createtable(L, 0, 8);

    lua_pushlightuserdatatagged(L, (void*)&handle, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, getAstAuxKind(handle));
    lua_setfield(L, -2, "kind");

    collectAuxProps(L, handle);

    return 1;
}

static int dispatchAstAuxMethod(lua_State* L, AstAuxData& handle, ReflectAtom atom, const char* str, size_t len)
{
    if (atom == ReflectAtom::Properties)
        return astAuxProperties(L, handle);

    if (dispatchAux(L, handle, atom))
        return 1;

    luaL_error(L, "%.*s is not a valid method of %s", int(len), str, getAstAuxKind(handle));
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astAuxMethodTrampoline, checkAstAux, dispatchAstAuxMethod)
LUAU_REFLECT_NAMECALL(astAuxNamecall, checkAstAux, dispatchAstAuxMethod)

static int astAuxIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstAux);

    switch (atom)
    {
    case ReflectAtom::Id:
        lua_pushlightuserdatatagged(L, (void*)&handle, TagId);
        return 1;

    case ReflectAtom::Kind:
        lua_pushstring(L, getAstAuxKind(handle));
        return 1;

    default:
        break;
    }

    if (atom != ReflectAtom::Unknown)
        return pushCachedUserdataMethod(L, TagAux, keyStr, astAuxMethodTrampoline);

    lua_pushnil(L);
    return 1;
}

static int astAuxToString(lua_State* L)
{
    auto& handle = checkAstAux(L, 1);
    switch (handle.kind)
    {
    case Aux_TableProp:
        lua_pushfstring(L, "AstAux(AstTableProp: %s)", handle.tableProp.name.value);
        return 1;
    case Aux_DeclaredExternTypeProperty:
        lua_pushfstring(L, "AstAux(AstDeclaredExternTypeProperty: %s)", handle.declaredExternProp.name.value);
        return 1;
    case Aux_ClassProperty:
        lua_pushfstring(L, "AstAux(AstClassProperty: %s)", handle.classProp.name.value);
        return 1;
    case Aux_ClassMethod:
        lua_pushfstring(L, "AstAux(AstClassMethod: %s)", handle.classMethod.functionName.value);
        return 1;
    case Aux_Comment:
        lua_pushfstring(L, "AstAux(AstComment: %s)", handle.comment.text.c_str());
        return 1;
    default:
        lua_pushfstring(L, "AstAux(%s)", getAstAuxKind(handle));
        return 1;
    }
}

static int astAuxEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagAux || lua_userdatatag(L, 2) != TagAux)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstAux(L, 1);
    auto& b = checkAstAux(L, 2);
    lua_pushboolean(L, &a == &b);
    return 1;
}

void registerAstAux(lua_State* L)
{
    registerUserdataType(L, TagAux, "AstAux", astAuxDtor, astAuxIndex, astAuxToString, astAuxEq, astAuxNamecall);
}

} // namespace Luau
