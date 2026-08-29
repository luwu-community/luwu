// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Reflect.h"

#include "lapix.h"
#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/Location.h"
#include "Luau/Common.h"
#include "Luau/DenseHash2.h"

#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <cstring>
#include <new>

LUAU_FASTFLAG(OptLuwuReflectUseAtoms)

namespace Luau
{

#define LUAU_REFLECT_ATOMS(ATOM, ATOM_RW) \
    /* Special & Document & Allocator */ \
    ATOM(Id, "id") \
    ATOM(Matches, "matches") \
    ATOM(Root, "root") \
    ATOM(Source, "source") \
    ATOM(Walk, "walk") \
    ATOM(Errors, "errors") \
    ATOM(Comments, "comments") \
    ATOM(LineOffsets, "lineOffsets") \
    ATOM(Properties, "properties") \
    ATOM(Allocator, "allocator") \
    ATOM(Parse, "parse") \
    ATOM(Parseexpr, "parseexpr") \
    \
    /* Read-Write AST / CST / Aux Properties */ \
    ATOM_RW(Body, "body", SetBody, "setBody") \
    ATOM_RW(Condition, "condition", SetCondition, "setCondition") \
    ATOM_RW(ThenBody, "thenbody", SetThenBody, "setThenBody") \
    ATOM_RW(ElseBody, "elsebody", SetElseBody, "setElseBody") \
    ATOM_RW(List, "list", SetList, "setList") \
    ATOM_RW(Expr, "expr", SetExpr, "setExpr") \
    ATOM_RW(Vars, "vars", SetVars, "setVars") \
    ATOM_RW(Values, "values", SetValues, "setValues") \
    ATOM_RW(Var, "var", SetVar, "setVar") \
    ATOM_RW(From, "from", SetFrom, "setFrom") \
    ATOM_RW(To, "to", SetTo, "setTo") \
    ATOM_RW(Step, "step", SetStep, "setStep") \
    ATOM_RW(Value, "value", SetValue, "setValue") \
    ATOM_RW(Upvalue, "upvalue", SetUpvalue, "setUpvalue") \
    ATOM_RW(Local, "local", SetLocal, "setLocal") \
    ATOM_RW(Name, "name", SetName, "setName") \
    ATOM_RW(Self, "self", SetSelf, "setSelf") \
    ATOM_RW(Func, "func", SetFunc, "setFunc") \
    ATOM_RW(Args, "args", SetArgs, "setArgs") \
    ATOM_RW(Index, "index", SetIndex, "setIndex") \
    ATOM_RW(Left, "left", SetLeft, "setLeft") \
    ATOM_RW(Right, "right", SetRight, "setRight") \
    ATOM_RW(Annotation, "annotation", SetAnnotation, "setAnnotation") \
    ATOM_RW(HasElse, "hasElse", SetHasElse, "setHasElse") \
    ATOM_RW(HasDo, "hasDo", SetHasDo, "setHasDo") \
    ATOM_RW(HasIn, "hasIn", SetHasIn, "setHasIn") \
    ATOM_RW(HasEnd, "hasEnd", SetHasEnd, "setHasEnd") \
    ATOM_RW(IsConst, "isConst", SetIsConst, "setIsConst") \
    ATOM_RW(Exported, "exported", SetExported, "setExported") \
    ATOM_RW(TrueExpr, "trueExpr", SetTrueExpr, "setTrueExpr") \
    ATOM_RW(FalseExpr, "falseExpr", SetFalseExpr, "setFalseExpr") \
    ATOM_RW(HasErrors, "hasErrors", SetHasErrors, "setHasErrors") \
    ATOM_RW(Generics, "generics", SetGenerics, "setGenerics") \
    ATOM_RW(GenericPacks, "genericPacks", SetGenericPacks, "setGenericPacks") \
    ATOM_RW(Params, "params", SetParams, "setParams") \
    ATOM_RW(ReturnTypes, "returnTypes", SetReturnTypes, "setReturnTypes") \
    ATOM_RW(Attributes, "attributes", SetAttributes, "setAttributes") \
    ATOM_RW(SuperName, "superName", SetSuperName, "setSuperName") \
    ATOM_RW(Props, "props", SetProps, "setProps") \
    ATOM_RW(Indexer, "indexer", SetIndexer, "setIndexer") \
    ATOM_RW(Members, "members", SetMembers, "setMembers") \
    ATOM_RW(TypeArguments, "typeArguments", SetTypeArguments, "setTypeArguments") \
    ATOM_RW(DebugName, "debugname", SetDebugName, "setDebugName") \
    ATOM_RW(Vararg, "vararg", SetVararg, "setVararg") \
    ATOM_RW(ReturnAnnotation, "returnAnnotation", SetReturnAnnotation, "setReturnAnnotation") \
    ATOM_RW(Strings, "strings", SetStrings, "setStrings") \
    ATOM_RW(Expressions, "expressions", SetExpressions, "setExpressions") \
    ATOM_RW(Items, "items", SetItems, "setItems") \
    ATOM_RW(Prefix, "prefix", SetPrefix, "setPrefix") \
    ATOM_RW(HasParameterList, "hasParameterList", SetHasParameterList, "setHasParameterList") \
    ATOM_RW(Parameters, "parameters", SetParameters, "setParameters") \
    ATOM_RW(ArgTypes, "argTypes", SetArgTypes, "setArgTypes") \
    ATOM_RW(Type, "type", SetType, "setType") \
    ATOM_RW(Types, "types", SetTypes, "setTypes") \
    ATOM_RW(TailType, "tailType", SetTailType, "setTailType") \
    ATOM_RW(TypeList, "typeList", SetTypeList, "setTypeList") \
    ATOM_RW(VariadicType, "variadicType", SetVariadicType, "setVariadicType") \
    ATOM_RW(Op, "op", SetOp, "setOp") \
    ATOM_RW(QuoteStyle, "quoteStyle", SetQuoteStyle, "setQuoteStyle") \
    ATOM_RW(Location, "location", SetLocation, "setLocation") \
    ATOM_RW(HasSemicolon, "hasSemicolon", SetHasSemicolon, "setHasSemicolon") \
    ATOM_RW(Shadow, "shadow", SetShadow, "setShadow") \
    ATOM_RW(Depth, "depth", SetDepth, "setDepth") \
    ATOM_RW(Access, "access", SetAccess, "setAccess") \
    ATOM_RW(IndexType, "indexType", SetIndexType, "setIndexType") \
    ATOM_RW(ResultType, "resultType", SetResultType, "setResultType") \
    ATOM_RW(IsMethod, "isMethod", SetIsMethod, "setIsMethod") \
    ATOM_RW(Text, "text", SetText, "setText") \
    ATOM_RW(Key, "key", SetKey, "setKey") \
    ATOM_RW(Kind, "kind", SetKind, "setKind") \
    ATOM_RW(Begin, "begin", SetBegin, "setBegin") \
    ATOM_RW(End, "end", SetEnd, "setEnd") \
    \
    /* Read-Only AST Properties */ \
    ATOM(Category, "category") \
    ATOM(Children, "children") \
    ATOM(Cst, "cst") \
    ATOM(Statements, "statements") \
    ATOM(MessageIndex, "messageIndex") \
    ATOM(IsMissing, "isMissing") \
    \
    /* Read-Only CST Properties */ \
    ATOM(HasAt, "hasAt") \
    ATOM(OpenParenPosition, "openParenPosition") \
    ATOM(CloseParenPosition, "closeParenPosition") \
    ATOM(ArgsCommaPositions, "argsCommaPositions") \
    ATOM(ClosePosition, "closePosition") \
    ATOM(SourceString, "sourceString") \
    ATOM(BlockDepth, "blockDepth") \
    ATOM(OpenParens, "openParens") \
    ATOM(CloseParens, "closeParens") \
    ATOM(CommaPositions, "commaPositions") \
    ATOM(OpenBracketPosition, "openBracketPosition") \
    ATOM(CloseBracketPosition, "closeBracketPosition") \
    ATOM(FunctionKeywordPosition, "functionKeywordPosition") \
    ATOM(OpenGenericsPosition, "openGenericsPosition") \
    ATOM(GenericsCommaPositions, "genericsCommaPositions") \
    ATOM(CloseGenericsPosition, "closeGenericsPosition") \
    ATOM(ArgsAnnotationColonPositions, "argsAnnotationColonPositions") \
    ATOM(VarargAnnotationColonPosition, "varargAnnotationColonPosition") \
    ATOM(ReturnSpecifierPosition, "returnSpecifierPosition") \
    ATOM(OpPosition, "opPosition") \
    ATOM(ThenPosition, "thenPosition") \
    ATOM(ElsePosition, "elsePosition") \
    ATOM(IsElseIf, "isElseIf") \
    ATOM(StatsStartPosition, "statsStartPosition") \
    ATOM(EndPosition, "endPosition") \
    ATOM(UntilPosition, "untilPosition") \
    ATOM(VarsAnnotationColonPositions, "varsAnnotationColonPositions") \
    ATOM(VarsCommaPositions, "varsCommaPositions") \
    ATOM(ValuesCommaPositions, "valuesCommaPositions") \
    ATOM(AnnotationColonPosition, "annotationColonPosition") \
    ATOM(EqualsPosition, "equalsPosition") \
    ATOM(EndCommaPosition, "endCommaPosition") \
    ATOM(StepCommaPosition, "stepCommaPosition") \
    ATOM(LocalKeywordPosition, "localKeywordPosition") \
    ATOM(DefaultEqualsPosition, "defaultEqualsPosition") \
    ATOM(EllipsisPosition, "ellipsisPosition") \
    ATOM(TypeKeywordPosition, "typeKeywordPosition") \
    ATOM(GenericsOpenPosition, "genericsOpenPosition") \
    ATOM(GenericsClosePosition, "genericsClosePosition") \
    ATOM(PrefixPointPosition, "prefixPointPosition") \
    ATOM(OpenParametersPosition, "openParametersPosition") \
    ATOM(ParametersCommaPositions, "parametersCommaPositions") \
    ATOM(CloseParametersPosition, "closeParametersPosition") \
    ATOM(IsArray, "isArray") \
    ATOM(OpenArgsPosition, "openArgsPosition") \
    ATOM(ArgumentNameColonPositions, "argumentNameColonPositions") \
    ATOM(ArgumentsCommaPositions, "argumentsCommaPositions") \
    ATOM(CloseArgsPosition, "closeArgsPosition") \
    ATOM(ReturnArrowPosition, "returnArrowPosition") \
    ATOM(OpenPosition, "openPosition") \
    ATOM(LeadingPosition, "leadingPosition") \
    ATOM(SeparatorPositions, "separatorPositions") \
    ATOM(OpenParenthesesPosition, "openParenthesesPosition") \
    ATOM(CloseParenthesesPosition, "closeParenthesesPosition") \
    \
    /* Read-Only Aux Fields */ \
    ATOM(IndexerOpenPosition, "indexerOpenPosition") \
    ATOM(IndexerClosePosition, "indexerClosePosition") \
    ATOM(SeparatorPosition, "separatorPosition") \
    ATOM(Separator, "separator")

enum class ReflectAtom : int16_t
{
    Unknown = -1,
#define ATOM(variant, str) variant,
#define ATOM_RW(variant, str, setVariant, setStr) variant, setVariant,
    LUAU_REFLECT_ATOMS(ATOM, ATOM_RW)
#undef ATOM
#undef ATOM_RW
    Count
};

inline const char* getAtomString(ReflectAtom atom)
{
    switch (atom)
    {
#define ATOM(variant, str) case ReflectAtom::variant: return str;
#define ATOM_RW(variant, str, setVariant, setStr) case ReflectAtom::variant: return str; case ReflectAtom::setVariant: return setStr;
    LUAU_REFLECT_ATOMS(ATOM, ATOM_RW)
#undef ATOM
#undef ATOM_RW
    default: return "";
    }
}

inline ReflectAtom resolveGlobalReflectAtom(std::string_view key)
{
    static const DenseHashMap2<std::string_view, ReflectAtom> s_atomMap = []() {
        DenseHashMap2<std::string_view, ReflectAtom> map;
#define ATOM(variant, str) map[str] = ReflectAtom::variant;
#define ATOM_RW(variant, str, setVariant, setStr) map[str] = ReflectAtom::variant; map[setStr] = ReflectAtom::setVariant;
        LUAU_REFLECT_ATOMS(ATOM, ATOM_RW)
#undef ATOM
#undef ATOM_RW
        return map;
    }();

    if (const ReflectAtom* atom = s_atomMap.find(key))
        return *atom;

    return ReflectAtom::Unknown;
}

inline ReflectAtom resolveReflectAtom(int atomId, std::string_view key)
{
    if (atomId >= 0)
    {
        if (atomId < int(ReflectAtom::Count))
            return ReflectAtom(atomId);
        return ReflectAtom::Unknown;
    }
    return resolveGlobalReflectAtom(key);
}

inline ReflectAtom resolveReflectAtom(int atomId, const char* str, size_t len)
{
    if (atomId >= 0)
    {
        if (atomId < int(ReflectAtom::Count))
            return ReflectAtom(atomId);
        return ReflectAtom::Unknown;
    }
    return resolveGlobalReflectAtom(std::string_view(str, len));
}

/**
 * Reserved userdata tags for AST/CST reflection objects.
 *
 * SAFETY: Uses the Luwu internal reserved tag range [LUA_UTAG_RESERVED_START..LUA_UTAG_RESERVED_END],
 * keeping public tags [0..LUA_UTAG_RESERVED_START-1] completely free for embedder use.
 */
enum AstUserdataTag : int
{
    TagDocument  = LUA_INTERNAL_UTAG(0),
    TagNode      = LUA_INTERNAL_UTAG(1),
    TagCstNode   = LUA_INTERNAL_UTAG(2),
    TagAux       = LUA_INTERNAL_UTAG(3),
    TagFilter    = LUA_INTERNAL_UTAG(4),
    TagAllocator = LUA_INTERNAL_UTAG(5),
};

enum AstLightUserdataTag : int
{
    TagId       = LUA_INTERNAL_LUTAG(0),
};

enum class NodeCategory : uint8_t
{
    Unknown = 0,
    Stat = 1 << 0,
    Expr = 1 << 1,
    Type = 1 << 2,
    TypePack = 1 << 3,
    Generic = 1 << 4,
    Attr = 1 << 5,
};

inline const char* categoryToString(NodeCategory cat)
{
    switch (cat)
    {
    case NodeCategory::Stat:     return "stat";
    case NodeCategory::Expr:     return "expr";
    case NodeCategory::Type:     return "type";
    case NodeCategory::TypePack: return "typePack";
    case NodeCategory::Generic:  return "generic";
    case NodeCategory::Attr:     return "attr";
    default:                     return "unknown";
    }
}

inline NodeCategory categoryFromString(std::string_view category)
{
    if (category == "stat")
        return NodeCategory::Stat;
    if (category == "expr")
        return NodeCategory::Expr;
    if (category == "type")
        return NodeCategory::Type;
    if (category == "typePack" || category == "typepack")
        return NodeCategory::TypePack;
    if (category == "generic")
        return NodeCategory::Generic;
    if (category == "attr")
        return NodeCategory::Attr;
    return NodeCategory::Unknown;
}

NodeCategory getNodeCategory(Luau::AstNode* node);
int getNodeClassIndexByKind(std::string_view kind);

struct AstFilterData
{
    uint64_t classMask[2] = {0, 0};
    uint8_t categoryMask = 0;

