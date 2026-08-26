// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

// Because the embedder may have themselves set useratom, we cannot use Luau's builtin atom system here, instead define the atoms separately using a hashmap + enum
enum CstNodeAtom : uint8_t
{
    Atom_Unknown = 0,
    Atom_Kind,
    Atom_HasAt,
    Atom_OpenParenPosition,
    Atom_CloseParenPosition,
    Atom_ArgsCommaPositions,
    Atom_ClosePosition,
    Atom_Value,
    Atom_QuoteStyle,
    Atom_SourceString,
    Atom_BlockDepth,
    Atom_OpenParens,
    Atom_CloseParens,
    Atom_CommaPositions,
    Atom_OpenBracketPosition,
    Atom_CloseBracketPosition,
    Atom_FunctionKeywordPosition,
    Atom_OpenGenericsPosition,
    Atom_GenericsCommaPositions,
    Atom_CloseGenericsPosition,
    Atom_ArgsAnnotationColonPositions,
    Atom_VarargAnnotationColonPosition,
    Atom_ReturnSpecifierPosition,
    Atom_Items,
    Atom_OpPosition,
    Atom_ThenPosition,
    Atom_ElsePosition,
    Atom_IsElseIf,
    Atom_StatsStartPosition,
    Atom_EndPosition,
    Atom_UntilPosition,
    Atom_VarsAnnotationColonPositions,
    Atom_VarsCommaPositions,
    Atom_ValuesCommaPositions,
    Atom_AnnotationColonPosition,
    Atom_EqualsPosition,
    Atom_EndCommaPosition,
    Atom_StepCommaPosition,
    Atom_LocalKeywordPosition,
    Atom_DefaultEqualsPosition,
    Atom_EllipsisPosition,
    Atom_TypeKeywordPosition,
    Atom_GenericsOpenPosition,
    Atom_GenericsClosePosition,
    Atom_PrefixPointPosition,
    Atom_OpenParametersPosition,
    Atom_ParametersCommaPositions,
    Atom_CloseParametersPosition,
    Atom_IsArray,
    Atom_OpenArgsPosition,
    Atom_ArgumentNameColonPositions,
    Atom_ArgumentsCommaPositions,
    Atom_CloseArgsPosition,
    Atom_ReturnArrowPosition,
    Atom_OpenPosition,
    Atom_LeadingPosition,
    Atom_SeparatorPositions,
    Atom_OpenParenthesesPosition,
    Atom_CloseParenthesesPosition,
};

