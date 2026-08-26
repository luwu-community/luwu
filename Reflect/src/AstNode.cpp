// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

// Because the embedder may have themselves set useratom, we cannot use Luau's builtin atom system here, instead define the atoms separately using a hashmap + enum
enum AstNodeAtom : uint8_t
{
    Atom_Unknown = 0,
    Atom_Kind,
    Atom_Category,
    Atom_Location,
    Atom_Text,
    Atom_Children,
    Atom_Walk,
    Atom_Cst,
    Atom_Name,
    Atom_Body,
    Atom_Condition,
    Atom_ThenBody,
    Atom_ElseBody,
    Atom_List,
    Atom_Expr,
    Atom_Vars,
    Atom_Values,
    Atom_Var,
    Atom_From,
    Atom_To,
    Atom_Step,
    Atom_Op,
    Atom_Func,
    Atom_Args,
    Atom_Self,
    Atom_Index,
    Atom_Items,
    Atom_Left,
    Atom_Right,
    Atom_Value,
    Atom_Local,
    Atom_TrueExpr,
    Atom_FalseExpr,
    Atom_Prefix,
    Atom_Type,
    Atom_Vararg,
    Atom_Annotation,
    Atom_HasSemicolon,
    Atom_Generics,
    Atom_GenericPacks,
    Atom_ReturnAnnotation,
};

static AstNodeAtom getAstNodeAtom(std::string_view key)
{
    static const std::unordered_map<std::string_view, AstNodeAtom> s_atomMap = {
        {"kind", Atom_Kind},
        {"category", Atom_Category},
        {"location", Atom_Location},
        {"text", Atom_Text},
        {"children", Atom_Children},
        {"walk", Atom_Walk},
        {"cst", Atom_Cst},
        {"name", Atom_Name},
        {"body", Atom_Body},
        {"condition", Atom_Condition},
        {"thenbody", Atom_ThenBody},
        {"elsebody", Atom_ElseBody},
        {"list", Atom_List},
        {"expr", Atom_Expr},
        {"vars", Atom_Vars},
        {"values", Atom_Values},
        {"var", Atom_Var},
        {"from", Atom_From},
        {"to", Atom_To},
        {"step", Atom_Step},
        {"op", Atom_Op},
        {"func", Atom_Func},
        {"args", Atom_Args},
        {"self", Atom_Self},
        {"index", Atom_Index},
        {"items", Atom_Items},
        {"left", Atom_Left},
        {"right", Atom_Right},
        {"value", Atom_Value},
        {"local", Atom_Local},
        {"trueExpr", Atom_TrueExpr},
        {"falseExpr", Atom_FalseExpr},
        {"prefix", Atom_Prefix},
        {"type", Atom_Type},
        {"vararg", Atom_Vararg},
        {"annotation", Atom_Annotation},
        {"hasSemicolon", Atom_HasSemicolon},
        {"generics", Atom_Generics},
        {"genericPacks", Atom_GenericPacks},
        {"returnAnnotation", Atom_ReturnAnnotation},
    };

    if (auto it = s_atomMap.find(key); it != s_atomMap.end())
        return it->second;

    return Atom_Unknown;
}

enum NodeCategory : uint8_t
{
    Category_Unknown = 0,
    Category_Stat,
    Category_Expr,
    Category_Type,
    Category_TypePack,
    Category_Generic,
    Category_Attr,
};

void pushAstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstNode* node)
{
    if (!node)
    {
        lua_pushnil(L);
        return;
    }
    AstNodeData* data = static_cast<AstNodeData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstNodeData), TagNode));
    new (data) AstNodeData{doc, node};
}

AstNodeData& checkAstNode(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagNode)
        luaL_typeerrorL(L, idx, "AstNode");
    return *static_cast<AstNodeData*>(lua_touserdata(L, idx));
}

static void astNodeDtor(lua_State* L, void* userdata)
{
    static_cast<AstNodeData*>(userdata)->~AstNodeData();
}

