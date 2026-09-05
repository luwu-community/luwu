// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/PrettyPrinter.h"
#include "Luau/ReflectCommon.h"
#include "Luau/ReflectAstHandler.h"

namespace Luau
{

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushAstNode, checkAstNode, astNodeDtor, AstNodeData, Luau::AstNode*, TagNode, "AstNode")

typedef bool (*NodeMethodHandler)(lua_State* L, AstNodeData& handle, ReflectAtom atom);
typedef void (*NodePropCollector)(lua_State* L, AstNodeData& handle);
typedef Luau::AstNode* (*NodeFactoryFn)(Luau::Allocator& alloc);

struct AstNodeClassInfo
{
    const char* kind = nullptr;
    const char* category = nullptr;
    // derived field used for filtering
    NodeCategory categoryEnum = NodeCategory::Unknown;
    NodeMethodHandler methodHandler = nullptr;
    NodePropCollector propCollector = nullptr;
    NodeFactoryFn factory = nullptr;
    bool canHoldComments = false;
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

bool canNodeHoldComments(Luau::AstNode* node)
{
    if (!node)
        return false;
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()))
        return s_nodeClassTable[idx].canHoldComments;
    return false;
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
    NodeMethodHandler methodHandler = nullptr,
    NodePropCollector propCollector = nullptr,
    NodeFactoryFn factory = nullptr
)
{
    int idx = T::ClassIndex();
    if (size_t(idx) >= s_nodeClassTable.size())
        s_nodeClassTable.resize(idx + 1, AstNodeClassInfo{"AstNode", "unknown", NodeCategory::Unknown, nullptr, nullptr, nullptr, false});
    bool canHoldComments = (category == NodeCategory::Stat);
    s_nodeClassTable[idx] = AstNodeClassInfo{kind, categoryToString(category), category, methodHandler, propCollector, factory, canHoldComments};
}

Luau::AstNode* createDefaultAstNode(std::string_view kind, Luau::Allocator& alloc)
{
    int idx = getNodeClassIndexByKind(kind);
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].factory)
        return s_nodeClassTable[idx].factory(alloc);
    return nullptr;
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

#define LUAU_AST_NODE_BASE \
    LUAU_AST_FIELD_RO(OrigLocation, location) \
    LUAU_AST_FIELD_FN_RO(Cst, getNodeCst(handle, n))

#define LUAU_AST_STAT_BASE \
    LUAU_AST_NODE_BASE \
    LUAU_AST_FIELD_RW(HasSemicolon, SetHasSemicolon, hasSemicolon)

