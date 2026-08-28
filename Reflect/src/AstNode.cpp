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

LUAU_AST_HANDLER_START(handleStatBlockMethods, AstStatBlock)
    LUAU_AST_FIELD_RW(HasEnd, SetHasEnd, hasEnd)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatIfMethods, AstStatIf)
    LUAU_AST_FIELD_RW(Condition, SetCondition, condition)
    LUAU_AST_FIELD_RW(ThenBody, SetThenBody, thenbody)
    LUAU_AST_FIELD_RW(ElseBody, SetElseBody, elsebody)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatWhileMethods, AstStatWhile)
    LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo)
    LUAU_AST_FIELD_RW(Condition, SetCondition, condition)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatRepeatMethods, AstStatRepeat)
    LUAU_AST_FIELD_RW(Condition, SetCondition, condition)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatReturnMethods, AstStatReturn)
    LUAU_AST_FIELD_RW(List, SetList, list)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatExprMethods, AstStatExpr)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatLocalMethods, AstStatLocal)
    LUAU_AST_FIELD_RW(IsConst, SetIsConst, isConst)
    LUAU_AST_FIELD_RW(Exported, SetExported, isExported)
    LUAU_AST_FIELD_RW(Vars, SetVars, vars)
    LUAU_AST_FIELD_RW(Values, SetValues, values)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatForMethods, AstStatFor)
    LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo)
    LUAU_AST_FIELD_RW(Var, SetVar, var)
    LUAU_AST_FIELD_RW(From, SetFrom, from)
    LUAU_AST_FIELD_RW(To, SetTo, to)
    LUAU_AST_FIELD_RW(Step, SetStep, step)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatForInMethods, AstStatForIn)
    LUAU_AST_FIELD_RW(HasIn, SetHasIn, hasIn)
    LUAU_AST_FIELD_RW(HasDo, SetHasDo, hasDo)
    LUAU_AST_FIELD_RW(Vars, SetVars, vars)
    LUAU_AST_FIELD_RW(Values, SetValues, values)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatAssignMethods, AstStatAssign)
    LUAU_AST_FIELD_RW(Vars, SetVars, vars)
    LUAU_AST_FIELD_RW(Values, SetValues, values)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatCompoundAssignMethods, AstStatCompoundAssign)
    LUAU_AST_FIELD_RW(Op, SetOp, op)
    LUAU_AST_FIELD_RW(Var, SetVar, var)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatFunctionMethods, AstStatFunction)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Func, SetFunc, func)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatLocalFunctionMethods, AstStatLocalFunction)
    LUAU_AST_FIELD_RW(IsConst, SetIsConst, isConst)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Func, SetFunc, func)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatTypeAliasMethods, AstStatTypeAlias)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Exported, SetExported, exported)
    LUAU_AST_FIELD_RW(Type, SetType, type)
    LUAU_AST_FIELD_RW(Generics, SetGenerics, generics)
    LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatTypeFunctionMethods, AstStatTypeFunction)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Exported, SetExported, exported)
    LUAU_AST_FIELD_RW(HasErrors, SetHasErrors, hasErrors)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareGlobalMethods, AstStatDeclareGlobal)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Type, SetType, type)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareFunctionMethods, AstStatDeclareFunction)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Vararg, SetVararg, vararg)
    LUAU_AST_FIELD_RW(Generics, SetGenerics, generics)
    LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)
    LUAU_AST_FIELD_RW(Params, SetParams, params)
    LUAU_AST_FIELD_RW(ReturnTypes, SetReturnTypes, retTypes)
    LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatClassMethods, AstStatClass)
    LUAU_AST_FIELD_RW(Exported, SetExported, exported)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Members, SetMembers, members)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareExternTypeMethods, AstStatDeclareExternType)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(SuperName, SetSuperName, superName)
    LUAU_AST_FIELD_RW(Props, SetProps, props)
    LUAU_AST_FIELD_RW(Indexer, SetIndexer, indexer)
    LUAU_AST_FIELD_RW(Generics, SetGenerics, generics)
    LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatErrorMethods, AstStatError)
    LUAU_AST_FIELD_RO(MessageIndex, messageIndex)
    LUAU_AST_FIELD_RO(Expressions, expressions)
    LUAU_AST_FIELD_RO(Statements, statements)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprGroupMethods, AstExprGroup)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantBoolMethods, AstExprConstantBool)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantNumberMethods, AstExprConstantNumber)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantIntegerMethods, AstExprConstantInteger)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantStringMethods, AstExprConstantString)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
    LUAU_AST_FIELD_RW(QuoteStyle, SetQuoteStyle, quoteStyle)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprLocalMethods, AstExprLocal)
    LUAU_AST_FIELD_RW(Upvalue, SetUpvalue, upvalue)
    LUAU_AST_FIELD_RW(Local, SetLocal, local)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprGlobalMethods, AstExprGlobal)
    LUAU_AST_FIELD_RW(Name, SetName, name)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprCallMethods, AstExprCall)
    LUAU_AST_FIELD_RW(Self, SetSelf, self)
    LUAU_AST_FIELD_RW(Func, SetFunc, func)
    LUAU_AST_FIELD_RW(Args, SetArgs, args)
    LUAU_AST_FIELD_RW(TypeArguments, SetTypeArguments, typeArguments)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIndexNameMethods, AstExprIndexName)
    LUAU_AST_FIELD_RW(Index, SetIndex, index)
    LUAU_AST_FIELD_RW(Op, SetOp, op)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIndexExprMethods, AstExprIndexExpr)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
    LUAU_AST_FIELD_RW(Index, SetIndex, index)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprFunctionMethods, AstExprFunction)
    LUAU_AST_FIELD_RW(Vararg, SetVararg, vararg)
    LUAU_AST_FIELD_RW(DebugName, SetDebugName, debugname)
    LUAU_AST_FIELD_RW(Args, SetArgs, args)
    LUAU_AST_FIELD_RW(Body, SetBody, body)
    LUAU_AST_FIELD_RW(Generics, SetGenerics, generics)
    LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)
    LUAU_AST_FIELD_RW(ReturnAnnotation, SetReturnAnnotation, returnAnnotation)
    LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprTableMethods, AstExprTable)
    LUAU_AST_FIELD_RW(Items, SetItems, items)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprUnaryMethods, AstExprUnary)
    LUAU_AST_FIELD_RW(Op, SetOp, op)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprBinaryMethods, AstExprBinary)
    LUAU_AST_FIELD_RW(Op, SetOp, op)
    LUAU_AST_FIELD_RW(Left, SetLeft, left)
    LUAU_AST_FIELD_RW(Right, SetRight, right)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprTypeAssertionMethods, AstExprTypeAssertion)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
    LUAU_AST_FIELD_RW(Annotation, SetAnnotation, annotation)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIfElseMethods, AstExprIfElse)
    LUAU_AST_FIELD_RW(HasElse, SetHasElse, hasElse)
    LUAU_AST_FIELD_RW(Condition, SetCondition, condition)
    LUAU_AST_FIELD_RW(TrueExpr, SetTrueExpr, trueExpr)
    LUAU_AST_FIELD_RW(FalseExpr, SetFalseExpr, falseExpr)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprInterpStringMethods, AstExprInterpString)
    LUAU_AST_FIELD_RW(Strings, SetStrings, strings)
    LUAU_AST_FIELD_RW(Expressions, SetExpressions, expressions)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprInstantiateMethods, AstExprInstantiate)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
    LUAU_AST_FIELD_RW(TypeArguments, SetTypeArguments, typeArguments)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprErrorMethods, AstExprError)
    LUAU_AST_FIELD_RO(MessageIndex, messageIndex)
    LUAU_AST_FIELD_RO(Expressions, expressions)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeReferenceMethods, AstTypeReference)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Prefix, SetPrefix, prefix)
    LUAU_AST_FIELD_RW(HasParameterList, SetHasParameterList, hasParameterList)
    LUAU_AST_FIELD_RW(Parameters, SetParameters, parameters)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeTableMethods, AstTypeTable)
    LUAU_AST_FIELD_RW(Props, SetProps, props)
    LUAU_AST_FIELD_RW(Indexer, SetIndexer, indexer)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeFunctionMethods, AstTypeFunction)
    LUAU_AST_FIELD_RW(Generics, SetGenerics, generics)
    LUAU_AST_FIELD_RW(GenericPacks, SetGenericPacks, genericPacks)
    LUAU_AST_FIELD_RW(ArgTypes, SetArgTypes, argTypes)
    LUAU_AST_FIELD_RW(ReturnTypes, SetReturnTypes, returnTypes)
    LUAU_AST_FIELD_RW(Attributes, SetAttributes, attributes)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeTypeofMethods, AstTypeTypeof)
    LUAU_AST_FIELD_RW(Expr, SetExpr, expr)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeUnionMethods, AstTypeUnion)
    LUAU_AST_FIELD_RW(Types, SetTypes, types)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeIntersectionMethods, AstTypeIntersection)
    LUAU_AST_FIELD_RW(Types, SetTypes, types)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeSingletonBoolMethods, AstTypeSingletonBool)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeSingletonStringMethods, AstTypeSingletonString)
    LUAU_AST_FIELD_RW(Value, SetValue, value)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeGroupMethods, AstTypeGroup)
    LUAU_AST_FIELD_RW(Type, SetType, type)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeErrorMethods, AstTypeError)
    LUAU_AST_FIELD_RO(IsMissing, isMissing)
    LUAU_AST_FIELD_RO(MessageIndex, messageIndex)
    LUAU_AST_FIELD_RO(Types, types)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypePackExplicitMethods, AstTypePackExplicit)
    LUAU_AST_FIELD_RW(TypeList, SetTypeList, typeList)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypePackVariadicMethods, AstTypePackVariadic)
    LUAU_AST_FIELD_RW(VariadicType, SetVariadicType, variadicType)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypePackGenericMethods, AstTypePackGeneric)
    LUAU_AST_FIELD_RW(Name, SetName, genericName)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleGenericTypeMethods, AstGenericType)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Type, SetType, defaultValue)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleGenericTypePackMethods, AstGenericTypePack)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Type, SetType, defaultValue)
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleAttrMethods, AstAttr)
    LUAU_AST_FIELD_RW(Type, SetType, type)
    LUAU_AST_FIELD_RW(Name, SetName, name)
    LUAU_AST_FIELD_RW(Args, SetArgs, args)
