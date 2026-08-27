// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{


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

typedef bool (*NodePropertyHandler)(lua_State* L, AstNodeData& handle, ReflectAtom atom);
typedef bool (*NodeMethodHandler)(lua_State* L, AstNodeData& handle, ReflectAtom atom);

struct AstNodeClassInfo
{
    const char* kind = nullptr;
    NodeCategory category = Category_Unknown;
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
        s_nodeClassTable.resize(idx + 1, AstNodeClassInfo{"AstNode", Category_Unknown, nullptr, nullptr});
    s_nodeClassTable[idx] = AstNodeClassInfo{kind, category, propHandler, methodHandler};
}

const char* getNodeKind(Luau::AstNode* node)
{
    if (!node)
        return "nil";
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].kind)
        return s_nodeClassTable[idx].kind;
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

static bool handleStatBlockProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatBlock*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::HasEnd: { lua_pushboolean(L, n->hasEnd); return true; }
    default: return false;
    }
}

static bool handleStatBlockMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatBlock*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Body: { pushNodeArray(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatIfMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatIf*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::ThenBody:  { pushAstNode(L, handle.doc, n->thenbody); return true; }
    case ReflectAtom::ElseBody:  { pushAstNode(L, handle.doc, n->elsebody); return true; }
    default: return false;
    }
}

static bool handleStatWhileProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatWhile*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::HasDo: { lua_pushboolean(L, n->hasDo); return true; }
    default: return false;
    }
}

static bool handleStatWhileMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatWhile*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::Body:      { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatRepeatMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatRepeat*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::Body:      { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatReturnMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatReturn*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::List: { pushNodeArray(L, handle.doc, n->list); return true; }
    default: return false;
    }
}

static bool handleStatExprMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatExpr*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleStatLocalProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatLocal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::IsConst:  { lua_pushboolean(L, n->isConst); return true; }
    case ReflectAtom::Exported: { lua_pushboolean(L, n->isExported); return true; }
    default: return false;
    }
}

static bool handleStatLocalMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatLocal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Vars:   { pushLocalArray(L, handle.doc, n->vars); return true; }
    case ReflectAtom::Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    default: return false;
    }
}

static bool handleStatForProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatFor*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::HasDo: { lua_pushboolean(L, n->hasDo); return true; }
    default: return false;
    }
}

static bool handleStatForMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatFor*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Var:  { pushAstLocal(L, handle.doc, n->var); return true; }
    case ReflectAtom::From: { pushAstNode(L, handle.doc, n->from); return true; }
    case ReflectAtom::To:   { pushAstNode(L, handle.doc, n->to); return true; }
    case ReflectAtom::Step: { pushAstNode(L, handle.doc, n->step); return true; }
    case ReflectAtom::Body: { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatForInProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatForIn*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::HasIn: { lua_pushboolean(L, n->hasIn); return true; }
    case ReflectAtom::HasDo: { lua_pushboolean(L, n->hasDo); return true; }
    default: return false;
    }
}

static bool handleStatForInMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatForIn*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Vars:   { pushLocalArray(L, handle.doc, n->vars); return true; }
    case ReflectAtom::Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    case ReflectAtom::Body:   { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatAssignMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatAssign*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Vars:   { pushNodeArray(L, handle.doc, n->vars); return true; }
    case ReflectAtom::Values: { pushNodeArray(L, handle.doc, n->values); return true; }
    default: return false;
    }
}

static bool handleStatCompoundAssignProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatCompoundAssign*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Op: { lua_pushstring(L, toString(n->op).c_str()); return true; }
    default: return false;
    }
}

static bool handleStatCompoundAssignMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatCompoundAssign*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Var:   { pushAstNode(L, handle.doc, n->var); return true; }
    case ReflectAtom::Value: { pushAstNode(L, handle.doc, n->value); return true; }
    default: return false;
    }
}

static bool handleStatFunctionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { pushAstNode(L, handle.doc, n->name); return true; }
    case ReflectAtom::Func: { pushAstNode(L, handle.doc, n->func); return true; }
    default: return false;
    }
}

static bool handleStatLocalFunctionProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatLocalFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::IsConst: { lua_pushboolean(L, n->isConst); return true; }
    default: return false;
    }
}