#define LUAU_REFLECT_AST_NODES(NODE) \
    /* Statements */ \
    NODE(AstStatBlock, "AstStatBlock", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstStat*>{nullptr, 0}, false) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(HasEnd, SetHasEnd, hasEnd) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatIf, "AstStatIf", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr, nullptr, std::nullopt, std::nullopt) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(ThenBody, SetThenBody, thenbody) \
        LUAU_AST_FIELD_RW(ElseBody, SetElseBody, elsebody)) \
    NODE(AstStatWhile, "AstStatWhile", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr, false, Luau::Location()) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo) \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatRepeat, "AstStatRepeat", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr, false) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatBreak, "AstStatBreak", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location()) \
        LUAU_AST_STAT_BASE) \
    NODE(AstStatContinue, "AstStatContinue", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location()) \
        LUAU_AST_STAT_BASE) \
    NODE(AstStatReturn, "AstStatReturn", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstExpr*>{nullptr, 0}) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(List, SetList, list)) \
    NODE(AstStatExpr, "AstStatExpr", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstStatLocal, "AstStatLocal", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstLocal*>{nullptr, 0}, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, std::nullopt, false) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(IsConst, SetIsConst, isConst) \
        LUAU_AST_FIELD_RW(Exported, SetExported, isExported) \
        LUAU_AST_FIELD_RW(Vars, SetVars, vars) \
        LUAU_AST_FIELD_RW(Values, SetValues, values)) \
    NODE(AstStatFor, "AstStatFor", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr, nullptr, nullptr, nullptr, false, Luau::Location()) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo) \
        LUAU_AST_FIELD_RW(Var, SetVar, var) \
        LUAU_AST_FIELD_RW(From, SetFrom, from) \
        LUAU_AST_FIELD_RW(To, SetTo, to) \
        LUAU_AST_FIELD_RW(Step, SetStep, step) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatForIn, "AstStatForIn", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstLocal*>{nullptr, 0}, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, nullptr, false, Luau::Location(), false, Luau::Location()) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(HasIn, SetHasIn, hasIn) \
        LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo) \
        LUAU_AST_FIELD_RW(Vars, SetVars, vars) \
        LUAU_AST_FIELD_RW(Values, SetValues, values) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatAssign, "AstStatAssign", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Vars, SetVars, vars) \
        LUAU_AST_FIELD_RW(Values, SetValues, values)) \
    NODE(AstStatCompoundAssign, "AstStatCompoundAssign", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstExprBinary::Op::Add, nullptr, nullptr) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Var, SetVar, var) \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstStatFunction, "AstStatFunction", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Func, SetFunc, func)) \
    NODE(AstStatLocalFunction, "AstStatLocalFunction", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr, false, Luau::Position::missing()) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(IsConst, SetIsConst, isConst) \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Func, SetFunc, func)) \
    NODE(AstStatTypeAlias, "AstStatTypeAlias", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName(), Luau::Location(), Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}, Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}, nullptr, false) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Exported, SetExported, exported) \
        LUAU_AST_FIELD_RW(Type, SetType, type) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)) \
    NODE(AstStatTypeFunction, "AstStatTypeFunction", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName(), Luau::Location(), nullptr, false, false) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Exported, SetExported, exported) \
        LUAU_AST_FIELD_RW(HasErrors, SetHasErrors, hasErrors) \
        LUAU_AST_FIELD_RW(Body, SetBody, body)) \
    NODE(AstStatDeclareGlobal, "AstStatDeclareGlobal", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName(), Luau::Location(), nullptr) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Type, SetType, type)) \
    NODE(AstStatDeclareFunction, "AstStatDeclareFunction", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstAttr*>{nullptr, 0}, Luau::AstName(), Luau::Location(), Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}, Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}, Luau::AstTypeList{}, Luau::AstArray<Luau::AstArgumentName>{nullptr, 0}, false, Luau::Location(), nullptr) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Vararg, SetVararg, vararg) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks) \
        LUAU_AST_FIELD_RW(Params, SetParams, params) \
        LUAU_AST_FIELD_RW(ReturnTypes, SetReturnTypes, retTypes) \
        LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)) \
    NODE(AstStatClass, "AstStatClass", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, Luau::AstArray<Luau::AstClassMember>{nullptr, 0}, false) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Exported, SetExported, exported) \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Members, SetMembers, members)) \
    NODE(AstStatDeclareExternType, "AstStatDeclareExternType", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName(), std::nullopt, Luau::AstArray<Luau::AstDeclaredExternTypeProperty>{nullptr, 0}, nullptr) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(SuperName, SetSuperName, superName) \
        LUAU_AST_FIELD_RW(Props, SetProps, props) \
        LUAU_AST_FIELD_RW(Indexer, SetIndexer, indexer) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)) \
    NODE(AstStatError, "AstStatError", Stat, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, Luau::AstArray<Luau::AstStat*>{nullptr, 0}, 0) \
        LUAU_AST_STAT_BASE \
        LUAU_AST_FIELD_RO(MessageIndex, messageIndex) \
        LUAU_AST_FIELD_RO(Expressions, expressions) \
        LUAU_AST_FIELD_RO(Statements, statements)) \
    \
    /* Expressions */ \
    NODE(AstExprGroup, "AstExprGroup", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstExprConstantNil, "AstExprConstantNil", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location()) \
        LUAU_AST_NODE_BASE) \
    NODE(AstExprConstantBool, "AstExprConstantBool", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), false) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstExprConstantNumber, "AstExprConstantNumber", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), 0.0) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstExprConstantInteger, "AstExprConstantInteger", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), int64_t(0)) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstExprConstantString, "AstExprConstantString", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<char>{nullptr, 0}, Luau::AstExprConstantString::QuoteStyle::QuotedSimple) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Value, SetValue, value) \
        LUAU_AST_FIELD_RW(QuoteStyle, SetQuoteStyle, quoteStyle)) \
    NODE(AstExprLocal, "AstExprLocal", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, false) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Upvalue, SetUpvalue, upvalue) \
        LUAU_AST_FIELD_RW(Local, SetLocal, local)) \
    NODE(AstExprGlobal, "AstExprGlobal", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName()) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name)) \
    NODE(AstExprVarargs, "AstExprVarargs", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location()) \
        LUAU_AST_NODE_BASE) \
    NODE(AstExprCall, "AstExprCall", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, false, Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}, Luau::Location()) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Self, SetSelf, self) \
        LUAU_AST_FIELD_RW(Func, SetFunc, func) \
        LUAU_AST_FIELD_RW(Args, SetArgs, args) \
        LUAU_AST_FIELD_RW(TypeArguments, SetTypeArguments, typeArguments)) \
    NODE(AstExprIndexName, "AstExprIndexName", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, Luau::AstName(), Luau::Location(), Luau::Position::missing(), '.') \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Index, SetIndex, index) \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstExprIndexExpr, "AstExprIndexExpr", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr) \
        LUAU_AST_FIELD_RW(Index, SetIndex, index)) \
    NODE(AstExprFunction, "AstExprFunction", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstAttr*>{nullptr, 0}, Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}, Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}, nullptr, Luau::AstArray<Luau::AstLocal*>{nullptr, 0}, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, false, Luau::Location(), nullptr, 0, Luau::AstName(), nullptr, nullptr, std::nullopt) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Vararg, SetVararg, vararg) \
        LUAU_AST_FIELD_RW(DebugName, SetDebugName, debugname) \
        LUAU_AST_FIELD_RW(Args, SetArgs, args) \
        LUAU_AST_FIELD_RW(Body, SetBody, body) \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks) \
        LUAU_AST_FIELD_RW(ReturnAnnotation, SetReturnAnnotation, returnAnnotation) \
        LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)) \
    NODE(AstExprTable, "AstExprTable", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstExprTable::Item>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Items, SetItems, items)) \
    NODE(AstExprUnary, "AstExprUnary", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstExprUnary::Op::Not, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstExprBinary, "AstExprBinary", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstExprBinary::Op::Add, nullptr, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Op, SetOp, op) \
        LUAU_AST_FIELD_RW(Left, SetLeft, left) \
        LUAU_AST_FIELD_RW(Right, SetRight, right)) \
    NODE(AstExprTypeAssertion, "AstExprTypeAssertion", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr) \
        LUAU_AST_FIELD_RW(Annotation, SetAnnotation, annotation)) \
    NODE(AstExprIfElse, "AstExprIfElse", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, true, nullptr, true, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(HasElse, SetHasElse, hasElse) \
        LUAU_AST_FIELD_RW(Condition, SetCondition, condition) \
        LUAU_AST_FIELD_RW(TrueExpr, SetTrueExpr, trueExpr) \
        LUAU_AST_FIELD_RW(FalseExpr, SetFalseExpr, falseExpr)) \
    NODE(AstExprInterpString, "AstExprInterpString", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstArray<char>>{nullptr, 0}, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Strings, SetStrings, strings) \
        LUAU_AST_FIELD_RW(Expressions, SetExpressions, expressions)) \
    NODE(AstExprInstantiate, "AstExprInstantiate", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr, Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr) \
        LUAU_AST_FIELD_RW(TypeArguments, SetTypeArguments, typeArguments)) \
    NODE(AstExprError, "AstExprError", Expr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, 0) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RO(MessageIndex, messageIndex) \
        LUAU_AST_FIELD_RO(Expressions, expressions)) \
    \
    /* Types */ \
    NODE(AstTypeReference, "AstTypeReference", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), std::nullopt, Luau::AstName(), std::nullopt, Luau::Location(), false, Luau::AstArray<Luau::AstTypeOrPack>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Prefix, SetPrefix, prefix) \
        LUAU_AST_FIELD_RW(HasParameterList, SetHasParameterList, hasParameterList) \
        LUAU_AST_FIELD_RW(Parameters, SetParameters, parameters)) \
    NODE(AstTypeTable, "AstTypeTable", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstTableProp>{nullptr, 0}, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Props, SetProps, props) \
        LUAU_AST_FIELD_RW(Indexer, SetIndexer, indexer)) \
    NODE(AstTypeFunction, "AstTypeFunction", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstAttr*>{nullptr, 0}, Luau::AstArray<Luau::AstGenericType*>{nullptr, 0}, Luau::AstArray<Luau::AstGenericTypePack*>{nullptr, 0}, Luau::AstTypeList{}, Luau::AstArray<std::optional<Luau::AstArgumentName>>{nullptr, 0}, nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Generics, SetGenerics, generics) \
        LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks) \
        LUAU_AST_FIELD_RW(ArgTypes, SetArgTypes, argTypes) \
        LUAU_AST_FIELD_RW(ReturnTypes, SetReturnTypes, returnTypes) \
        LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)) \
    NODE(AstTypeTypeof, "AstTypeTypeof", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Expr, SetExpr, expr)) \
    NODE(AstTypeOptional, "AstTypeOptional", Type, \
        LUAU_NODE_DEFAULT(Luau::Location()) \
        LUAU_AST_NODE_BASE) \
    NODE(AstTypeUnion, "AstTypeUnion", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstType*>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Types, SetTypes, types)) \
    NODE(AstTypeIntersection, "AstTypeIntersection", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstType*>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Types, SetTypes, types)) \
    NODE(AstTypeSingletonBool, "AstTypeSingletonBool", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), false) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstTypeSingletonString, "AstTypeSingletonString", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<char>{nullptr, 0}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Value, SetValue, value)) \
    NODE(AstTypeGroup, "AstTypeGroup", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Type, SetType, type)) \
    NODE(AstTypeError, "AstTypeError", Type, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstArray<Luau::AstType*>{nullptr, 0}, false, 0) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RO(IsMissing, isMissing) \
        LUAU_AST_FIELD_RO(MessageIndex, messageIndex) \
        LUAU_AST_FIELD_RO(Types, types)) \
    \
    /* Type Packs */ \
    NODE(AstTypePackExplicit, "AstTypePackExplicit", TypePack, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstTypeList{}) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(TypeList, SetTypeList, typeList)) \
    NODE(AstTypePackVariadic, "AstTypePackVariadic", TypePack, \
        LUAU_NODE_DEFAULT(Luau::Location(), nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(VariadicType, SetVariadicType, variadicType)) \
    NODE(AstTypePackGeneric, "AstTypePackGeneric", TypePack, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName()) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, genericName)) \
    \
    /* Generics & Attributes */ \
    NODE(AstGenericType, "AstGenericType", Generic, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName(), nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Type, SetType, defaultValue)) \
    NODE(AstGenericTypePack, "AstGenericTypePack", Generic, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstName(), nullptr) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Type, SetType, defaultValue)) \
    NODE(AstAttr, "AstAttr", Attr, \
        LUAU_NODE_DEFAULT(Luau::Location(), Luau::AstAttr::Type::Checked, Luau::AstArray<Luau::AstExpr*>{nullptr, 0}, Luau::AstName()) \
        LUAU_AST_NODE_BASE \
        LUAU_AST_FIELD_RW(Type, SetType, type) \
        LUAU_AST_FIELD_RW(Name, SetName, name) \
        LUAU_AST_FIELD_RW(Args, SetArgs, args))

