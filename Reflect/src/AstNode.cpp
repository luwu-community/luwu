// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{


enum class NodeCategory : uint8_t
{
    Unknown = 0,
    Stat,
    Expr,
    Type,
    TypePack,
    Generic,
    Attr,
};

LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushAstNode, checkAstNode, astNodeDtor, AstNodeData, Luau::AstNode*, TagNode, "AstNode")

typedef bool (*NodePropertyHandler)(lua_State* L, AstNodeData& handle, ReflectAtom atom);
typedef bool (*NodeMethodHandler)(lua_State* L, AstNodeData& handle, ReflectAtom atom);

struct AstNodeClassInfo
{
    const char* kind = nullptr;
    NodeCategory category = NodeCategory::Unknown;
    NodePropertyHandler propHandler = nullptr;
    NodeMethodHandler methodHandler = nullptr;
};

static std::vector<AstNodeClassInfo> s_nodeClassTable;

// Register every node statically to allow for all AST node operations to be direct jumps and not if-else hell.
//
// (the previous way required doing node->is<T> for every single AST type which is both ugh and also not great for performance)
template<typename T>
static void registerNodeClass(
    const char* kind,
    NodeCategory category,
    NodePropertyHandler propHandler = nullptr,
    NodeMethodHandler methodHandler = nullptr
)
{
    int idx = T::ClassIndex();
    if (size_t(idx) >= s_nodeClassTable.size())
        s_nodeClassTable.resize(idx + 1, AstNodeClassInfo{"AstNode", NodeCategory::Unknown, nullptr, nullptr});
    s_nodeClassTable[idx] = AstNodeClassInfo{kind, category, propHandler, methodHandler};
}

LUAU_REFLECT_GET_NODE_KIND(getNodeKind, Luau::AstNode*, s_nodeClassTable, "AstNode")

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

#define LUAU_AST_HANDLER_START(name, NodeType) \
    static bool name(lua_State* L, AstNodeData& handle, ReflectAtom atom) \
    { \
        auto* n = static_cast<Luau::NodeType*>(handle.node); \
        switch (atom) \
        {

#define LUAU_AST_HANDLER_END() \
        default: \
            return false; \
        } \
    }

