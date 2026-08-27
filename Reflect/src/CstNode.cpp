// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{


LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushCstNode, checkCstNode, cstNodeDtor, CstNodeData, const Luau::CstNode*, TagCstNode, "CstNode")

typedef bool (*CstNodePropertyHandler)(lua_State* L, CstNodeData& handle, ReflectAtom atom);
typedef bool (*CstNodeMethodHandler)(lua_State* L, CstNodeData& handle, ReflectAtom atom);

struct CstNodeClassInfo
{
    const char* kind = nullptr;
    CstNodePropertyHandler propHandler = nullptr;
    CstNodeMethodHandler methodHandler = nullptr;
};

static std::vector<CstNodeClassInfo> s_cstClassTable;

template<typename T>
static void registerCstNodeClass(
    const char* kind,
    CstNodePropertyHandler propHandler = nullptr,
    CstNodeMethodHandler methodHandler = nullptr
)
{
    int idx = T::CstClassIndex();
    if (size_t(idx) >= s_cstClassTable.size())
        s_cstClassTable.resize(idx + 1, CstNodeClassInfo{"CstNode", nullptr, nullptr});
    s_cstClassTable[idx] = CstNodeClassInfo{kind, propHandler, methodHandler};
}

LUAU_REFLECT_GET_NODE_KIND(getCstNodeKind, const Luau::CstNode*, s_cstClassTable, "CstNode")

#define LUAU_CST_HANDLER_START(name, NodeType) \
    static bool name(lua_State* L, CstNodeData& handle, ReflectAtom atom) \
    { \
        auto* n = static_cast<const Luau::NodeType*>(handle.node); \
        switch (atom) \
        {

#define LUAU_CST_HANDLER_END() \
        default: \
            return false; \
        } \
    }

