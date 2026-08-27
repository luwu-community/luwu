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

enum class ReflectAtom : int16_t
{
    Unknown = -1,

    // Common / shared
    Kind = 0,
    Location,
    Text,
    Name,
    Type,
    Value,
    Func,
    Items,
    IsConst,
    Annotation,

    // AstDocument
    Root,
    Source,
    Walk,
    Find,
    Errors,
    Comments,
    LineOffsets,

    // AstNode
    Category,
    Children,
    Cst,
    Body,
    Condition,
    ThenBody,
    ElseBody,
    List,
    Expr,
    Vars,
    Values,
    Var,
    From,
    To,
    Step,
    Op,
    Args,
    Self,
    Index,
    Left,
    Right,
    Local,
    TrueExpr,
    FalseExpr,
    Prefix,
    Vararg,
    HasSemicolon,
    Generics,
    GenericPacks,
    ReturnAnnotation,
    Exported,
    HasErrors,
    SuperName,
    Props,
    Indexer,
    Members,
    Statements,
    Expressions,
    Strings,
    MessageIndex,
    TypeArguments,
    Parameters,
    HasParameterList,
    ArgTypes,
    ReturnTypes,
    Attributes,
    DebugName,
    Upvalue,
    HasDo,
    HasIn,
    HasEnd,
    HasElse,
    QuoteStyle,
    IsMissing,
    Types,
    TailType,
    VariadicType,
    Params,

    // CstNode
    HasAt,
    OpenParenPosition,
    CloseParenPosition,
    ArgsCommaPositions,
    ClosePosition,
    SourceString,
    BlockDepth,
    OpenParens,
    CloseParens,
    CommaPositions,
    OpenBracketPosition,
    CloseBracketPosition,
    FunctionKeywordPosition,
    OpenGenericsPosition,
    GenericsCommaPositions,
    CloseGenericsPosition,
    ArgsAnnotationColonPositions,
    VarargAnnotationColonPosition,
    ReturnSpecifierPosition,
    OpPosition,
    ThenPosition,
    ElsePosition,
    IsElseIf,
    StatsStartPosition,
    EndPosition,
    UntilPosition,
    VarsAnnotationColonPositions,
    VarsCommaPositions,
    ValuesCommaPositions,
    AnnotationColonPosition,
    EqualsPosition,
    EndCommaPosition,
    StepCommaPosition,
    LocalKeywordPosition,
    DefaultEqualsPosition,
    EllipsisPosition,
    TypeKeywordPosition,
    GenericsOpenPosition,
    GenericsClosePosition,
    PrefixPointPosition,
    OpenParametersPosition,
    ParametersCommaPositions,
    CloseParametersPosition,
    IsArray,
    OpenArgsPosition,
    ArgumentNameColonPositions,
    ArgumentsCommaPositions,
    CloseArgsPosition,
    ReturnArrowPosition,
    OpenPosition,
    LeadingPosition,
    SeparatorPositions,
    OpenParenthesesPosition,
    CloseParenthesesPosition,

    // AstLocation
    BeginLine,
    BeginColumn,
    EndLine,
    EndColumn,
    StartOffset,
    EndOffset,

    // AstPosition
    Line,
    Column,
    ComputedOffset,

    // AstLocal
    Shadow,
    Depth,

    // AstAux
    Access,
    IndexType,
    ResultType,
    IsMethod,

    Count
};

