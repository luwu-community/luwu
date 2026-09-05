// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
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
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstTableProp{Luau::AstName(), Luau::Location(), nullptr, Luau::AstTableAccess::ReadWrite, std::nullopt})) \
        LUAU_AUX_FIELD_RW(Name, SetName, n.name) \
        LUAU_AUX_FIELD_RO(OrigLocation, n.location) \
        LUAU_AUX_FIELD_RW(Type, SetType, n.type) \
        LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)) \
    AUX(Aux_TableIndexer, tableIndexer, "AstTableIndexer", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstTableIndexer{nullptr, nullptr, Luau::Location(), Luau::AstTableAccess::ReadWrite, std::nullopt})) \
        LUAU_AUX_FIELD_RO(OrigLocation, n.location) \
        LUAU_AUX_FIELD_RW(IndexType, SetIndexType, n.indexType) \
        LUAU_AUX_FIELD_RW(ResultType, SetResultType, n.resultType) \
        LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)) \
    AUX(Aux_DeclaredExternTypeProperty, declaredExternProp, "AstDeclaredExternTypeProperty", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstDeclaredExternTypeProperty{Luau::AstName(), Luau::Location(), nullptr, false, Luau::Location(), Luau::AstTableAccess::ReadWrite})) \
        LUAU_AUX_FIELD_RW(Name, SetName, n.name) \
        LUAU_AUX_FIELD_RO(OrigLocation, n.location) \
        LUAU_AUX_FIELD_RW(Type, SetType, n.ty) \
        LUAU_AUX_FIELD_RW(IsMethod, SetIsMethod, n.isMethod) \
        LUAU_AUX_FIELD_RW(Access, SetAccess, n.access)) \
    AUX(Aux_ClassProperty, classProp, "AstClassProperty", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstClassProperty{Luau::Location(), Luau::AstName(), Luau::Location(), std::nullopt, nullptr})) \
        LUAU_AUX_FIELD_RW(Name, SetName, n.name) \
        LUAU_AUX_FIELD_RW(Type, SetType, n.ty)) \
    AUX(Aux_ClassMethod, classMethod, "AstClassMethod", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstClassMethod{std::nullopt, Luau::Location(), Luau::AstName(), Luau::Location(), nullptr})) \
        LUAU_AUX_FIELD_RW(Name, SetName, n.functionName) \
        LUAU_AUX_FIELD_RW(Func, SetFunc, n.function)) \
    AUX(Aux_Comment, comment, "AstComment", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::Comment{Luau::Lexeme::Type::Comment, Luau::Location()})) \
        LUAU_AUX_FIELD_RO(OrigLocation, n.location) \
        LUAU_AUX_FIELD_RO(Type, n.type) \
        LUAU_AUX_FIELD_FN_RO(Text, getCommentText(handle))) \
    AUX(Aux_TableItem, tableItem, "AstTableItem", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstExprTable::Item{Luau::AstExprTable::Item::Kind::List, nullptr, nullptr})) \
        LUAU_AUX_FIELD_RW(Key, SetKey, n.key) \
        LUAU_AUX_FIELD_RW(Value, SetValue, n.value) \
        LUAU_AUX_FIELD_RW(Kind, SetKind, n.kind)) \
    AUX(Aux_TypeList, typeList, "AstTypeList", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::AstTypeList{Luau::AstArray<Luau::AstType*>{nullptr, 0}, nullptr})) \
        LUAU_AUX_FIELD_RW(Types, SetTypes, n.types) \
        LUAU_AUX_FIELD_RW(TailType, SetTailType, n.tailType)) \
    AUX(Aux_CstTableItem, cstTableItem, "CstTableItem", \
        LUAU_AUX_DEFAULT(AstAuxData(doc, Luau::CstExprTable::Item{Luau::Position::missing(), Luau::Position::missing(), Luau::Position::missing(), Luau::CstExprTable::Separator::Missing, Luau::Position::missing()})) \
        LUAU_AUX_FIELD_RO(IndexerOpenPosition, n.indexerOpenPosition) \
        LUAU_AUX_FIELD_RO(IndexerClosePosition, n.indexerClosePosition) \
        LUAU_AUX_FIELD_RO(EqualsPosition, n.equalsPosition) \
        LUAU_AUX_FIELD_RO(SeparatorPosition, n.separatorPosition) \
        LUAU_AUX_FIELD_RO(Separator, n.separator))

/* default ctor */
#define LUAU_AUX_DEFAULT(...) out = __VA_ARGS__; return true;
#define LUAU_AUX_FIELD_RW(atomGet, atomSet, memberExpr)
#define LUAU_AUX_FIELD_RO(atomGet, memberExpr)
#define LUAU_AUX_FIELD_FN_RO(atomGet, expr)

#define LUAU_CHECK_AUX_DEFAULT(KindEnum, Member, KindStr, Fields) \
    if (kind == KindStr) \
    { \
        Fields \
    }

bool createDefaultAstAux(std::string_view kind, const std::shared_ptr<AstDocumentState>& doc, AstAuxData& out)
{
    LUAU_REFLECT_AUX_TYPES(LUAU_CHECK_AUX_DEFAULT)
    return false;
}

#undef LUAU_CHECK_AUX_DEFAULT
#undef LUAU_AUX_DEFAULT
#undef LUAU_AUX_FIELD_RW
#undef LUAU_AUX_FIELD_RO
#undef LUAU_AUX_FIELD_FN_RO

/* methods */
#define LUAU_AUX_DEFAULT(...)

#define LUAU_AUX_FIELD_RW(atomGet, atomSet, memberExpr) \
    case ReflectAtom::atomGet: \
        pushReflectValue(L, handle.doc, memberExpr); \
        return true; \
    case ReflectAtom::atomSet: \
        readReflectValue(L, handle.doc, 2, memberExpr); \
        lua_pushvalue(L, 1); \
        return true;

#define LUAU_AUX_FIELD_RO(atomGet, memberExpr) \
    case ReflectAtom::atomGet: \
        pushReflectValue(L, handle.doc, memberExpr); \
        return true;

#define LUAU_AUX_FIELD_FN_RO(atomGet, expr) \
    case ReflectAtom::atomGet: \
        pushReflectValue(L, handle.doc, expr); \
        return true;

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
#undef LUAU_AUX_DEFAULT
#undef LUAU_AUX_FIELD_RW
#undef LUAU_AUX_FIELD_RO
#undef LUAU_AUX_FIELD_FN_RO

/* properties */
#define LUAU_AUX_DEFAULT(...)

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
#undef LUAU_AUX_DEFAULT
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