static bool handleStatLocalFunctionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatLocalFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { pushAstLocal(L, handle.doc, n->name); return true; }
    case ReflectAtom::Func: { pushAstNode(L, handle.doc, n->func); return true; }
    default: return false;
    }
}

static bool handleStatTypeAliasProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatTypeAlias*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name:     { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Exported: { lua_pushboolean(L, n->exported); return true; }
    default: return false;
    }
}

static bool handleStatTypeAliasMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatTypeAlias*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Type:         { pushAstNode(L, handle.doc, n->type); return true; }
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    default: return false;
    }
}

static bool handleStatTypeFunctionProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatTypeFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name:      { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Exported:  { lua_pushboolean(L, n->exported); return true; }
    case ReflectAtom::HasErrors: { lua_pushboolean(L, n->hasErrors); return true; }
    default: return false;
    }
}

static bool handleStatTypeFunctionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatTypeFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Body: { pushAstNode(L, handle.doc, n->body); return true; }
    default: return false;
    }
}

static bool handleStatDeclareGlobalProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatDeclareGlobal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    default: return false;
    }
}

static bool handleStatDeclareGlobalMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatDeclareGlobal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->type); return true; }
    default: return false;
    }
}

static bool handleStatDeclareFunctionProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatDeclareFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name:   { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::Vararg: { lua_pushboolean(L, n->vararg); return true; }
    default: return false;
    }
}

static bool handleStatDeclareFunctionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatDeclareFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case ReflectAtom::Params:       { pushNodeArray(L, handle.doc, n->params.types); return true; }
    case ReflectAtom::ReturnTypes:  { pushAstNode(L, handle.doc, n->retTypes); return true; }
    case ReflectAtom::Attributes:   { pushNodeArray(L, handle.doc, n->attributes); return true; }
    default: return false;
    }
}

static bool handleStatClassProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatClass*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Exported: { lua_pushboolean(L, n->exported); return true; }
    default: return false;
    }
}

static bool handleStatClassMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatClass*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { pushAstLocal(L, handle.doc, n->name); return true; }
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
    default: return false;
    }
}

static bool handleStatDeclareExternTypeProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatDeclareExternType*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    case ReflectAtom::SuperName:
    {
        if (n->superName)
            lua_pushstring(L, n->superName->value);
        else
            lua_pushnil(L);
        return true;
    }
    default: return false;
    }
}

static bool handleStatDeclareExternTypeMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatDeclareExternType*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleStatErrorProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatError*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::MessageIndex: { lua_pushinteger(L, n->messageIndex); return true; }
    default: return false;
    }
}

static bool handleStatErrorMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstStatError*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expressions: { pushNodeArray(L, handle.doc, n->expressions); return true; }
    case ReflectAtom::Statements:  { pushNodeArray(L, handle.doc, n->statements); return true; }
    default: return false;
    }
}

static bool handleExprGroupMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprGroup*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleExprConstantBoolProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantBool*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushboolean(L, n->value); return true; }
    default: return false;
    }
}

static bool handleExprConstantNumberProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantNumber*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushnumber(L, n->value); return true; }
    default: return false;
    }
}

static bool handleExprConstantIntegerProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantInteger*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushinteger64(L, n->value); return true; }
    default: return false;
    }
}

static bool handleExprConstantStringProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprConstantString*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleExprLocalProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprLocal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Upvalue: { lua_pushboolean(L, n->upvalue); return true; }
    default: return false;
    }
}

static bool handleExprLocalMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprLocal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Local: { pushAstLocal(L, handle.doc, n->local); return true; }
    default: return false;
    }
}

static bool handleExprGlobalProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprGlobal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    default: return false;
    }
}

static bool handleExprCallProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprCall*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Self: { lua_pushboolean(L, n->self); return true; }
    default: return false;
    }
}

static bool handleExprCallMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprCall*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Func:          { pushAstNode(L, handle.doc, n->func); return true; }
    case ReflectAtom::Args:          { pushNodeArray(L, handle.doc, n->args); return true; }
    case ReflectAtom::TypeArguments: { pushTypeOrPackArray(L, handle.doc, n->typeArguments); return true; }
    default: return false;
    }
}