inline ReflectAtom resolveGlobalReflectAtom(std::string_view key)
{
    static const DenseHashMap2<std::string_view, ReflectAtom> s_atomMap = []() {
        static const std::pair<std::string_view, ReflectAtom> entries[] = {
            {"kind", ReflectAtom::Kind},
            {"location", ReflectAtom::Location},
            {"text", ReflectAtom::Text},
            {"name", ReflectAtom::Name},
            {"type", ReflectAtom::Type},
            {"value", ReflectAtom::Value},
            {"func", ReflectAtom::Func},
            {"items", ReflectAtom::Items},
            {"isConst", ReflectAtom::IsConst},
            {"annotation", ReflectAtom::Annotation},

            {"root", ReflectAtom::Root},
            {"source", ReflectAtom::Source},
            {"walk", ReflectAtom::Walk},
            {"find", ReflectAtom::Find},
            {"errors", ReflectAtom::Errors},
            {"comments", ReflectAtom::Comments},
            {"lineOffsets", ReflectAtom::LineOffsets},

            {"category", ReflectAtom::Category},
            {"children", ReflectAtom::Children},
            {"cst", ReflectAtom::Cst},
            {"body", ReflectAtom::Body},
            {"condition", ReflectAtom::Condition},
            {"thenbody", ReflectAtom::ThenBody},
            {"elsebody", ReflectAtom::ElseBody},
            {"list", ReflectAtom::List},
            {"expr", ReflectAtom::Expr},
            {"vars", ReflectAtom::Vars},
            {"values", ReflectAtom::Values},
            {"var", ReflectAtom::Var},
            {"from", ReflectAtom::From},
            {"to", ReflectAtom::To},
            {"step", ReflectAtom::Step},
            {"op", ReflectAtom::Op},
            {"args", ReflectAtom::Args},
            {"self", ReflectAtom::Self},
            {"index", ReflectAtom::Index},
            {"left", ReflectAtom::Left},
            {"right", ReflectAtom::Right},
            {"local", ReflectAtom::Local},
            {"trueExpr", ReflectAtom::TrueExpr},
            {"falseExpr", ReflectAtom::FalseExpr},
            {"prefix", ReflectAtom::Prefix},
            {"vararg", ReflectAtom::Vararg},
            {"hasSemicolon", ReflectAtom::HasSemicolon},
            {"generics", ReflectAtom::Generics},
            {"genericPacks", ReflectAtom::GenericPacks},
            {"returnAnnotation", ReflectAtom::ReturnAnnotation},
            {"exported", ReflectAtom::Exported},
            {"hasErrors", ReflectAtom::HasErrors},
            {"superName", ReflectAtom::SuperName},
            {"props", ReflectAtom::Props},
            {"indexer", ReflectAtom::Indexer},
            {"members", ReflectAtom::Members},
            {"statements", ReflectAtom::Statements},
            {"expressions", ReflectAtom::Expressions},
            {"strings", ReflectAtom::Strings},
            {"messageIndex", ReflectAtom::MessageIndex},
            {"typeArguments", ReflectAtom::TypeArguments},
            {"parameters", ReflectAtom::Parameters},
            {"hasParameterList", ReflectAtom::HasParameterList},
            {"argTypes", ReflectAtom::ArgTypes},
            {"returnTypes", ReflectAtom::ReturnTypes},
            {"attributes", ReflectAtom::Attributes},
            {"debugname", ReflectAtom::DebugName},
            {"upvalue", ReflectAtom::Upvalue},
            {"hasDo", ReflectAtom::HasDo},
            {"hasIn", ReflectAtom::HasIn},
            {"hasEnd", ReflectAtom::HasEnd},
            {"hasElse", ReflectAtom::HasElse},
            {"quoteStyle", ReflectAtom::QuoteStyle},
            {"isMissing", ReflectAtom::IsMissing},
            {"types", ReflectAtom::Types},
            {"tailType", ReflectAtom::TailType},
            {"variadicType", ReflectAtom::VariadicType},
            {"params", ReflectAtom::Params},

            {"hasAt", ReflectAtom::HasAt},
            {"openParenPosition", ReflectAtom::OpenParenPosition},
            {"closeParenPosition", ReflectAtom::CloseParenPosition},
            {"argsCommaPositions", ReflectAtom::ArgsCommaPositions},
            {"closePosition", ReflectAtom::ClosePosition},
            {"sourceString", ReflectAtom::SourceString},
            {"blockDepth", ReflectAtom::BlockDepth},
            {"openParens", ReflectAtom::OpenParens},
            {"closeParens", ReflectAtom::CloseParens},
            {"commaPositions", ReflectAtom::CommaPositions},
            {"openBracketPosition", ReflectAtom::OpenBracketPosition},
            {"closeBracketPosition", ReflectAtom::CloseBracketPosition},
            {"functionKeywordPosition", ReflectAtom::FunctionKeywordPosition},
            {"openGenericsPosition", ReflectAtom::OpenGenericsPosition},
            {"genericsCommaPositions", ReflectAtom::GenericsCommaPositions},
            {"closeGenericsPosition", ReflectAtom::CloseGenericsPosition},
            {"argsAnnotationColonPositions", ReflectAtom::ArgsAnnotationColonPositions},
            {"varargAnnotationColonPosition", ReflectAtom::VarargAnnotationColonPosition},
            {"returnSpecifierPosition", ReflectAtom::ReturnSpecifierPosition},
            {"opPosition", ReflectAtom::OpPosition},
            {"thenPosition", ReflectAtom::ThenPosition},
            {"elsePosition", ReflectAtom::ElsePosition},
            {"isElseIf", ReflectAtom::IsElseIf},
            {"statsStartPosition", ReflectAtom::StatsStartPosition},
            {"endPosition", ReflectAtom::EndPosition},
            {"untilPosition", ReflectAtom::UntilPosition},
            {"varsAnnotationColonPositions", ReflectAtom::VarsAnnotationColonPositions},
            {"varsCommaPositions", ReflectAtom::VarsCommaPositions},
            {"valuesCommaPositions", ReflectAtom::ValuesCommaPositions},
            {"annotationColonPosition", ReflectAtom::AnnotationColonPosition},
            {"equalsPosition", ReflectAtom::EqualsPosition},
            {"endCommaPosition", ReflectAtom::EndCommaPosition},
            {"stepCommaPosition", ReflectAtom::StepCommaPosition},
            {"localKeywordPosition", ReflectAtom::LocalKeywordPosition},
            {"defaultEqualsPosition", ReflectAtom::DefaultEqualsPosition},
            {"ellipsisPosition", ReflectAtom::EllipsisPosition},
            {"typeKeywordPosition", ReflectAtom::TypeKeywordPosition},
            {"genericsOpenPosition", ReflectAtom::GenericsOpenPosition},
            {"genericsClosePosition", ReflectAtom::GenericsClosePosition},
            {"prefixPointPosition", ReflectAtom::PrefixPointPosition},
            {"openParametersPosition", ReflectAtom::OpenParametersPosition},
            {"parametersCommaPositions", ReflectAtom::ParametersCommaPositions},
            {"closeParametersPosition", ReflectAtom::CloseParametersPosition},
            {"isArray", ReflectAtom::IsArray},
            {"openArgsPosition", ReflectAtom::OpenArgsPosition},
            {"argumentNameColonPositions", ReflectAtom::ArgumentNameColonPositions},
            {"argumentsCommaPositions", ReflectAtom::ArgumentsCommaPositions},
            {"closeArgsPosition", ReflectAtom::CloseArgsPosition},
            {"returnArrowPosition", ReflectAtom::ReturnArrowPosition},
            {"openPosition", ReflectAtom::OpenPosition},
            {"leadingPosition", ReflectAtom::LeadingPosition},
            {"separatorPositions", ReflectAtom::SeparatorPositions},
            {"openParenthesesPosition", ReflectAtom::OpenParenthesesPosition},
            {"closeParenthesesPosition", ReflectAtom::CloseParenthesesPosition},

            {"beginLine", ReflectAtom::BeginLine},
            {"beginColumn", ReflectAtom::BeginColumn},
            {"endLine", ReflectAtom::EndLine},
            {"endColumn", ReflectAtom::EndColumn},
            {"startOffset", ReflectAtom::StartOffset},
            {"endOffset", ReflectAtom::EndOffset},

            {"line", ReflectAtom::Line},
            {"column", ReflectAtom::Column},
            {"computedOffset", ReflectAtom::ComputedOffset},

            {"shadow", ReflectAtom::Shadow},
            {"depth", ReflectAtom::Depth},

            {"access", ReflectAtom::Access},
            {"indexType", ReflectAtom::IndexType},
            {"resultType", ReflectAtom::ResultType},
            {"isMethod", ReflectAtom::IsMethod},
        };
        DenseHashMap2<std::string_view, ReflectAtom> map;
        for (const auto& [k, v] : entries)
            map[k] = v;
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
void pushPositionArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::Position>& array);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableProp& prop);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableIndexer& indexer);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstDeclaredExternTypeProperty& prop);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassProperty& prop);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassMethod& method);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* local);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& comment);