    bool matches(Luau::AstNode* node) const;
    bool addKind(std::string_view kind);
    bool addCategory(std::string_view category);
    bool empty() const { return classMask[0] == 0 && classMask[1] == 0 && categoryMask == 0; }
};

struct AstAllocatorState
{
    Luau::Allocator allocator;
    Luau::AstNameTable names;

    AstAllocatorState()
        : allocator()
        , names(allocator)
    {
    }
};

struct AstAllocatorData
{
    std::shared_ptr<AstAllocatorState> state;
};

struct AstDocumentState
{
    std::string source;
    std::shared_ptr<AstAllocatorState> arena;
    Luau::ParseResult parseResult;
    std::vector<size_t> lineOffsets;

    AstDocumentState()
        : arena(std::make_shared<AstAllocatorState>())
    {
    }

    explicit AstDocumentState(std::shared_ptr<AstAllocatorState> arena)
        : arena(std::move(arena))
    {
    }

    Luau::Allocator* allocator() const { return arena ? &arena->allocator : nullptr; }
    Luau::AstNameTable* names() const { return arena ? &arena->names : nullptr; }
};

struct AstDocumentData
{
    std::shared_ptr<AstDocumentState> doc;
};

struct AstNodeData
{
    std::shared_ptr<AstDocumentState> doc;
    Luau::AstNode* node = nullptr;
};

struct CstNodeData
{
    std::shared_ptr<AstDocumentState> doc;
    const Luau::CstNode* node = nullptr;
};

enum AstAuxKind : uint8_t
{
    Aux_TableProp,
    Aux_TableIndexer,
    Aux_DeclaredExternTypeProperty,
    Aux_ClassProperty,
    Aux_ClassMethod,
    Aux_Local,
    Aux_Comment,
    Aux_TableItem,
    Aux_CstTableItem,
    Aux_TypeList,
};

struct AstAuxData
{
    std::shared_ptr<AstDocumentState> doc;
    AstAuxKind kind;
    union
    {
        Luau::AstTableProp tableProp;
        Luau::AstTableIndexer tableIndexer;
        Luau::AstDeclaredExternTypeProperty declaredExternProp;
        Luau::AstClassProperty classProp;
        Luau::AstClassMethod classMethod;
        Luau::AstLocal* local;
        Luau::Comment comment;
        Luau::AstExprTable::Item tableItem;
        Luau::CstExprTable::Item cstTableItem;
        Luau::AstTypeList typeList;
    };

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableProp& p) : doc(doc), kind(Aux_TableProp), tableProp(p) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableIndexer& idx) : doc(doc), kind(Aux_TableIndexer), tableIndexer(idx) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstDeclaredExternTypeProperty& p) : doc(doc), kind(Aux_DeclaredExternTypeProperty), declaredExternProp(p) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassProperty& p) : doc(doc), kind(Aux_ClassProperty), classProp(p) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassMethod& m) : doc(doc), kind(Aux_ClassMethod), classMethod(m) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* l) : doc(doc), kind(Aux_Local), local(l) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& c) : doc(doc), kind(Aux_Comment), comment(c) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstExprTable::Item& item) : doc(doc), kind(Aux_TableItem), tableItem(item) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::CstExprTable::Item& item) : doc(doc), kind(Aux_CstTableItem), cstTableItem(item) {}
    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTypeList& tl) : doc(doc), kind(Aux_TypeList), typeList(tl) {}
    ~AstAuxData() {}
};

