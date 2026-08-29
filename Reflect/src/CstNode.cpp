// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushCstNode, checkCstNode, cstNodeDtor, CstNodeData, const Luau::CstNode*, TagCstNode, "CstNode")

typedef bool (*CstNodeMethodHandler)(lua_State* L, CstNodeData& handle, ReflectAtom atom);
typedef void (*CstNodePropCollector)(lua_State* L, CstNodeData& handle);

struct CstNodeClassInfo
{
    const char* kind = nullptr;
    const char* category = "generic";
    CstNodeMethodHandler methodHandler = nullptr;
    CstNodePropCollector propCollector = nullptr;
};

static std::vector<CstNodeClassInfo> s_cstClassTable;

template<typename T>
static void registerCstNodeClass(
    const char* kind,
    CstNodeMethodHandler methodHandler = nullptr,
    CstNodePropCollector propCollector = nullptr
)
{
    int idx = T::CstClassIndex();
    if (size_t(idx) >= s_cstClassTable.size())
        s_cstClassTable.resize(idx + 1, CstNodeClassInfo{"CstNode", "generic", nullptr, nullptr});
    s_cstClassTable[idx] = CstNodeClassInfo{kind, "generic", methodHandler, propCollector};
}

LUAU_REFLECT_GET_NODE_KIND(getCstNodeKind, const Luau::CstNode*, s_cstClassTable, "CstNode")
LUAU_REFLECT_GET_NODE_CATEGORY(getCstNodeCategory, const Luau::CstNode*, s_cstClassTable, "generic")

