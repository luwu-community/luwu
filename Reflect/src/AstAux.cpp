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

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* local)
{
    if (!local)
    {
        lua_pushnil(L);
        return;
    }
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, local};
}

void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& comment)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, comment};
}

LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkAstAux, astAuxDtor, AstAuxData, TagAux, "AstAux")

static int astAuxIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstAux);

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
    case Aux_Local:
    {
        Luau::AstLocal* local = handle.local;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstLocal");
            return 1;
        case ReflectAtom::Name:
            lua_pushstring(L, local->name.value);
            return 1;
        case ReflectAtom::Location:
            pushLocation(L, doc, local->location);
            return 1;
        case ReflectAtom::Shadow:
            pushAstAux(L, doc, local->shadow);
            return 1;
        case ReflectAtom::IsConst:
            lua_pushboolean(L, local->isConst);
            return 1;
        case ReflectAtom::Depth:
            lua_pushinteger(L, int(local->functionDepth));
            return 1;
        case ReflectAtom::Annotation:
            pushAstNode(L, doc, local->annotation);
            return 1;
        default:
            lua_pushnil(L);
            return 1;
        }
    }
    case Aux_Comment:
    {
        const auto& comment = handle.comment;
        switch (atom)
        {
        case ReflectAtom::Kind:
            lua_pushstring(L, "AstComment");
            return 1;
        case ReflectAtom::Type:
        {
            switch (comment.type)
            {
            case Luau::Lexeme::Comment:
                lua_pushstring(L, "single");
                break;
            case Luau::Lexeme::BlockComment:
                lua_pushstring(L, "block");
                break;
            case Luau::Lexeme::BrokenComment:
                lua_pushstring(L, "broken");
                break;
            default:
                lua_pushstring(L, "unknown");
                break;
            }
            return 1;
        }
        case ReflectAtom::Location:
            pushLocation(L, doc, comment.location);
            return 1;
        case ReflectAtom::Text:
        {
            LUAU_ASSERT(doc);
            auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), comment.location);
            lua_pushlstring(L, doc->source.data() + startOff, endOff - startOff);
            return 1;
        }
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
    case Aux_Local:
        lua_pushfstring(L, "AstAux(AstLocal: %s)", handle.local->name.value ? handle.local->name.value : "");
        return 1;
    case Aux_Comment:
    {
        const auto& loc = handle.comment.location;
        lua_pushfstring(L, "AstAux(AstComment: %d:%d - %d:%d)", loc.begin.line + 1, loc.begin.column + 1, loc.end.line + 1, loc.end.column + 1);
        return 1;
    }
    default:
        lua_pushstring(L, "AstAux(Unknown)");
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
    if (a.kind != b.kind || a.doc != b.doc)
    {
        lua_pushboolean(L, false);
        return 1;
    }

    switch (a.kind)
    {
    case Aux_Local:
        lua_pushboolean(L, a.local == b.local);
        return 1;
    case Aux_Comment:
        lua_pushboolean(L, a.comment.location == b.comment.location && a.comment.type == b.comment.type);
        return 1;
    case Aux_TableProp:
        lua_pushboolean(L, a.tableProp.location == b.tableProp.location && a.tableProp.name == b.tableProp.name);
        return 1;
    case Aux_TableIndexer:
        lua_pushboolean(L, a.tableIndexer.location == b.tableIndexer.location);
        return 1;
    case Aux_DeclaredExternTypeProperty:
        lua_pushboolean(L, a.declaredExternProp.location == b.declaredExternProp.location && a.declaredExternProp.name == b.declaredExternProp.name);
        return 1;
    case Aux_ClassProperty:
        lua_pushboolean(L, a.classProp.name == b.classProp.name && a.classProp.ty == b.classProp.ty);
        return 1;
    case Aux_ClassMethod:
        lua_pushboolean(L, a.classMethod.functionName == b.classMethod.functionName && a.classMethod.function == b.classMethod.function);
        return 1;
    default:
        lua_pushboolean(L, false);
        return 1;
    }
}

void registerAstAux(lua_State* L)
{
    registerUserdataType(L, TagAux, "AstAux", astAuxDtor, astAuxIndex, astAuxToString, astAuxEq);
}

} // namespace Luau