// Line and location offset utilities (adapted from lute)
inline std::vector<size_t> computeLineOffsets(std::string_view content)
{
    std::vector<size_t> result{};
    result.emplace_back(0);

    for (size_t i = 0; i < content.size(); i++)
    {
        auto ch = content[i];
        if (ch == '\r' || ch == '\n')
        {
            if (ch == '\r' && i + 1 < content.size() && content[i + 1] == '\n')
            {
                i++;
            }
            result.push_back(i + 1);
        }
    }
    return result;
}

inline size_t positionToOffset(const std::vector<size_t>& lineOffsets, size_t sourceLen, const Luau::Position& pos)
{
    if (pos.line < lineOffsets.size())
        return std::min(lineOffsets[pos.line] + pos.column, sourceLen);
    return 0;
}

inline std::pair<size_t, size_t> locationToOffsets(const std::vector<size_t>& lineOffsets, size_t sourceLen, const Luau::Location& loc)
{
    size_t start = positionToOffset(lineOffsets, sourceLen, loc.begin);
    size_t end = positionToOffset(lineOffsets, sourceLen, loc.end);
    if (end < start)
        end = start;
    return {start, end};
}

// Push helpers
void pushAstAllocator(lua_State* L, std::shared_ptr<AstAllocatorState> state);
void pushAstDocument(lua_State* L, std::shared_ptr<AstDocumentState> doc);
void pushAstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstNode* node);
void pushCstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::CstNode* node);

