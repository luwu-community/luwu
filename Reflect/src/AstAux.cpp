// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

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
    case Aux_Local:                      return "AstLocal";
    case Aux_Comment:                    return "AstComment";
    case Aux_TableItem:
        if (handle.tableItem.kind == Luau::AstExprTable::Item::Kind::List)
            return "list";
        else if (handle.tableItem.kind == Luau::AstExprTable::Item::Kind::Record)
            return "record";
        return "general";
    case Aux_CstTableItem:               return "CstTableItem";
    }
    return "unknown";
}

static int astAuxIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstAux);

    switch (atom)
    {
    case ReflectAtom::Id:
        if (handle.kind == Aux_Local)
            lua_pushlightuserdatatagged(L, (void*)handle.local, TagAux);
        else
            lua_pushlightuserdatatagged(L, (void*)&handle, TagAux);
        return 1;

    case ReflectAtom::Kind:
        lua_pushstring(L, getAstAuxKind(handle));
        return 1;

    case ReflectAtom::Name:
        switch (handle.kind)
        {
        case Aux_TableProp:                  lua_pushstring(L, handle.tableProp.name.value); return 1;
        case Aux_DeclaredExternTypeProperty: lua_pushstring(L, handle.declaredExternProp.name.value); return 1;
        case Aux_ClassProperty:              lua_pushstring(L, handle.classProp.name.value); return 1;
        case Aux_ClassMethod:                lua_pushstring(L, handle.classMethod.functionName.value); return 1;
        case Aux_Local:                      lua_pushstring(L, handle.local->name.value); return 1;
        default: break;
        }
        break;

    case ReflectAtom::Location:
        switch (handle.kind)
        {
        case Aux_TableProp:                  pushLocation(L, doc, handle.tableProp.location); return 1;
        case Aux_TableIndexer:               pushLocation(L, doc, handle.tableIndexer.location); return 1;
        case Aux_DeclaredExternTypeProperty: pushLocation(L, doc, handle.declaredExternProp.location); return 1;
        case Aux_Local:                      pushLocation(L, doc, handle.local->location); return 1;
        case Aux_Comment:                    pushLocation(L, doc, handle.comment.location); return 1;
        default: break;
        }
        break;

    case ReflectAtom::Type:
        switch (handle.kind)
        {
        case Aux_TableProp:                  pushAstNode(L, doc, handle.tableProp.type); return 1;
        case Aux_DeclaredExternTypeProperty: pushAstNode(L, doc, handle.declaredExternProp.ty); return 1;
        case Aux_ClassProperty:              pushAstNode(L, doc, handle.classProp.ty); return 1;
        case Aux_Comment:
            switch (handle.comment.type)
            {
            case Luau::Lexeme::Comment:       lua_pushstring(L, "single"); return 1;
            case Luau::Lexeme::BlockComment:  lua_pushstring(L, "block"); return 1;
            case Luau::Lexeme::BrokenComment: lua_pushstring(L, "broken"); return 1;
            default:                          lua_pushstring(L, "unknown"); return 1;
            }
        default: break;
        }
        break;

    case ReflectAtom::Access:
        switch (handle.kind)
        {
        case Aux_TableProp:                  lua_pushstring(L, tableAccessToString(handle.tableProp.access)); return 1;
        case Aux_TableIndexer:               lua_pushstring(L, tableAccessToString(handle.tableIndexer.access)); return 1;
        case Aux_DeclaredExternTypeProperty: lua_pushstring(L, tableAccessToString(handle.declaredExternProp.access)); return 1;
        default: break;
        }
        break;

    case ReflectAtom::IndexType:
        if (handle.kind == Aux_TableIndexer)
        {
            pushAstNode(L, doc, handle.tableIndexer.indexType);
            return 1;
        }
        break;

    case ReflectAtom::ResultType:
        if (handle.kind == Aux_TableIndexer)
        {
            pushAstNode(L, doc, handle.tableIndexer.resultType);
            return 1;
        }
        break;

    case ReflectAtom::IsMethod:
        if (handle.kind == Aux_DeclaredExternTypeProperty)
        {
            lua_pushboolean(L, handle.declaredExternProp.isMethod);
            return 1;
        }
        break;

    case ReflectAtom::Func:
        if (handle.kind == Aux_ClassMethod)
        {
            pushAstNode(L, doc, handle.classMethod.function);
            return 1;
        }
        break;

    case ReflectAtom::Shadow:
        if (handle.kind == Aux_Local)
        {
            pushAstAux(L, doc, handle.local->shadow);
            return 1;
        }
        break;

    case ReflectAtom::IsConst:
        if (handle.kind == Aux_Local)
        {
            lua_pushboolean(L, handle.local->isConst);
            return 1;
        }
        break;

    case ReflectAtom::Depth:
        if (handle.kind == Aux_Local)
        {
            lua_pushinteger(L, int(handle.local->functionDepth));
            return 1;
        }
        break;

    case ReflectAtom::Annotation:
        if (handle.kind == Aux_Local)
        {
            pushAstNode(L, doc, handle.local->annotation);
            return 1;
        }
        break;

    case ReflectAtom::Text:
        if (handle.kind == Aux_Comment)
        {
            LUAU_ASSERT(doc);
            auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), handle.comment.location);
            lua_pushlstring(L, doc->source.data() + startOff, endOff - startOff);
            return 1;
        }
        break;

    case ReflectAtom::Key:
        if (handle.kind == Aux_TableItem)
        {
            if (handle.tableItem.key)
                pushAstNode(L, doc, handle.tableItem.key);
            else
                lua_pushnil(L);
            return 1;
        }
        break;

    case ReflectAtom::Value:
        if (handle.kind == Aux_TableItem)
        {
            pushAstNode(L, doc, handle.tableItem.value);
            return 1;
        }
        break;

    case ReflectAtom::IndexerOpenPosition:
        if (handle.kind == Aux_CstTableItem)
        {
            pushPosition(L, doc, handle.cstTableItem.indexerOpenPosition);
            return 1;
        }
        break;

    case ReflectAtom::IndexerClosePosition:
        if (handle.kind == Aux_CstTableItem)
        {
            pushPosition(L, doc, handle.cstTableItem.indexerClosePosition);
            return 1;
        }
        break;

    case ReflectAtom::EqualsPosition:
        if (handle.kind == Aux_CstTableItem)
        {
            pushPosition(L, doc, handle.cstTableItem.equalsPosition);
            return 1;
        }
        break;

    case ReflectAtom::SeparatorPosition:
        if (handle.kind == Aux_CstTableItem)
        {
            pushPosition(L, doc, handle.cstTableItem.separatorPosition);
            return 1;
        }
        break;

    case ReflectAtom::Separator:
        if (handle.kind == Aux_CstTableItem)
        {
            if (handle.cstTableItem.separator == Luau::CstExprTable::Separator::Comma)
                lua_pushstring(L, "comma");
            else if (handle.cstTableItem.separator == Luau::CstExprTable::Separator::Semicolon)
                lua_pushstring(L, "semicolon");
            else
                lua_pushstring(L, "missing");
            return 1;
        }
        break;

    default:
        break;
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
        lua_pushfstring(L, "AstAux(%s)", getAstAuxKind(handle));
        return 1;
    }
}

void registerAstAux(lua_State* L)
{
    // eq for astaux are not actually implemented in parser so reflect also does not attempt to provide any support for it
    registerUserdataType(L, TagAux, "AstAux", astAuxDtor, astAuxIndex, astAuxToString, nullptr);
}

} // namespace Luau
