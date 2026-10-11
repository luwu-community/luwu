// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/ReflectCommon.h"
#include <type_traits>
#include <cstring>

namespace Luau
{

inline void checkSameAllocator(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const std::shared_ptr<AstDocumentState>& other, const char* what)
{
    if (doc && other && other->allocator() != doc->allocator())
        luaL_error(L, "cross-allocator %s assignment is not permitted", what);
}

inline void beginReflectWrite(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, bool structural)
{
    AstAllocatorState& arena = *doc->arena;
    if (structural && arena.activeWalks > 0)
        luaL_error(L, "cannot change the structure of an AST while it is being walked");
    arena.mutated = true;
}

inline Luau::AstName readName(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    if (lua_isnil(L, idx))
        return Luau::AstName();
    size_t len = 0;
    const char* s = luaL_checklstring(L, idx, &len);
    return doc->names() ? doc->names()->getOrAdd(s, len) : Luau::AstName(s);
}

inline Luau::AstArray<char> readString(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    size_t len = 0;
    const char* s = luaL_checklstring(L, idx, &len);
    char* copy = static_cast<char*>(doc->allocator()->allocate(len));
    std::memcpy(copy, s, len);
    return Luau::AstArray<char>{copy, len};
}

inline char readChar(lua_State* L, int idx)
{
    size_t len = 0;
    const char* s = luaL_checklstring(L, idx, &len);
    if (len != 1)
        luaL_error(L, "expected single character for op");
    return s[0];
}

template<typename T>
inline T readEnum(lua_State* L, int idx)
{
    size_t len = 0;
    const char* str = luaL_checklstring(L, idx, &len);
    if (auto res = Luau::fromString<T>(std::string_view(str, len)))
        return *res;
    luaL_error(L, "invalid enum value '%.*s'", int(len), str);
}

template<typename T>
inline T* castAstNode(Luau::AstNode* node)
{
    if (!node)
        return nullptr;
    if constexpr (std::is_same_v<T, Luau::AstNode>)
        return node;
    else if constexpr (std::is_same_v<T, Luau::AstExpr>)
        return node->asExpr();
    else if constexpr (std::is_same_v<T, Luau::AstStat>)
        return node->asStat();
    else if constexpr (std::is_same_v<T, Luau::AstType>)
        return node->asType();
    else if constexpr (std::is_same_v<T, Luau::AstAttr>)
        return node->asAttr();
    else if constexpr (std::is_same_v<T, Luau::AstTypePack>)
        return (node->as<Luau::AstTypePackExplicit>() || node->as<Luau::AstTypePackVariadic>() || node->as<Luau::AstTypePackGeneric>())
            ? static_cast<Luau::AstTypePack*>(node)
            : nullptr;
    else
        return node->as<T>();
}

template<typename T>
inline T* readNode(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    if (lua_isnil(L, idx))
        return nullptr;
    auto& nodeData = checkAstNode(L, idx);
    if (!nodeData.node)
        luaL_typeerror(L, idx, "AstNode");
    if (doc && nodeData.doc && nodeData.doc->allocator() != doc->allocator())
        luaL_error(L, "cross-allocator AST node assignment is not permitted");
    T* casted = castAstNode<T>(nodeData.node);
    if (!casted)
        luaL_error(L, "invalid AstNode subtype at argument #%d", idx);
    return casted;
}

inline Luau::AstLocal* readLocal(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    if (lua_isnil(L, idx))
        return nullptr;
    auto& handle = checkAstLocal(L, idx);
    if (!handle.local)
        luaL_typeerror(L, idx, "AstLocal");
    if (doc && handle.doc && handle.doc->allocator() != doc->allocator())
        luaL_error(L, "cross-allocator AstLocal assignment is not permitted");
    return handle.local;
}

// Luwu will pass a table to the setter so we need to allocate space for the passed table when reading from luau
template<typename T, typename F>
inline Luau::AstArray<T> readArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, F&& readElem)
{
    luaL_checktype(L, argIdx, LUA_TTABLE);
    int len = lua_objlen(L, argIdx);
    T* buffer = static_cast<T*>(doc->allocator()->allocate(sizeof(T) * len));
    for (int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, argIdx, i);
        new (&buffer[i - 1]) T(readElem(i));
        lua_pop(L, 1);
    }
    return Luau::AstArray<T>{buffer, size_t(len)};
}