inline void pushPosition(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Position& pos)
{
    if (pos == Luau::Position::missing())
    {
        lua_pushnil(L);
        return;
    }
    double off = doc ? double(positionToOffset(doc->lineOffsets, doc->source.size(), pos)) : 0.0;
#if LUA_VECTOR_SIZE == 4
    lua_pushvector(L, float(pos.line + 1), float(pos.column + 1), float(off), 0.0f);
#else
    lua_pushvector(L, float(pos.line + 1), float(pos.column + 1), float(off));
#endif
}

inline void pushLocation(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Location& loc)
{
    lua_createtable(L, 0, 2);
    pushPosition(L, doc, loc.begin);
    lua_setfield(L, -2, "begin");
    pushPosition(L, doc, loc.end);
    lua_setfield(L, -2, "end");
    lua_setreadonly(L, -1, true);
}

template<typename T>
inline void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const T& item)
{
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, item};
}

inline void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* local)
{
    if (!local)
    {
        lua_pushnil(L);
        return;
    }
    AstAuxData* data = static_cast<AstAuxData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAuxData), TagAux));
    new (data) AstAuxData{doc, local};
}

void pushAstFilter(lua_State* L, const AstFilterData& filter);

// Check helpers
AstDocumentData& checkAstDocument(lua_State* L, int idx);
AstNodeData& checkAstNode(lua_State* L, int idx);
CstNodeData& checkCstNode(lua_State* L, int idx);
AstAuxData& checkAstAux(lua_State* L, int idx);
AstFilterData& checkAstFilter(lua_State* L, int idx);
AstFilterData extractAstFilter(lua_State* L, int idx);