/* default ctor */
#define LUAU_NODE_DEFAULT(...) return alloc.alloc<CurrentAstNode>(__VA_ARGS__);
#define LUAU_AST_FIELD_RW(atomGet, atomSet, memberExpr)
#define LUAU_AST_FIELD_RO(atomGet, memberExpr)
#define LUAU_AST_FIELD_FN_RO(atomGet, expr)

#define LUAU_GENERATE_AST_FACTORY(Class, KindStr, Cat, Fields) \
    static ::Luau::AstNode* createDefault##Class(::Luau::Allocator& alloc) \
    { \
        using CurrentAstNode = ::Luau::Class; \
        Fields \
        return nullptr; \
    }

LUAU_REFLECT_AST_NODES(LUAU_GENERATE_AST_FACTORY)

#undef LUAU_GENERATE_AST_FACTORY
#undef LUAU_NODE_DEFAULT
#undef LUAU_AST_FIELD_RW
#undef LUAU_AST_FIELD_RO
#undef LUAU_AST_FIELD_FN_RO

/* methods */
#define LUAU_NODE_DEFAULT(...)

#define LUAU_AST_FIELD_RW(atomGet, atomSet, memberExpr) \
    case ReflectAtom::atomGet: \
        pushReflectValue(L, handle.doc, n->memberExpr); \
        return true; \
    case ReflectAtom::atomSet: \
        readReflectValue(L, handle.doc, 2, n->memberExpr); \
        lua_pushvalue(L, 1); \
        return true;