static bool handleExprIndexNameProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprIndexName*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Index: { lua_pushstring(L, n->index.value); return true; }
    case ReflectAtom::Op:    { char s[2] = {n->op, '\0'}; lua_pushstring(L, s); return true; }
    default: return false;
    }
}

static bool handleExprIndexNameMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprIndexName*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleExprIndexExprMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprIndexExpr*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr:  { pushAstNode(L, handle.doc, n->expr); return true; }
    case ReflectAtom::Index: { pushAstNode(L, handle.doc, n->index); return true; }
    default: return false;
    }
}

static bool handleExprFunctionProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Vararg:    { lua_pushboolean(L, n->vararg); return true; }
    case ReflectAtom::DebugName: { lua_pushstring(L, n->debugname.value); return true; }
    default: return false;
    }
}

static bool handleExprFunctionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Args:             { pushLocalArray(L, handle.doc, n->args); return true; }
    case ReflectAtom::Body:             { pushAstNode(L, handle.doc, n->body); return true; }
    case ReflectAtom::Generics:         { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks:     { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case ReflectAtom::ReturnAnnotation: { pushAstNode(L, handle.doc, n->returnAnnotation); return true; }
    case ReflectAtom::Attributes:       { pushNodeArray(L, handle.doc, n->attributes); return true; }
    default: return false;
    }
}

static bool handleExprTableMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprTable*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleExprUnaryProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprUnary*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Op: { lua_pushstring(L, toString(n->op).c_str()); return true; }
    default: return false;
    }
}

static bool handleExprUnaryMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprUnary*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleExprBinaryProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprBinary*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Op: { lua_pushstring(L, toString(n->op).c_str()); return true; }
    default: return false;
    }
}

static bool handleExprBinaryMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprBinary*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Left:  { pushAstNode(L, handle.doc, n->left); return true; }
    case ReflectAtom::Right: { pushAstNode(L, handle.doc, n->right); return true; }
    default: return false;
    }
}

static bool handleExprTypeAssertionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprTypeAssertion*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr:       { pushAstNode(L, handle.doc, n->expr); return true; }
    case ReflectAtom::Annotation: { pushAstNode(L, handle.doc, n->annotation); return true; }
    default: return false;
    }
}

static bool handleExprIfElseProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprIfElse*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::HasElse: { lua_pushboolean(L, n->hasElse); return true; }
    default: return false;
    }
}

static bool handleExprIfElseMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprIfElse*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Condition: { pushAstNode(L, handle.doc, n->condition); return true; }
    case ReflectAtom::TrueExpr:  { pushAstNode(L, handle.doc, n->trueExpr); return true; }
    case ReflectAtom::FalseExpr: { pushAstNode(L, handle.doc, n->falseExpr); return true; }
    default: return false;
    }
}

static bool handleExprInterpStringProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprInterpString*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleExprInterpStringMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprInterpString*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expressions: { pushNodeArray(L, handle.doc, n->expressions); return true; }
    default: return false;
    }
}

static bool handleExprInstantiateMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprInstantiate*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr:          { pushAstNode(L, handle.doc, n->expr); return true; }
    case ReflectAtom::TypeArguments: { pushTypeOrPackArray(L, handle.doc, n->typeArguments); return true; }
    default: return false;
    }
}

static bool handleExprErrorProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprError*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::MessageIndex: { lua_pushinteger(L, n->messageIndex); return true; }
    default: return false;
    }
}

static bool handleExprErrorMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstExprError*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expressions: { pushNodeArray(L, handle.doc, n->expressions); return true; }
    default: return false;
    }
}

static bool handleTypeReferenceProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeReference*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleTypeReferenceMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeReference*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Parameters: { pushTypeOrPackArray(L, handle.doc, n->parameters); return true; }
    default: return false;
    }
}

static bool handleTypeTableMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeTable*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleTypeFunctionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Generics:     { pushNodeArray(L, handle.doc, n->generics); return true; }
    case ReflectAtom::GenericPacks: { pushNodeArray(L, handle.doc, n->genericPacks); return true; }
    case ReflectAtom::ArgTypes:     { pushNodeArray(L, handle.doc, n->argTypes.types); return true; }
    case ReflectAtom::ReturnTypes:  { pushAstNode(L, handle.doc, n->returnTypes); return true; }
    case ReflectAtom::Attributes:   { pushNodeArray(L, handle.doc, n->attributes); return true; }
    default: return false;
    }
}

