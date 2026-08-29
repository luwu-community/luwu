// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushAstNode, checkAstNode, astNodeDtor, AstNodeData, Luau::AstNode*, TagNode, "AstNode")

typedef bool (*NodeMethodHandler)(lua_State* L, AstNodeData& handle, ReflectAtom atom);

struct AstNodeClassInfo
{
    const char* kind = nullptr;
    const char* category = nullptr;
    // derived field used for filtering
    NodeCategory categoryEnum = NodeCategory::Unknown;
    NodeMethodHandler methodHandler = nullptr;
};

static std::vector<AstNodeClassInfo> s_nodeClassTable;

NodeCategory getNodeCategory(Luau::AstNode* node)
{
    if (!node)
        return NodeCategory::Unknown;
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()))
        return s_nodeClassTable[idx].categoryEnum;
    return NodeCategory::Unknown;
}

int getNodeClassIndexByKind(std::string_view kind)
{
    for (size_t i = 0; i < s_nodeClassTable.size(); i++)
    {
        if (s_nodeClassTable[i].kind && kind == s_nodeClassTable[i].kind)
            return int(i);
    }
    return -1;
}

// Register every node statically to allow for all AST node operations to be direct jumps and not if-else hell.
template<typename T>
static void registerNodeClass(
    const char* kind,
    NodeCategory category,
    NodeMethodHandler methodHandler = nullptr
)
{
    int idx = T::ClassIndex();
    if (size_t(idx) >= s_nodeClassTable.size())
        s_nodeClassTable.resize(idx + 1, AstNodeClassInfo{"AstNode", "unknown", NodeCategory::Unknown, nullptr});
    s_nodeClassTable[idx] = AstNodeClassInfo{kind, categoryToString(category), category, methodHandler};
}

LUAU_REFLECT_GET_NODE_KIND(getNodeKind, Luau::AstNode*, s_nodeClassTable, "AstNode")
LUAU_REFLECT_GET_NODE_CATEGORY(getAstNodeCategory, Luau::AstNode*, s_nodeClassTable, "unknown")

struct DirectChildCollector : public Luau::AstVisitor
{
    std::vector<Luau::AstNode*> children;
    bool isRoot = true;

    bool visit(Luau::AstNode* node) override
    {
        if (isRoot)
        {
            isRoot = false;
            return true;
        }
        children.push_back(node);
        return false;
    }

    // By default visiting type annotations is disabled; we override this so visitor inspects these nodes
    bool visit(Luau::AstType* node) override
    {
        return visit(static_cast<Luau::AstNode*>(node));
    }

    bool visit(Luau::AstTypePack* node) override
    {
        return visit(static_cast<Luau::AstNode*>(node));
    }
};