#define LUAU_REFLECT_CST_NODES(NODE, NODE_EMPTY) \
    NODE(CstAttr, "CstAttr", \
        LUAU_CST_FIELD_RO(HasAt, hasAt)) \
    NODE(CstParametrizedAttr, "CstParametrizedAttr", \
        LUAU_CST_FIELD_RO(OpenParenPosition, openParenPosition) \
        LUAU_CST_FIELD_RO(CloseParenPosition, closeParenPosition) \
        LUAU_CST_FIELD_RO(ArgsCommaPositions, argsCommaPositions)) \
    NODE(CstExprGroup, "CstExprGroup", \
        LUAU_CST_FIELD_RO(ClosePosition, closePosition)) \
    NODE(CstExprConstantNumber, "CstExprConstantNumber", \
        LUAU_CST_FIELD_RO(Value, value)) \
    NODE(CstExprConstantInteger, "CstExprConstantInteger", \
        LUAU_CST_FIELD_RO(Value, value)) \
    NODE(CstExprConstantString, "CstExprConstantString", \
        LUAU_CST_FIELD_RO(QuoteStyle, quoteStyle) \
        LUAU_CST_FIELD_RO(BlockDepth, blockDepth) \
        LUAU_CST_FIELD_RO(SourceString, sourceString)) \
    NODE(CstExprCall, "CstExprCall", \
        LUAU_CST_FIELD_RO(OpenParens, openParens) \
        LUAU_CST_FIELD_RO(CloseParens, closeParens) \
        LUAU_CST_FIELD_RO(CommaPositions, commaPositions)) \
    NODE(CstExprIndexExpr, "CstExprIndexExpr", \
        LUAU_CST_FIELD_RO(OpenBracketPosition, openBracketPosition) \
        LUAU_CST_FIELD_RO(CloseBracketPosition, closeBracketPosition)) \
    NODE(CstExprFunction, "CstExprFunction", \
        LUAU_CST_FIELD_RO(FunctionKeywordPosition, functionKeywordPosition) \
        LUAU_CST_FIELD_RO(OpenGenericsPosition, openGenericsPosition) \
        LUAU_CST_FIELD_RO(GenericsCommaPositions, genericsCommaPositions) \
        LUAU_CST_FIELD_RO(CloseGenericsPosition, closeGenericsPosition) \
        LUAU_CST_FIELD_RO(ArgsAnnotationColonPositions, argsAnnotationColonPositions) \
        LUAU_CST_FIELD_RO(ArgsCommaPositions, argsCommaPositions) \
        LUAU_CST_FIELD_RO(VarargAnnotationColonPosition, varargAnnotationColonPosition) \
        LUAU_CST_FIELD_RO(ReturnSpecifierPosition, returnSpecifierPosition)) \
    NODE(CstExprTable, "CstExprTable", \
        LUAU_CST_FIELD_RO(Items, items)) \
    NODE(CstExprOp, "CstExprOp", \
        LUAU_CST_FIELD_RO(OpPosition, opPosition)) \
    NODE(CstExprTypeAssertion, "CstExprTypeAssertion", \
        LUAU_CST_FIELD_RO(OpPosition, opPosition)) \
    NODE(CstExprIfElse, "CstExprIfElse", \
        LUAU_CST_FIELD_RO(IsElseIf, isElseIf) \
        LUAU_CST_FIELD_RO(ThenPosition, thenPosition) \
        LUAU_CST_FIELD_RO(ElsePosition, elsePosition)) \
    NODE(CstExprInterpString, "CstExprInterpString", \
        LUAU_CST_FIELD_RO(CommaPositions, stringPositions)) \
    NODE_EMPTY(CstExprExplicitTypeInstantiation, "CstExprExplicitTypeInstantiation") \
    NODE(CstStatDo, "CstStatDo", \
        LUAU_CST_FIELD_RO(StatsStartPosition, statsStartPosition) \
        LUAU_CST_FIELD_RO(EndPosition, endPosition)) \
    NODE(CstStatRepeat, "CstStatRepeat", \
        LUAU_CST_FIELD_RO(UntilPosition, untilPosition)) \
    NODE(CstStatReturn, "CstStatReturn", \
        LUAU_CST_FIELD_RO(CommaPositions, commaPositions)) \
    NODE(CstStatLocal, "CstStatLocal", \
        LUAU_CST_FIELD_RO(VarsAnnotationColonPositions, varsAnnotationColonPositions) \
        LUAU_CST_FIELD_RO(VarsCommaPositions, varsCommaPositions) \
        LUAU_CST_FIELD_RO(ValuesCommaPositions, valuesCommaPositions)) \
    NODE(CstStatFor, "CstStatFor", \
        LUAU_CST_FIELD_RO(AnnotationColonPosition, annotationColonPosition) \
        LUAU_CST_FIELD_RO(EqualsPosition, equalsPosition) \
        LUAU_CST_FIELD_RO(EndCommaPosition, endCommaPosition) \
        LUAU_CST_FIELD_RO(StepCommaPosition, stepCommaPosition)) \
    NODE(CstStatForIn, "CstStatForIn", \
        LUAU_CST_FIELD_RO(VarsAnnotationColonPositions, varsAnnotationColonPositions) \
        LUAU_CST_FIELD_RO(VarsCommaPositions, varsCommaPositions) \
        LUAU_CST_FIELD_RO(ValuesCommaPositions, valuesCommaPositions)) \
    NODE(CstStatAssign, "CstStatAssign", \
        LUAU_CST_FIELD_RO(VarsCommaPositions, varsCommaPositions) \
        LUAU_CST_FIELD_RO(EqualsPosition, equalsPosition) \
        LUAU_CST_FIELD_RO(ValuesCommaPositions, valuesCommaPositions)) \
    NODE(CstStatCompoundAssign, "CstStatCompoundAssign", \
        LUAU_CST_FIELD_RO(OpPosition, opPosition)) \
    NODE(CstStatFunction, "CstStatFunction", \
        LUAU_CST_FIELD_RO(FunctionKeywordPosition, functionKeywordPosition)) \
    NODE(CstStatLocalFunction, "CstStatLocalFunction", \
        LUAU_CST_FIELD_RO(LocalKeywordPosition, localKeywordPosition) \
        LUAU_CST_FIELD_RO(FunctionKeywordPosition, functionKeywordPosition)) \
    NODE(CstGenericType, "CstGenericType", \
        LUAU_CST_FIELD_RO(DefaultEqualsPosition, defaultEqualsPosition)) \
    NODE(CstGenericTypePack, "CstGenericTypePack", \
        LUAU_CST_FIELD_RO(EllipsisPosition, ellipsisPosition) \
        LUAU_CST_FIELD_RO(DefaultEqualsPosition, defaultEqualsPosition)) \
    NODE(CstStatTypeAlias, "CstStatTypeAlias", \
        LUAU_CST_FIELD_RO(TypeKeywordPosition, typeKeywordPosition) \
        LUAU_CST_FIELD_RO(GenericsOpenPosition, genericsOpenPosition) \
        LUAU_CST_FIELD_RO(GenericsCommaPositions, genericsCommaPositions) \
        LUAU_CST_FIELD_RO(GenericsClosePosition, genericsClosePosition) \
        LUAU_CST_FIELD_RO(EqualsPosition, equalsPosition)) \
    NODE(CstStatTypeFunction, "CstStatTypeFunction", \
        LUAU_CST_FIELD_RO(TypeKeywordPosition, typeKeywordPosition) \
        LUAU_CST_FIELD_RO(FunctionKeywordPosition, functionKeywordPosition)) \
    NODE(CstTypeReference, "CstTypeReference", \
        LUAU_CST_FIELD_RO(PrefixPointPosition, prefixPointPosition) \
        LUAU_CST_FIELD_RO(OpenParametersPosition, openParametersPosition) \
        LUAU_CST_FIELD_RO(ParametersCommaPositions, parametersCommaPositions) \
        LUAU_CST_FIELD_RO(CloseParametersPosition, closeParametersPosition)) \
    NODE(CstTypeTable, "CstTypeTable", \
        LUAU_CST_FIELD_RO(IsArray, isArray)) \
    NODE(CstTypeFunction, "CstTypeFunction", \
        LUAU_CST_FIELD_RO(OpenGenericsPosition, openGenericsPosition) \
        LUAU_CST_FIELD_RO(GenericsCommaPositions, genericsCommaPositions) \
        LUAU_CST_FIELD_RO(CloseGenericsPosition, closeGenericsPosition) \
        LUAU_CST_FIELD_RO(OpenArgsPosition, openArgsPosition) \
        LUAU_CST_FIELD_RO(ArgumentNameColonPositions, argumentNameColonPositions) \
        LUAU_CST_FIELD_RO(ArgumentsCommaPositions, argumentsCommaPositions) \
        LUAU_CST_FIELD_RO(CloseArgsPosition, closeArgsPosition) \
        LUAU_CST_FIELD_RO(ReturnArrowPosition, returnArrowPosition)) \
    NODE(CstTypeTypeof, "CstTypeTypeof", \
        LUAU_CST_FIELD_RO(OpenPosition, openPosition) \
        LUAU_CST_FIELD_RO(ClosePosition, closePosition)) \
    NODE(CstTypeUnion, "CstTypeUnion", \
        LUAU_CST_FIELD_RO(LeadingPosition, leadingPosition) \
        LUAU_CST_FIELD_RO(SeparatorPositions, separatorPositions)) \
    NODE(CstTypeIntersection, "CstTypeIntersection", \
        LUAU_CST_FIELD_RO(LeadingPosition, leadingPosition) \
        LUAU_CST_FIELD_RO(SeparatorPositions, separatorPositions)) \
    NODE(CstTypeSingletonString, "CstTypeSingletonString", \
        LUAU_CST_FIELD_RO(BlockDepth, blockDepth) \
        LUAU_CST_FIELD_RO(SourceString, sourceString)) \
    NODE(CstTypeGroup, "CstTypeGroup", \
        LUAU_CST_FIELD_RO(ClosePosition, closePosition)) \
    NODE(CstTypePackExplicit, "CstTypePackExplicit", \
        LUAU_CST_FIELD_RO(OpenParenthesesPosition, openParenthesesPosition) \
        LUAU_CST_FIELD_RO(CloseParenthesesPosition, closeParenthesesPosition) \
        LUAU_CST_FIELD_RO(CommaPositions, commaPositions)) \
    NODE(CstTypePackGeneric, "CstTypePackGeneric", \
        LUAU_CST_FIELD_RO(EllipsisPosition, ellipsisPosition))

