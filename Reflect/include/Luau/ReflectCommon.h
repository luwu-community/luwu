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

#define LUAU_REFLECT_ATOMS \
    /* Common / shared */ \
    ATOM(Id, "id") \
    ATOM(Kind, "kind") \
    ATOM(Location, "location") \
    ATOM(Text, "text") \
    ATOM(Name, "name") \
    ATOM(Type, "type") \
    ATOM(Value, "value") \
    ATOM(Func, "func") \
    ATOM(Items, "items") \
    ATOM(IsConst, "isConst") \
    ATOM(Annotation, "annotation") \
    ATOM(Matches, "matches") \
    \
    /* AstDocument */ \
    ATOM(Root, "root") \
    ATOM(Source, "source") \
    ATOM(Walk, "walk") \
    ATOM(Errors, "errors") \
    ATOM(Comments, "comments") \
    ATOM(LineOffsets, "lineOffsets") \
    \
    /* AstNode */ \
    ATOM(Category, "category") \
    ATOM(Children, "children") \
    ATOM(Cst, "cst") \
    ATOM(Body, "body") \
    ATOM(Condition, "condition") \
    ATOM(ThenBody, "thenbody") \
    ATOM(ElseBody, "elsebody") \
    ATOM(List, "list") \
    ATOM(Expr, "expr") \
    ATOM(Vars, "vars") \
    ATOM(Values, "values") \
    ATOM(Var, "var") \
    ATOM(From, "from") \
    ATOM(To, "to") \
    ATOM(Step, "step") \
    ATOM(Op, "op") \
    ATOM(Args, "args") \
    ATOM(Self, "self") \
    ATOM(Index, "index") \
    ATOM(Left, "left") \
    ATOM(Right, "right") \
    ATOM(Local, "local") \
    ATOM(TrueExpr, "trueExpr") \
    ATOM(FalseExpr, "falseExpr") \
    ATOM(Prefix, "prefix") \
    ATOM(Vararg, "vararg") \
    ATOM(HasSemicolon, "hasSemicolon") \
    ATOM(Generics, "generics") \
    ATOM(GenericPacks, "genericPacks") \
    ATOM(ReturnAnnotation, "returnAnnotation") \
    ATOM(Exported, "exported") \
    ATOM(HasErrors, "hasErrors") \
    ATOM(SuperName, "superName") \
    ATOM(Props, "props") \
    ATOM(Indexer, "indexer") \
    ATOM(Members, "members") \
    ATOM(Statements, "statements") \
    ATOM(Expressions, "expressions") \
    ATOM(Strings, "strings") \
    ATOM(MessageIndex, "messageIndex") \
    ATOM(TypeArguments, "typeArguments") \
    ATOM(Parameters, "parameters") \
    ATOM(HasParameterList, "hasParameterList") \
    ATOM(ArgTypes, "argTypes") \
    ATOM(ReturnTypes, "returnTypes") \
    ATOM(Attributes, "attributes") \
    ATOM(DebugName, "debugname") \
    ATOM(Upvalue, "upvalue") \
    ATOM(HasDo, "hasDo") \
    ATOM(HasIn, "hasIn") \
    ATOM(HasEnd, "hasEnd") \
    ATOM(HasElse, "hasElse") \
    ATOM(QuoteStyle, "quoteStyle") \
    ATOM(IsMissing, "isMissing") \
    ATOM(Types, "types") \
    ATOM(TailType, "tailType") \
    ATOM(VariadicType, "variadicType") \
    ATOM(Params, "params") \
    \
    /* CstNode */ \
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
    /* AstLocation */ \
    ATOM(BeginLine, "beginLine") \
    ATOM(BeginColumn, "beginColumn") \
    ATOM(EndLine, "endLine") \
    ATOM(EndColumn, "endColumn") \
    ATOM(StartOffset, "startOffset") \
    ATOM(EndOffset, "endOffset") \
    \
    /* AstPosition */ \
    ATOM(Line, "line") \
    ATOM(Column, "column") \
    ATOM(ComputedOffset, "computedOffset") \
    \
    /* AstLocal */ \
    ATOM(Shadow, "shadow") \
    ATOM(Depth, "depth") \
    \
    /* AstAux */ \
    ATOM(Access, "access") \
    ATOM(IndexType, "indexType") \
    ATOM(ResultType, "resultType") \
    ATOM(IsMethod, "isMethod") \
    ATOM(Key, "key") \
    ATOM(IndexerOpenPosition, "indexerOpenPosition") \
    ATOM(IndexerClosePosition, "indexerClosePosition") \
    ATOM(SeparatorPosition, "separatorPosition") \
    ATOM(Separator, "separator")

enum class ReflectAtom : int16_t
{
    Unknown = -1,
#define ATOM(variant, str) variant,
    LUAU_REFLECT_ATOMS
#undef ATOM
    Count
};