#define LUAU_REFLECT_AST_NODES(NODE, NODE_EMPTY) \
    /* Statements */ \
    NODE(AstStatBlock, "AstStatBlock", Stat, \
        LUAU_AST_FIELD_RW(HasEnd, SetHasEnd, hasEnd) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatIf, "AstStatIf", Stat, \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(ThenBody, SetThenBody, thenbody) \
        LUAU_AST_FIELD_RW(ElseBody, SetElseBody, elsebody)) \
    NODE(AstStatWhile, "AstStatWhile", Stat, \
        LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo) \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatRepeat, "AstStatRepeat", Stat, \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE_EMPTY(AstStatBreak, "AstStatBreak", Stat) \
    NODE_EMPTY(AstStatContinue, "AstStatContinue", Stat) \
    NODE(AstStatReturn, "AstStatReturn", Stat, \
        LUAU_AST_FIELD_RW(List, SetList, list)) \
    NODE(AstStatExpr, "AstStatExpr", Stat, \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstStatLocal, "AstStatLocal", Stat, \
        LUAU_AST_FIELD_RW(IsConst, SetIsConst, isConst) \
        LUAU_AST_FIELD_RW(Exported, SetExported, isExported) \
        LUAU_AST_FIELD_RW(Vars, SetVars, vars) \
        LUAU_AST_FIELD_RW(Values, SetValues, values)) \
    NODE(AstStatFor, "AstStatFor", Stat, \
        LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo) \
        LUAU_AST_FIELD_RW(Var, SetVar, var) \
        LUAU_AST_FIELD_RW(From, SetFrom, from) \
        LUAU_AST_FIELD_RW(To, SetTo, to) \
        LUAU_AST_FIELD_RW(Step, SetStep, step) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatForIn, "AstStatForIn", Stat, \
        LUAU_AST_FIELD_RW(HasIn, SetHasIn, hasIn) \
        LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo) \
        LUAU_AST_FIELD_RW(Vars, SetVars, vars) \
        LUAU_AST_FIELD_RW(Values, SetValues, values) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatAssign, "AstStatAssign", Stat, \
        LUAU_AST_FIELD_RW(Vars, SetVars, vars) \
        LUAU_AST_FIELD_RW(Values, SetValues, values)) \
    NODE(AstStatCompoundAssign, "AstStatCompoundAssign", Stat, \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Var, SetVar, var) \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstStatFunction, "AstStatFunction", Stat, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Func, SetFunc, func)) \
    NODE(AstStatLocalFunction, "AstStatLocalFunction", Stat, \
        LUAU_AST_FIELD_RW(IsConst, SetIsConst, isConst) \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Func, SetFunc, func)) \
    NODE(AstStatTypeAlias, "AstStatTypeAlias", Stat, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Exported, SetExported, exported) \
        LUAU_AST_FIELD_RW(Type, SetType, type) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)) \
    NODE(AstStatTypeFunction, "AstStatTypeFunction", Stat, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Exported, SetExported, exported) \
        LUAU_AST_FIELD_RW(HasErrors, SetHasErrors, hasErrors) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatDeclareGlobal, "AstStatDeclareGlobal", Stat, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Type, SetType, type)) \
    NODE(AstStatDeclareFunction, "AstStatDeclareFunction", Stat, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Vararg, SetVararg, vararg) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks) \
        LUAU_AST_FIELD_RW(Params, SetParams, params) \
        LUAU_AST_FIELD_RW(ReturnTypes, SetReturnTypes, retTypes) \
        LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)) \
    NODE(AstStatClass, "AstStatClass", Stat, \
        LUAU_AST_FIELD_RW(Exported, SetExported, exported) \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Members, SetMembers, members)) \
    NODE(AstStatDeclareExternType, "AstStatDeclareExternType", Stat, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(SuperName, SetSuperName, superName) \
        LUAU_AST_FIELD_RW(Props, SetProps, props) \
        LUAU_AST_FIELD_RW(Indexer, SetIndexer, indexer) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)) \
    NODE(AstStatError, "AstStatError", Stat, \
        LUAU_AST_FIELD_RO(MessageIndex, messageIndex) \
        LUAU_AST_FIELD_RO(Expressions, expressions) \
        LUAU_AST_FIELD_RO(Statements, statements)) \
    \
    /* Expressions */ \
    NODE(AstExprGroup, "AstExprGroup", Expr, \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE_EMPTY(AstExprConstantNil, "AstExprConstantNil", Expr) \
    NODE(AstExprConstantBool, "AstExprConstantBool", Expr, \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstExprConstantNumber, "AstExprConstantNumber", Expr, \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstExprConstantInteger, "AstExprConstantInteger", Expr, \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstExprConstantString, "AstExprConstantString", Expr, \
        LUAU_AST_FIELD_RW(Value, SetValue, value) \
        LUAU_AST_FIELD_RW(QuoteStyle, SetQuoteStyle, quoteStyle)) \
    NODE(AstExprLocal, "AstExprLocal", Expr, \
        LUAU_AST_FIELD_RW(Upvalue, SetUpvalue, upvalue) \
        LUAU_AST_FIELD_RW(Local, SetLocal, local)) \
    NODE(AstExprGlobal, "AstExprGlobal", Expr, \
        LUAU_AST_FIELD_RW(Name, SetName, name)) \
    NODE_EMPTY(AstExprVarargs, "AstExprVarargs", Expr) \
    NODE(AstExprCall, "AstExprCall", Expr, \
        LUAU_AST_FIELD_RW(Self, SetSelf, self) \
        LUAU_AST_FIELD_RW(Func, SetFunc, func) \
        LUAU_AST_FIELD_RW(Args, SetArgs, args) \
        LUAU_AST_FIELD_RW(TypeArguments, SetTypeArguments, typeArguments)) \
    NODE(AstExprIndexName, "AstExprIndexName", Expr, \
        LUAU_AST_FIELD_RW(Index, SetIndex, index) \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstExprIndexExpr, "AstExprIndexExpr", Expr, \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr) \
        LUAU_AST_FIELD_RW(Index, SetIndex, index)) \
    NODE(AstExprFunction, "AstExprFunction", Expr, \
        LUAU_AST_FIELD_RW(Vararg, SetVararg, vararg) \
        LUAU_AST_FIELD_RW(DebugName, SetDebugName, debugname) \
        LUAU_AST_FIELD_RW(Args, SetArgs, args) \
        LUAU_AST_FIELD_RW(Body, SetBody, body) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks) \
        LUAU_AST_FIELD_RW(ReturnAnnotation, SetReturnAnnotation, returnAnnotation) \
        LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)) \
    NODE(AstExprTable, "AstExprTable", Expr, \
        LUAU_AST_FIELD_RW(Items, SetItems, items)) \
    NODE(AstExprUnary, "AstExprUnary", Expr, \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstExprBinary, "AstExprBinary", Expr, \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Left, SetLeft, left) \
        LUAU_AST_FIELD_RW(Right, SetRight, right)) \
    NODE(AstExprTypeAssertion, "AstExprTypeAssertion", Expr, \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr) \
        LUAU_AST_FIELD_RW(Annotation, SetAnnotation, annotation)) \
    NODE(AstExprIfElse, "AstExprIfElse", Expr, \
        LUAU_AST_FIELD_RW(HasElse, SetHasElse, hasElse) \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(TrueExpr, SetTrueExpr, trueExpr) \
        LUAU_AST_FIELD_RW(FalseExpr, SetFalseExpr, falseExpr)) \
    NODE(AstExprInterpString, "AstExprInterpString", Expr, \
        LUAU_AST_FIELD_RW(Strings, SetStrings, strings) \
        LUAU_AST_FIELD_RW(Expressions, SetExpressions, expressions)) \
    NODE(AstExprInstantiate, "AstExprInstantiate", Expr, \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr) \
        LUAU_AST_FIELD_RW(TypeArguments, SetTypeArguments, typeArguments)) \
    NODE(AstExprError, "AstExprError", Expr, \
        LUAU_AST_FIELD_RO(MessageIndex, messageIndex) \
        LUAU_AST_FIELD_RO(Expressions, expressions)) \
    \
    /* Types */ \
    NODE(AstTypeReference, "AstTypeReference", Type, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Prefix, SetPrefix, prefix) \
        LUAU_AST_FIELD_RW(HasParameterList, SetHasParameterList, hasParameterList) \
        LUAU_AST_FIELD_RW(Parameters, SetParameters, parameters)) \
    NODE(AstTypeTable, "AstTypeTable", Type, \
        LUAU_AST_FIELD_RW(Props, SetProps, props) \
        LUAU_AST_FIELD_RW(Indexer, SetIndexer, indexer)) \
    NODE(AstTypeFunction, "AstTypeFunction", Type, \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks) \
        LUAU_AST_FIELD_RW(ArgTypes, SetArgTypes, argTypes) \
        LUAU_AST_FIELD_RW(ReturnTypes, SetReturnTypes, returnTypes) \
        LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)) \
    NODE(AstTypeTypeof, "AstTypeTypeof", Type, \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE_EMPTY(AstTypeOptional, "AstTypeOptional", Type) \
    NODE(AstTypeUnion, "AstTypeUnion", Type, \
        LUAU_AST_FIELD_RW(Types, SetTypes, types)) \
    NODE(AstTypeIntersection, "AstTypeIntersection", Type, \
        LUAU_AST_FIELD_RW(Types, SetTypes, types)) \
    NODE(AstTypeSingletonBool, "AstTypeSingletonBool", Type, \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstTypeSingletonString, "AstTypeSingletonString", Type, \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstTypeGroup, "AstTypeGroup", Type, \
        LUAU_AST_FIELD_RW(Type, SetType, type)) \
    NODE(AstTypeError, "AstTypeError", Type, \
        LUAU_AST_FIELD_RO(IsMissing, isMissing) \
        LUAU_AST_FIELD_RO(MessageIndex, messageIndex) \
        LUAU_AST_FIELD_RO(Types, types)) \
    \
    /* Type Packs */ \
    NODE(AstTypePackExplicit, "AstTypePackExplicit", TypePack, \
        LUAU_AST_FIELD_RW(TypeList, SetTypeList, typeList)) \
    NODE(AstTypePackVariadic, "AstTypePackVariadic", TypePack, \
        LUAU_AST_FIELD_RW(VariadicType, SetVariadicType, variadicType)) \
    NODE(AstTypePackGeneric, "AstTypePackGeneric", TypePack, \
        LUAU_AST_FIELD_RW(Name, SetName, genericName)) \
    \
    /* Generics & Attributes */ \
    NODE(AstGenericType, "AstGenericType", Generic, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Type, SetType, defaultValue)) \
    NODE(AstGenericTypePack, "AstGenericTypePack", Generic, \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Type, SetType, defaultValue)) \
    NODE(AstAttr, "AstAttr", Attr, \
        LUAU_AST_FIELD_RW(Type, SetType, type) \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Args, SetArgs, args))