LUAU_AST_HANDLER_START(handleStatBlockProps, AstStatBlock)
    case ReflectAtom::HasEnd: { lua_pushboolean(L, n->hasEnd); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatBlockMethods, AstStatBlock)
    case ReflectAtom::Body: { pushNodeArray(L, handle.doc, n->body); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatIfMethods, AstStatIf)
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::ThenBody:  { pushAstNode(L, handle.doc, n->thenbody); return true; }
    case ReflectAtom::ElseBody:  { pushAstNode(L, handle.doc, n->elsebody); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatWhileProps, AstStatWhile)
    case ReflectAtom::HasDo: { lua_pushboolean(L, n->hasDo); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatWhileMethods, AstStatWhile)
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::Body:      { pushAstNode(L, handle.doc, n->body); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatRepeatMethods, AstStatRepeat)
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::Body:      { pushAstNode(L, handle.doc, n->body); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatReturnMethods, AstStatReturn)
    case ReflectAtom::List: { pushNodeArray(L, handle.doc, n->list); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatExprMethods, AstStatExpr)
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatLocalProps, AstStatLocal)
    case ReflectAtom::IsConst:  { lua_pushboolean(L, n->isConst); return true; }
    case ReflectAtom::Exported: { lua_pushboolean(L, n->isExported); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatLocalMethods, AstStatLocal)
    case ReflectAtom::Vars:   { pushLocalArray(L, handle.doc, n->vars); return true; }
    case ReflectAtom::Values: { pushNodeArray(L, handle.doc, n->values); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatForProps, AstStatFor)
    case ReflectAtom::HasDo: { lua_pushboolean(L, n->hasDo); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatForMethods, AstStatFor)
    case ReflectAtom::Var:  { pushAstAux(L, handle.doc, n->var); return true; }
    case ReflectAtom::From: { pushAstNode(L, handle.doc, n->from); return true; }
    case ReflectAtom::To:   { pushAstNode(L, handle.doc, n->to); return true; }
    case ReflectAtom::Step: { pushAstNode(L, handle.doc, n->step); return true; }
    case ReflectAtom::Body: { pushAstNode(L, handle.doc, n->body); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatForInProps, AstStatForIn)
    case ReflectAtom::HasIn: { lua_pushboolean(L, n->hasIn); return true; }
    case ReflectAtom::HasDo: { lua_pushboolean(L, n->hasDo); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatForInMethods, AstStatForIn)
    case ReflectAtom::Vars:   { pushLocalArray(L, handle.doc, n->vars); return true; }
    case ReflectAtom::Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    case ReflectAtom::Body:   { pushAstNode(L, handle.doc, n->body); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatAssignMethods, AstStatAssign)
    case ReflectAtom::Vars:   { pushNodeArray(L, handle.doc, n->vars); return true; }
    case ReflectAtom::Values: { pushNodeArray(L, handle.doc, n->values); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatCompoundAssignProps, AstStatCompoundAssign)
    case ReflectAtom::Op: { lua_pushstring(L, toString(n->op).c_str()); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatCompoundAssignMethods, AstStatCompoundAssign)
    case ReflectAtom::Var:   { pushAstNode(L, handle.doc, n->var); return true; }
    case ReflectAtom::Value: { pushAstNode(L, handle.doc, n->value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatFunctionMethods, AstStatFunction)
    case ReflectAtom::Name: { pushAstNode(L, handle.doc, n->name); return true; }
    case ReflectAtom::Func: { pushAstNode(L, handle.doc, n->func); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatLocalFunctionProps, AstStatLocalFunction)
    case ReflectAtom::IsConst: { lua_pushboolean(L, n->isConst); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatLocalFunctionMethods, AstStatLocalFunction)
    case ReflectAtom::Name: { pushAstAux(L, handle.doc, n->name); return true; }
    case ReflectAtom::Func: { pushAstNode(L, handle.doc, n->func); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatTypeAliasProps, AstStatTypeAlias)
    case ReflectAtom::Name:     { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Exported: { lua_pushboolean(L, n->exported); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatTypeAliasMethods, AstStatTypeAlias)
    case ReflectAtom::Type:         { pushAstNode(L, handle.doc, n->type); return true; }
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatTypeFunctionProps, AstStatTypeFunction)
    case ReflectAtom::Name:      { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Exported:  { lua_pushboolean(L, n->exported); return true; }
    case ReflectAtom::HasErrors: { lua_pushboolean(L, n->hasErrors); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatTypeFunctionMethods, AstStatTypeFunction)
    case ReflectAtom::Body: { pushAstNode(L, handle.doc, n->body); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareGlobalProps, AstStatDeclareGlobal)
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareGlobalMethods, AstStatDeclareGlobal)
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->type); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareFunctionProps, AstStatDeclareFunction)
    case ReflectAtom::Name:   { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Vararg: { lua_pushboolean(L, n->vararg); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareFunctionMethods, AstStatDeclareFunction)
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case ReflectAtom::Params:       { pushNodeArray(L, handle.doc, n->params.types); return true; }
    case ReflectAtom::ReturnTypes:  { pushAstNode(L, handle.doc, n->retTypes); return true; }
    case ReflectAtom::Attributes:   { pushNodeArray(L, handle.doc, n->attributes); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatClassProps, AstStatClass)
    case ReflectAtom::Exported: { lua_pushboolean(L, n->exported); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatClassMethods, AstStatClass)
    case ReflectAtom::Name: { pushAstAux(L, handle.doc, n->name); return true; }
    case ReflectAtom::Members:
    {
        lua_createtable(L, int(n->members.size), 0);
        for (size_t i = 0; i < n->members.size; i++)
        {
            const auto& member = n->members.data[i];
            if (const auto* p = Luau::get_if<Luau::AstClassProperty>(&member))
                pushAstAux(L, handle.doc, *p);
            else if (const auto* m = Luau::get_if<Luau::AstClassMethod>(&member))
                pushAstAux(L, handle.doc, *m);
            else
                lua_pushnil(L);
            lua_rawseti(L, -2, int(i + 1));
        }
        return true;
    }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareExternTypeProps, AstStatDeclareExternType)
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::SuperName:
    {
        if (n->superName)
            lua_pushstring(L, n->superName->value);
        else
            lua_pushnil(L);
        return true;
    }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatDeclareExternTypeMethods, AstStatDeclareExternType)
    case ReflectAtom::Props:
    {
        lua_createtable(L, int(n->props.size), 0);
        for (size_t i = 0; i < n->props.size; i++)
        {
            pushAstAux(L, handle.doc, n->props.data[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        return true;
    }
    case ReflectAtom::Indexer:
    {
        if (n->indexer)
            pushAstAux(L, handle.doc, *n->indexer);
        else
            lua_pushnil(L);
        return true;
    }
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatErrorProps, AstStatError)
    case ReflectAtom::MessageIndex: { lua_pushinteger(L, n->messageIndex); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleStatErrorMethods, AstStatError)
    case ReflectAtom::Expressions: { pushNodeArray(L, handle.doc, n->expressions); return true; }
    case ReflectAtom::Statements:  { pushNodeArray(L, handle.doc, n->statements); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprGroupMethods, AstExprGroup)
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantBoolProps, AstExprConstantBool)
    case ReflectAtom::Value: { lua_pushboolean(L, n->value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantNumberProps, AstExprConstantNumber)
    case ReflectAtom::Value: { lua_pushnumber(L, n->value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantIntegerProps, AstExprConstantInteger)
    case ReflectAtom::Value: { lua_pushinteger64(L, n->value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprConstantStringProps, AstExprConstantString)
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    case ReflectAtom::QuoteStyle:
    {
        switch (n->quoteStyle)
        {
        case Luau::AstExprConstantString::QuoteStyle::QuotedSingle: lua_pushstring(L, "single"); break;
        case Luau::AstExprConstantString::QuoteStyle::QuotedSimple: lua_pushstring(L, "double"); break;
        case Luau::AstExprConstantString::QuoteStyle::QuotedRaw:    lua_pushstring(L, "raw"); break;
        case Luau::AstExprConstantString::QuoteStyle::Unquoted:     lua_pushstring(L, "unquoted"); break;
        default: lua_pushstring(L, "simple"); break;
        }
        return true;
    }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprLocalProps, AstExprLocal)
    case ReflectAtom::Upvalue: { lua_pushboolean(L, n->upvalue); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprLocalMethods, AstExprLocal)
    case ReflectAtom::Local: { pushAstAux(L, handle.doc, n->local); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprGlobalProps, AstExprGlobal)
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprCallProps, AstExprCall)
    case ReflectAtom::Self: { lua_pushboolean(L, n->self); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprCallMethods, AstExprCall)
    case ReflectAtom::Func:          { pushAstNode(L, handle.doc, n->func); return true; }
    case ReflectAtom::Args:          { pushNodeArray(L, handle.doc, n->args); return true; }
    case ReflectAtom::TypeArguments: { pushTypeOrPackArray(L, handle.doc, n->typeArguments); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIndexNameProps, AstExprIndexName)
    case ReflectAtom::Index: { lua_pushstring(L, n->index.value); return true; }
    case ReflectAtom::Op:    { char s[2] = {n->op, '\0'}; lua_pushstring(L, s); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIndexNameMethods, AstExprIndexName)
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIndexExprMethods, AstExprIndexExpr)
    case ReflectAtom::Expr:  { pushAstNode(L, handle.doc, n->expr); return true; }
    case ReflectAtom::Index: { pushAstNode(L, handle.doc, n->index); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprFunctionProps, AstExprFunction)
    case ReflectAtom::Vararg:    { lua_pushboolean(L, n->vararg); return true; }
    case ReflectAtom::DebugName: { lua_pushstring(L, n->debugname.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprFunctionMethods, AstExprFunction)
    case ReflectAtom::Args:             { pushLocalArray(L, handle.doc, n->args); return true; }
    case ReflectAtom::Body:             { pushAstNode(L, handle.doc, n->body); return true; }
    case ReflectAtom::Generics:         { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks:     { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case ReflectAtom::ReturnAnnotation: { pushAstNode(L, handle.doc, n->returnAnnotation); return true; }
    case ReflectAtom::Attributes:       { pushNodeArray(L, handle.doc, n->attributes); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprTableMethods, AstExprTable)
    case ReflectAtom::Items:
    {
        lua_createtable(L, int(n->items.size), 0);
        for (size_t i = 0; i < n->items.size; i++)
        {
            const auto& item = n->items.data[i];
            lua_createtable(L, 0, 3);
            if (item.kind == Luau::AstExprTable::Item::Kind::List)
                lua_pushstring(L, "list");
            else if (item.kind == Luau::AstExprTable::Item::Kind::Record)
                lua_pushstring(L, "record");
            else
                lua_pushstring(L, "general");
            lua_setfield(L, -2, "kind");

            if (item.key)
                pushAstNode(L, handle.doc, item.key);
            else
                lua_pushnil(L);
            lua_setfield(L, -2, "key");

            pushAstNode(L, handle.doc, item.value);
            lua_setfield(L, -2, "value");

            lua_rawseti(L, -2, int(i + 1));
        }
        return true;
    }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprUnaryProps, AstExprUnary)
    case ReflectAtom::Op: { lua_pushstring(L, toString(n->op).c_str()); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprUnaryMethods, AstExprUnary)
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprBinaryProps, AstExprBinary)
    case ReflectAtom::Op: { lua_pushstring(L, toString(n->op).c_str()); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprBinaryMethods, AstExprBinary)
    case ReflectAtom::Left:  { pushAstNode(L, handle.doc, n->left); return true; }
    case ReflectAtom::Right: { pushAstNode(L, handle.doc, n->right); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprTypeAssertionMethods, AstExprTypeAssertion)
    case ReflectAtom::Expr:       { pushAstNode(L, handle.doc, n->expr); return true; }
    case ReflectAtom::Annotation: { pushAstNode(L, handle.doc, n->annotation); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIfElseProps, AstExprIfElse)
    case ReflectAtom::HasElse: { lua_pushboolean(L, n->hasElse); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprIfElseMethods, AstExprIfElse)
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::TrueExpr:  { pushAstNode(L, handle.doc, n->trueExpr); return true; }
    case ReflectAtom::FalseExpr: { pushAstNode(L, handle.doc, n->falseExpr); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprInterpStringProps, AstExprInterpString)
    case ReflectAtom::Strings:
    {
        lua_createtable(L, int(n->strings.size), 0);
        for (size_t i = 0; i < n->strings.size; i++)
        {
            lua_pushlstring(L, n->strings.data[i].data, n->strings.data[i].size);
            lua_rawseti(L, -2, int(i + 1));
        }
        return true;
    }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprInterpStringMethods, AstExprInterpString)
    case ReflectAtom::Expressions: { pushNodeArray(L, handle.doc, n->expressions); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprInstantiateMethods, AstExprInstantiate)
    case ReflectAtom::Expr:          { pushAstNode(L, handle.doc, n->expr); return true; }
    case ReflectAtom::TypeArguments: { pushTypeOrPackArray(L, handle.doc, n->typeArguments); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprErrorProps, AstExprError)
    case ReflectAtom::MessageIndex: { lua_pushinteger(L, n->messageIndex); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleExprErrorMethods, AstExprError)
    case ReflectAtom::Expressions: { pushNodeArray(L, handle.doc, n->expressions); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeReferenceProps, AstTypeReference)
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Prefix:
    {
        if (n->prefix)
            lua_pushstring(L, n->prefix->value);
        else
            lua_pushnil(L);
        return true;
    }
    case ReflectAtom::HasParameterList: { lua_pushboolean(L, n->hasParameterList); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeReferenceMethods, AstTypeReference)
    case ReflectAtom::Parameters: { pushTypeOrPackArray(L, handle.doc, n->parameters); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeTableMethods, AstTypeTable)
    case ReflectAtom::Props:
    {
        lua_createtable(L, int(n->props.size), 0);
        for (size_t i = 0; i < n->props.size; i++)
        {
            pushAstAux(L, handle.doc, n->props.data[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        return true;
    }
    case ReflectAtom::Indexer:
    {
        if (n->indexer)
            pushAstAux(L, handle.doc, *n->indexer);
        else
            lua_pushnil(L);
        return true;
    }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeFunctionMethods, AstTypeFunction)
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case ReflectAtom::ArgTypes:     { pushNodeArray(L, handle.doc, n->argTypes.types); return true; }
    case ReflectAtom::ReturnTypes:  { pushAstNode(L, handle.doc, n->returnTypes); return true; }
    case ReflectAtom::Attributes:   { pushNodeArray(L, handle.doc, n->attributes); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeTypeofMethods, AstTypeTypeof)
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeUnionMethods, AstTypeUnion)
    case ReflectAtom::Types: { pushNodeArray(L, handle.doc, n->types); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeIntersectionMethods, AstTypeIntersection)
    case ReflectAtom::Types: { pushNodeArray(L, handle.doc, n->types); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeSingletonBoolProps, AstTypeSingletonBool)
    case ReflectAtom::Value: { lua_pushboolean(L, n->value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeSingletonStringProps, AstTypeSingletonString)
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeGroupMethods, AstTypeGroup)
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->type); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeErrorProps, AstTypeError)
    case ReflectAtom::IsMissing:    { lua_pushboolean(L, n->isMissing); return true; }
    case ReflectAtom::MessageIndex: { lua_pushinteger(L, n->messageIndex); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypeErrorMethods, AstTypeError)
    case ReflectAtom::Types: { pushNodeArray(L, handle.doc, n->types); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypePackExplicitMethods, AstTypePackExplicit)
    case ReflectAtom::Types:    { pushNodeArray(L, handle.doc, n->typeList.types); return true; }
    case ReflectAtom::TailType: { pushAstNode(L, handle.doc, n->typeList.tailType); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypePackVariadicMethods, AstTypePackVariadic)
    case ReflectAtom::VariadicType: { pushAstNode(L, handle.doc, n->variadicType); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleTypePackGenericProps, AstTypePackGeneric)
    case ReflectAtom::Name: { lua_pushstring(L, n->genericName.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleGenericTypeProps, AstGenericType)
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleGenericTypeMethods, AstGenericType)
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->defaultValue); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleGenericTypePackProps, AstGenericTypePack)
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleGenericTypePackMethods, AstGenericTypePack)
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->defaultValue); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleAttrProps, AstAttr)
    case ReflectAtom::Type:
    {
        switch (n->type)
        {
        case Luau::AstAttr::Type::Checked:       lua_pushstring(L, "checked"); break;
        case Luau::AstAttr::Type::Native:        lua_pushstring(L, "native"); break;
        case Luau::AstAttr::Type::Deprecated:    lua_pushstring(L, "deprecated"); break;
        case Luau::AstAttr::Type::DebugNoinline: lua_pushstring(L, "debugNoinline"); break;
        default: lua_pushstring(L, "unknown"); break;
        }
        return true;
    }
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
LUAU_AST_HANDLER_END()

