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

// Reserved userdata tags for AST/CST reflection objects
enum AstUserdataTag : int
{
    TagDocument = 10,
    TagNode = 11,
    TagLocal = 12,
    TagLocation = 13,
    TagCstNode = 14,
    TagPosition = 15,
    TagComment = 16,
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

// Line and location offset utilities (adapted from lute)
std::vector<size_t> computeLineOffsets(std::string_view content);
std::pair<size_t, size_t> locationToOffsets(const std::vector<size_t>& lineOffsets, size_t sourceLen, const Luau::Location& loc);

// Push helpers
void pushAstDocument(lua_State* L, std::shared_ptr<AstDocumentState> doc);
void pushAstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstNode* node);
void pushAstLocal(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* local);
void pushLocation(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Location& loc);
void pushCstNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::CstNode* node);
void pushPosition(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Position& pos);
void pushPositionArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::Position>& array);
void pushAstComment(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& comment);

// Check helpers
std::shared_ptr<AstDocumentState>& checkAstDocument(lua_State* L, int idx);
AstNodeData& checkAstNode(lua_State* L, int idx);
AstLocalData& checkAstLocal(lua_State* L, int idx);
AstLocationData& checkAstLocation(lua_State* L, int idx);
CstNodeData& checkCstNode(lua_State* L, int idx);
AstPositionData& checkAstPosition(lua_State* L, int idx);
AstCommentData& checkAstComment(lua_State* L, int idx);

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

// Module registration functions
void registerAstDocument(lua_State* L);
void registerAstNode(lua_State* L);
void registerAstLocal(lua_State* L);
void registerAstLocation(lua_State* L);
void registerCstNode(lua_State* L);
void registerAstPosition(lua_State* L);
void registerAstComment(lua_State* L);

} // namespace Luau
