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

static bool dispatchAux(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    switch (handle.kind)
    {
    case Aux_TableProp:
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
    case Aux_TableIndexer:
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
    case Aux_DeclaredExternTypeProperty:
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
    case Aux_ClassProperty:
    {
        auto& n = handle.classProp;
        switch (atom)
        {
        LUAU_AUX_FIELD_RW(Name, SetName, n.name)
        LUAU_AUX_FIELD_RW(Type, SetType, n.ty)
        default: return false;
        }
    }
    case Aux_ClassMethod:
    {
        auto& n = handle.classMethod;
        switch (atom)
        {
        LUAU_AUX_FIELD_RW(Name, SetName, n.functionName)
        LUAU_AUX_FIELD_RW(Func, SetFunc, n.function)
        default: return false;
        }
    }
    case Aux_Local:
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
    case Aux_Comment:
    {
        auto& n = handle.comment;
        switch (atom)
        {
        LUAU_AUX_FIELD_RW(Location, SetLocation, n.location)
        LUAU_AUX_FIELD_RO(Type, n.type)
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
    case Aux_TableItem:
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
    case Aux_TypeList:
    {
        auto& n = handle.typeList;
        switch (atom)
        {
        LUAU_AUX_FIELD_RW(Types, SetTypes, n.types)
        LUAU_AUX_FIELD_RW(TailType, SetTailType, n.tailType)
        default: return false;
        }
    }
    case Aux_CstTableItem:
    {
        auto& n = handle.cstTableItem;
        switch (atom)
        {
        LUAU_AUX_FIELD_RO(IndexerOpenPosition, n.indexerOpenPosition)
        LUAU_AUX_FIELD_RO(IndexerClosePosition, n.indexerClosePosition)
        LUAU_AUX_FIELD_RO(EqualsPosition, n.equalsPosition)
        LUAU_AUX_FIELD_RO(SeparatorPosition, n.separatorPosition)
        LUAU_AUX_FIELD_RO(Separator, n.separator)
        default: return false;
        }
    }
    }
    return false;
}

static int dispatchAstAuxMethod(lua_State* L, AstAuxData& handle, ReflectAtom atom, const char* str, size_t len)
{
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