LUAU_AST_HANDLER_START(handleAttrMethods, AstAttr)
    case ReflectAtom::Args: { pushNodeArray(L, handle.doc, n->args); return true; }
LUAU_AST_HANDLER_END()

static void initializeDispatchTables()
{
    // SAFETY: c++ guarantees thread safety in static inits like this (see https://iamroman.org/blog/2017/04/cpp11-static-init/) from c++11
    static const bool initialized = []() {
        // Statements
        registerNodeClass<Luau::AstStatBlock>("AstStatBlock", NodeCategory::Stat, handleStatBlockProps, handleStatBlockMethods);
        registerNodeClass<Luau::AstStatIf>("AstStatIf", NodeCategory::Stat, nullptr, handleStatIfMethods);
        registerNodeClass<Luau::AstStatWhile>("AstStatWhile", NodeCategory::Stat, handleStatWhileProps, handleStatWhileMethods);
        registerNodeClass<Luau::AstStatRepeat>("AstStatRepeat", NodeCategory::Stat, nullptr, handleStatRepeatMethods);
        registerNodeClass<Luau::AstStatBreak>("AstStatBreak", NodeCategory::Stat);
        registerNodeClass<Luau::AstStatContinue>("AstStatContinue", NodeCategory::Stat);
        registerNodeClass<Luau::AstStatReturn>("AstStatReturn", NodeCategory::Stat, nullptr, handleStatReturnMethods);
        registerNodeClass<Luau::AstStatExpr>("AstStatExpr", NodeCategory::Stat, nullptr, handleStatExprMethods);
        registerNodeClass<Luau::AstStatLocal>("AstStatLocal", NodeCategory::Stat, handleStatLocalProps, handleStatLocalMethods);
        registerNodeClass<Luau::AstStatFor>("AstStatFor", NodeCategory::Stat, handleStatForProps, handleStatForMethods);
        registerNodeClass<Luau::AstStatForIn>("AstStatForIn", NodeCategory::Stat, handleStatForInProps, handleStatForInMethods);
        registerNodeClass<Luau::AstStatAssign>("AstStatAssign", NodeCategory::Stat, nullptr, handleStatAssignMethods);
        registerNodeClass<Luau::AstStatCompoundAssign>("AstStatCompoundAssign", NodeCategory::Stat, handleStatCompoundAssignProps, handleStatCompoundAssignMethods);
        registerNodeClass<Luau::AstStatFunction>("AstStatFunction", NodeCategory::Stat, nullptr, handleStatFunctionMethods);
        registerNodeClass<Luau::AstStatLocalFunction>("AstStatLocalFunction", NodeCategory::Stat, handleStatLocalFunctionProps, handleStatLocalFunctionMethods);
        registerNodeClass<Luau::AstStatTypeAlias>("AstStatTypeAlias", NodeCategory::Stat, handleStatTypeAliasProps, handleStatTypeAliasMethods);
        registerNodeClass<Luau::AstStatTypeFunction>("AstStatTypeFunction", NodeCategory::Stat, handleStatTypeFunctionProps, handleStatTypeFunctionMethods);
        registerNodeClass<Luau::AstStatDeclareGlobal>("AstStatDeclareGlobal", NodeCategory::Stat, handleStatDeclareGlobalProps, handleStatDeclareGlobalMethods);
        registerNodeClass<Luau::AstStatDeclareFunction>("AstStatDeclareFunction", NodeCategory::Stat, handleStatDeclareFunctionProps, handleStatDeclareFunctionMethods);
        registerNodeClass<Luau::AstStatClass>("AstStatClass", NodeCategory::Stat, handleStatClassProps, handleStatClassMethods);
        registerNodeClass<Luau::AstStatDeclareExternType>("AstStatDeclareExternType", NodeCategory::Stat, handleStatDeclareExternTypeProps, handleStatDeclareExternTypeMethods);
        registerNodeClass<Luau::AstStatError>("AstStatError", NodeCategory::Stat, handleStatErrorProps, handleStatErrorMethods);

        // Expressions
        registerNodeClass<Luau::AstExprGroup>("AstExprGroup", NodeCategory::Expr, nullptr, handleExprGroupMethods);
        registerNodeClass<Luau::AstExprConstantNil>("AstExprConstantNil", NodeCategory::Expr);
        registerNodeClass<Luau::AstExprConstantBool>("AstExprConstantBool", NodeCategory::Expr, handleExprConstantBoolProps);
        registerNodeClass<Luau::AstExprConstantNumber>("AstExprConstantNumber", NodeCategory::Expr, handleExprConstantNumberProps);
        registerNodeClass<Luau::AstExprConstantInteger>("AstExprConstantInteger", NodeCategory::Expr, handleExprConstantIntegerProps);
        registerNodeClass<Luau::AstExprConstantString>("AstExprConstantString", NodeCategory::Expr, handleExprConstantStringProps);
        registerNodeClass<Luau::AstExprLocal>("AstExprLocal", NodeCategory::Expr, handleExprLocalProps, handleExprLocalMethods);
        registerNodeClass<Luau::AstExprGlobal>("AstExprGlobal", NodeCategory::Expr, handleExprGlobalProps);
        registerNodeClass<Luau::AstExprVarargs>("AstExprVarargs", NodeCategory::Expr);
        registerNodeClass<Luau::AstExprCall>("AstExprCall", NodeCategory::Expr, handleExprCallProps, handleExprCallMethods);
        registerNodeClass<Luau::AstExprIndexName>("AstExprIndexName", NodeCategory::Expr, handleExprIndexNameProps, handleExprIndexNameMethods);
        registerNodeClass<Luau::AstExprIndexExpr>("AstExprIndexExpr", NodeCategory::Expr, nullptr, handleExprIndexExprMethods);
        registerNodeClass<Luau::AstExprFunction>("AstExprFunction", NodeCategory::Expr, handleExprFunctionProps, handleExprFunctionMethods);
        registerNodeClass<Luau::AstExprTable>("AstExprTable", NodeCategory::Expr, nullptr, handleExprTableMethods);
        registerNodeClass<Luau::AstExprUnary>("AstExprUnary", NodeCategory::Expr, handleExprUnaryProps, handleExprUnaryMethods);
        registerNodeClass<Luau::AstExprBinary>("AstExprBinary", NodeCategory::Expr, handleExprBinaryProps, handleExprBinaryMethods);
        registerNodeClass<Luau::AstExprTypeAssertion>("AstExprTypeAssertion", NodeCategory::Expr, nullptr, handleExprTypeAssertionMethods);
        registerNodeClass<Luau::AstExprIfElse>("AstExprIfElse", NodeCategory::Expr, handleExprIfElseProps, handleExprIfElseMethods);
        registerNodeClass<Luau::AstExprInterpString>("AstExprInterpString", NodeCategory::Expr, handleExprInterpStringProps, handleExprInterpStringMethods);
        registerNodeClass<Luau::AstExprInstantiate>("AstExprInstantiate", NodeCategory::Expr, nullptr, handleExprInstantiateMethods);
        registerNodeClass<Luau::AstExprError>("AstExprError", NodeCategory::Expr, handleExprErrorProps, handleExprErrorMethods);

        // Types
        registerNodeClass<Luau::AstTypeReference>("AstTypeReference", NodeCategory::Type, handleTypeReferenceProps, handleTypeReferenceMethods);
        registerNodeClass<Luau::AstTypeTable>("AstTypeTable", NodeCategory::Type, nullptr, handleTypeTableMethods);
        registerNodeClass<Luau::AstTypeFunction>("AstTypeFunction", NodeCategory::Type, nullptr, handleTypeFunctionMethods);
        registerNodeClass<Luau::AstTypeTypeof>("AstTypeTypeof", NodeCategory::Type, nullptr, handleTypeTypeofMethods);
        registerNodeClass<Luau::AstTypeOptional>("AstTypeOptional", NodeCategory::Type);
        registerNodeClass<Luau::AstTypeUnion>("AstTypeUnion", NodeCategory::Type, nullptr, handleTypeUnionMethods);
        registerNodeClass<Luau::AstTypeIntersection>("AstTypeIntersection", NodeCategory::Type, nullptr, handleTypeIntersectionMethods);
        registerNodeClass<Luau::AstTypeSingletonBool>("AstTypeSingletonBool", NodeCategory::Type, handleTypeSingletonBoolProps);
        registerNodeClass<Luau::AstTypeSingletonString>("AstTypeSingletonString", NodeCategory::Type, handleTypeSingletonStringProps);
        registerNodeClass<Luau::AstTypeGroup>("AstTypeGroup", NodeCategory::Type, nullptr, handleTypeGroupMethods);
        registerNodeClass<Luau::AstTypeError>("AstTypeError", NodeCategory::Type, handleTypeErrorProps, handleTypeErrorMethods);

        // Type Packs
        registerNodeClass<Luau::AstTypePackExplicit>("AstTypePackExplicit", NodeCategory::TypePack, nullptr, handleTypePackExplicitMethods);
        registerNodeClass<Luau::AstTypePackVariadic>("AstTypePackVariadic", NodeCategory::TypePack, nullptr, handleTypePackVariadicMethods);
        registerNodeClass<Luau::AstTypePackGeneric>("AstTypePackGeneric", NodeCategory::TypePack, handleTypePackGenericProps);

        // Generics & Attributes
        registerNodeClass<Luau::AstGenericType>("AstGenericType", NodeCategory::Generic, handleGenericTypeProps, handleGenericTypeMethods);
        registerNodeClass<Luau::AstGenericTypePack>("AstGenericTypePack", NodeCategory::Generic, handleGenericTypePackProps, handleGenericTypePackMethods);
        registerNodeClass<Luau::AstAttr>("AstAttr", NodeCategory::Attr, handleAttrProps, handleAttrMethods);
        return true;
    }();
    (void)initialized;
}