static CstNodeAtom getCstNodeAtom(std::string_view key)
{
    static const std::unordered_map<std::string_view, CstNodeAtom> s_atomMap = {
        {"kind", Atom_Kind},
        {"hasAt", Atom_HasAt},
        {"openParenPosition", Atom_OpenParenPosition},
        {"closeParenPosition", Atom_CloseParenPosition},
        {"argsCommaPositions", Atom_ArgsCommaPositions},
        {"closePosition", Atom_ClosePosition},
        {"value", Atom_Value},
        {"quoteStyle", Atom_QuoteStyle},
        {"sourceString", Atom_SourceString},
        {"blockDepth", Atom_BlockDepth},
        {"openParens", Atom_OpenParens},
        {"closeParens", Atom_CloseParens},
        {"commaPositions", Atom_CommaPositions},
        {"openBracketPosition", Atom_OpenBracketPosition},
        {"closeBracketPosition", Atom_CloseBracketPosition},
        {"functionKeywordPosition", Atom_FunctionKeywordPosition},
        {"openGenericsPosition", Atom_OpenGenericsPosition},
        {"genericsCommaPositions", Atom_GenericsCommaPositions},
        {"closeGenericsPosition", Atom_CloseGenericsPosition},
        {"argsAnnotationColonPositions", Atom_ArgsAnnotationColonPositions},
        {"varargAnnotationColonPosition", Atom_VarargAnnotationColonPosition},
        {"returnSpecifierPosition", Atom_ReturnSpecifierPosition},
        {"items", Atom_Items},
        {"opPosition", Atom_OpPosition},
        {"thenPosition", Atom_ThenPosition},
        {"elsePosition", Atom_ElsePosition},
        {"isElseIf", Atom_IsElseIf},
        {"statsStartPosition", Atom_StatsStartPosition},
        {"endPosition", Atom_EndPosition},
        {"untilPosition", Atom_UntilPosition},
        {"varsAnnotationColonPositions", Atom_VarsAnnotationColonPositions},
        {"varsCommaPositions", Atom_VarsCommaPositions},
        {"valuesCommaPositions", Atom_ValuesCommaPositions},
        {"annotationColonPosition", Atom_AnnotationColonPosition},
        {"equalsPosition", Atom_EqualsPosition},
        {"endCommaPosition", Atom_EndCommaPosition},
        {"stepCommaPosition", Atom_StepCommaPosition},
        {"localKeywordPosition", Atom_LocalKeywordPosition},
        {"defaultEqualsPosition", Atom_DefaultEqualsPosition},
        {"ellipsisPosition", Atom_EllipsisPosition},
        {"typeKeywordPosition", Atom_TypeKeywordPosition},
        {"genericsOpenPosition", Atom_GenericsOpenPosition},
        {"genericsClosePosition", Atom_GenericsClosePosition},
        {"prefixPointPosition", Atom_PrefixPointPosition},
        {"openParametersPosition", Atom_OpenParametersPosition},
        {"parametersCommaPositions", Atom_ParametersCommaPositions},
        {"closeParametersPosition", Atom_CloseParametersPosition},
        {"isArray", Atom_IsArray},
        {"openArgsPosition", Atom_OpenArgsPosition},
        {"argumentNameColonPositions", Atom_ArgumentNameColonPositions},
        {"argumentsCommaPositions", Atom_ArgumentsCommaPositions},
        {"closeArgsPosition", Atom_CloseArgsPosition},
        {"returnArrowPosition", Atom_ReturnArrowPosition},
        {"openPosition", Atom_OpenPosition},
        {"leadingPosition", Atom_LeadingPosition},
        {"separatorPositions", Atom_SeparatorPositions},
        {"openParenthesesPosition", Atom_OpenParenthesesPosition},
        {"closeParenthesesPosition", Atom_CloseParenthesesPosition},
    };

    if (auto it = s_atomMap.find(key); it != s_atomMap.end())
        return it->second;

    return Atom_Unknown;
}

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

typedef bool (*CstNodePropertyHandler)(lua_State* L, CstNodeData& handle, CstNodeAtom atom);

static std::vector<const char*> s_cstKindTable;
static std::vector<CstNodePropertyHandler> s_cstPropertyTable;

template<typename T>
static void registerCstNodeClass(const char* kind, CstNodePropertyHandler handler = nullptr)
{
    int idx = T::CstClassIndex();
    s_cstKindTable[idx] = kind;
    s_cstPropertyTable[idx] = handler;
}

const char* getCstNodeKind(const Luau::CstNode* node)
{
    if (!node)
        return "nil";
    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_cstKindTable.size()) && s_cstKindTable[idx])
        return s_cstKindTable[idx];
    return "CstNode";
}

static bool handleCstAttr(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstAttr*>(handle.node);
    switch (atom)
    {
    case Atom_HasAt: { lua_pushboolean(L, n->hasAt); return true; }
    default: return false;
    }
}