inline ReflectAtom resolveGlobalReflectAtom(std::string_view key)
{
    static const DenseHashMap2<std::string_view, ReflectAtom> s_atomMap = []() {
        DenseHashMap2<std::string_view, ReflectAtom> map;
#define ATOM(variant, str) map[str] = ReflectAtom::variant;
        LUAU_REFLECT_ATOMS
#undef ATOM
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
 * SAFETY: The use of reserved tags in this range (10-15) is safe: embedders are expected
 * to either dynamically query for available tags using `lua_findunuseduserdatatag` or
 * hardcode all reserved tags on their own. Raising the userdata tag limit is also an option.
 *
 * `TagAux` acts as a catch-all tag for auxillary structures to avoid
 * bloating the tags needed by Reflect 
 */
enum AstUserdataTag : int
{
    TagDocument = 10,
    TagNode = 11,
    TagLocation = 12,
    TagCstNode = 13,
    TagPosition = 14,
    TagAux = 15,
    TagFilter = 16,
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

struct AstDocumentState
{
    std::string source;
    std::shared_ptr<Luau::Allocator> allocator;
    std::shared_ptr<Luau::AstNameTable> names;
    Luau::ParseResult parseResult;
    std::vector<size_t> lineOffsets;

    AstDocumentState()
        : allocator(std::make_shared<Luau::Allocator>())
        , names(std::make_shared<Luau::AstNameTable>(*allocator))
    {
    }
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

struct AstLocationData
{
    std::shared_ptr<AstDocumentState> doc;
    Luau::Location location;
};

struct CstNodeData
{
    std::shared_ptr<AstDocumentState> doc;
    const Luau::CstNode* node = nullptr;
};

struct AstPositionData
{
    std::shared_ptr<AstDocumentState> doc;
    Luau::Position position;
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
    };

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableProp& p)
        : doc(doc)
        , kind(Aux_TableProp)
        , tableProp(p)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableIndexer& idx)
        : doc(doc)
        , kind(Aux_TableIndexer)
        , tableIndexer(idx)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstDeclaredExternTypeProperty& p)
        : doc(doc)
        , kind(Aux_DeclaredExternTypeProperty)
        , declaredExternProp(p)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassProperty& p)
        : doc(doc)
        , kind(Aux_ClassProperty)
        , classProp(p)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassMethod& m)
        : doc(doc)
        , kind(Aux_ClassMethod)
        , classMethod(m)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* l)
        : doc(doc)
        , kind(Aux_Local)
        , local(l)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& c)
        : doc(doc)
        , kind(Aux_Comment)
        , comment(c)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::AstExprTable::Item& item)
        : doc(doc)
        , kind(Aux_TableItem)
        , tableItem(item)
    {
    }

    AstAuxData(const std::shared_ptr<AstDocumentState>& doc, const Luau::CstExprTable::Item& item)
        : doc(doc)
        , kind(Aux_CstTableItem)
        , cstTableItem(item)
    {
    }

    ~AstAuxData() {}
};

// Line and location offset utilities (adapted from lute)
std::vector<size_t> computeLineOffsets(std::string_view content);

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
void pushAstDocument(lua_State* L, std::shared_ptr<AstDocumentState> doc);
void pushAstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstNode* node);
void pushLocation(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Location& loc);
void pushCstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::CstNode* node);
void pushPosition(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Position& pos);

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
AstLocationData& checkAstLocation(lua_State* L, int idx);
CstNodeData& checkCstNode(lua_State* L, int idx);
AstPositionData& checkAstPosition(lua_State* L, int idx);
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

inline const char* tableAccessToString(Luau::AstTableAccess access)
{
    switch (access)
    {
    case Luau::AstTableAccess::Read:
        return "read";
    case Luau::AstTableAccess::Write:
        return "write";
    case Luau::AstTableAccess::ReadWrite:
        return "readwrite";
    default:
        return "unknown";
    }
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
    const luaL_Reg* methods = nullptr,
    lua_CFunction namecall = nullptr
)
{
    lua_setuserdatadtor(L, tag, dtor);
    lua_createtable(L, 0, (eq ? 5 : 4) + (methods ? 4 : 0) + (namecall ? 1 : 0));
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
    if (methods)
    {
        for (const luaL_Reg* l = methods; l->name != nullptr; l++)
        {
            lua_pushcfunction(L, l->func, l->name);
            lua_setfield(L, -2, l->name);
        }
    }
    lua_setuserdatametatable(L, tag);
}

// Push pre-cached userdata method from metatable without creating GC objects
inline int pushUserdataMethod(lua_State* L, int tag, const char* name)
{
    lua_getuserdatametatable(L, tag);
    lua_getfield(L, -1, name);
    lua_replace(L, -2);
    return 1;
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
    const auto& doc = handle.doc; \
    (void)doc; \
    LUAU_REFLECT_RESOLVE_INDEX_ATOM()

#define LUAU_REFLECT_PREPARE_NAMECALL(checkFunc) \
    auto& handle = checkFunc(L, 1); \
    const auto& doc = handle.doc; \
    (void)doc; \
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
            lua_pushlightuserdatatagged(L, (void*)handle.node, TagValue); \
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
void registerAstDocument(lua_State* L);
void registerAstNode(lua_State* L);
void registerAstLocation(lua_State* L);
void registerCstNode(lua_State* L);
void registerAstPosition(lua_State* L);
void registerAstAux(lua_State* L);

} // namespace Luau
