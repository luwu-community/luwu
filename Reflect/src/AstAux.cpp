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

inline AstArray<char> getCommentText(const AstAuxData& handle)
{
    if (!handle.doc)
        return {nullptr, 0};
    auto [startOff, endOff] = locationToOffsets(handle.doc->lineOffsets, handle.doc->source.size(), handle.comment.location);
    return {const_cast<char*>(handle.doc->source.data() + startOff), endOff - startOff};
}

#define LUAU_REFLECT_AUX_TYPES(AUX) \
    AUX(Aux_TableProp, tableProp, "AstTableProp", \
        LUAU_AUX_FIELD_RW(Name, SetName, n.name) \
        LUAU_AUX_FIELD_RW(Location, SetLocation, n.location) \
        LUAU_AUX_FIELD_RW(Type, SetType, n.type) \
        LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)) \
    AUX(Aux_TableIndexer, tableIndexer, "AstTableIndexer", \
        LUAU_AUX_FIELD_RW(Location, SetLocation, n.location) \
        LUAU_AUX_FIELD_RW(IndexType, SetIndexType, n.indexType) \
        LUAU_AUX_FIELD_RW(ResultType, SetResultType, n.resultType) \
        LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)) \
    AUX(Aux_DeclaredExternTypeProperty, declaredExternProp, "AstDeclaredExternTypeProperty", \
        LUAU_AUX_FIELD_RW(Name, SetName, n.name) \
        LUAU_AUX_FIELD_RW(Location, SetLocation, n.location) \
        LUAU_AUX_FIELD_RW(Type, SetType, n.ty) \
        LUAU_AUX_FIELD_RW(IsMethod, SetIsMethod, n.isMethod) \
        LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)) \
    AUX(Aux_ClassProperty, classProp, "AstClassProperty", \
        LUAU_AUX_FIELD_RW(Name, SetName, n.name) \
        LUAU_AUX_FIELD_RW(Type, SetType, n.ty)) \
    AUX(Aux_ClassMethod, classMethod, "AstClassMethod", \
        LUAU_AUX_FIELD_RW(Name, SetName, n.functionName) \
        LUAU_AUX_FIELD_RW(Func, SetFunc, n.function)) \
    AUX(Aux_Local, local, "AstLocal", \
        LUAU_AUX_FIELD_RW(Name, SetName, n->name) \
        LUAU_AUX_FIELD_RW(Location, SetLocation, n->location) \
        LUAU_AUX_FIELD_RW(Shadow, SetShadow, n->shadow) \
        LUAU_AUX_FIELD_RW(IsConst, SetIsConst, n->isConst) \
        LUAU_AUX_FIELD_RW(Depth, SetDepth, n->functionDepth) \
        LUAU_AUX_FIELD_RW(Annotation, SetAnnotation, n->annotation)) \
    AUX(Aux_Comment, comment, "AstComment", \
        LUAU_AUX_FIELD_RW(Location, SetLocation, n.location) \
        LUAU_AUX_FIELD_RO(Type, n.type) \
        LUAU_AUX_FIELD_FN_RO(Text, getCommentText(handle))) \
    AUX(Aux_TableItem, tableItem, "AstTableItem", \
        LUAU_AUX_FIELD_RW(Key, SetKey, n.key) \
        LUAU_AUX_FIELD_RW(Value, SetValue, n.value) \
        LUAU_AUX_FIELD_RW(Kind, SetKind, n.kind)) \
    AUX(Aux_TypeList, typeList, "AstTypeList", \
        LUAU_AUX_FIELD_RW(Types, SetTypes, n.types) \
        LUAU_AUX_FIELD_RW(TailType, SetTailType, n.tailType)) \
    AUX(Aux_CstTableItem, cstTableItem, "CstTableItem", \
        LUAU_AUX_FIELD_RO(IndexerOpenPosition, n.indexerOpenPosition) \
        LUAU_AUX_FIELD_RO(IndexerClosePosition, n.indexerClosePosition) \
        LUAU_AUX_FIELD_RO(EqualsPosition, n.equalsPosition) \
        LUAU_AUX_FIELD_RO(SeparatorPosition, n.separatorPosition) \
        LUAU_AUX_FIELD_RO(Separator, n.separator))

#define LUAU_GENERATE_AUX_DISPATCH(KindEnum, Member, KindStr, Fields) \
    case KindEnum: \
    { \
        auto& n = handle.Member; \
        (void)n; \
        switch (atom) \
        { \
        Fields \
        default: return false; \
        } \
    }

static bool dispatchAux(lua_State* L, AstAuxData& handle, ReflectAtom atom)
{
    switch (handle.kind)
    {
    LUAU_REFLECT_AUX_TYPES(LUAU_GENERATE_AUX_DISPATCH)
    }
    return false;
}

#undef LUAU_GENERATE_AUX_DISPATCH
#undef LUAU_AUX_FIELD_RW
#undef LUAU_AUX_FIELD_RO
#undef LUAU_AUX_FIELD_FN_RO

#define LUAU_AUX_FIELD_RW(atomGet, atomSet, memberExpr) \
    pushReflectValue(L, handle.doc, memberExpr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_AUX_FIELD_RO(atomGet, memberExpr) \
    pushReflectValue(L, handle.doc, memberExpr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_AUX_FIELD_FN_RO(atomGet, expr) \
    pushReflectValue(L, handle.doc, expr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_GENERATE_AUX_PROPS(KindEnum, Member, KindStr, Fields) \
    case KindEnum: \
    { \
        auto& n = handle.Member; \
        (void)n; \
        Fields \
        break; \
    }

static int astAuxProperties(lua_State* L, AstAuxData& handle)
{
    lua_createtable(L, 0, 8);

    if (handle.kind == Aux_Local)
        lua_pushlightuserdatatagged(L, (void*)handle.local, TagId);
    else
        lua_pushlightuserdatatagged(L, (void*)&handle, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, getAstAuxKind(handle));
    lua_setfield(L, -2, "kind");

    switch (handle.kind)
    {
    LUAU_REFLECT_AUX_TYPES(LUAU_GENERATE_AUX_PROPS)
    }

    return 1;
}

#undef LUAU_GENERATE_AUX_PROPS
#undef LUAU_AUX_FIELD_RW
#undef LUAU_AUX_FIELD_RO
#undef LUAU_AUX_FIELD_FN_RO

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
    registerUserdataType(L, TagAux, "AstAux", astAuxDtor, astAuxIndex, astAuxToString, nullptr, astAuxNamecall);
}

} // namespace Luau