LUAU_AST_HANDLER_END()

static void initializeDispatchTables()
{
    // SAFETY: c++ guarantees thread safety in static inits like this (see https://iamroman.org/blog/2017/04/cpp11-static-init/) from c++11
    static const bool initialized = []() {
        // Statements
        registerNodeClass<Luau::AstStatBlock>("AstStatBlock", NodeCategory::Stat, handleStatBlockMethods);
        registerNodeClass<Luau::AstStatIf>("AstStatIf", NodeCategory::Stat, handleStatIfMethods);
        registerNodeClass<Luau::AstStatWhile>("AstStatWhile", NodeCategory::Stat, handleStatWhileMethods);
        registerNodeClass<Luau::AstStatRepeat>("AstStatRepeat", NodeCategory::Stat, handleStatRepeatMethods);
        registerNodeClass<Luau::AstStatBreak>("AstStatBreak", NodeCategory::Stat);
        registerNodeClass<Luau::AstStatContinue>("AstStatContinue", NodeCategory::Stat);
        registerNodeClass<Luau::AstStatReturn>("AstStatReturn", NodeCategory::Stat, handleStatReturnMethods);
        registerNodeClass<Luau::AstStatExpr>("AstStatExpr", NodeCategory::Stat, handleStatExprMethods);
        registerNodeClass<Luau::AstStatLocal>("AstStatLocal", NodeCategory::Stat, handleStatLocalMethods);
        registerNodeClass<Luau::AstStatFor>("AstStatFor", NodeCategory::Stat, handleStatForMethods);
        registerNodeClass<Luau::AstStatForIn>("AstStatForIn", NodeCategory::Stat, handleStatForInMethods);
        registerNodeClass<Luau::AstStatAssign>("AstStatAssign", NodeCategory::Stat, handleStatAssignMethods);
        registerNodeClass<Luau::AstStatCompoundAssign>("AstStatCompoundAssign", NodeCategory::Stat, handleStatCompoundAssignMethods);
        registerNodeClass<Luau::AstStatFunction>("AstStatFunction", NodeCategory::Stat, handleStatFunctionMethods);
        registerNodeClass<Luau::AstStatLocalFunction>("AstStatLocalFunction", NodeCategory::Stat, handleStatLocalFunctionMethods);
        registerNodeClass<Luau::AstStatTypeAlias>("AstStatTypeAlias", NodeCategory::Stat, handleStatTypeAliasMethods);
        registerNodeClass<Luau::AstStatTypeFunction>("AstStatTypeFunction", NodeCategory::Stat, handleStatTypeFunctionMethods);
        registerNodeClass<Luau::AstStatDeclareGlobal>("AstStatDeclareGlobal", NodeCategory::Stat, handleStatDeclareGlobalMethods);
        registerNodeClass<Luau::AstStatDeclareFunction>("AstStatDeclareFunction", NodeCategory::Stat, handleStatDeclareFunctionMethods);
        registerNodeClass<Luau::AstStatClass>("AstStatClass", NodeCategory::Stat, handleStatClassMethods);
        registerNodeClass<Luau::AstStatDeclareExternType>("AstStatDeclareExternType", NodeCategory::Stat, handleStatDeclareExternTypeMethods);
        registerNodeClass<Luau::AstStatError>("AstStatError", NodeCategory::Stat, handleStatErrorMethods);

        // Expressions
        registerNodeClass<Luau::AstExprGroup>("AstExprGroup", NodeCategory::Expr, handleExprGroupMethods);
        registerNodeClass<Luau::AstExprConstantNil>("AstExprConstantNil", NodeCategory::Expr);
        registerNodeClass<Luau::AstExprConstantBool>("AstExprConstantBool", NodeCategory::Expr, handleExprConstantBoolMethods);
        registerNodeClass<Luau::AstExprConstantNumber>("AstExprConstantNumber", NodeCategory::Expr, handleExprConstantNumberMethods);
        registerNodeClass<Luau::AstExprConstantInteger>("AstExprConstantInteger", NodeCategory::Expr, handleExprConstantIntegerMethods);
        registerNodeClass<Luau::AstExprConstantString>("AstExprConstantString", NodeCategory::Expr, handleExprConstantStringMethods);
        registerNodeClass<Luau::AstExprLocal>("AstExprLocal", NodeCategory::Expr, handleExprLocalMethods);
        registerNodeClass<Luau::AstExprGlobal>("AstExprGlobal", NodeCategory::Expr, handleExprGlobalMethods);
        registerNodeClass<Luau::AstExprVarargs>("AstExprVarargs", NodeCategory::Expr);
        registerNodeClass<Luau::AstExprCall>("AstExprCall", NodeCategory::Expr, handleExprCallMethods);
        registerNodeClass<Luau::AstExprIndexName>("AstExprIndexName", NodeCategory::Expr, handleExprIndexNameMethods);
        registerNodeClass<Luau::AstExprIndexExpr>("AstExprIndexExpr", NodeCategory::Expr, handleExprIndexExprMethods);
        registerNodeClass<Luau::AstExprFunction>("AstExprFunction", NodeCategory::Expr, handleExprFunctionMethods);
        registerNodeClass<Luau::AstExprTable>("AstExprTable", NodeCategory::Expr, handleExprTableMethods);
        registerNodeClass<Luau::AstExprUnary>("AstExprUnary", NodeCategory::Expr, handleExprUnaryMethods);
        registerNodeClass<Luau::AstExprBinary>("AstExprBinary", NodeCategory::Expr, handleExprBinaryMethods);
        registerNodeClass<Luau::AstExprTypeAssertion>("AstExprTypeAssertion", NodeCategory::Expr, handleExprTypeAssertionMethods);
        registerNodeClass<Luau::AstExprIfElse>("AstExprIfElse", NodeCategory::Expr, handleExprIfElseMethods);
        registerNodeClass<Luau::AstExprInterpString>("AstExprInterpString", NodeCategory::Expr, handleExprInterpStringMethods);
        registerNodeClass<Luau::AstExprInstantiate>("AstExprInstantiate", NodeCategory::Expr, handleExprInstantiateMethods);
        registerNodeClass<Luau::AstExprError>("AstExprError", NodeCategory::Expr, handleExprErrorMethods);

        // Types
        registerNodeClass<Luau::AstTypeReference>("AstTypeReference", NodeCategory::Type, handleTypeReferenceMethods);
        registerNodeClass<Luau::AstTypeTable>("AstTypeTable", NodeCategory::Type, handleTypeTableMethods);
        registerNodeClass<Luau::AstTypeFunction>("AstTypeFunction", NodeCategory::Type, handleTypeFunctionMethods);
        registerNodeClass<Luau::AstTypeTypeof>("AstTypeTypeof", NodeCategory::Type, handleTypeTypeofMethods);
        registerNodeClass<Luau::AstTypeOptional>("AstTypeOptional", NodeCategory::Type);
        registerNodeClass<Luau::AstTypeUnion>("AstTypeUnion", NodeCategory::Type, handleTypeUnionMethods);
        registerNodeClass<Luau::AstTypeIntersection>("AstTypeIntersection", NodeCategory::Type, handleTypeIntersectionMethods);
        registerNodeClass<Luau::AstTypeSingletonBool>("AstTypeSingletonBool", NodeCategory::Type, handleTypeSingletonBoolMethods);
        registerNodeClass<Luau::AstTypeSingletonString>("AstTypeSingletonString", NodeCategory::Type, handleTypeSingletonStringMethods);
        registerNodeClass<Luau::AstTypeGroup>("AstTypeGroup", NodeCategory::Type, handleTypeGroupMethods);
        registerNodeClass<Luau::AstTypeError>("AstTypeError", NodeCategory::Type, handleTypeErrorMethods);

        // Type Packs
        registerNodeClass<Luau::AstTypePackExplicit>("AstTypePackExplicit", NodeCategory::TypePack, handleTypePackExplicitMethods);
        registerNodeClass<Luau::AstTypePackVariadic>("AstTypePackVariadic", NodeCategory::TypePack, handleTypePackVariadicMethods);
        registerNodeClass<Luau::AstTypePackGeneric>("AstTypePackGeneric", NodeCategory::TypePack, handleTypePackGenericMethods);

        // Generics & Attributes
        registerNodeClass<Luau::AstGenericType>("AstGenericType", NodeCategory::Generic, handleGenericTypeMethods);
        registerNodeClass<Luau::AstGenericTypePack>("AstGenericTypePack", NodeCategory::Generic, handleGenericTypePackMethods);
        registerNodeClass<Luau::AstAttr>("AstAttr", NodeCategory::Attr, handleAttrMethods);
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