#define LUAU_GENERATE_CST_HANDLER(Class, KindStr, Fields) \
    static bool handle##Class##Methods(lua_State* L, CstNodeData& handle, ReflectAtom atom) \
    { \
        auto* n = static_cast<const Luau::Class*>(handle.node); \
        switch (atom) \
        { \
        Fields \
        default: return false; \
        } \
    }
#define LUAU_GENERATE_CST_HANDLER_EMPTY(Class, KindStr)

LUAU_REFLECT_CST_NODES(LUAU_GENERATE_CST_HANDLER, LUAU_GENERATE_CST_HANDLER_EMPTY)

#undef LUAU_GENERATE_CST_HANDLER
#undef LUAU_GENERATE_CST_HANDLER_EMPTY
#undef LUAU_CST_FIELD_RO

#define LUAU_CST_FIELD_RO(atomGet, memberExpr) \
    pushReflectValue(L, handle.doc, n->memberExpr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_GENERATE_CST_PROP_COLLECTOR(Class, KindStr, Fields) \
    static void collect##Class##CstProps(lua_State* L, CstNodeData& handle) \
    { \
        auto* n = static_cast<const Luau::Class*>(handle.node); \
        (void)n; \
        Fields \
    }
#define LUAU_GENERATE_CST_PROP_COLLECTOR_EMPTY(Class, KindStr)

LUAU_REFLECT_CST_NODES(LUAU_GENERATE_CST_PROP_COLLECTOR, LUAU_GENERATE_CST_PROP_COLLECTOR_EMPTY)

#undef LUAU_GENERATE_CST_PROP_COLLECTOR
#undef LUAU_GENERATE_CST_PROP_COLLECTOR_EMPTY
#undef LUAU_CST_FIELD_RO

static void initializeCstDispatchTables()
{
    static const bool initialized = []() {
#define LUAU_REGISTER_CST_NODE(Class, KindStr, Fields) \
        registerCstNodeClass<Luau::Class>(KindStr, handle##Class##Methods, collect##Class##CstProps);
#define LUAU_REGISTER_CST_NODE_EMPTY(Class, KindStr) \
        registerCstNodeClass<Luau::Class>(KindStr);

        LUAU_REFLECT_CST_NODES(LUAU_REGISTER_CST_NODE, LUAU_REGISTER_CST_NODE_EMPTY)

#undef LUAU_REGISTER_CST_NODE
#undef LUAU_REGISTER_CST_NODE_EMPTY
        return true;
    }();
    (void)initialized;
}

static int cstNodeProperties(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    if (!handle.node)
    {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, 0, 6);

    lua_pushlightuserdatatagged(L, (void*)handle.node, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, getCstNodeCategory(handle.node));
    lua_setfield(L, -2, "category");

    int idx = handle.node->classIndex;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].propCollector)
    {
        s_cstClassTable[idx].propCollector(L, handle);
    }

    return 1;
}

static int dispatchCstNodeMethod(lua_State* L, CstNodeData& handle, ReflectAtom atom, const char* str, size_t len)
{
    if (atom == ReflectAtom::Properties)
        return cstNodeProperties(L);

    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].methodHandler)
    {
        if (s_cstClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }
    luaL_error(L, "%.*s is not a valid method of CstNode", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(cstNodeMethodTrampoline, checkCstNode, dispatchCstNodeMethod)
LUAU_REFLECT_NAMECALL(cstNodeNamecall, checkCstNode, dispatchCstNodeMethod)
LUAU_REFLECT_INDEX(cstNodeIndex, checkCstNode, getCstNodeKind, getCstNodeCategory, TagCstNode, cstNodeMethodTrampoline)

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

void registerCstNode(lua_State* L)
{
    initializeCstDispatchTables();
    registerUserdataType(L, TagCstNode, "CstNode", cstNodeDtor, cstNodeIndex, cstNodeToString, cstNodeEq, cstNodeNamecall);
}

} // namespace Luau