template<typename T>
inline Luau::AstArray<T*> readNodeArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    return readArray<T*>(L, doc, idx, [&](int i) {
        auto& nodeData = checkAstNode(L, -1);
        if (doc && nodeData.doc && nodeData.doc->allocator() != doc->allocator())
            luaL_error(L, "cross-allocator AST node assignment is not permitted at table index %d", i);
        T* casted = castAstNode<T>(nodeData.node);
        if (!casted)
            luaL_error(L, "invalid AstNode subtype at table index %d", i);
        return casted;
    });
}

template<typename T>
inline Luau::AstArray<T*> readOptionalNodeArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    return readArray<T*>(L, doc, idx, [&](int i) -> T* {
        if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
            return nullptr;
        auto& nodeData = checkAstNode(L, -1);
        if (doc && nodeData.doc && nodeData.doc->allocator() != doc->allocator())
            luaL_error(L, "cross-allocator AST node assignment is not permitted at table index %d", i);
        T* casted = castAstNode<T>(nodeData.node);
        if (!casted)
            luaL_error(L, "invalid AstNode subtype at table index %d", i);
        return casted;
    });
}

inline Luau::AstArray<Luau::AstLocal*> readLocalArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    return readArray<Luau::AstLocal*>(L, doc, idx, [&](int) {
        return readLocal(L, doc, -1);
    });
}

inline Luau::AstArray<Luau::AstTypeOrPack> readTypeOrPackArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    return readArray<Luau::AstTypeOrPack>(L, doc, idx, [&](int) {
        if (lua_isnil(L, -1))
            return Luau::AstTypeOrPack{};
        auto& nodeData = checkAstNode(L, -1);
        if (!nodeData.node)
            luaL_typeerror(L, -1, "AstNode");
        checkSameAllocator(L, doc, nodeData.doc, "AST node");
        if (auto* t = nodeData.node->asType())
            return Luau::AstTypeOrPack{t, nullptr};
        if (auto* tp = castAstNode<Luau::AstTypePack>(nodeData.node))
            return Luau::AstTypeOrPack{nullptr, tp};
        luaL_error(L, "expected AstType or AstTypePack at argument #%d", -1);
    });
}

template<typename T>
inline T readAux(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx, AstAuxKind kind, T AstAuxData::* member, const char* name)
{
    auto& aux = checkAstAux(L, idx);
    if (aux.kind != kind)
        luaL_typeerror(L, idx, name);
    checkSameAllocator(L, doc, aux.doc, name);
    return aux.*member;
}

inline Luau::AstClassMember readClassMember(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    auto& aux = checkAstAux(L, idx);
    if (aux.kind == Aux_ClassProperty)
    {
        checkSameAllocator(L, doc, aux.doc, "AstClassProperty");
        Luau::AstClassProperty prop = aux.classProp;
        if (prop.ty && !prop.typeColonLocation)
            prop.typeColonLocation = prop.ty->location;
        return Luau::AstClassMember(prop);
    }
    if (aux.kind == Aux_ClassMethod)
    {
        checkSameAllocator(L, doc, aux.doc, "AstClassMethod");
        return Luau::AstClassMember(aux.classMethod);
    }
    luaL_error(L, "expected AstClassProperty or AstClassMethod at argument #%d", idx);
}

inline void pushClassMembers(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::AstClassMember>& members)
{
    pushArray(L, members.size, [&](size_t i) {
        if (const auto* p = Luau::get_if<Luau::AstClassProperty>(&members.data[i]))
            pushAstAux(L, doc, *p);
        else if (const auto* m = Luau::get_if<Luau::AstClassMethod>(&members.data[i]))
            pushAstAux(L, doc, *m);
        else
            lua_pushnil(L);
    });
}

inline const Luau::CstNode* getNodeCst(const AstNodeData& handle, const Luau::AstNode* n)
{
    if (!handle.doc)
        return nullptr;
    if (const Luau::CstNode* const* cst = handle.doc->parseResult.cstNodeMap.find(const_cast<Luau::AstNode*>(n)))
        return *cst;
    return nullptr;
}

struct AstValidator
{
    lua_State* L;
    bool deep;
    bool forPrint;
    int maxDepth;

    int depth = 0;
    int tallestChild = 0;
    const char* kind = "AstNode";
    DenseHashMap2<const Luau::AstNode*, int> heights;

    void node(Luau::AstNode* node);
    void child(Luau::AstNode* node, const char* field, int index, const char* missing);
    void local(Luau::AstLocal* local, const char* field, int index, const char* missing);
    void name(const Luau::AstName& name, const char* field, int index, const char* missing);
    [[noreturn]] void fail(const char* field, int index, const char* problem);
};

enum class AstValidation
{
    Node,
    Walk,
    Print
};

void validateAstNode(lua_State* L, const AstNodeData& handle, AstValidation mode);

} // namespace Luau