typedef bool (*NodePropertyHandler)(lua_State* L, AstNodeData& handle, AstNodeAtom atom);

static std::vector<const char*> s_nodeKindTable;
static std::vector<NodeCategory> s_nodeCategoryTable;
static std::vector<NodePropertyHandler> s_nodePropertyTable;

// Register every node statically to allow for all AST node operations to be direct jumps and not if-else hell.
//
// (the previous way required doing node->is<T> for every single AST type which is both ugh and also not great for performance)
template<typename T>
static void registerNodeClass(const char* kind, NodeCategory category, NodePropertyHandler handler = nullptr)
{
    int idx = T::ClassIndex();
    s_nodeKindTable[idx] = kind;
    s_nodeCategoryTable[idx] = category;
    s_nodePropertyTable[idx] = handler;
}

const char* getNodeKind(Luau::AstNode* node)
{
    if (!node)
        return "nil";
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeKindTable.size()) && s_nodeKindTable[idx])
        return s_nodeKindTable[idx];
    return "AstNode";
}

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
};

// Node-specific property dispatch handlers using AstNodeAtom switch
static bool handleStatBlock(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatBlock*>(handle.node);
    switch (atom)
    {
    case Atom_Body: { pushNodeArray(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatIf(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatIf*>(handle.node);
    switch (atom)
    {
    case Atom_Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case Atom_ThenBody:  { pushAstNode(L, handle.doc, n->thenbody); return true; }
    case Atom_ElseBody:  { pushAstNode(L, handle.doc, n->elsebody); return true; }
    default: return false;
    }
}

static bool handleStatWhile(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatWhile*>(handle.node);
    switch (atom)
    {
    case Atom_Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case Atom_Body:      { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatRepeat(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatRepeat*>(handle.node);
    switch (atom)
    {
    case Atom_Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case Atom_Body:      { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatReturn(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatReturn*>(handle.node);
    switch (atom)
    {
    case Atom_List: { pushNodeArray(L, handle.doc, n->list); return true; }
    default: return false;
    }
}

static bool handleStatExpr(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatExpr*>(handle.node);
    switch (atom)
    {
    case Atom_Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleStatLocal(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatLocal*>(handle.node);
    switch (atom)
    {
    case Atom_Vars:   { pushLocalArray(L, handle.doc, n->vars); return true; }
    case Atom_Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    default: return false;
    }
}

static bool handleStatFor(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatFor*>(handle.node);
    switch (atom)
    {
    case Atom_Var:  { pushAstLocal(L, handle.doc, n->var); return true; }
    case Atom_From: { pushAstNode(L, handle.doc, n->from); return true; }
    case Atom_To:   { pushAstNode(L, handle.doc, n->to); return true; }
    case Atom_Step: { pushAstNode(L, handle.doc, n->step); return true; }
    case Atom_Body: { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatForIn(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatForIn*>(handle.node);
    switch (atom)
    {
    case Atom_Vars:   { pushLocalArray(L, handle.doc, n->vars); return true; }
    case Atom_Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    case Atom_Body:   { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatAssign(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatAssign*>(handle.node);
    switch (atom)
    {
    case Atom_Vars:   { pushNodeArray(L, handle.doc, n->vars); return true; }
    case Atom_Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    default: return false;
    }
}

static bool handleStatCompoundAssign(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatCompoundAssign*>(handle.node);
    switch (atom)
    {
    case Atom_Var:   { pushAstNode(L, handle.doc, n->var); return true; }
    case Atom_Value: { pushAstNode(L, handle.doc, n->value); return true; }
    case Atom_Op:    { lua_pushstring(L, toString(n->op).c_str()); return true; }
    default: return false;
    }
}

static bool handleStatFunction(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatFunction*>(handle.node);
    switch (atom)
    {
    case Atom_Name: { pushAstNode(L, handle.doc, n->name); return true; }
    case Atom_Func: { pushAstNode(L, handle.doc, n->func); return true; }
    default: return false;
    }
}

static bool handleStatLocalFunction(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatLocalFunction*>(handle.node);
    switch (atom)
    {
    case Atom_Name: { pushAstLocal(L, handle.doc, n->name); return true; }
    case Atom_Func: { pushAstNode(L, handle.doc, n->func); return true; }
    default: return false;
    }
}

static bool handleStatTypeAlias(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstStatTypeAlias*>(handle.node);
    switch (atom)
    {
    case Atom_Name:         { lua_pushstring(L, n->name.value); return true; }
    case Atom_Type:         { pushAstNode(L, handle.doc, n->type); return true; }
    case Atom_Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case Atom_GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    default: return false;
    }
}

static bool handleExprCall(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprCall*>(handle.node);
    switch (atom)
    {
    case Atom_Func: { pushAstNode(L, handle.doc, n->func); return true; }
    case Atom_Args: { pushNodeArray(L, handle.doc, n->args); return true; }
    case Atom_Self: { lua_pushboolean(L, n->self); return true; }
    default: return false;
    }
}

static bool handleExprIndexName(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprIndexName*>(handle.node);
    switch (atom)
    {
    case Atom_Expr:  { pushAstNode(L, handle.doc, n->expr); return true; }
    case Atom_Index: { lua_pushstring(L, n->index.value); return true; }
    default: return false;
    }
}

static bool handleExprIndexExpr(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprIndexExpr*>(handle.node);
    switch (atom)
    {
    case Atom_Expr:  { pushAstNode(L, handle.doc, n->expr); return true; }
    case Atom_Index: { pushAstNode(L, handle.doc, n->index); return true; }
    default: return false;
    }
}

static bool handleExprFunction(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprFunction*>(handle.node);
    switch (atom)
    {
    case Atom_Args:             { pushLocalArray(L, handle.doc, n->args); return true; }
    case Atom_Body:             { pushAstNode(L, handle.doc, n->body); return true; }
    case Atom_Vararg:           { lua_pushboolean(L, n->vararg); return true; }
    case Atom_Generics:         { pushNodeArray(L, handle.doc, n->generics); return true; }
    case Atom_GenericPacks:     { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case Atom_ReturnAnnotation: { pushAstNode(L, handle.doc, n->returnAnnotation); return true; }
    default: return false;
    }
}

static bool handleExprTable(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprTable*>(handle.node);
    switch (atom)
    {
    case Atom_Items:
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
    default: return false;
    }
}

static bool handleExprUnary(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprUnary*>(handle.node);
    switch (atom)
    {
    case Atom_Op:   { lua_pushstring(L, toString(n->op).c_str()); return true; }
    case Atom_Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleExprBinary(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprBinary*>(handle.node);
    switch (atom)
    {
    case Atom_Op:    { lua_pushstring(L, toString(n->op).c_str()); return true; }
    case Atom_Left:  { pushAstNode(L, handle.doc, n->left); return true; }
    case Atom_Right: { pushAstNode(L, handle.doc, n->right); return true; }
    default: return false;
    }
}

static bool handleExprConstantBool(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantBool*>(handle.node);
    switch (atom)
    {
    case Atom_Value: { lua_pushboolean(L, n->value); return true; }
    default: return false;
    }
}

static bool handleExprConstantNumber(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantNumber*>(handle.node);
    switch (atom)
    {
    case Atom_Value: { lua_pushnumber(L, n->value); return true; }
    default: return false;
    }
}

static bool handleExprConstantInteger(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantInteger*>(handle.node);
    switch (atom)
    {
    case Atom_Value: { lua_pushinteger(L, n->value); return true; }
    default: return false;
    }
}

static bool handleExprConstantString(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantString*>(handle.node);
    switch (atom)
    {
    case Atom_Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    default: return false;
    }
}

static bool handleExprLocal(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprLocal*>(handle.node);
    switch (atom)
    {
    case Atom_Local: { pushAstLocal(L, handle.doc, n->local); return true; }
    default: return false;
    }
}

static bool handleExprGlobal(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprGlobal*>(handle.node);
    switch (atom)
    {
    case Atom_Name: { lua_pushstring(L, n->name.value); return true; }
    default: return false;
    }
}

static bool handleExprGroup(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprGroup*>(handle.node);
    switch (atom)
    {
    case Atom_Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleExprTypeAssertion(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprTypeAssertion*>(handle.node);
    switch (atom)
    {
    case Atom_Expr:       { pushAstNode(L, handle.doc, n->expr); return true; }
    case Atom_Annotation: { pushAstNode(L, handle.doc, n->annotation); return true; }
    default: return false;
    }
}

static bool handleExprIfElse(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstExprIfElse*>(handle.node);
    switch (atom)
    {
    case Atom_Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case Atom_TrueExpr:  { pushAstNode(L, handle.doc, n->trueExpr); return true; }
    case Atom_FalseExpr: { pushAstNode(L, handle.doc, n->falseExpr); return true; }
    default: return false;
    }
}

static bool handleTypeReference(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstTypeReference*>(handle.node);
    switch (atom)
    {
    case Atom_Name: { lua_pushstring(L, n->name.value); return true; }
    case Atom_Prefix:
    {
        if (n->prefix)
            lua_pushstring(L, n->prefix->value);
        else
            lua_pushnil(L);
        return true;
    }
    default: return false;
    }
}

static bool handleGenericType(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstGenericType*>(handle.node);
    switch (atom)
    {
    case Atom_Name: { lua_pushstring(L, n->name.value); return true; }
    case Atom_Type: { pushAstNode(L, handle.doc, n->defaultValue); return true; }
    default: return false;
    }
}

static bool handleGenericTypePack(lua_State* L, AstNodeData& handle, AstNodeAtom atom)
{
    auto* n = static_cast<Luau::AstGenericTypePack*>(handle.node);
    switch (atom)
    {
    case Atom_Name: { lua_pushstring(L, n->name.value); return true; }
    case Atom_Type: { pushAstNode(L, handle.doc, n->defaultValue); return true; }
    default: return false;
    }
}

static void initializeDispatchTables()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    s_nodeKindTable.assign(gAstRttiIndex + 1, "AstNode");
    s_nodeCategoryTable.assign(gAstRttiIndex + 1, Category_Unknown);
    s_nodePropertyTable.assign(gAstRttiIndex + 1, nullptr);

    // Statements
    registerNodeClass<Luau::AstStatBlock>("AstStatBlock", Category_Stat, handleStatBlock);
    registerNodeClass<Luau::AstStatIf>("AstStatIf", Category_Stat, handleStatIf);
    registerNodeClass<Luau::AstStatWhile>("AstStatWhile", Category_Stat, handleStatWhile);
    registerNodeClass<Luau::AstStatRepeat>("AstStatRepeat", Category_Stat, handleStatRepeat);
    registerNodeClass<Luau::AstStatBreak>("AstStatBreak", Category_Stat);
    registerNodeClass<Luau::AstStatContinue>("AstStatContinue", Category_Stat);
    registerNodeClass<Luau::AstStatReturn>("AstStatReturn", Category_Stat, handleStatReturn);
    registerNodeClass<Luau::AstStatExpr>("AstStatExpr", Category_Stat, handleStatExpr);
    registerNodeClass<Luau::AstStatLocal>("AstStatLocal", Category_Stat, handleStatLocal);
    registerNodeClass<Luau::AstStatFor>("AstStatFor", Category_Stat, handleStatFor);
    registerNodeClass<Luau::AstStatForIn>("AstStatForIn", Category_Stat, handleStatForIn);
    registerNodeClass<Luau::AstStatAssign>("AstStatAssign", Category_Stat, handleStatAssign);
    registerNodeClass<Luau::AstStatCompoundAssign>("AstStatCompoundAssign", Category_Stat, handleStatCompoundAssign);
    registerNodeClass<Luau::AstStatFunction>("AstStatFunction", Category_Stat, handleStatFunction);
    registerNodeClass<Luau::AstStatLocalFunction>("AstStatLocalFunction", Category_Stat, handleStatLocalFunction);
    registerNodeClass<Luau::AstStatTypeAlias>("AstStatTypeAlias", Category_Stat, handleStatTypeAlias);
    registerNodeClass<Luau::AstStatTypeFunction>("AstStatTypeFunction", Category_Stat);
    registerNodeClass<Luau::AstStatDeclareGlobal>("AstStatDeclareGlobal", Category_Stat);
    registerNodeClass<Luau::AstStatDeclareFunction>("AstStatDeclareFunction", Category_Stat);
    registerNodeClass<Luau::AstStatClass>("AstStatClass", Category_Stat);
    registerNodeClass<Luau::AstStatDeclareExternType>("AstStatDeclareExternType", Category_Stat);
    registerNodeClass<Luau::AstStatError>("AstStatError", Category_Stat);

    // Expressions
    registerNodeClass<Luau::AstExprGroup>("AstExprGroup", Category_Expr, handleExprGroup);
    registerNodeClass<Luau::AstExprConstantNil>("AstExprConstantNil", Category_Expr);
    registerNodeClass<Luau::AstExprConstantBool>("AstExprConstantBool", Category_Expr, handleExprConstantBool);
    registerNodeClass<Luau::AstExprConstantNumber>("AstExprConstantNumber", Category_Expr, handleExprConstantNumber);
    registerNodeClass<Luau::AstExprConstantInteger>("AstExprConstantInteger", Category_Expr, handleExprConstantInteger);
    registerNodeClass<Luau::AstExprConstantString>("AstExprConstantString", Category_Expr, handleExprConstantString);
    registerNodeClass<Luau::AstExprLocal>("AstExprLocal", Category_Expr, handleExprLocal);
    registerNodeClass<Luau::AstExprGlobal>("AstExprGlobal", Category_Expr, handleExprGlobal);
    registerNodeClass<Luau::AstExprVarargs>("AstExprVarargs", Category_Expr);
    registerNodeClass<Luau::AstExprCall>("AstExprCall", Category_Expr, handleExprCall);
    registerNodeClass<Luau::AstExprIndexName>("AstExprIndexName", Category_Expr, handleExprIndexName);
    registerNodeClass<Luau::AstExprIndexExpr>("AstExprIndexExpr", Category_Expr, handleExprIndexExpr);
    registerNodeClass<Luau::AstExprFunction>("AstExprFunction", Category_Expr, handleExprFunction);
    registerNodeClass<Luau::AstExprTable>("AstExprTable", Category_Expr, handleExprTable);
    registerNodeClass<Luau::AstExprUnary>("AstExprUnary", Category_Expr, handleExprUnary);
    registerNodeClass<Luau::AstExprBinary>("AstExprBinary", Category_Expr, handleExprBinary);
    registerNodeClass<Luau::AstExprTypeAssertion>("AstExprTypeAssertion", Category_Expr, handleExprTypeAssertion);
    registerNodeClass<Luau::AstExprIfElse>("AstExprIfElse", Category_Expr, handleExprIfElse);
    registerNodeClass<Luau::AstExprInterpString>("AstExprInterpString", Category_Expr);
    registerNodeClass<Luau::AstExprInstantiate>("AstExprInstantiate", Category_Expr);
    registerNodeClass<Luau::AstExprError>("AstExprError", Category_Expr);

    // Types
    registerNodeClass<Luau::AstTypeReference>("AstTypeReference", Category_Type, handleTypeReference);
    registerNodeClass<Luau::AstTypeTable>("AstTypeTable", Category_Type);
    registerNodeClass<Luau::AstTypeFunction>("AstTypeFunction", Category_Type);
    registerNodeClass<Luau::AstTypeTypeof>("AstTypeTypeof", Category_Type);
    registerNodeClass<Luau::AstTypeOptional>("AstTypeOptional", Category_Type);
    registerNodeClass<Luau::AstTypeUnion>("AstTypeUnion", Category_Type);
    registerNodeClass<Luau::AstTypeIntersection>("AstTypeIntersection", Category_Type);
    registerNodeClass<Luau::AstTypeSingletonBool>("AstTypeSingletonBool", Category_Type);
    registerNodeClass<Luau::AstTypeSingletonString>("AstTypeSingletonString", Category_Type);
    registerNodeClass<Luau::AstTypeGroup>("AstTypeGroup", Category_Type);
    registerNodeClass<Luau::AstTypeError>("AstTypeError", Category_Type);

    // Type Packs
    registerNodeClass<Luau::AstTypePackExplicit>("AstTypePackExplicit", Category_TypePack);
    registerNodeClass<Luau::AstTypePackVariadic>("AstTypePackVariadic", Category_TypePack);
    registerNodeClass<Luau::AstTypePackGeneric>("AstTypePackGeneric", Category_TypePack);

    // Generics & Attributes
    registerNodeClass<Luau::AstGenericType>("AstGenericType", Category_Generic, handleGenericType);
    registerNodeClass<Luau::AstGenericTypePack>("AstGenericTypePack", Category_Generic, handleGenericTypePack);
    registerNodeClass<Luau::AstAttr>("AstAttr", Category_Attr);
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

static int astNodeIndex(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    size_t keyLen = 0;
    const char* keyStr = luaL_checklstring(L, 2, &keyLen);
    AstNodeAtom atom = getAstNodeAtom(std::string_view(keyStr, keyLen));
    Luau::AstNode* node = handle.node;
    auto& doc = handle.doc;
    int idx = node->classIndex;

    switch (atom)
    {
    case Atom_Children:
    {
        return pushUserdataMethod(L, TagNode, "children");
    }
    case Atom_Walk:
    {
        return pushUserdataMethod(L, TagNode, "walk");
    }
    case Atom_Text:
    {
        auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), node->location);
        lua_pushlstring(L, doc->source.data() + startOff, endOff - startOff);
        return 1;
    }
    case Atom_Kind:
    {
        lua_pushstring(L, getNodeKind(node));
        return 1;
    }
    case Atom_Category:
    {
        NodeCategory cat = (idx >= 0 && idx < int(s_nodeCategoryTable.size())) ? s_nodeCategoryTable[idx] : Category_Unknown;
        switch (cat)
        {
        case Category_Stat:     lua_pushstring(L, "stat"); break;
        case Category_Expr:     lua_pushstring(L, "expr"); break;
        case Category_Type:     lua_pushstring(L, "type"); break;
        case Category_TypePack: lua_pushstring(L, "typePack"); break;
        case Category_Generic:  lua_pushstring(L, "generic"); break;
        case Category_Attr:     lua_pushstring(L, "attr"); break;
        default:                lua_pushstring(L, "unknown"); break;
        }
        return 1;
    }
    case Atom_Location:
    {
        pushLocation(L, doc, node->location);
        return 1;
    }
    case Atom_HasSemicolon:
    {
        if (auto* stat = node->asStat())
            lua_pushboolean(L, stat->hasSemicolon);
        else
            lua_pushboolean(L, false);
        return 1;
    }
    case Atom_Cst:
    {
        if (const Luau::CstNode* const* cst = doc->parseResult.cstNodeMap.find(node))
            pushCstNode(L, doc, *cst);
        else
            lua_pushnil(L);
        return 1;
    }
    default:
        break;
    }

    if (idx >= 0 && idx < int(s_nodePropertyTable.size()) && s_nodePropertyTable[idx])
    {
        if (s_nodePropertyTable[idx](L, handle, atom))
            return 1;
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

void registerAstNode(lua_State* L)
{
    initializeDispatchTables();
    static const luaL_Reg s_nodeMethods[] = {
        {"children", astNodeChildren},
        {"walk", astNodeWalk},
        {nullptr, nullptr},
    };
    registerUserdataType(L, TagNode, "AstNode", astNodeDtor, astNodeIndex, astNodeToString, astNodeEq, s_nodeMethods);
}

} // namespace Luau