static bool handleTypeTypeofMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeTypeof*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Expr: { pushAstNode(L, handle.doc, n->expr); return true; }
    default: return false;
    }
}

static bool handleTypeUnionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeUnion*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Types: { pushNodeArray(L, handle.doc, n->types); return true; }
    default: return false;
    }
}

static bool handleTypeIntersectionMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeIntersection*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Types: { pushNodeArray(L, handle.doc, n->types); return true; }
    default: return false;
    }
}

static bool handleTypeSingletonBoolProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeSingletonBool*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushboolean(L, n->value); return true; }
    default: return false;
    }
}

static bool handleTypeSingletonStringProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeSingletonString*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    default: return false;
    }
}

static bool handleTypeGroupMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeGroup*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->type); return true; }
    default: return false;
    }
}

static bool handleTypeErrorProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeError*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::IsMissing:    { lua_pushboolean(L, n->isMissing); return true; }
    case ReflectAtom::MessageIndex: { lua_pushinteger(L, n->messageIndex); return true; }
    default: return false;
    }
}

static bool handleTypeErrorMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypeError*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Types: { pushNodeArray(L, handle.doc, n->types); return true; }
    default: return false;
    }
}

static bool handleTypePackExplicitMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypePackExplicit*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Types:    { pushNodeArray(L, handle.doc, n->typeList.types); return true; }
    case ReflectAtom::TailType: { pushAstNode(L, handle.doc, n->typeList.tailType); return true; }
    default: return false;
    }
}

static bool handleTypePackVariadicMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypePackVariadic*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::VariadicType: { pushAstNode(L, handle.doc, n->variadicType); return true; }
    default: return false;
    }
}

static bool handleTypePackGenericProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstTypePackGeneric*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { lua_pushstring(L, n->genericName.value); return true; }
    default: return false;
    }
}

static bool handleGenericTypeProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstGenericType*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    default: return false;
    }
}

static bool handleGenericTypeMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstGenericType*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->defaultValue); return true; }
    default: return false;
    }
}

static bool handleGenericTypePackProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstGenericTypePack*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Name: { lua_pushstring(L, n->name.value); return true; }
    default: return false;
    }
}

static bool handleGenericTypePackMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstGenericTypePack*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Type: { pushAstNode(L, handle.doc, n->defaultValue); return true; }
    default: return false;
    }
}

static bool handleAttrProps(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstAttr*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleAttrMethods(lua_State* L, AstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<Luau::AstAttr*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Args: { pushNodeArray(L, handle.doc, n->args); return true; }
    default: return false;
    }
}