void registerAstFilter(lua_State* L);
int reflectFilter(lua_State* L);

const char* getAstAuxKind(const AstAuxData& handle);

// Array push helpers
template<typename F>
inline void pushArray(lua_State* L, size_t size, F&& pushElem)
{
    lua_createtable(L, int(size), 0);
    for (size_t i = 0; i < size; i++)
    {
        pushElem(i);
        lua_rawseti(L, -2, int(i + 1));
    }
}

inline void pushPositionArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::Position>& array)
{
    pushArray(L, array.size, [&](size_t i) {
        pushPosition(L, doc, array.data[i]);
    });
}

template<typename T>
inline void pushNodeArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<T*>& array)
{
    pushArray(L, array.size, [&](size_t i) {
        pushAstNode(L, doc, array.data[i]);
    });
}

inline void pushLocalArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::AstLocal*>& array)
{
    pushArray(L, array.size, [&](size_t i) {
        pushAstAux(L, doc, array.data[i]);
    });
}

inline void pushTypeOrPack(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTypeOrPack& tp)
{
    if (tp.type)
        pushAstNode(L, doc, tp.type);
    else if (tp.typePack)
        pushAstNode(L, doc, tp.typePack);
    else
        lua_pushnil(L);
}

inline void pushTypeOrPackArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::AstTypeOrPack>& array)
{
    pushArray(L, array.size, [&](size_t i) {
        pushTypeOrPack(L, doc, array.data[i]);
    });
}

