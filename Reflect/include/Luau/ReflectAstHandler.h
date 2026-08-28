// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/ReflectCommon.h"
#include <type_traits>
#include <cstring>

namespace Luau
{

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, bool val)
{
    lua_pushboolean(L, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int64_t val)
{
    lua_pushinteger64(L, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, uint64_t val)
{
    lua_pushinteger64(L, int64_t(val));
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, char val)
{
    lua_pushlstring(L, &val, 1);
}

template<typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && (sizeof(T) < 8)>>
inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, T val)
{
    lua_pushinteger(L, int(val));
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, double val)
{
    lua_pushnumber(L, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstName& val)
{
    if (val.value)
        lua_pushstring(L, val.value);
    else
        lua_pushnil(L);
}

template<typename T>
inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const std::optional<T>& val)
{
    if (val)
        pushReflectValue(L, doc, *val);
    else
        lua_pushnil(L);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<char>& val)
{
    lua_pushlstring(L, val.data, val.size);
}

template<typename T>
inline std::enable_if_t<std::is_enum_v<T>> pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, T val)
{
    lua_pushstring(L, toString(val).c_str());
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableIndexer* indexer)
{
    if (indexer)
        pushAstAux(L, doc, *indexer);
    else
        lua_pushnil(L);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstNode* val)
{
    if (val)
        pushAstNode(L, doc, val);
    else
        lua_pushnil(L);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, Luau::AstLocal* val)
{
    if (val)
        pushAstAux(L, doc, val);
    else
        lua_pushnil(L);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTypeOrPack& val)
{
    pushTypeOrPack(L, doc, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTypeList& val)
{
    pushAstAux(L, doc, val);
}

template<typename T, typename = std::enable_if_t<std::is_base_of_v<Luau::AstNode, T>>>
inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<T*>& array)
{
    pushNodeArray(L, doc, array);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::AstLocal*>& array)
{
    pushLocalArray(L, doc, array);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstExprTable::Item& val)
{
    pushAstAux(L, doc, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstTableProp& val)
{
    pushAstAux(L, doc, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstDeclaredExternTypeProperty& val)
{
    pushAstAux(L, doc, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassProperty& val)
{
    pushAstAux(L, doc, val);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstClassMember& member)
{
    if (const auto* p = Luau::get_if<Luau::AstClassProperty>(&member))
        pushAstAux(L, doc, *p);
    else if (const auto* m = Luau::get_if<Luau::AstClassMethod>(&member))
        pushAstAux(L, doc, *m);
    else
        lua_pushnil(L);
}

template<typename T>
inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<T>& val)
{
    pushArray(L, val.size, [&](size_t i) {
        pushReflectValue(L, doc, val.data[i]);
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, bool& out)
{
    out = (lua_toboolean(L, argIdx) != 0);
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, int64_t& out)
{
    if (lua_isnumber(L, argIdx))
        out = static_cast<int64_t>(lua_tointeger(L, argIdx));
    else
        out = luaL_checkinteger64(L, argIdx);
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, uint64_t& out)
{
    if (lua_isnumber(L, argIdx))
        out = static_cast<uint64_t>(lua_tointeger(L, argIdx));
    else
        out = static_cast<uint64_t>(luaL_checkinteger64(L, argIdx));
}

template<typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, int64_t> && !std::is_same_v<T, uint64_t>>>
inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, T& out)
{
    out = static_cast<T>(luaL_checkinteger(L, argIdx));
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, double& out)
{
    out = luaL_checknumber(L, argIdx);
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstName& out)
{
    if (lua_isnil(L, argIdx))
    {
        out = Luau::AstName();
    }
    else
    {
        size_t len = 0;
        const char* s = luaL_checklstring(L, argIdx, &len);
        out = doc->names ? doc->names->getOrAdd(s, len) : Luau::AstName(s);
    }
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<char>& out)
{
    size_t len = 0;
    const char* s = luaL_checklstring(L, argIdx, &len);
    char* copy = static_cast<char*>(doc->allocator->allocate(len));
    std::memcpy(copy, s, len);
    out = Luau::AstArray<char>{copy, len};
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, const Luau::AstArray<char>& out)
{
    Luau::AstArray<char>& mutableOut = const_cast<Luau::AstArray<char>&>(out);
    readReflectValue(L, doc, argIdx, mutableOut);
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

template<typename T, typename = std::enable_if_t<std::is_base_of_v<Luau::AstNode, T>>>
inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, T*& out)
{
    if (lua_isnil(L, argIdx))
    {
        out = nullptr;
        return;
    }
    auto& nodeData = checkAstNode(L, argIdx);
    if (!nodeData.node)
        luaL_typeerror(L, argIdx, "AstNode");
    T* casted = castAstNode<T>(nodeData.node);
    if (!casted)
        luaL_error(L, "invalid AstNode subtype at argument #%d", argIdx);
    out = casted;
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstLocal*& out)
{
    if (lua_isnil(L, argIdx))
    {
        out = nullptr;
        return;
    }
    auto& aux = checkAstAux(L, argIdx);
    if (aux.kind != Aux_Local || !aux.local)
        luaL_typeerror(L, argIdx, "AstLocal");
    out = aux.local;
}

// Luwu will pass a table to the setter so we need to allocate space for the passed table when reading from luau
template<typename T, typename F>
inline Luau::AstArray<T> readArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, F&& readElem)
{
    luaL_checktype(L, argIdx, LUA_TTABLE);
    int len = lua_objlen(L, argIdx);
    T* buffer = static_cast<T*>(doc->allocator->allocate(sizeof(T) * len));
    for (int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, argIdx, i);
        buffer[i - 1] = readElem(i);
        lua_pop(L, 1);
    }
    return Luau::AstArray<T>{buffer, size_t(len)};
}

template<typename T, typename = std::enable_if_t<std::is_base_of_v<Luau::AstNode, T>>>
inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<T*>& out)
{
    out = readArray<T*>(L, doc, argIdx, [&](int i) {
        auto& nodeData = checkAstNode(L, -1);
        T* casted = castAstNode<T>(nodeData.node);
        if (!casted)
            luaL_error(L, "invalid AstNode subtype at table index %d", i);
        return casted;
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstLocal*>& out)
{
    out = readArray<Luau::AstLocal*>(L, doc, argIdx, [&](int i) {
        auto& aux = checkAstAux(L, -1);
        if (aux.kind != Aux_Local || !aux.local)
            luaL_error(L, "expected AstLocal at table index %d", i);
        return aux.local;
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, std::optional<Luau::AstName>& out)
{
    if (lua_isnil(L, argIdx))
    {
        out = std::nullopt;
    }
    else
    {
        Luau::AstName name;
        readReflectValue(L, doc, argIdx, name);
        out = name;
    }
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstTypeList& out)
{
    auto& aux = checkAstAux(L, argIdx);
    if (aux.kind != Aux_TypeList)
        luaL_typeerror(L, argIdx, "AstTypeList");
    out = aux.typeList;
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstTableIndexer*& out)
{
    if (lua_isnil(L, argIdx))
    {
        out = nullptr;
        return;
    }
    auto& aux = checkAstAux(L, argIdx);
    if (aux.kind != Aux_TableIndexer)
        luaL_typeerror(L, argIdx, "AstTableIndexer");
    auto* copy = static_cast<Luau::AstTableIndexer*>(doc->allocator->allocate(sizeof(Luau::AstTableIndexer)));
    *copy = aux.tableIndexer;
    out = copy;
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, const Luau::AstTableIndexer*& out)
{
    Luau::AstTableIndexer* mutablePtr = nullptr;
    readReflectValue(L, doc, argIdx, mutablePtr);
    out = mutablePtr;
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstArray<char>>& out)
{
    out = readArray<Luau::AstArray<char>>(L, doc, argIdx, [&](int) {
        Luau::AstArray<char> s;
        readReflectValue(L, doc, -1, s);
        return s;
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstTableProp>& out)
{
    out = readArray<Luau::AstTableProp>(L, doc, argIdx, [&](int i) {
        auto& aux = checkAstAux(L, -1);
        if (aux.kind != Aux_TableProp)
            luaL_error(L, "expected AstTableProp at table index %d", i);
        return aux.tableProp;
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstDeclaredExternTypeProperty>& out)
{
    out = readArray<Luau::AstDeclaredExternTypeProperty>(L, doc, argIdx, [&](int i) {
        auto& aux = checkAstAux(L, -1);
        if (aux.kind != Aux_DeclaredExternTypeProperty)
            luaL_error(L, "expected AstDeclaredExternTypeProperty at table index %d", i);
        return aux.declaredExternProp;
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstClassMember>& out)
{
    out = readArray<Luau::AstClassMember>(L, doc, argIdx, [&](int i) {
        auto& aux = checkAstAux(L, -1);
        if (aux.kind == Aux_ClassProperty)
            return Luau::AstClassMember(aux.classProp);
        if (aux.kind == Aux_ClassMethod)
            return Luau::AstClassMember(aux.classMethod);
        luaL_error(L, "expected AstClassProperty or AstClassMethod at table index %d", i);
        return Luau::AstClassMember();
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstExprTable::Item>& out)
{
    out = readArray<Luau::AstExprTable::Item>(L, doc, argIdx, [&](int i) {
        auto& aux = checkAstAux(L, -1);
        if (aux.kind != Aux_TableItem)
            luaL_error(L, "expected AstTableItem at table index %d", i);
        return aux.tableItem;
    });
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstTypeOrPack& out)
{
    if (lua_isnil(L, argIdx))
    {
        out = Luau::AstTypeOrPack{};
        return;
    }
    auto& nodeData = checkAstNode(L, argIdx);
    if (!nodeData.node)
        luaL_typeerror(L, argIdx, "AstNode");
    if (auto* t = nodeData.node->asType())
        out = Luau::AstTypeOrPack{t, nullptr};
    else if (auto* tp = castAstNode<Luau::AstTypePack>(nodeData.node))
        out = Luau::AstTypeOrPack{nullptr, tp};
    else
        luaL_error(L, "expected AstType or AstTypePack at argument #%d", argIdx);
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::AstArray<Luau::AstTypeOrPack>& out)
{
    out = readArray<Luau::AstTypeOrPack>(L, doc, argIdx, [&](int i) {
        auto& nodeData = checkAstNode(L, -1);
        if (auto* t = nodeData.node->asType())
            return Luau::AstTypeOrPack{t, nullptr};
        else if (auto* tp = castAstNode<Luau::AstTypePack>(nodeData.node))
            return Luau::AstTypeOrPack{nullptr, tp};
        luaL_error(L, "expected AstType or AstTypePack at table index %d", i);
        return Luau::AstTypeOrPack{};
    });
}

template<typename T>
inline std::enable_if_t<std::is_enum_v<T>> readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, T& out)
{
    size_t len = 0;
    const char* str = luaL_checklstring(L, argIdx, &len);
    if (auto res = Luau::fromString<T>(std::string_view(str, len)))
        out = *res;
    else
        luaL_error(L, "invalid enum value '%.*s'", int(len), str);
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, char& out)
{
    size_t len = 0;
    const char* s = luaL_checklstring(L, argIdx, &len);
    if (len != 1)
        luaL_error(L, "expected single character for op");
    out = s[0];
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Location& loc)
{
    pushLocation(L, doc, loc);
}

inline void pushReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Position& pos)
{
    pushPosition(L, doc, pos);
}

inline void readReflectValue(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int argIdx, Luau::Location& out)
{
    out = checkAstLocation(L, argIdx).location;
}

#define LUAU_AUX_FIELD_RW(atomGet, atomSet, memberExpr) \
    case ReflectAtom::atomGet: pushReflectValue(L, handle.doc, memberExpr); return true; \
    case ReflectAtom::atomSet: readReflectValue(L, handle.doc, 2, memberExpr); lua_pushvalue(L, 1); return true;

#define LUAU_AUX_FIELD_RO(atomGet, memberExpr) \
    case ReflectAtom::atomGet: pushReflectValue(L, handle.doc, memberExpr); return true;

#define LUAU_AST_HANDLER_START(name, NodeClass) \
    static bool name(lua_State* L, AstNodeData& handle, ReflectAtom atom) \
    { \
        using NodeType = Luau::NodeClass; \
        auto* n = static_cast<NodeType*>(handle.node); \
        switch (atom) \
        {

#define LUAU_AST_FIELD_RW(atomGet, atomSet, memberName) \
    case ReflectAtom::atomGet: pushReflectValue(L, handle.doc, n->memberName); return true; \
    case ReflectAtom::atomSet: readReflectValue(L, handle.doc, 2, n->memberName); lua_pushvalue(L, 1); return true;

#define LUAU_AST_FIELD_RO(atomGet, memberName) \
    case ReflectAtom::atomGet: pushReflectValue(L, handle.doc, n->memberName); return true;

#define LUAU_AST_HANDLER_END() \
        default: \
            return false; \
        } \
    }

} // namespace Luau