static bool handleCstParametrizedAttr(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstParametrizedAttr*>(handle.node);
    switch (atom)
    {
    case Atom_OpenParenPosition:  { pushPosition(L, handle.doc, n->openParenPosition); return true; }
    case Atom_CloseParenPosition: { pushPosition(L, handle.doc, n->closeParenPosition); return true; }
    case Atom_ArgsCommaPositions: { pushPositionArray(L, handle.doc, n->argsCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstExprGroup(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprGroup*>(handle.node);
    switch (atom)
    {
    case Atom_ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
    default: return false;
    }
}

static bool handleCstExprConstantNumber(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantNumber*>(handle.node);
    switch (atom)
    {
    case Atom_Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    default: return false;
    }
}

static bool handleCstExprConstantInteger(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantInteger*>(handle.node);
    switch (atom)
    {
    case Atom_Value: { lua_pushlstring(L, n->value.data, n->value.size); return true; }
    default: return false;
    }
}

static bool handleCstExprConstantString(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprConstantString*>(handle.node);
    switch (atom)
    {
    case Atom_QuoteStyle:
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
    case Atom_SourceString: { lua_pushlstring(L, n->sourceString.data, n->sourceString.size); return true; }
    case Atom_BlockDepth:   { lua_pushinteger(L, int(n->blockDepth)); return true; }
    default: return false;
    }
}

static bool handleCstExprCall(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprCall*>(handle.node);
    switch (atom)
    {
    case Atom_OpenParens:      { pushPosition(L, handle.doc, n->openParens); return true; }
    case Atom_CloseParens:     { pushPosition(L, handle.doc, n->closeParens); return true; }
    case Atom_CommaPositions:  { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
    default: return false;
    }
}

static bool handleCstExprIndexExpr(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprIndexExpr*>(handle.node);
    switch (atom)
    {
    case Atom_OpenBracketPosition:  { pushPosition(L, handle.doc, n->openBracketPosition); return true; }
    case Atom_CloseBracketPosition: { pushPosition(L, handle.doc, n->closeBracketPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprFunction(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprFunction*>(handle.node);
    switch (atom)
    {
    case Atom_FunctionKeywordPosition:         { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    case Atom_OpenGenericsPosition:            { pushPosition(L, handle.doc, n->openGenericsPosition); return true; }
    case Atom_GenericsCommaPositions:          { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case Atom_CloseGenericsPosition:           { pushPosition(L, handle.doc, n->closeGenericsPosition); return true; }
    case Atom_ArgsAnnotationColonPositions:    { pushPositionArray(L, handle.doc, n->argsAnnotationColonPositions); return true; }
    case Atom_ArgsCommaPositions:              { pushPositionArray(L, handle.doc, n->argsCommaPositions); return true; }
    case Atom_VarargAnnotationColonPosition:   { pushPosition(L, handle.doc, n->varargAnnotationColonPosition); return true; }
    case Atom_ReturnSpecifierPosition:         { pushPosition(L, handle.doc, n->returnSpecifierPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprTable(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprTable*>(handle.node);
    switch (atom)
    {
    case Atom_Items:
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

static bool handleCstExprOp(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprOp*>(handle.node);
    switch (atom)
    {
    case Atom_OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprTypeAssertion(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprTypeAssertion*>(handle.node);
    switch (atom)
    {
    case Atom_OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
    default: return false;
    }
}

static bool handleCstExprIfElse(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprIfElse*>(handle.node);
    switch (atom)
    {
    case Atom_ThenPosition: { pushPosition(L, handle.doc, n->thenPosition); return true; }
    case Atom_ElsePosition: { pushPosition(L, handle.doc, n->elsePosition); return true; }
    case Atom_IsElseIf:     { lua_pushboolean(L, n->isElseIf); return true; }
    default: return false;
    }
}

static bool handleCstExprInterpString(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstExprInterpString*>(handle.node);
    switch (atom)
    {
    case Atom_CommaPositions: { pushPositionArray(L, handle.doc, n->stringPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatDo(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatDo*>(handle.node);
    switch (atom)
    {
    case Atom_StatsStartPosition: { pushPosition(L, handle.doc, n->statsStartPosition); return true; }
    case Atom_EndPosition:        { pushPosition(L, handle.doc, n->endPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatRepeat(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatRepeat*>(handle.node);
    switch (atom)
    {
    case Atom_UntilPosition: { pushPosition(L, handle.doc, n->untilPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatReturn(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatReturn*>(handle.node);
    switch (atom)
    {
    case Atom_CommaPositions: { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatLocal(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatLocal*>(handle.node);
    switch (atom)
    {
    case Atom_VarsAnnotationColonPositions: { pushPositionArray(L, handle.doc, n->varsAnnotationColonPositions); return true; }
    case Atom_VarsCommaPositions:           { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case Atom_ValuesCommaPositions:         { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatFor(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatFor*>(handle.node);
    switch (atom)
    {
    case Atom_AnnotationColonPosition: { pushPosition(L, handle.doc, n->annotationColonPosition); return true; }
    case Atom_EqualsPosition:          { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    case Atom_EndCommaPosition:        { pushPosition(L, handle.doc, n->endCommaPosition); return true; }
    case Atom_StepCommaPosition:       { pushPosition(L, handle.doc, n->stepCommaPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatForIn(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatForIn*>(handle.node);
    switch (atom)
    {
    case Atom_VarsAnnotationColonPositions: { pushPositionArray(L, handle.doc, n->varsAnnotationColonPositions); return true; }
    case Atom_VarsCommaPositions:           { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case Atom_ValuesCommaPositions:         { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatAssign(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatAssign*>(handle.node);
    switch (atom)
    {
    case Atom_VarsCommaPositions:   { pushPositionArray(L, handle.doc, n->varsCommaPositions); return true; }
    case Atom_EqualsPosition:       { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    case Atom_ValuesCommaPositions: { pushPositionArray(L, handle.doc, n->valuesCommaPositions); return true; }
    default: return false;
    }
}

static bool handleCstStatCompoundAssign(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatCompoundAssign*>(handle.node);
    switch (atom)
    {
    case Atom_OpPosition: { pushPosition(L, handle.doc, n->opPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatFunction(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatFunction*>(handle.node);
    switch (atom)
    {
    case Atom_FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatLocalFunction(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatLocalFunction*>(handle.node);
    switch (atom)
    {
    case Atom_LocalKeywordPosition:    { pushPosition(L, handle.doc, n->localKeywordPosition); return true; }
    case Atom_FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    default: return false;
    }
}

static bool handleCstGenericType(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstGenericType*>(handle.node);
    switch (atom)
    {
    case Atom_DefaultEqualsPosition: { pushPosition(L, handle.doc, n->defaultEqualsPosition); return true; }
    default: return false;
    }
}

static bool handleCstGenericTypePack(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstGenericTypePack*>(handle.node);
    switch (atom)
    {
    case Atom_EllipsisPosition:      { pushPosition(L, handle.doc, n->ellipsisPosition); return true; }
    case Atom_DefaultEqualsPosition: { pushPosition(L, handle.doc, n->defaultEqualsPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatTypeAlias(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatTypeAlias*>(handle.node);
    switch (atom)
    {
    case Atom_TypeKeywordPosition:    { pushPosition(L, handle.doc, n->typeKeywordPosition); return true; }
    case Atom_GenericsOpenPosition:   { pushPosition(L, handle.doc, n->genericsOpenPosition); return true; }
    case Atom_GenericsCommaPositions: { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case Atom_GenericsClosePosition:  { pushPosition(L, handle.doc, n->genericsClosePosition); return true; }
    case Atom_EqualsPosition:         { pushPosition(L, handle.doc, n->equalsPosition); return true; }
    default: return false;
    }
}

static bool handleCstStatTypeFunction(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstStatTypeFunction*>(handle.node);
    switch (atom)
    {
    case Atom_TypeKeywordPosition:     { pushPosition(L, handle.doc, n->typeKeywordPosition); return true; }
    case Atom_FunctionKeywordPosition: { pushPosition(L, handle.doc, n->functionKeywordPosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeReference(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeReference*>(handle.node);
    switch (atom)
    {
    case Atom_PrefixPointPosition:      { pushPosition(L, handle.doc, n->prefixPointPosition); return true; }
    case Atom_OpenParametersPosition:   { pushPosition(L, handle.doc, n->openParametersPosition); return true; }
    case Atom_ParametersCommaPositions: { pushPositionArray(L, handle.doc, n->parametersCommaPositions); return true; }
    case Atom_CloseParametersPosition:  { pushPosition(L, handle.doc, n->closeParametersPosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeTable(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeTable*>(handle.node);
    switch (atom)
    {
    case Atom_IsArray: { lua_pushboolean(L, n->isArray); return true; }
    default: return false;
    }
}

static bool handleCstTypeFunction(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeFunction*>(handle.node);
    switch (atom)
    {
    case Atom_OpenGenericsPosition:         { pushPosition(L, handle.doc, n->openGenericsPosition); return true; }
    case Atom_GenericsCommaPositions:       { pushPositionArray(L, handle.doc, n->genericsCommaPositions); return true; }
    case Atom_CloseGenericsPosition:        { pushPosition(L, handle.doc, n->closeGenericsPosition); return true; }
    case Atom_OpenArgsPosition:             { pushPosition(L, handle.doc, n->openArgsPosition); return true; }
    case Atom_ArgumentNameColonPositions:   { pushPositionArray(L, handle.doc, n->argumentNameColonPositions); return true; }
    case Atom_ArgumentsCommaPositions:      { pushPositionArray(L, handle.doc, n->argumentsCommaPositions); return true; }
    case Atom_CloseArgsPosition:            { pushPosition(L, handle.doc, n->closeArgsPosition); return true; }
    case Atom_ReturnArrowPosition:          { pushPosition(L, handle.doc, n->returnArrowPosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeTypeof(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeTypeof*>(handle.node);
    switch (atom)
    {
    case Atom_OpenPosition:  { pushPosition(L, handle.doc, n->openPosition); return true; }
    case Atom_ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
    default: return false;
    }
}

static bool handleCstTypeUnion(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeUnion*>(handle.node);
    switch (atom)
    {
    case Atom_LeadingPosition:    { pushPosition(L, handle.doc, n->leadingPosition); return true; }
    case Atom_SeparatorPositions: { pushPositionArray(L, handle.doc, n->separatorPositions); return true; }
    default: return false;
    }
}

static bool handleCstTypeIntersection(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeIntersection*>(handle.node);
    switch (atom)
    {
    case Atom_LeadingPosition:    { pushPosition(L, handle.doc, n->leadingPosition); return true; }
    case Atom_SeparatorPositions: { pushPositionArray(L, handle.doc, n->separatorPositions); return true; }
    default: return false;
    }
}

static bool handleCstTypeSingletonString(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeSingletonString*>(handle.node);
    switch (atom)
    {
    case Atom_SourceString: { lua_pushlstring(L, n->sourceString.data, n->sourceString.size); return true; }
    case Atom_BlockDepth:   { lua_pushinteger(L, int(n->blockDepth)); return true; }
    default: return false;
    }
}

static bool handleCstTypeGroup(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypeGroup*>(handle.node);
    switch (atom)
    {
    case Atom_ClosePosition: { pushPosition(L, handle.doc, n->closePosition); return true; }
    default: return false;
    }
}

static bool handleCstTypePackExplicit(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypePackExplicit*>(handle.node);
    switch (atom)
    {
    case Atom_OpenParenthesesPosition:  { pushPosition(L, handle.doc, n->openParenthesesPosition); return true; }
    case Atom_CloseParenthesesPosition: { pushPosition(L, handle.doc, n->closeParenthesesPosition); return true; }
    case Atom_CommaPositions:           { pushPositionArray(L, handle.doc, n->commaPositions); return true; }
    default: return false;
    }
}

static bool handleCstTypePackGeneric(lua_State* L, CstNodeData& handle, CstNodeAtom atom)
{
    auto* n = static_cast<const Luau::CstTypePackGeneric*>(handle.node);
    switch (atom)
    {
    case Atom_EllipsisPosition: { pushPosition(L, handle.doc, n->ellipsisPosition); return true; }
    default: return false;
    }
}

static void initializeCstDispatchTables()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    s_cstKindTable.assign(gCstRttiIndex + 1, "CstNode");
    s_cstPropertyTable.assign(gCstRttiIndex + 1, nullptr);

    registerCstNodeClass<Luau::CstAttr>("CstAttr", handleCstAttr);
    registerCstNodeClass<Luau::CstParametrizedAttr>("CstParametrizedAttr", handleCstParametrizedAttr);
    registerCstNodeClass<Luau::CstExprGroup>("CstExprGroup", handleCstExprGroup);
    registerCstNodeClass<Luau::CstExprConstantNumber>("CstExprConstantNumber", handleCstExprConstantNumber);
    registerCstNodeClass<Luau::CstExprConstantInteger>("CstExprConstantInteger", handleCstExprConstantInteger);
    registerCstNodeClass<Luau::CstExprConstantString>("CstExprConstantString", handleCstExprConstantString);
    registerCstNodeClass<Luau::CstExprCall>("CstExprCall", handleCstExprCall);
    registerCstNodeClass<Luau::CstExprIndexExpr>("CstExprIndexExpr", handleCstExprIndexExpr);
    registerCstNodeClass<Luau::CstExprFunction>("CstExprFunction", handleCstExprFunction);
    registerCstNodeClass<Luau::CstExprTable>("CstExprTable", handleCstExprTable);
    registerCstNodeClass<Luau::CstExprOp>("CstExprOp", handleCstExprOp);
    registerCstNodeClass<Luau::CstExprTypeAssertion>("CstExprTypeAssertion", handleCstExprTypeAssertion);
    registerCstNodeClass<Luau::CstExprIfElse>("CstExprIfElse", handleCstExprIfElse);
    registerCstNodeClass<Luau::CstExprInterpString>("CstExprInterpString", handleCstExprInterpString);
    registerCstNodeClass<Luau::CstExprExplicitTypeInstantiation>("CstExprExplicitTypeInstantiation");
    registerCstNodeClass<Luau::CstStatDo>("CstStatDo", handleCstStatDo);
    registerCstNodeClass<Luau::CstStatRepeat>("CstStatRepeat", handleCstStatRepeat);
    registerCstNodeClass<Luau::CstStatReturn>("CstStatReturn", handleCstStatReturn);
    registerCstNodeClass<Luau::CstStatLocal>("CstStatLocal", handleCstStatLocal);
    registerCstNodeClass<Luau::CstStatFor>("CstStatFor", handleCstStatFor);
    registerCstNodeClass<Luau::CstStatForIn>("CstStatForIn", handleCstStatForIn);
    registerCstNodeClass<Luau::CstStatAssign>("CstStatAssign", handleCstStatAssign);
    registerCstNodeClass<Luau::CstStatCompoundAssign>("CstStatCompoundAssign", handleCstStatCompoundAssign);
    registerCstNodeClass<Luau::CstStatFunction>("CstStatFunction", handleCstStatFunction);
    registerCstNodeClass<Luau::CstStatLocalFunction>("CstStatLocalFunction", handleCstStatLocalFunction);
    registerCstNodeClass<Luau::CstGenericType>("CstGenericType", handleCstGenericType);
    registerCstNodeClass<Luau::CstGenericTypePack>("CstGenericTypePack", handleCstGenericTypePack);
    registerCstNodeClass<Luau::CstStatTypeAlias>("CstStatTypeAlias", handleCstStatTypeAlias);
    registerCstNodeClass<Luau::CstStatTypeFunction>("CstStatTypeFunction", handleCstStatTypeFunction);
    registerCstNodeClass<Luau::CstTypeReference>("CstTypeReference", handleCstTypeReference);
    registerCstNodeClass<Luau::CstTypeTable>("CstTypeTable", handleCstTypeTable);
    registerCstNodeClass<Luau::CstTypeFunction>("CstTypeFunction", handleCstTypeFunction);
    registerCstNodeClass<Luau::CstTypeTypeof>("CstTypeTypeof", handleCstTypeTypeof);
    registerCstNodeClass<Luau::CstTypeUnion>("CstTypeUnion", handleCstTypeUnion);
    registerCstNodeClass<Luau::CstTypeIntersection>("CstTypeIntersection", handleCstTypeIntersection);
    registerCstNodeClass<Luau::CstTypeSingletonString>("CstTypeSingletonString", handleCstTypeSingletonString);
    registerCstNodeClass<Luau::CstTypeGroup>("CstTypeGroup", handleCstTypeGroup);
    registerCstNodeClass<Luau::CstTypePackExplicit>("CstTypePackExplicit", handleCstTypePackExplicit);
    registerCstNodeClass<Luau::CstTypePackGeneric>("CstTypePackGeneric", handleCstTypePackGeneric);
}

static int cstNodeIndex(lua_State* L)
{
    auto& handle = checkCstNode(L, 1);
    size_t keyLen = 0;
    const char* keyStr = luaL_checklstring(L, 2, &keyLen);
    CstNodeAtom atom = getCstNodeAtom(std::string_view(keyStr, keyLen));
    const Luau::CstNode* node = handle.node;

    if (atom == Atom_Kind)
    {
        lua_pushstring(L, getCstNodeKind(node));
        return 1;
    }

    int idx = node->classIndex;
    if (idx >= 0 && idx < int(s_cstPropertyTable.size()) && s_cstPropertyTable[idx])
    {
        if (s_cstPropertyTable[idx](L, handle, atom))
            return 1;
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

void registerCstNode(lua_State* L)
{
    initializeCstDispatchTables();
    registerUserdataType(L, TagCstNode, "CstNode", cstNodeDtor, cstNodeIndex, cstNodeToString, cstNodeEq);
}

} // namespace Luau