// Node Kind Lookup
const char* getNodeKind(Luau::AstNode* node);
const char* getCstNodeKind(const Luau::CstNode* node);

// Visitor Helpers
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

// Userdata registration helper
inline void registerUserdataType(
    lua_State* L,
    int tag,
    const char* typeName,
    lua_Destructor dtor,
    lua_CFunction index,
    lua_CFunction tostring,
    lua_CFunction eq = nullptr,
    lua_CFunction namecall = nullptr
)
{
    lua_setuserdatadtor(L, tag, dtor);
    lua_createtable(L, 0, (eq ? 5 : 4) + (namecall ? 1 : 0));
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "__metatable");
    lua_pushstring(L, typeName);
    lua_setfield(L, -2, "__type");
    lua_pushcfunction(L, index, "__index");
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, tostring, "__tostring");
    lua_setfield(L, -2, "__tostring");
    if (eq)
    {
        lua_pushcfunction(L, eq, "__eq");
        lua_setfield(L, -2, "__eq");
    }
    if (namecall)
    {
        lua_pushcfunction(L, namecall, "__namecall");
        lua_setfield(L, -2, "__namecall");
    }
    lua_setuserdatametatable(L, tag);
}

// Push cached userdata method from metatable, or lazily allocate and cache it on first access
inline int pushCachedUserdataMethod(lua_State* L, int tag, const char* name, lua_CFunction thunk)
{
    lua_getuserdatametatable(L, tag);
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushstring(L, name);
        lua_pushcclosure(L, thunk, name, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name);
    }
    lua_replace(L, -2);
    return 1;
}

#define LUAU_REFLECT_RESOLVE_INDEX_ATOM() \
    int atomId = -1; \
    size_t keyLen = 0; \
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr); \
    if (!keyStr) \
    { \
        lua_pushnil(L); \
        return 1; \
    } \
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen)

#define LUAU_REFLECT_RESOLVE_NAMECALL_ATOM() \
    int atomId = -1; \
    size_t len = 0; \
    const char* str = lua_namecallwithlen(L, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr, &len); \
    if (!str) \
        luaL_error(L, "missing method name in namecall"); \
    ReflectAtom atom = resolveReflectAtom(atomId, str, len)

#define LUAU_REFLECT_PREPARE_INDEX(checkFunc) \
    auto& handle = checkFunc(L, 1); \
    LUAU_REFLECT_RESOLVE_INDEX_ATOM()

#define LUAU_REFLECT_PREPARE_NAMECALL(checkFunc) \
    auto& handle = checkFunc(L, 1); \
    LUAU_REFLECT_RESOLVE_NAMECALL_ATOM()

