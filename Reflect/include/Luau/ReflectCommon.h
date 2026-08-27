// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Reflect.h"

#include "Luau/Ast.h"
#include "Luau/Cst.h"
#include "Luau/Parser.h"
#include "Luau/Location.h"
#include "Luau/Common.h"

#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <cstring>
#include <new>

namespace Luau
{

/**
 * Reserved userdata tags for AST/CST reflection objects.
 *
 * SAFETY: The use of reserved tags in this range (10-17) is safe: embedders are expected
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
    TagLocal = 12,
    TagLocation = 13,
    TagCstNode = 14,
    TagPosition = 15,
    TagComment = 16,
    TagAux = 17,
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

struct Ref
{
    int ref = LUA_NOREF;

    bool isValid() const
    {
        return ref != LUA_NOREF;
    }

    void create(lua_State* L, int idx)
    {
        release(L);
        ref = lua_refpool(L, idx);
    }

    int get(lua_State* L) const
    {
        return lua_getrefpool(L, ref);
    }

    void release(lua_State* L)
    {
        if (ref != LUA_NOREF)
        {
            lua_unrefpool(L, ref);
            ref = LUA_NOREF;
        }
    }
};

struct AstDocumentData
{
    std::shared_ptr<AstDocumentState> doc;
    Ref cacheRef;
};

struct AstNodeData
{
    std::shared_ptr<AstDocumentState> doc;
    Luau::AstNode* node = nullptr;
};

struct AstLocalData
{
    std::shared_ptr<AstDocumentState> doc;
    Luau::AstLocal* local = nullptr;
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

struct AstCommentData
{
    std::shared_ptr<AstDocumentState> doc;
    Luau::Comment comment;
};

enum AstAuxKind : uint8_t
{
    Aux_TableProp,
    Aux_TableIndexer,
    Aux_DeclaredExternTypeProperty,
    Aux_ClassProperty,
    Aux_ClassMethod,
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
void pushAstLocal(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* local);
void pushLocation(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Location& loc);
void pushCstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::CstNode* node);
void pushPosition(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Position& pos);
void pushPositionArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::Position>& array);
void pushAstComment(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& comment);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableProp& prop);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableIndexer& indexer);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstDeclaredExternTypeProperty& prop);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassProperty& prop);
void pushAstAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassMethod& method);

// Check helpers
AstDocumentData& checkAstDocument(lua_State* L, int idx);
AstNodeData& checkAstNode(lua_State* L, int idx);
AstLocalData& checkAstLocal(lua_State* L, int idx);
AstLocationData& checkAstLocation(lua_State* L, int idx);
CstNodeData& checkCstNode(lua_State* L, int idx);
AstPositionData& checkAstPosition(lua_State* L, int idx);
AstCommentData& checkAstComment(lua_State* L, int idx);
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
        pushAstLocal(L, doc, array.data[i]);
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
    const luaL_Reg* methods = nullptr
)
{
    lua_setuserdatadtor(L, tag, dtor);
    lua_createtable(L, 0, (eq ? 4 : 3) + (methods ? 4 : 0));
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

// Push cached value using lua_refpool on demand
template<typename InitFn>
inline void pushCachedValue(lua_State* L, Ref& cacheRef, const char* cacheKey, InitFn&& init)
{
    if (!cacheRef.isValid())
    {
        lua_createtable(L, 0, 4);
        cacheRef.create(L, -1);
    }
    else
    {
        cacheRef.get(L);
    }

    lua_getfield(L, -1, cacheKey);
    if (!lua_isnil(L, -1))
    {
        lua_remove(L, -2);
        return;
    }

    lua_pop(L, 1);
    init(L);

    lua_pushvalue(L, -1);
    lua_setfield(L, -3, cacheKey);
    lua_remove(L, -2);
}

// Module registration functions
void registerAstDocument(lua_State* L);
void registerAstNode(lua_State* L);
void registerAstLocal(lua_State* L);
void registerAstLocation(lua_State* L);
void registerCstNode(lua_State* L);
void registerAstPosition(lua_State* L);
void registerAstComment(lua_State* L);
void registerAstAux(lua_State* L);

} // namespace Luau
