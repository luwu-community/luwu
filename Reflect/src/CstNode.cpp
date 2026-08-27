// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{


void pushCstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::CstNode* node)
{
    if (!node)
    {
        lua_pushnil(L);
        return;
    }
    CstNodeData* data = static_cast<CstNodeData*>(lua_newuserdatataggedwithmetatable(L, sizeof(CstNodeData), TagCstNode));
    new (data) CstNodeData{doc, node};
}

CstNodeData& checkCstNode(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagCstNode)
        luaL_typeerrorL(L, idx, "CstNode");
    return *static_cast<CstNodeData*>(lua_touserdata(L, idx));
}

static void cstNodeDtor(lua_State* L, void* userdata)
{
    static_cast<CstNodeData*>(userdata)->~CstNodeData();
}

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

const char* getCstNodeKind(const Luau::CstNode* node)
{
    if (!node)
        return "nil";
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].kind)
        return s_cstClassTable[idx].kind;
    return "CstNode";
}

static bool handleCstAttrProps(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstAttr*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::HasAt: { lua_pushboolean(L, n->hasAt); return true; }
    default: return false;
    }
}

static bool handleCstParametrizedAttrMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstParametrizedAttr*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpenParenPosition:  { pushPosition(L, handle.doc, n->openParenPosition); return true; }
    case ReflectAtom::CloseParenPosition: { pushPosition(L, handle.doc, n->closeParenPosition); return true; }
    case ReflectAtom::ArgsCommaPositions: { pushPositionArray(L, handle.doc, n->argsCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstExprGroupMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprGroup*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
    default: return false;
    }
}

static bool handleCstExprConstantNumberMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantNumber*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    default: return false;
    }
}

static bool handleCstExprConstantIntegerMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantInteger*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    default: return false;
    }
}

static bool handleCstExprConstantStringProps(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantString*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleCstExprConstantStringMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantString*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::SourceString: { lua_pushlstring(L, n->sourceString.data, n->sourceString.size); return true; }
    default: return false;
    }
}

static bool handleCstExprCallMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprCall*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpenParens:      { pushPosition(L, handle.doc, n->openParens); return true; }
    case ReflectAtom::CloseParens:     { pushPosition(L, handle.doc, n->closeParens); return true; }
    case ReflectAtom::CommaPositions:  { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
    default: return false;
    }
}