#define LUAU_REFLECT_METHOD_TRAMPOLINE(funcName, checkFunc, dispatchFunc) \
    static int funcName(lua_State* L) \
    { \
        auto& handle = checkFunc(L, 1); \
        size_t len = 0; \
        const char* str = lua_tolstring(L, lua_upvalueindex(1), &len); \
        ReflectAtom atom = resolveGlobalReflectAtom(std::string_view(str, len)); \
        return dispatchFunc(L, handle, atom, str, len); \
    }

#define LUAU_REFLECT_NAMECALL(funcName, checkFunc, dispatchFunc) \
    static int funcName(lua_State* L) \
    { \
        LUAU_REFLECT_PREPARE_NAMECALL(checkFunc); \
        return dispatchFunc(L, handle, atom, str, len); \
    }

#define LUAU_REFLECT_INDEX(funcName, checkFunc, getKindFunc, getCategoryFunc, TagValue, trampolineFunc) \
    static int funcName(lua_State* L) \
    { \
        LUAU_REFLECT_PREPARE_INDEX(checkFunc); \
        switch (atom) \
        { \
        case ReflectAtom::Kind: \
            lua_pushstring(L, getKindFunc(handle.node)); \
            return 1; \
        case ReflectAtom::Category: \
            lua_pushstring(L, getCategoryFunc(handle.node)); \
            return 1; \
        case ReflectAtom::Id: \
            lua_pushlightuserdatatagged(L, (void*)handle.node, TagId); \
            return 1; \
        default: \
            break; \
        } \
        if (atom != ReflectAtom::Unknown) \
            return pushCachedUserdataMethod(L, TagValue, keyStr, trampolineFunc); \
        lua_pushnil(L); \
        return 1; \
    }

#define LUAU_REFLECT_GET_NODE_KIND(funcName, NodeType, classTable, defaultName) \
    const char* funcName(NodeType node) \
    { \
        if (!node) \
            return "nil"; \
        int idx = node->classIndex; \
        if (idx >= 0 && idx < int(classTable.size()) && classTable[idx].kind) \
            return classTable[idx].kind; \
        return defaultName; \
    }

#define LUAU_REFLECT_GET_NODE_CATEGORY(funcName, NodeType, classTable, defaultName) \
    const char* funcName(NodeType node) \
    { \
        if (!node) \
            return "nil"; \
        int idx = node->classIndex; \
        if (idx >= 0 && idx < int(classTable.size()) && classTable[idx].category) \
            return classTable[idx].category; \
        return defaultName; \
    }

#define LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkName, dtorName, DataType, TagValue, TypeNameStr) \
    DataType& checkName(lua_State* L, int idx) \
    { \
        if (lua_userdatatag(L, idx) != TagValue) \
            luaL_typeerrorL(L, idx, TypeNameStr); \
        return *static_cast<DataType*>(lua_touserdata(L, idx)); \
    } \
    static void dtorName(lua_State* L, void* userdata) \
    { \
        static_cast<DataType*>(userdata)->~DataType(); \
    }

#define LUAU_REFLECT_DEFINE_POINTER_USERDATA(pushName, checkName, dtorName, DataType, NodeType, TagValue, TypeNameStr) \
    void pushName(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, NodeType node) \
    { \
        if (!node) \
        { \
            lua_pushnil(L); \
            return; \
        } \
        DataType* data = static_cast<DataType*>(lua_newuserdatataggedwithmetatable(L, sizeof(DataType), TagValue)); \
        new (data) DataType{doc, node}; \
    } \
    LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkName, dtorName, DataType, TagValue, TypeNameStr)

#define LUAU_REFLECT_DEFINE_VALUE_USERDATA(pushName, checkName, dtorName, DataType, ValueType, TagValue, TypeNameStr) \
    void pushName(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, ValueType value) \
    { \
        DataType* data = static_cast<DataType*>(lua_newuserdatataggedwithmetatable(L, sizeof(DataType), TagValue)); \
        new (data) DataType{doc, value}; \
    } \
    LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkName, dtorName, DataType, TagValue, TypeNameStr)

// Module registration functions
void registerAstAllocator(lua_State* L);
void registerAstDocument(lua_State* L);
void registerAstNode(lua_State* L);
void registerCstNode(lua_State* L);
void registerAstAux(lua_State* L);

} // namespace Luau