LUAU_CST_HANDLER_START(handleCstAttrProps, CstAttr)
    case ReflectAtom::HasAt: { lua_pushboolean(L, n->hasAt); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstParametrizedAttrMethods, CstParametrizedAttr)
    case ReflectAtom::OpenParenPosition:  { pushPosition(L, handle.doc, n->openParenPosition); return true; }
    case ReflectAtom::CloseParenPosition: { pushPosition(L, handle.doc, n->closeParenPosition); return true; }
    case ReflectAtom::ArgsCommaPositions: { pushPositionArray(L, handle.doc, n->argsCommaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprGroupMethods, CstExprGroup)
    case ReflectAtom::ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprConstantNumberMethods, CstExprConstantNumber)
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprConstantIntegerMethods, CstExprConstantInteger)
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprConstantStringProps, CstExprConstantString)
    case ReflectAtom::QuoteStyle:
    {
        switch (n->quoteStyle)
        {
        case Luau::CstExprConstantString::QuoteStyle::QuotedSingle:
            lua_pushstring(L, "single");
            break;
        case Luau::CstExprConstantString::QuoteStyle::QuotedDouble:
            lua_pushstring(L, "double");
            break;
        case Luau::CstExprConstantString::QuoteStyle::QuotedRaw:
            lua_pushstring(L, "raw");
            break;
        case Luau::CstExprConstantString::QuoteStyle::QuotedInterp:
            lua_pushstring(L, "interp");
            break;
        }
        return true;
    }
    case ReflectAtom::BlockDepth:   { lua_pushinteger(L, int(n->blockDepth)); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprConstantStringMethods, CstExprConstantString)
    case ReflectAtom::SourceString: { lua_pushlstring(L, n->sourceString.data, n->sourceString.size); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprCallMethods, CstExprCall)
    case ReflectAtom::OpenParens:      { pushPosition(L, handle.doc, n->openParens); return true; }
    case ReflectAtom::CloseParens:     { pushPosition(L, handle.doc, n->closeParens); return true; }
    case ReflectAtom::CommaPositions:  { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprIndexExprMethods, CstExprIndexExpr)
    case ReflectAtom::OpenBracketPosition:  { pushPosition(L, handle.doc, n->openBracketPosition); return true; }
    case ReflectAtom::CloseBracketPosition: { pushPosition(L, handle.doc, n->closeBracketPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprFunctionMethods, CstExprFunction)
    case ReflectAtom::FunctionKeywordPosition:         { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    case ReflectAtom::OpenGenericsPosition:            { pushPosition(L, handle.doc, n->openGenericsPosition); return true; }
    case ReflectAtom::GenericsCommaPositions:          { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case ReflectAtom::CloseGenericsPosition:           { pushPosition(L, handle.doc, n->closeGenericsPosition); return true; }
    case ReflectAtom::ArgsAnnotationColonPositions:    { pushPositionArray(L, handle.doc, n->argsAnnotationColonPositions); return true; }
    case ReflectAtom::ArgsCommaPositions:              { pushPositionArray(L, handle.doc, n->argsCommaPositions); return true; }
    case ReflectAtom::VarargAnnotationColonPosition:   { pushPosition(L, handle.doc, n->varargAnnotationColonPosition); return true; }
    case ReflectAtom::ReturnSpecifierPosition:         { pushPosition(L, handle.doc, n->returnSpecifierPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprTableMethods, CstExprTable)
    case ReflectAtom::Items:
    {
        lua_createtable(L, int(n->items.size), 0);
        for (size_t i = 0; i < n->items.size; i++)
        {
            const auto& item = n->items.data[i];
            lua_createtable(L, 0, 5);
            pushPosition(L, handle.doc, item.indexerOpenPosition);
            lua_setfield(L, -2, "indexerOpenPosition");
            pushPosition(L, handle.doc, item.indexerClosePosition);
            lua_setfield(L, -2, "indexerClosePosition");
            pushPosition(L, handle.doc, item.equalsPosition);
            lua_setfield(L, -2, "equalsPosition");
            pushPosition(L, handle.doc, item.separatorPosition);
            lua_setfield(L, -2, "separatorPosition");
            if (item.separator == Luau::CstExprTable::Separator::Comma)
                lua_pushstring(L, "comma");
            else if (item.separator == Luau::CstExprTable::Separator::Semicolon)
                lua_pushstring(L, "semicolon");
            else
                lua_pushstring(L, "missing");
            lua_setfield(L, -2, "separator");

            lua_rawseti(L, -2, int(i + 1));
        }
        return true;
    }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprOpMethods, CstExprOp)
    case ReflectAtom::OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprTypeAssertionMethods, CstExprTypeAssertion)
    case ReflectAtom::OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprIfElseProps, CstExprIfElse)
    case ReflectAtom::IsElseIf: { lua_pushboolean(L, n->isElseIf); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprIfElseMethods, CstExprIfElse)
    case ReflectAtom::ThenPosition: { pushPosition(L, handle.doc, n->thenPosition); return true; }
    case ReflectAtom::ElsePosition: { pushPosition(L, handle.doc, n->elsePosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstExprInterpStringMethods, CstExprInterpString)
    case ReflectAtom::CommaPositions: { pushPositionArray(L, handle.doc, n->stringPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatDoMethods, CstStatDo)
    case ReflectAtom::StatsStartPosition: { pushPosition(L, handle.doc, n->statsStartPosition); return true; }
    case ReflectAtom::EndPosition:        { pushPosition(L, handle.doc, n->endPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatRepeatMethods, CstStatRepeat)
    case ReflectAtom::UntilPosition: { pushPosition(L, handle.doc, n->untilPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatReturnMethods, CstStatReturn)
    case ReflectAtom::CommaPositions: { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatLocalMethods, CstStatLocal)
    case ReflectAtom::VarsAnnotationColonPositions: { pushPositionArray(L, handle.doc, n->varsAnnotationColonPositions); return true; }
    case ReflectAtom::VarsCommaPositions:           { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case ReflectAtom::ValuesCommaPositions:         { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatForMethods, CstStatFor)
    case ReflectAtom::AnnotationColonPosition: { pushPosition(L, handle.doc, n->annotationColonPosition); return true; }
    case ReflectAtom::EqualsPosition:          { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    case ReflectAtom::EndCommaPosition:        { pushPosition(L, handle.doc, n->endCommaPosition); return true; }
    case ReflectAtom::StepCommaPosition:       { pushPosition(L, handle.doc, n->stepCommaPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatForInMethods, CstStatForIn)
    case ReflectAtom::VarsAnnotationColonPositions: { pushPositionArray(L, handle.doc, n->varsAnnotationColonPositions); return true; }
    case ReflectAtom::VarsCommaPositions:           { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case ReflectAtom::ValuesCommaPositions:         { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatAssignMethods, CstStatAssign)
    case ReflectAtom::VarsCommaPositions:   { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case ReflectAtom::EqualsPosition:       { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    case ReflectAtom::ValuesCommaPositions: { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatCompoundAssignMethods, CstStatCompoundAssign)
    case ReflectAtom::OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatFunctionMethods, CstStatFunction)
    case ReflectAtom::FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatLocalFunctionMethods, CstStatLocalFunction)
    case ReflectAtom::LocalKeywordPosition:    { pushPosition(L, handle.doc, n->localKeywordPosition); return true; }
    case ReflectAtom::FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstGenericTypeMethods, CstGenericType)
    case ReflectAtom::DefaultEqualsPosition: { pushPosition(L, handle.doc, n->defaultEqualsPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstGenericTypePackMethods, CstGenericTypePack)
    case ReflectAtom::EllipsisPosition:      { pushPosition(L, handle.doc, n->ellipsisPosition); return true; }
    case ReflectAtom::DefaultEqualsPosition: { pushPosition(L, handle.doc, n->defaultEqualsPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatTypeAliasMethods, CstStatTypeAlias)
    case ReflectAtom::TypeKeywordPosition:    { pushPosition(L, handle.doc, n->typeKeywordPosition); return true; }
    case ReflectAtom::GenericsOpenPosition:   { pushPosition(L, handle.doc, n->genericsOpenPosition); return true; }
    case ReflectAtom::GenericsCommaPositions: { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case ReflectAtom::GenericsClosePosition:  { pushPosition(L, handle.doc, n->genericsClosePosition); return true; }
    case ReflectAtom::EqualsPosition:         { pushPosition(L, handle.doc, n->equalsPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstStatTypeFunctionMethods, CstStatTypeFunction)
    case ReflectAtom::TypeKeywordPosition:     { pushPosition(L, handle.doc, n->typeKeywordPosition); return true; }
    case ReflectAtom::FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeReferenceMethods, CstTypeReference)
    case ReflectAtom::PrefixPointPosition:      { pushPosition(L, handle.doc, n->prefixPointPosition); return true; }
    case ReflectAtom::OpenParametersPosition:   { pushPosition(L, handle.doc, n->openParametersPosition); return true; }
    case ReflectAtom::ParametersCommaPositions: { pushPositionArray(L, handle.doc, n->parametersCommaPositions); return true; }
    case ReflectAtom::CloseParametersPosition:  { pushPosition(L, handle.doc, n->closeParametersPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeTableProps, CstTypeTable)
    case ReflectAtom::IsArray: { lua_pushboolean(L, n->isArray); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeFunctionMethods, CstTypeFunction)
    case ReflectAtom::OpenGenericsPosition:         { pushPosition(L, handle.doc, n->openGenericsPosition); return true; }
    case ReflectAtom::GenericsCommaPositions:       { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case ReflectAtom::CloseGenericsPosition:        { pushPosition(L, handle.doc, n->closeGenericsPosition); return true; }
    case ReflectAtom::OpenArgsPosition:             { pushPosition(L, handle.doc, n->openArgsPosition); return true; }
    case ReflectAtom::ArgumentNameColonPositions:   { pushPositionArray(L, handle.doc, n->argumentNameColonPositions); return true; }
    case ReflectAtom::ArgumentsCommaPositions:      { pushPositionArray(L, handle.doc, n->argumentsCommaPositions); return true; }
    case ReflectAtom::CloseArgsPosition:            { pushPosition(L, handle.doc, n->closeArgsPosition); return true; }
    case ReflectAtom::ReturnArrowPosition:          { pushPosition(L, handle.doc, n->returnArrowPosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeTypeofMethods, CstTypeTypeof)
    case ReflectAtom::OpenPosition:  { pushPosition(L, handle.doc, n->openPosition); return true; }
    case ReflectAtom::ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeUnionMethods, CstTypeUnion)
    case ReflectAtom::LeadingPosition:    { pushPosition(L, handle.doc, n->leadingPosition); return true; }
    case ReflectAtom::SeparatorPositions: { pushPositionArray(L, handle.doc, n->separatorPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeIntersectionMethods, CstTypeIntersection)
    case ReflectAtom::LeadingPosition:    { pushPosition(L, handle.doc, n->leadingPosition); return true; }
    case ReflectAtom::SeparatorPositions: { pushPositionArray(L, handle.doc, n->separatorPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeSingletonStringProps, CstTypeSingletonString)
    case ReflectAtom::BlockDepth: { lua_pushinteger(L, int(n->blockDepth)); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeSingletonStringMethods, CstTypeSingletonString)
    case ReflectAtom::SourceString: { lua_pushlstring(L, n->sourceString.data, n->sourceString.size); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypeGroupMethods, CstTypeGroup)
    case ReflectAtom::ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypePackExplicitMethods, CstTypePackExplicit)
    case ReflectAtom::OpenParenthesesPosition:  { pushPosition(L, handle.doc, n->openParenthesesPosition); return true; }
    case ReflectAtom::CloseParenthesesPosition: { pushPosition(L, handle.doc, n->closeParenthesesPosition); return true; }
    case ReflectAtom::CommaPositions:           { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
LUAU_CST_HANDLER_END()

LUAU_CST_HANDLER_START(handleCstTypePackGenericMethods, CstTypePackGeneric)
    case ReflectAtom::EllipsisPosition: { pushPosition(L, handle.doc, n->ellipsisPosition); return true; }
LUAU_CST_HANDLER_END()

static void initializeCstDispatchTables()
{
    // SAFETY: c++ guarantees thread safety in static inits like this (see https://iamroman.org/blog/2017/04/cpp11-static-init/) from c++11
    static const bool initialized = []() {
        registerCstNodeClass<Luau::CstAttr>("CstAttr", handleCstAttrProps);
        registerCstNodeClass<Luau::CstParametrizedAttr>("CstParametrizedAttr", nullptr, handleCstParametrizedAttrMethods);
        registerCstNodeClass<Luau::CstExprGroup>("CstExprGroup", nullptr, handleCstExprGroupMethods);
        registerCstNodeClass<Luau::CstExprConstantNumber>("CstExprConstantNumber", nullptr, handleCstExprConstantNumberMethods);
        registerCstNodeClass<Luau::CstExprConstantInteger>("CstExprConstantInteger", nullptr, handleCstExprConstantIntegerMethods);
        registerCstNodeClass<Luau::CstExprConstantString>("CstExprConstantString", handleCstExprConstantStringProps, handleCstExprConstantStringMethods);
        registerCstNodeClass<Luau::CstExprCall>("CstExprCall", nullptr, handleCstExprCallMethods);
        registerCstNodeClass<Luau::CstExprIndexExpr>("CstExprIndexExpr", nullptr, handleCstExprIndexExprMethods);
        registerCstNodeClass<Luau::CstExprFunction>("CstExprFunction", nullptr, handleCstExprFunctionMethods);
        registerCstNodeClass<Luau::CstExprTable>("CstExprTable", nullptr, handleCstExprTableMethods);
        registerCstNodeClass<Luau::CstExprOp>("CstExprOp", nullptr, handleCstExprOpMethods);
        registerCstNodeClass<Luau::CstExprTypeAssertion>("CstExprTypeAssertion", nullptr, handleCstExprTypeAssertionMethods);
        registerCstNodeClass<Luau::CstExprIfElse>("CstExprIfElse", handleCstExprIfElseProps, handleCstExprIfElseMethods);
        registerCstNodeClass<Luau::CstExprInterpString>("CstExprInterpString", nullptr, handleCstExprInterpStringMethods);
        registerCstNodeClass<Luau::CstExprExplicitTypeInstantiation>("CstExprExplicitTypeInstantiation");
        registerCstNodeClass<Luau::CstStatDo>("CstStatDo", nullptr, handleCstStatDoMethods);
        registerCstNodeClass<Luau::CstStatRepeat>("CstStatRepeat", nullptr, handleCstStatRepeatMethods);
        registerCstNodeClass<Luau::CstStatReturn>("CstStatReturn", nullptr, handleCstStatReturnMethods);
        registerCstNodeClass<Luau::CstStatLocal>("CstStatLocal", nullptr, handleCstStatLocalMethods);
        registerCstNodeClass<Luau::CstStatFor>("CstStatFor", nullptr, handleCstStatForMethods);
        registerCstNodeClass<Luau::CstStatForIn>("CstStatForIn", nullptr, handleCstStatForInMethods);
        registerCstNodeClass<Luau::CstStatAssign>("CstStatAssign", nullptr, handleCstStatAssignMethods);
        registerCstNodeClass<Luau::CstStatCompoundAssign>("CstStatCompoundAssign", nullptr, handleCstStatCompoundAssignMethods);
        registerCstNodeClass<Luau::CstStatFunction>("CstStatFunction", nullptr, handleCstStatFunctionMethods);
        registerCstNodeClass<Luau::CstStatLocalFunction>("CstStatLocalFunction", nullptr, handleCstStatLocalFunctionMethods);
        registerCstNodeClass<Luau::CstGenericType>("CstGenericType", nullptr, handleCstGenericTypeMethods);
        registerCstNodeClass<Luau::CstGenericTypePack>("CstGenericTypePack", nullptr, handleCstGenericTypePackMethods);
        registerCstNodeClass<Luau::CstStatTypeAlias>("CstStatTypeAlias", nullptr, handleCstStatTypeAliasMethods);
        registerCstNodeClass<Luau::CstStatTypeFunction>("CstStatTypeFunction", nullptr, handleCstStatTypeFunctionMethods);
        registerCstNodeClass<Luau::CstTypeReference>("CstTypeReference", nullptr, handleCstTypeReferenceMethods);
        registerCstNodeClass<Luau::CstTypeTable>("CstTypeTable", handleCstTypeTableProps);
        registerCstNodeClass<Luau::CstTypeFunction>("CstTypeFunction", nullptr, handleCstTypeFunctionMethods);
        registerCstNodeClass<Luau::CstTypeTypeof>("CstTypeTypeof", nullptr, handleCstTypeTypeofMethods);
        registerCstNodeClass<Luau::CstTypeUnion>("CstTypeUnion", nullptr, handleCstTypeUnionMethods);
        registerCstNodeClass<Luau::CstTypeIntersection>("CstTypeIntersection", nullptr, handleCstTypeIntersectionMethods);
        registerCstNodeClass<Luau::CstTypeSingletonString>("CstTypeSingletonString", handleCstTypeSingletonStringProps, handleCstTypeSingletonStringMethods);
        registerCstNodeClass<Luau::CstTypeGroup>("CstTypeGroup", nullptr, handleCstTypeGroupMethods);
        registerCstNodeClass<Luau::CstTypePackExplicit>("CstTypePackExplicit", nullptr, handleCstTypePackExplicitMethods);
        registerCstNodeClass<Luau::CstTypePackGeneric>("CstTypePackGeneric", nullptr, handleCstTypePackGenericMethods);
        return true;
    }();
    (void)initialized;
}

LUAU_REFLECT_METHOD_TRAMPOLINE(cstNodeMethodTrampoline, checkCstNode, s_cstClassTable, "CstNode");

static int cstNodeIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkCstNode);
    const Luau::CstNode* node = handle.node;

    if (atom == ReflectAtom::Kind)
    {
        lua_pushstring(L, getCstNodeKind(node));
        return 1;
    }

    int idx = node->classIndex;
    if (atom != ReflectAtom::Unknown && idx >= 0 && idx < int(s_cstClassTable.size()))
    {
        const CstNodeClassInfo& info = s_cstClassTable[idx];
        if (info.propHandler && info.propHandler(L, handle, atom))
            return 1;

        if (info.methodHandler)
            return pushCachedUserdataMethod(L, TagCstNode, keyStr, cstNodeMethodTrampoline);
    }

    lua_pushnil(L);
    return 1;
}

static int cstNodeToString(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    lua_pushfstring(L, "CstNode(%s)", getCstNodeKind(handle.node));
    return 1;
}

static int cstNodeEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagCstNode || lua_userdatatag(L, 2) != TagCstNode)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkCstNode(L, 1);
    auto& b = checkCstNode(L, 2);
    lua_pushboolean(L, a.node == b.node && a.doc == b.doc);
    return 1;
}

static int cstNodeNamecall(lua_State* L)
{
    LUAU_REFLECT_PREPARE_NAMECALL(checkCstNode);
    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].methodHandler)
    {
        if (s_cstClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }

    luaL_error(L, "%.*s is not a valid method of CstNode", int(len), str);
}

void registerCstNode(lua_State* L)
{
    initializeCstDispatchTables();
    registerUserdataType(L, TagCstNode, "CstNode", cstNodeDtor, cstNodeIndex, cstNodeToString, cstNodeEq, nullptr, cstNodeNamecall);
}

} // namespace Luau