// Check helpers
AstDocumentData& checkAstDocument(lua_State* L, int idx);
AstNodeData& checkAstNode(lua_State* L, int idx);
AstLocationData& checkAstLocation(lua_State* L, int idx);
CstNodeData& checkCstNode(lua_State* L, int idx);
AstPositionData& checkAstPosition(lua_State* L, int idx);
AstAuxData& checkAstAux(lua_State* L, int idx);

// Array push helpers
template<typename T>
inline void pushNodeArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<T*>& array)
{
    lua_createtable(L, int(array.size), 0);
    for (size_t i = 0; i < array.size; i++)
    {
        pushAstNode(L, doc, array.data[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
}

inline void pushLocalArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::AstLocal*>& array)
{
    lua_createtable(L, int(array.size), 0);
    for (size_t i = 0; i < array.size; i++)
    {
        pushAstAux(L, doc, array.data[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
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
    lua_createtable(L, int(array.size), 0);
    for (size_t i = 0; i < array.size; i++)
    {
        pushTypeOrPack(L, doc, array.data[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
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
    bool errorOccurred = false;

    CallbackVisitor(lua_State* L, std::shared_ptr<AstDocumentState> doc, int callbackIndex)
        : L(L)
        , doc(doc)
        , callbackIndex(callbackIndex)
    {
    }

    bool visit(Luau::AstNode* node) override
    {
        if (errorOccurred || !node)
            return false;

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

struct FindKindVisitor : public Luau::AstVisitor
{
    std::string_view targetKind;
    std::vector<Luau::AstNode*> matches;

    FindKindVisitor(std::string_view targetKind)
        : targetKind(targetKind)
    {
    }

    bool visit(Luau::AstNode* node) override
    {
        if (node && getNodeKind(node) == targetKind)
            matches.push_back(node);
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
    lua_createtable(L, 0, (eq ? 4 : 3) + (methods ? 4 : 0) + (namecall ? 1 : 0));
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


// Module registration functions
void registerAstDocument(lua_State* L);
void registerAstNode(lua_State* L);
void registerAstLocation(lua_State* L);
void registerCstNode(lua_State* L);
void registerAstPosition(lua_State* L);
void registerAstAux(lua_State* L);

} // namespace Luau