#define LUAU_GENERATE_AST_HANDLER(Class, KindStr, Cat, Fields) \
    static bool handle##Class##Methods(lua_State* L, AstNodeData& handle, ReflectAtom atom) \
    { \
        auto* n = static_cast<Luau::Class*>(handle.node); \
        switch (atom) \
        { \
        Fields \
        default: return false; \
        } \
    }
#define LUAU_GENERATE_AST_HANDLER_EMPTY(Class, KindStr, Cat)

LUAU_REFLECT_AST_NODES(LUAU_GENERATE_AST_HANDLER, LUAU_GENERATE_AST_HANDLER_EMPTY)

#undef LUAU_GENERATE_AST_HANDLER
#undef LUAU_GENERATE_AST_HANDLER_EMPTY

static void initializeDispatchTables()
{
    static const bool initialized = []() {
#define LUAU_REGISTER_AST_NODE(Class, KindStr, Cat, Fields) \
        registerNodeClass<Luau::Class>(KindStr, NodeCategory::Cat, handle##Class##Methods);
#define LUAU_REGISTER_AST_NODE_EMPTY(Class, KindStr, Cat) \
        registerNodeClass<Luau::Class>(KindStr, NodeCategory::Cat);

        LUAU_REFLECT_AST_NODES(LUAU_REGISTER_AST_NODE, LUAU_REGISTER_AST_NODE_EMPTY)

#undef LUAU_REGISTER_AST_NODE
#undef LUAU_REGISTER_AST_NODE_EMPTY
        return true;
    }();
    (void)initialized;
}

static int astNodeChildren(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    DirectChildCollector collector;
    handle.node->visit(&collector);
    pushArray(L, collector.children.size(), [&](size_t i) {
        pushAstNode(L, handle.doc, collector.children[i]);
    });
    return 1;
}

static int astNodeLocation(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    pushLocation(L, handle.doc, handle.node->location);
    return 1;
}

static int astNodeCst(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (const Luau::CstNode* const* cst = handle.doc->parseResult.cstNodeMap.find(handle.node))
        pushCstNode(L, handle.doc, *cst);
    else
        lua_pushnil(L);
    return 1;
}

static int astNodeText(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    auto [startOff, endOff] = locationToOffsets(handle.doc->lineOffsets, handle.doc->source.size(), handle.node->location);
    lua_pushlstring(L, handle.doc->source.data() + startOff, endOff - startOff);
    return 1;
}

static int astNodeSetLocation(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    handle.node->location = checkAstLocation(L, 2).location;
    lua_pushvalue(L, 1);
    return 1;
}

static int astNodeHasSemicolon(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (auto* stat = handle.node->asStat())
        lua_pushboolean(L, stat->hasSemicolon);
    else
        lua_pushboolean(L, false);
    return 1;
}

static int astNodeSetHasSemicolon(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (auto* stat = handle.node->asStat())
    {
        stat->hasSemicolon = lua_toboolean(L, 2);
        lua_pushvalue(L, 1);
        return 1;
    }
    luaL_error(L, "setHasSemicolon is only valid on AstStat nodes");
}

static int astNodeWalk(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    AstFilterData filter = extractAstFilter(L, 3);

    CallbackVisitor visitor(L, handle.doc, 2, filter);
    handle.node->visit(&visitor);

    if (visitor.errorOccurred)
        lua_error(L);

    return 0;
}

static int dispatchAstNodeMethod(lua_State* L, AstNodeData& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Children:        return astNodeChildren(L);
    case ReflectAtom::Walk:            return astNodeWalk(L);
    case ReflectAtom::Location:        return astNodeLocation(L);
    case ReflectAtom::SetLocation:     return astNodeSetLocation(L);
    case ReflectAtom::Cst:             return astNodeCst(L);
    case ReflectAtom::Text:            return astNodeText(L);
    case ReflectAtom::HasSemicolon:    return astNodeHasSemicolon(L);
    case ReflectAtom::SetHasSemicolon: return astNodeSetHasSemicolon(L);
    default: break;
    }

    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].methodHandler)
    {
        if (s_nodeClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }
    luaL_error(L, "%.*s is not a valid method of AstNode", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astNodeMethodTrampoline, checkAstNode, dispatchAstNodeMethod)
LUAU_REFLECT_NAMECALL(astNodeNamecall, checkAstNode, dispatchAstNodeMethod)
LUAU_REFLECT_INDEX(astNodeIndex, checkAstNode, getNodeKind, getAstNodeCategory, TagNode, astNodeMethodTrampoline)

static int astNodeToString(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    lua_pushfstring(L, "AstNode(%s)", getNodeKind(handle.node));
    return 1;
}

static int astNodeEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagNode || lua_userdatatag(L, 2) != TagNode)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstNode(L, 1);
    auto& b = checkAstNode(L, 2);
    lua_pushboolean(L, a.node == b.node && a.doc == b.doc);
    return 1;
}

void registerAstNode(lua_State* L)
{
    initializeDispatchTables();
    static const luaL_Reg s_nodeMethods[] = {
        {"children", astNodeChildren},
        {"walk", astNodeWalk},
        {"location", astNodeLocation},
        {"cst", astNodeCst},
        {"text", astNodeText},
        {"hasSemicolon", astNodeHasSemicolon},
        {nullptr, nullptr},
    };
    registerUserdataType(L, TagNode, "AstNode", astNodeDtor, astNodeIndex, astNodeToString, astNodeEq, s_nodeMethods, astNodeNamecall);
}

} // namespace Luau