#define LUAU_AST_FIELD_RO(atomGet, memberExpr) \
    case ReflectAtom::atomGet: \
        pushReflectValue(L, handle.doc, n->memberExpr); \
        return true;

#define LUAU_AST_FIELD_FN_RO(atomGet, expr) \
    case ReflectAtom::atomGet: \
        pushReflectValue(L, handle.doc, expr); \
        return true;

#define LUAU_GENERATE_AST_HANDLER(Class, KindStr, Cat, Fields) \
    static bool handle##Class##Methods(lua_State* L, AstNodeData& handle, ReflectAtom atom) \
    { \
        auto* n = static_cast<::Luau::Class*>(handle.node); \
        switch (atom) \
        { \
        Fields \
        default: return false; \
        } \
    }

LUAU_REFLECT_AST_NODES(LUAU_GENERATE_AST_HANDLER)

#undef LUAU_GENERATE_AST_HANDLER
#undef LUAU_NODE_DEFAULT
#undef LUAU_AST_FIELD_RW
#undef LUAU_AST_FIELD_RO
#undef LUAU_AST_FIELD_FN_RO

/* properties */
#define LUAU_NODE_DEFAULT(...)

#define LUAU_AST_FIELD_RW(atomGet, atomSet, memberExpr) \
    pushReflectValue(L, handle.doc, n->memberExpr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_AST_FIELD_RO(atomGet, memberExpr) \
    pushReflectValue(L, handle.doc, n->memberExpr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_AST_FIELD_FN_RO(atomGet, expr) \
    pushReflectValue(L, handle.doc, expr); \
    lua_setfield(L, -2, getAtomString(ReflectAtom::atomGet));

#define LUAU_GENERATE_AST_PROP_COLLECTOR(Class, KindStr, Cat, Fields) \
    static void collect##Class##Props(lua_State* L, AstNodeData& handle) \
    { \
        auto* n = static_cast<::Luau::Class*>(handle.node); \
        (void)n; \
        Fields \
    }

LUAU_REFLECT_AST_NODES(LUAU_GENERATE_AST_PROP_COLLECTOR)

#undef LUAU_GENERATE_AST_PROP_COLLECTOR
#undef LUAU_NODE_DEFAULT
#undef LUAU_AST_FIELD_RW
#undef LUAU_AST_FIELD_RO
#undef LUAU_AST_FIELD_FN_RO

/* reg table */
static void initializeDispatchTables()
{
    static const bool initialized = []() {
#define LUAU_REGISTER_AST_NODE(Class, KindStr, Cat, Fields) \
        registerNodeClass<::Luau::Class>(KindStr, NodeCategory::Cat, handle##Class##Methods, collect##Class##Props, createDefault##Class);

        LUAU_REFLECT_AST_NODES(LUAU_REGISTER_AST_NODE)

#undef LUAU_REGISTER_AST_NODE
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

struct CallbackVisitor : public Luau::AstVisitor
{
    lua_State* L;
    std::shared_ptr<AstDocumentState> doc;
    int callbackIndex;
    AstFilterData filter;
    bool hasFilter = false;
    bool errorOccurred = false;

    CallbackVisitor(lua_State* L, std::shared_ptr<AstDocumentState> doc, int callbackIndex, const AstFilterData& filter = {})
        : L(L)
        , doc(doc)
        , callbackIndex(callbackIndex)
        , filter(filter)
        , hasFilter(!filter.empty())
    {
    }

    bool visit(Luau::AstNode* node) override
    {
        if (errorOccurred || !node)
            return false;

        if (hasFilter && !filter.matches(node))
            return true;

        lua_pushvalue(L, callbackIndex);
        pushAstNode(L, doc, node);

        int status = lua_pcall(L, 1, 1, 0);
        if (status != 0)
        {
            errorOccurred = true;
            return false;
        }

        if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }

        lua_pop(L, 1);
        return true;
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

static int astNodeProperties(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.node)
    {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, 0, 10);

    lua_pushlightuserdatatagged(L, (void*)handle.node, TagId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, getNodeKind(handle.node));
    lua_setfield(L, -2, "kind");

    lua_pushstring(L, getAstNodeCategory(handle.node));
    lua_setfield(L, -2, "category");

    int idx = handle.node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].propCollector)
    {
        s_nodeClassTable[idx].propCollector(L, handle);
    }

    if (handle.doc && handle.node)
    {
        if (const auto* comments = handle.doc->nodeComments.find(handle.node))
        {
            pushArray(L, comments->size(), [&](size_t i) {
                pushAstAux(L, handle.doc, (*comments)[i]);
            });
        }
        else
        {
            lua_newtable(L);
        }
    }
    else
    {
        lua_newtable(L);
    }
    lua_setfield(L, -2, "comments");

    return 1;
}

static int astNodePrettyprint(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.node)
    {
        lua_pushliteral(L, "");
        return 1;
    }
    std::string s = Luau::toString(handle.node);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

struct CommentAttacher : public Luau::AstVisitor
{
    AstDocumentState& doc;
    const std::vector<Luau::Comment>& comments;
    size_t cursor = 0;

    CommentAttacher(AstDocumentState& doc)
        : doc(doc)
        , comments(doc.parseResult.commentLocations)
    {
    }

    bool visit(Luau::AstNode* node) override
    {
        if (!canNodeHoldComments(node))
            return true;

        if (auto block = node->as<AstStatBlock>())
        {
            if (block->body.size > 0)
                return true;
        }

        while (cursor < comments.size() && comments[cursor].location.end <= node->location.begin)
        {
            doc.nodeComments[node].push_back(comments[cursor++]);
        }

        if (cursor < comments.size() && comments[cursor].location.begin.line == node->location.end.line)
        {
            doc.nodeComments[node].push_back(comments[cursor++]);
        }

        return true;
    }
};

void attachCommentsToAst(AstDocumentState& doc, Luau::AstNode* rootNode)
{
    const auto& comments = doc.parseResult.commentLocations;
    Luau::AstNode* root = rootNode ? rootNode : static_cast<Luau::AstNode*>(doc.parseResult.root);
    if (!root || comments.empty())
        return;

    CommentAttacher attacher(doc);
    root->visit(&attacher);

    while (attacher.cursor < comments.size())
    {
        doc.nodeComments[root].push_back(comments[attacher.cursor++]);
    }
}

static int astNodeComments(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    if (!handle.doc || !handle.node)
    {
        lua_newtable(L);
        return 1;
    }
    const auto* comments = handle.doc->nodeComments.find(handle.node);
    if (!comments)
    {
        lua_newtable(L);
        return 1;
    }
    pushArray(L, comments->size(), [&](size_t i) {
        pushAstAux(L, handle.doc, (*comments)[i]);
    });
    return 1;
}

static int dispatchAstNodeMethod(lua_State* L, AstNodeData& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Children:    return astNodeChildren(L);
    case ReflectAtom::Walk:        return astNodeWalk(L);
    case ReflectAtom::Properties:  return astNodeProperties(L);
    case ReflectAtom::Prettyprint: return astNodePrettyprint(L);
    case ReflectAtom::Comments:    return astNodeComments(L);
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

LUAU_REFLECT_DEFINE_EQ(astNodeEq, TagNode, checkAstNode, a.node == b.node)

void registerAstNode(lua_State* L)
{
    initializeDispatchTables();
    registerUserdataType(L, TagNode, "AstNode", astNodeDtor, astNodeIndex, astNodeToString, astNodeEq, astNodeNamecall);
}

} // namespace Luau