static int astNodeChildren(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    DirectChildCollector collector;
    handle.node->visit(&collector);

    lua_createtable(L, int(collector.children.size()), 0);
    for (size_t i = 0; i < collector.children.size(); i++)
    {
        pushAstNode(L, handle.doc, collector.children[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
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

static int astNodeWalk(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    CallbackVisitor visitor(L, handle.doc, 2);
    handle.node->visit(&visitor);

    if (visitor.errorOccurred)
        lua_error(L);

    return 0;
}

static int astNodeFind(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    size_t len = 0;
    const char* kindStr = luaL_checklstring(L, 2, &len);
    std::string_view kind(kindStr, len);

    FindKindVisitor visitor(kind);
    if (handle.node)
        handle.node->visit(&visitor);

    lua_createtable(L, int(visitor.matches.size()), 0);
    for (size_t i = 0; i < visitor.matches.size(); i++)
    {
        pushAstNode(L, handle.doc, visitor.matches[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astNodeMethodTrampoline, checkAstNode, s_nodeClassTable, "AstNode");

static int astNodeIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstNode);
    Luau::AstNode* node = handle.node;
    int idx = node->classIndex;

    switch (atom)
    {
    case ReflectAtom::Children:
    {
        return pushUserdataMethod(L, TagNode, "children");
    }
    case ReflectAtom::Walk:
    {
        return pushUserdataMethod(L, TagNode, "walk");
    }
    case ReflectAtom::Find:
    {
        return pushUserdataMethod(L, TagNode, "find");
    }
    case ReflectAtom::Location:
    {
        return pushUserdataMethod(L, TagNode, "location");
    }
    case ReflectAtom::Cst:
    {
        return pushUserdataMethod(L, TagNode, "cst");
    }
    case ReflectAtom::Text:
    {
        auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), node->location);
        lua_pushlstring(L, doc->source.data() + startOff, endOff - startOff);
        return 1;
    }
    case ReflectAtom::Kind:
    {
        lua_pushstring(L, getNodeKind(node));
        return 1;
    }
    case ReflectAtom::Category:
    {
        NodeCategory cat = (idx >= 0 && idx < int(s_nodeClassTable.size())) ? s_nodeClassTable[idx].category : NodeCategory::Unknown;
        switch (cat)
        {
        case NodeCategory::Stat:     lua_pushstring(L, "stat"); break;
        case NodeCategory::Expr:     lua_pushstring(L, "expr"); break;
        case NodeCategory::Type:     lua_pushstring(L, "type"); break;
        case NodeCategory::TypePack: lua_pushstring(L, "typePack"); break;
        case NodeCategory::Generic:  lua_pushstring(L, "generic"); break;
        case NodeCategory::Attr:     lua_pushstring(L, "attr"); break;
        default:                lua_pushstring(L, "unknown"); break;
        }
        return 1;
    }
    case ReflectAtom::HasSemicolon:
    {
        if (auto* stat = node->asStat())
            lua_pushboolean(L, stat->hasSemicolon);
        else
            lua_pushboolean(L, false);
        return 1;
    }
    default:
        break;
    }

    if (atom != ReflectAtom::Unknown && idx >= 0 && idx < int(s_nodeClassTable.size()))
    {
        const AstNodeClassInfo& info = s_nodeClassTable[idx];
        if (info.propHandler && info.propHandler(L, handle, atom))
            return 1;

        if (info.methodHandler)
            return pushCachedUserdataMethod(L, TagNode, keyStr, astNodeMethodTrampoline);
    }

    lua_pushnil(L);
    return 1;
}

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

static int astNodeNamecall(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    int atomId = -1;
    size_t len = 0;
    const char* str = lua_namecallwithlen(L, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr, &len);
    if (!str)
        luaL_error(L, "missing method name in namecall");

    ReflectAtom atom = resolveReflectAtom(atomId, str, len);

    switch (atom)
    {
    case ReflectAtom::Children: return astNodeChildren(L);
    case ReflectAtom::Walk:     return astNodeWalk(L);
    case ReflectAtom::Find:     return astNodeFind(L);
    case ReflectAtom::Location: return astNodeLocation(L);
    case ReflectAtom::Cst:      return astNodeCst(L);
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

void registerAstNode(lua_State* L)
{
    initializeDispatchTables();
    static const luaL_Reg s_nodeMethods[] = {
        {"children", astNodeChildren},
        {"walk", astNodeWalk},
        {"find", astNodeFind},
        {"location", astNodeLocation},
        {"cst", astNodeCst},
        {nullptr, nullptr},
    };
    registerUserdataType(L, TagNode, "AstNode", astNodeDtor, astNodeIndex, astNodeToString, astNodeEq, s_nodeMethods, astNodeNamecall);
}

} // namespace Luau
