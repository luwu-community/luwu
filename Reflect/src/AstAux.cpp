// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableProp& prop)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, prop};
}

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableIndexer& indexer)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, indexer};
}

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstDeclaredExternTypeProperty& prop)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, prop};
}

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassProperty& prop)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, prop};
}

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassMethod& method)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, method};
}

AstAuxData& checkAstAux(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagAux)
        luaL_typeerrorL(L, idx, "AstAux");
    return *static_cast<AstAuxData*>(lua_touserdata(L, idx));
}

static void astAuxDtor(lua_State* L, void* userdata)
{
    static_cast<AstAuxData*>(userdata)->~AstAuxData();
}

static int astAuxIndex(lua_State* L)
{
    auto& handle = checkAstAux(L, 1);
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr);
    if (!keyStr)
    {
        lua_pushnil(L);
        return 1;
    }
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen);
    const auto& doc = handle.doc;

    switch (handle.kind)
    {
    case Aux_TableProp:
    {
        const auto& prop = handle.tableProp;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstTableProp");
            return 1;
        case ReflectAtom::Name:
            lua_pushstring(L, prop.name.value);
            return 1;
        case ReflectAtom::Location:
            pushLocation(L, doc, prop.location);
            return 1;
        case ReflectAtom::Type:
            pushAstNode(L, doc, prop.type);
            return 1;
        case ReflectAtom::Access:
            lua_pushstring(L, tableAccessToString(prop.access));
            return 1;
        default:
            lua_pushnil(L);
            return 1;
        }
    }
    case Aux_TableIndexer:
    {
        const auto& indexer = handle.tableIndexer;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstTableIndexer");
            return 1;
        case ReflectAtom::IndexType:
            pushAstNode(L, doc, indexer.indexType);
            return 1;
        case ReflectAtom::ResultType:
            pushAstNode(L, doc, indexer.resultType);
            return 1;
        case ReflectAtom::Location:
            pushLocation(L, doc, indexer.location);
            return 1;
        case ReflectAtom::Access:
            lua_pushstring(L, tableAccessToString(indexer.access));
            return 1;
        default:
            lua_pushnil(L);
            return 1;
        }
    }
    case Aux_DeclaredExternTypeProperty:
    {
        const auto& prop = handle.declaredExternProp;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstDeclaredExternTypeProperty");
            return 1;
        case ReflectAtom::Name:
            lua_pushstring(L, prop.name.value);
            return 1;
        case ReflectAtom::Location:
            pushLocation(L, doc, prop.location);
            return 1;
        case ReflectAtom::Type:
            pushAstNode(L, doc, prop.ty);
            return 1;
        case ReflectAtom::IsMethod:
            lua_pushboolean(L, prop.isMethod);
            return 1;
        case ReflectAtom::Access:
            lua_pushstring(L, tableAccessToString(prop.access));
            return 1;
        default:
            lua_pushnil(L);
            return 1;
        }
    }
    case Aux_ClassProperty:
    {
        const auto& prop = handle.classProp;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstClassProperty");
            return 1;
        case ReflectAtom::Name:
            lua_pushstring(L, prop.name.value);
            return 1;
        case ReflectAtom::Type:
            pushAstNode(L, doc, prop.ty);
            return 1;
        default:
            lua_pushnil(L);
            return 1;
        }
    }
    case Aux_ClassMethod:
    {
        const auto& method = handle.classMethod;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstClassMethod");
            return 1;
        case ReflectAtom::Name:
            lua_pushstring(L, method.functionName.value);
            return 1;
        case ReflectAtom::Func:
            pushAstNode(L, doc, method.function);
            return 1;
        default:
            lua_pushnil(L);
            return 1;
        }
    }
    }

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
    case Aux_TableIndexer:
        lua_pushstring(L, "AstAux(AstTableIndexer)");
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
    default:
        lua_pushstring(L, "AstAux(Unknown)");
        return 1;
    }
}

void registerAstAux(lua_State* L)
{
    registerUserdataType(L, TagAux, "AstAux", astAuxDtor, astAuxIndex, astAuxToString);
}

} // namespace Luau
