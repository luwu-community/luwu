// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

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
    case Aux_TypeList:                   return "AstTypeList";
    }
    return "unknown";
}

static bool handleAuxTableProp(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.tableProp;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Name, SetName, n.name)
    LUAU_AUX_FIELD_RW(Location, SetLocation, n.location)
    LUAU_AUX_FIELD_RW(Type, SetType, n.type)
    LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)
    default: return false;
    }
}

static bool handleAuxTableIndexer(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.tableIndexer;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Location, SetLocation, n.location)
    LUAU_AUX_FIELD_RW(IndexType, SetIndexType, n.indexType)
    LUAU_AUX_FIELD_RW(ResultType, SetResultType, n.resultType)
    LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)
    default: return false;
    }
}

static bool handleAuxDeclaredExternProp(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.declaredExternProp;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Name, SetName, n.name)
    LUAU_AUX_FIELD_RW(Location, SetLocation, n.location)
    LUAU_AUX_FIELD_RW(Type, SetType, n.ty)
    LUAU_AUX_FIELD_RW(IsMethod, SetIsMethod, n.isMethod)
    LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)
    default: return false;
    }
}

static bool handleAuxClassProperty(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.classProp;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Name, SetName, n.name)
    LUAU_AUX_FIELD_RW(Type, SetType, n.ty)
    default: return false;
    }
}

static bool handleAuxClassMethod(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.classMethod;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Name, SetName, n.functionName)
    LUAU_AUX_FIELD_RW(Func, SetFunc, n.function)
    default: return false;
    }
}

static bool handleAuxLocal(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto* n = handle.local;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Name, SetName, n->name)
    LUAU_AUX_FIELD_RW(Location, SetLocation, n->location)
    LUAU_AUX_FIELD_RW(Shadow, SetShadow, n->shadow)
    LUAU_AUX_FIELD_RW(IsConst, SetIsConst, n->isConst)
    LUAU_AUX_FIELD_RW(Depth, SetDepth, n->functionDepth)
    LUAU_AUX_FIELD_RW(Annotation, SetAnnotation, n->annotation)
    default: return false;
    }
}

static bool handleAuxComment(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.comment;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Location, SetLocation, n.location)
    case ReflectAtom::Type:
        switch (n.type)
        {
        case Luau::Lexeme::Comment:       lua_pushstring(L, "single"); return true;
        case Luau::Lexeme::BlockComment:  lua_pushstring(L, "block"); return true;
        case Luau::Lexeme::BrokenComment: lua_pushstring(L, "broken"); return true;
        default:                          lua_pushstring(L, "unknown"); return true;
        }
    case ReflectAtom::Text:
    {
        LUAU_ASSERT(handle.doc);
        auto [startOff, endOff] = locationToOffsets(handle.doc->lineOffsets, handle.doc->source.size(), n.location);
        lua_pushlstring(L, handle.doc->source.data() + startOff, endOff - startOff);
        return true;
    }
    default: return false;
    }
}

static bool handleAuxTableItem(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.tableItem;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Key, SetKey, n.key)
    LUAU_AUX_FIELD_RW(Value, SetValue, n.value)
    LUAU_AUX_FIELD_RW(Kind, SetKind, n.kind)
    default: return false;
    }
}

static bool handleAuxTypeList(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.typeList;
    switch (atom)
    {
    LUAU_AUX_FIELD_RW(Types, SetTypes, n.types)
    LUAU_AUX_FIELD_RW(TailType, SetTailType, n.tailType)
    default: return false;
    }
}

static bool handleAuxCstTableItem(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    auto& n = handle.cstTableItem;
    switch (atom)
    {
    LUAU_AUX_FIELD_RO(IndexerOpenPosition, n.indexerOpenPosition)
    LUAU_AUX_FIELD_RO(IndexerClosePosition, n.indexerClosePosition)
    LUAU_AUX_FIELD_RO(EqualsPosition, n.equalsPosition)
    LUAU_AUX_FIELD_RO(SeparatorPosition, n.separatorPosition)
    case ReflectAtom::Separator:
        if (n.separator == Luau::CstExprTable::Separator::Comma)
            lua_pushstring(L, "comma");
        else if (n.separator == Luau::CstExprTable::Separator::Semicolon)
            lua_pushstring(L, "semicolon");
        else
            lua_pushstring(L, "missing");
        return true;
    default: return false;
    }
}

static int dispatchAstAuxMethod(lua_State* L, AstAuxData& handle, ReflectAtom atom, const char* str, size_t len)
{
    bool ok = false;
    switch (handle.kind)
    {
    case Aux_TableProp:                  ok = handleAuxTableProp(L, handle, atom); break;
    case Aux_TableIndexer:               ok = handleAuxTableIndexer(L, handle, atom); break;
    case Aux_DeclaredExternTypeProperty: ok = handleAuxDeclaredExternProp(L, handle, atom); break;
    case Aux_ClassProperty:              ok = handleAuxClassProperty(L, handle, atom); break;
    case Aux_ClassMethod:                ok = handleAuxClassMethod(L, handle, atom); break;
    case Aux_Local:                      ok = handleAuxLocal(L, handle, atom); break;
    case Aux_Comment:                    ok = handleAuxComment(L, handle, atom); break;
    case Aux_TableItem:                  ok = handleAuxTableItem(L, handle, atom); break;
    case Aux_TypeList:                   ok = handleAuxTypeList(L, handle, atom); break;
    case Aux_CstTableItem:               ok = handleAuxCstTableItem(L, handle, atom); break;
    }

    if (ok)
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
        if (handle.kind == Aux_Local)
            lua_pushlightuserdatatagged(L, (void*)handle.local, TagId);
        else
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
    registerUserdataType(L, TagAux, "AstAux", astAuxDtor, astAuxIndex, astAuxToString, nullptr, nullptr, astAuxNamecall);
}

} // namespace Luau