static bool handleCstExprIndexExprMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprIndexExpr*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpenBracketPosition:  { pushPosition(L, handle.doc, n->openBracketPosition); return true; }
    case ReflectAtom::CloseBracketPosition: { pushPosition(L, handle.doc, n->closeBracketPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprFunctionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::FunctionKeywordPosition:         { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    case ReflectAtom::OpenGenericsPosition:            { pushPosition(L, handle.doc, n->openGenericsPosition); return true; }
    case ReflectAtom::GenericsCommaPositions:          { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case ReflectAtom::CloseGenericsPosition:           { pushPosition(L, handle.doc, n->closeGenericsPosition); return true; }
    case ReflectAtom::ArgsAnnotationColonPositions:    { pushPositionArray(L, handle.doc, n->argsAnnotationColonPositions); return true; }
    case ReflectAtom::ArgsCommaPositions:              { pushPositionArray(L, handle.doc, n->argsCommaPositions); return true; }
    case ReflectAtom::VarargAnnotationColonPosition:   { pushPosition(L, handle.doc, n->varargAnnotationColonPosition); return true; }
    case ReflectAtom::ReturnSpecifierPosition:         { pushPosition(L, handle.doc, n->returnSpecifierPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprTableMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprTable*>(handle.node);
    switch (atom)
    {
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
    default: return false;
    }
}

static bool handleCstExprOpMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprOp*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprTypeAssertionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprTypeAssertion*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprIfElseProps(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprIfElse*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::IsElseIf: { lua_pushboolean(L, n->isElseIf); return true; }
    default: return false;
    }
}

static bool handleCstExprIfElseMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprIfElse*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::ThenPosition: { pushPosition(L, handle.doc, n->thenPosition); return true; }
    case ReflectAtom::ElsePosition: { pushPosition(L, handle.doc, n->elsePosition); return true; }
    default: return false;
    }
}

static bool handleCstExprInterpStringMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstExprInterpString*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::CommaPositions: { pushPositionArray(L, handle.doc, n->stringPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatDoMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatDo*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::StatsStartPosition: { pushPosition(L, handle.doc, n->statsStartPosition); return true; }
    case ReflectAtom::EndPosition:        { pushPosition(L, handle.doc, n->endPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatRepeatMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatRepeat*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::UntilPosition: { pushPosition(L, handle.doc, n->untilPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatReturnMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatReturn*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::CommaPositions: { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatLocalMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatLocal*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::VarsAnnotationColonPositions: { pushPositionArray(L, handle.doc, n->varsAnnotationColonPositions); return true; }
    case ReflectAtom::VarsCommaPositions:           { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case ReflectAtom::ValuesCommaPositions:         { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatForMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatFor*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::AnnotationColonPosition: { pushPosition(L, handle.doc, n->annotationColonPosition); return true; }
    case ReflectAtom::EqualsPosition:          { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    case ReflectAtom::EndCommaPosition:        { pushPosition(L, handle.doc, n->endCommaPosition); return true; }
    case ReflectAtom::StepCommaPosition:       { pushPosition(L, handle.doc, n->stepCommaPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatForInMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatForIn*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::VarsAnnotationColonPositions: { pushPositionArray(L, handle.doc, n->varsAnnotationColonPositions); return true; }
    case ReflectAtom::VarsCommaPositions:           { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case ReflectAtom::ValuesCommaPositions:         { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatAssignMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatAssign*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::VarsCommaPositions:   { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case ReflectAtom::EqualsPosition:       { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    case ReflectAtom::ValuesCommaPositions: { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatCompoundAssignMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatCompoundAssign*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatFunctionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatLocalFunctionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatLocalFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::LocalKeywordPosition:    { pushPosition(L, handle.doc, n->localKeywordPosition); return true; }
    case ReflectAtom::FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    default: return false;
    }
}

static bool handleCstGenericTypeMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstGenericType*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::DefaultEqualsPosition: { pushPosition(L, handle.doc, n->defaultEqualsPosition); return true; }
    default: return false;
    }
}

static bool handleCstGenericTypePackMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstGenericTypePack*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::EllipsisPosition:      { pushPosition(L, handle.doc, n->ellipsisPosition); return true; }
    case ReflectAtom::DefaultEqualsPosition: { pushPosition(L, handle.doc, n->defaultEqualsPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatTypeAliasMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatTypeAlias*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::TypeKeywordPosition:    { pushPosition(L, handle.doc, n->typeKeywordPosition); return true; }
    case ReflectAtom::GenericsOpenPosition:   { pushPosition(L, handle.doc, n->genericsOpenPosition); return true; }
    case ReflectAtom::GenericsCommaPositions: { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case ReflectAtom::GenericsClosePosition:  { pushPosition(L, handle.doc, n->genericsClosePosition); return true; }
    case ReflectAtom::EqualsPosition:         { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatTypeFunctionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstStatTypeFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::TypeKeywordPosition:     { pushPosition(L, handle.doc, n->typeKeywordPosition); return true; }
    case ReflectAtom::FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeReferenceMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeReference*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::PrefixPointPosition:      { pushPosition(L, handle.doc, n->prefixPointPosition); return true; }
    case ReflectAtom::OpenParametersPosition:   { pushPosition(L, handle.doc, n->openParametersPosition); return true; }
    case ReflectAtom::ParametersCommaPositions: { pushPositionArray(L, handle.doc, n->parametersCommaPositions); return true; }
    case ReflectAtom::CloseParametersPosition:  { pushPosition(L, handle.doc, n->closeParametersPosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeTableProps(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeTable*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::IsArray: { lua_pushboolean(L, n->isArray); return true; }
    default: return false;
    }
}

static bool handleCstTypeFunctionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeFunction*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpenGenericsPosition:         { pushPosition(L, handle.doc, n->openGenericsPosition); return true; }
    case ReflectAtom::GenericsCommaPositions:       { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case ReflectAtom::CloseGenericsPosition:        { pushPosition(L, handle.doc, n->closeGenericsPosition); return true; }
    case ReflectAtom::OpenArgsPosition:             { pushPosition(L, handle.doc, n->openArgsPosition); return true; }
    case ReflectAtom::ArgumentNameColonPositions:   { pushPositionArray(L, handle.doc, n->argumentNameColonPositions); return true; }
    case ReflectAtom::ArgumentsCommaPositions:      { pushPositionArray(L, handle.doc, n->argumentsCommaPositions); return true; }
    case ReflectAtom::CloseArgsPosition:            { pushPosition(L, handle.doc, n->closeArgsPosition); return true; }
    case ReflectAtom::ReturnArrowPosition:          { pushPosition(L, handle.doc, n->returnArrowPosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeTypeofMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeTypeof*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpenPosition:  { pushPosition(L, handle.doc, n->openPosition); return true; }
    case ReflectAtom::ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeUnionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeUnion*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::LeadingPosition:    { pushPosition(L, handle.doc, n->leadingPosition); return true; }
    case ReflectAtom::SeparatorPositions: { pushPositionArray(L, handle.doc, n->separatorPositions); return true; }
    default: return false;
    }
}

static bool handleCstTypeIntersectionMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeIntersection*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::LeadingPosition:    { pushPosition(L, handle.doc, n->leadingPosition); return true; }
    case ReflectAtom::SeparatorPositions: { pushPositionArray(L, handle.doc, n->separatorPositions); return true; }
    default: return false;
    }
}

static bool handleCstTypeSingletonStringProps(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeSingletonString*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::BlockDepth: { lua_pushinteger(L, int(n->blockDepth)); return true; }
    default: return false;
    }
}

static bool handleCstTypeSingletonStringMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeSingletonString*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::SourceString: { lua_pushlstring(L, n->sourceString.data, n->sourceString.size); return true; }
    default: return false;
    }
}

static bool handleCstTypeGroupMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeGroup*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
    default: return false;
    }
}

static bool handleCstTypePackExplicitMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypePackExplicit*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::OpenParenthesesPosition:  { pushPosition(L, handle.doc, n->openParenthesesPosition); return true; }
    case ReflectAtom::CloseParenthesesPosition: { pushPosition(L, handle.doc, n->closeParenthesesPosition); return true; }
    case ReflectAtom::CommaPositions:           { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
    default: return false;
    }
}

static bool handleCstTypePackGenericMethods(lua_State* L, CstNodeData& handle, ReflectAtom atom)
{
    auto* n = static_cast<const Luau::CstTypePackGeneric*>(handle.node);
    switch (atom)
    {
    case ReflectAtom::EllipsisPosition: { pushPosition(L, handle.doc, n->ellipsisPosition); return true; }
    default: return false;
    }
}

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

static int cstNodeMethodTrampoline(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    size_t len = 0;
    const char* str = lua_tolstring(L, lua_upvalueindex(1), &len);
    ReflectAtom atom = resolveGlobalReflectAtom(std::string_view(str, len));
    int idx = handle.node ? handle.node->classIndex : -1;
    if (idx >= 0 && idx < int(s_cstClassTable.size()) && s_cstClassTable[idx].methodHandler)
    {
        if (s_cstClassTable[idx].methodHandler(L, handle, atom))
            return 1;
    }
    luaL_error(L, "%.*s is not a valid method of CstNode", int(len), str);
}

static int cstNodeIndex(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr);
    if (!keyStr)
    {
        lua_pushnil(L);
        return 1;
    }
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen);
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
    auto& handle = checkCstNode(L, 1);
    int atomId = -1;
    size_t len = 0;
    const char* str = lua_namecallwithlen(L, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr, &len);
    if (!str)
        luaL_error(L, "missing method name in namecall");

    ReflectAtom atom = resolveReflectAtom(atomId, str, len);
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