static void initializeDispatchTables()
{
    // SAFETY: c++ guarantees thread safety in static inits like this (see https://iamroman.org/blog/2017/04/cpp11-static-init/) from c++11
    static const bool initialized = []() {
        // Statements
        registerNodeClass<Luau::AstStatBlock>("AstStatBlock", Category_Stat, handleStatBlockProps, handleStatBlockMethods);
        registerNodeClass<Luau::AstStatIf>("AstStatIf", Category_Stat, nullptr, handleStatIfMethods);
        registerNodeClass<Luau::AstStatWhile>("AstStatWhile", Category_Stat, handleStatWhileProps, handleStatWhileMethods);
        registerNodeClass<Luau::AstStatRepeat>("AstStatRepeat", Category_Stat, nullptr, handleStatRepeatMethods);
        registerNodeClass<Luau::AstStatBreak>("AstStatBreak", Category_Stat);
        registerNodeClass<Luau::AstStatContinue>("AstStatContinue", Category_Stat);
        registerNodeClass<Luau::AstStatReturn>("AstStatReturn", Category_Stat, nullptr, handleStatReturnMethods);
        registerNodeClass<Luau::AstStatExpr>("AstStatExpr", Category_Stat, nullptr, handleStatExprMethods);
        registerNodeClass<Luau::AstStatLocal>("AstStatLocal", Category_Stat, handleStatLocalProps, handleStatLocalMethods);
        registerNodeClass<Luau::AstStatFor>("AstStatFor", Category_Stat, handleStatForProps, handleStatForMethods);
        registerNodeClass<Luau::AstStatForIn>("AstStatForIn", Category_Stat, handleStatForInProps, handleStatForInMethods);
        registerNodeClass<Luau::AstStatAssign>("AstStatAssign", Category_Stat, nullptr, handleStatAssignMethods);
        registerNodeClass<Luau::AstStatCompoundAssign>("AstStatCompoundAssign", Category_Stat, handleStatCompoundAssignProps, handleStatCompoundAssignMethods);
        registerNodeClass<Luau::AstStatFunction>("AstStatFunction", Category_Stat, nullptr, handleStatFunctionMethods);
        registerNodeClass<Luau::AstStatLocalFunction>("AstStatLocalFunction", Category_Stat, handleStatLocalFunctionProps, handleStatLocalFunctionMethods);
        registerNodeClass<Luau::AstStatTypeAlias>("AstStatTypeAlias", Category_Stat, handleStatTypeAliasProps, handleStatTypeAliasMethods);
        registerNodeClass<Luau::AstStatTypeFunction>("AstStatTypeFunction", Category_Stat, handleStatTypeFunctionProps, handleStatTypeFunctionMethods);
        registerNodeClass<Luau::AstStatDeclareGlobal>("AstStatDeclareGlobal", Category_Stat, handleStatDeclareGlobalProps, handleStatDeclareGlobalMethods);
        registerNodeClass<Luau::AstStatDeclareFunction>("AstStatDeclareFunction", Category_Stat, handleStatDeclareFunctionProps, handleStatDeclareFunctionMethods);
        registerNodeClass<Luau::AstStatClass>("AstStatClass", Category_Stat, handleStatClassProps, handleStatClassMethods);
        registerNodeClass<Luau::AstStatDeclareExternType>("AstStatDeclareExternType", Category_Stat, handleStatDeclareExternTypeProps, handleStatDeclareExternTypeMethods);
        registerNodeClass<Luau::AstStatError>("AstStatError", Category_Stat, handleStatErrorProps, handleStatErrorMethods);

        // Expressions
        registerNodeClass<Luau::AstExprGroup>("AstExprGroup", Category_Expr, nullptr, handleExprGroupMethods);
        registerNodeClass<Luau::AstExprConstantNil>("AstExprConstantNil", Category_Expr);
        registerNodeClass<Luau::AstExprConstantBool>("AstExprConstantBool", Category_Expr, handleExprConstantBoolProps);
        registerNodeClass<Luau::AstExprConstantNumber>("AstExprConstantNumber", Category_Expr, handleExprConstantNumberProps);
        registerNodeClass<Luau::AstExprConstantInteger>("AstExprConstantInteger", Category_Expr, handleExprConstantIntegerProps);
        registerNodeClass<Luau::AstExprConstantString>("AstExprConstantString", Category_Expr, handleExprConstantStringProps);
        registerNodeClass<Luau::AstExprLocal>("AstExprLocal", Category_Expr, handleExprLocalProps, handleExprLocalMethods);
        registerNodeClass<Luau::AstExprGlobal>("AstExprGlobal", Category_Expr, handleExprGlobalProps);
        registerNodeClass<Luau::AstExprVarargs>("AstExprVarargs", Category_Expr);
        registerNodeClass<Luau::AstExprCall>("AstExprCall", Category_Expr, handleExprCallProps, handleExprCallMethods);
        registerNodeClass<Luau::AstExprIndexName>("AstExprIndexName", Category_Expr, handleExprIndexNameProps, handleExprIndexNameMethods);
        registerNodeClass<Luau::AstExprIndexExpr>("AstExprIndexExpr", Category_Expr, nullptr, handleExprIndexExprMethods);
        registerNodeClass<Luau::AstExprFunction>("AstExprFunction", Category_Expr, handleExprFunctionProps, handleExprFunctionMethods);
        registerNodeClass<Luau::AstExprTable>("AstExprTable", Category_Expr, nullptr, handleExprTableMethods);
        registerNodeClass<Luau::AstExprUnary>("AstExprUnary", Category_Expr, handleExprUnaryProps, handleExprUnaryMethods);
        registerNodeClass<Luau::AstExprBinary>("AstExprBinary", Category_Expr, handleExprBinaryProps, handleExprBinaryMethods);
        registerNodeClass<Luau::AstExprTypeAssertion>("AstExprTypeAssertion", Category_Expr, nullptr, handleExprTypeAssertionMethods);
        registerNodeClass<Luau::AstExprIfElse>("AstExprIfElse", Category_Expr, handleExprIfElseProps, handleExprIfElseMethods);
        registerNodeClass<Luau::AstExprInterpString>("AstExprInterpString", Category_Expr, handleExprInterpStringProps, handleExprInterpStringMethods);
        registerNodeClass<Luau::AstExprInstantiate>("AstExprInstantiate", Category_Expr, nullptr, handleExprInstantiateMethods);
        registerNodeClass<Luau::AstExprError>("AstExprError", Category_Expr, handleExprErrorProps, handleExprErrorMethods);

        // Types
        registerNodeClass<Luau::AstTypeReference>("AstTypeReference", Category_Type, handleTypeReferenceProps, handleTypeReferenceMethods);
        registerNodeClass<Luau::AstTypeTable>("AstTypeTable", Category_Type, nullptr, handleTypeTableMethods);
        registerNodeClass<Luau::AstTypeFunction>("AstTypeFunction", Category_Type, nullptr, handleTypeFunctionMethods);
        registerNodeClass<Luau::AstTypeTypeof>("AstTypeTypeof", Category_Type, nullptr, handleTypeTypeofMethods);
        registerNodeClass<Luau::AstTypeOptional>("AstTypeOptional", Category_Type);
        registerNodeClass<Luau::AstTypeUnion>("AstTypeUnion", Category_Type, nullptr, handleTypeUnionMethods);
        registerNodeClass<Luau::AstTypeIntersection>("AstTypeIntersection", Category_Type, nullptr, handleTypeIntersectionMethods);
        registerNodeClass<Luau::AstTypeSingletonBool>("AstTypeSingletonBool", Category_Type, handleTypeSingletonBoolProps);
        registerNodeClass<Luau::AstTypeSingletonString>("AstTypeSingletonString", Category_Type, handleTypeSingletonStringProps);
        registerNodeClass<Luau::AstTypeGroup>("AstTypeGroup", Category_Type, nullptr, handleTypeGroupMethods);
        registerNodeClass<Luau::AstTypeError>("AstTypeError", Category_Type, handleTypeErrorProps, handleTypeErrorMethods);

        // Type Packs
        registerNodeClass<Luau::AstTypePackExplicit>("AstTypePackExplicit", Category_TypePack, nullptr, handleTypePackExplicitMethods);
        registerNodeClass<Luau::AstTypePackVariadic>("AstTypePackVariadic", Category_TypePack, nullptr, handleTypePackVariadicMethods);
        registerNodeClass<Luau::AstTypePackGeneric>("AstTypePackGeneric", Category_TypePack, handleTypePackGenericProps);

        // Generics & Attributes
        registerNodeClass<Luau::AstGenericType>("AstGenericType", Category_Generic, handleGenericTypeProps, handleGenericTypeMethods);
        registerNodeClass<Luau::AstGenericTypePack>("AstGenericTypePack", Category_Generic, handleGenericTypePackProps, handleGenericTypePackMethods);
        registerNodeClass<Luau::AstAttr>("AstAttr", Category_Attr, handleAttrProps, handleAttrMethods);
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

static int astNodeMethodTrampoline(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    size_t len = 0;
    const char* str = lua_tolstring(L, lua_upvalueindex(1), &len);
    ReflectAtom atom = resolveGlobalReflectAtom(std::string_view(str, len));
    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_nodeClassTable.size()) && s_nodeClassTable[idx].methodHandler)
    {
        if (s_nodeClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }
    luaL_error(L, "%.*s is not a valid method of AstNode", int(len), str);
}

static int astNodeIndex(lua_State* L)
{
    auto& handle = checkAstNode(L, 1);
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr);
    if (!keyStr)
    {
        lua_pushnil(L);
        return 1;
    }
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen);
    Luau::AstNode* node = handle.node;
    auto& doc = handle.doc;
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
        NodeCategory cat = (idx >= 0 && idx < int(s_nodeClassTable.size())) ? s_nodeClassTable[idx].category : Category_Unknown;
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
