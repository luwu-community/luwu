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

enum class ReflectWrite
{
    Value,
    Validity,
    Structural,
};

inline void beginReflectWrite(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, ReflectWrite write)
{
    if (write == ReflectWrite::Structural && treeOf(*doc).activeWalks > 0)
        luaL_error(L, "cannot change the structure of an AST while it is being walked");
}

inline void finishReflectWrite(const std::shared_ptr<AstDocumentState>& doc, ReflectWrite write)
{
    AstTreeState& tree = treeOf(*doc);
    tree.edits++;
    if (write != ReflectWrite::Value)
    {
        tree.structureEdits++;
        tree.untainted.clear();
    }
}

inline Luau::AstName readName(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    if (lua_isnil(L, idx))
        return Luau::AstName();
    size_t len = 0;
    const char* s = luaL_checklstring(L, idx, &len);
    if (len == 0)
        luaL_error(L, "a name cannot be an empty string");
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
    if (doc && nodeData.doc && nodeData.doc->allocator() != doc->allocator())
        luaL_error(L, "cross-allocator AST node assignment is not permitted");
    T* casted = castAstNode<T>(nodeData.node());
    if (!casted)
        luaL_error(L, "invalid AstNode subtype at argument #%d", idx);
    return casted;
}

inline Luau::AstLocal* readLocal(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    if (lua_isnil(L, idx))
        return nullptr;
    auto& handle = checkItem<Luau::AstLocal>(L, idx);
    if (doc && handle.doc && handle.doc->allocator() != doc->allocator())
        luaL_error(L, "cross-allocator AstLocal assignment is not permitted");
    return static_cast<Luau::AstLocal*>(handle.target);
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
        T* casted = castAstNode<T>(nodeData.node());
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
        T* casted = castAstNode<T>(nodeData.node());
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
        checkSameAllocator(L, doc, nodeData.doc, "AST node");
        if (auto* t = nodeData.node()->asType())
            return Luau::AstTypeOrPack{t, nullptr};
        if (auto* tp = castAstNode<Luau::AstTypePack>(nodeData.node()))
            return Luau::AstTypeOrPack{nullptr, tp};
        luaL_error(L, "expected AstType or AstTypePack at argument #%d", -1);
    });
}

template<typename T>
inline Luau::AstArray<T> readArgumentNames(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    return readArray<T>(L, doc, idx, [&](int) -> T {
        if (!lua_toboolean(L, -1))
            return T{};
        return T(Luau::AstArgumentName(readName(L, doc, -1), Luau::Location()));
    });
}

template<typename T>
inline Luau::AstArray<T> realignCompanion(
    const std::shared_ptr<AstDocumentState>& doc,
    const Luau::AstArray<Luau::AstLocal*>& before,
    const Luau::AstArray<Luau::AstLocal*>& after,
    const Luau::AstArray<T>& companion
)
{
    if (companion.size == 0)
        return companion;
    if (after.size == 0)
        return Luau::AstArray<T>{nullptr, 0};

    T* buffer = static_cast<T*>(doc->allocator()->allocate(sizeof(T) * after.size));
    for (size_t i = 0; i < after.size; i++)
    {
        new (&buffer[i]) T();
        for (size_t j = 0; j < before.size && j < companion.size; j++)
        {
            if (before.data[j] == after.data[i])
            {
                buffer[i] = companion.data[j];
                copyExplicitVisibility(treeOf(*doc), &companion.data[j], &buffer[i]);
                break;
            }
        }
    }
    return Luau::AstArray<T>{buffer, after.size};
}

template<typename T>
inline Luau::AstArray<T> fitCompanion(
    lua_State* L,
    const std::shared_ptr<AstDocumentState>& doc,
    int idx,
    const Luau::AstArray<T>& list,
    size_t count,
    const char* field,
    const char* owner
)
{
    if (list.size > count)
        luaL_error(L, "%s has more entries than %s", field, owner);
    if (list.size == 0 || list.size == count)
        return list;

    T* buffer = static_cast<T*>(doc->allocator()->allocate(sizeof(T) * count));
    for (size_t i = 0; i < count; i++)
        new (&buffer[i]) T();

    for (size_t i = 0; i < list.size; i++)
    {
        buffer[i] = list.data[i];

        lua_rawgeti(L, idx, int(i + 1));
        AstItem* item = static_cast<AstItem*>(lua_touserdatatagged(L, -1, TagItem));
        if (item && item->target == &list.data[i])
        {
            copyExplicitVisibility(treeOf(*doc), item->target, &buffer[i]);
            item->target = &buffer[i];
        }
        lua_pop(L, 1);
    }
    return Luau::AstArray<T>{buffer, count};
}

template<typename T>
inline AstItem& checkOwnedItem(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    AstItem& item = checkItem<T>(L, idx);
    checkSameAllocator(L, doc, item.doc, item.cls->kind);
    return item;
}

template<typename T>
inline void readItemInto(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx, T& dest)
{
    AstItem& item = checkOwnedItem<T>(L, doc, idx);
    if (item.target != &dest)
        dest = *static_cast<T*>(item.target);
    copyExplicitVisibility(treeOf(*doc), item.target, &dest);
    item.target = &dest;
}

template<typename T>
inline T* readItemPointer(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    if (lua_isnil(L, idx))
        return nullptr;
    AstItem& item = checkOwnedItem<T>(L, doc, idx);
    T* copy = doc->allocator()->alloc<T>(*static_cast<T*>(item.target));
    copyExplicitVisibility(treeOf(*doc), item.target, copy);
    item.target = copy;
    return copy;
}

template<typename T>
inline Luau::AstArray<T> readItemArray(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    Luau::AstArray<T> result = readArray<T>(L, doc, idx, [&](int) -> T {
        return *static_cast<T*>(checkOwnedItem<T>(L, doc, -1).target);
    });

    for (size_t i = 0; i < result.size; i++)
    {
        lua_rawgeti(L, idx, int(i + 1));
        AstItem* item = static_cast<AstItem*>(lua_touserdata(L, -1));
        copyExplicitVisibility(treeOf(*doc), item->target, &result.data[i]);
        item->target = &result.data[i];
        lua_pop(L, 1);
    }

    return result;
}

inline Luau::AstArray<Luau::AstClassMember> readClassMembers(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, int idx)
{
    Luau::AstArray<Luau::AstClassMember> result = readArray<Luau::AstClassMember>(L, doc, idx, [&](int) -> Luau::AstClassMember {
        AstItem* item = static_cast<AstItem*>(lua_touserdatatagged(L, -1, TagItem));
        if (item && item->cls == itemClass<Luau::AstClassProperty>())
        {
            checkSameAllocator(L, doc, item->doc, item->cls->kind);
            Luau::AstClassProperty prop = *static_cast<Luau::AstClassProperty*>(item->target);
            if (prop.ty && !prop.typeColonLocation)
                prop.typeColonLocation = prop.ty->location;
            return Luau::AstClassMember(prop);
        }
        if (item && item->cls == itemClass<Luau::AstClassMethod>())
        {
            checkSameAllocator(L, doc, item->doc, item->cls->kind);
            return Luau::AstClassMember(*static_cast<Luau::AstClassMethod*>(item->target));
        }
        luaL_error(L, "expected AstClassProperty or AstClassMethod at argument #%d", -1);
    });

    for (size_t i = 0; i < result.size; i++)
    {
        lua_rawgeti(L, idx, int(i + 1));
        AstItem* item = static_cast<AstItem*>(lua_touserdata(L, -1));
        if (auto* prop = Luau::get_if<Luau::AstClassProperty>(&result.data[i]))
            item->target = prop;
        else
            item->target = Luau::get_if<Luau::AstClassMethod>(&result.data[i]);
        lua_pop(L, 1);
    }

    return result;
}

inline void pushClassMembers(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<Luau::AstClassMember>& members)
{
    pushArray(L, members.size, [&](size_t i) {
        if (auto* p = Luau::get_if<Luau::AstClassProperty>(&members.data[i]))
            pushItem(L, doc, p);
        else if (auto* m = Luau::get_if<Luau::AstClassMethod>(&members.data[i]))
            pushItem(L, doc, m);
        else
            lua_pushnil(L);
    });
}

inline const Luau::CstNode* getNodeCst(const AstItem& handle, const Luau::AstNode* n)
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

    const char* kind = "AstNode";
    DenseHashMap2<const Luau::AstNode*, bool> finished;
    std::vector<const Luau::AstNode*> order;
    std::vector<Luau::AstNode*> found;
    const DenseHashMap2<const Luau::AstNode*, bool>* untainted = nullptr;

    void node(Luau::AstNode* root);
    void child(Luau::AstNode* node, const char* field, int index, const char* missing);
    void local(Luau::AstLocal* local, const char* field, int index, const char* missing);
    void name(const Luau::AstName& name, const char* field, int index, const char* missing);
    [[noreturn]] void fail(const char* field, int index, const char* problem);
};

} // namespace Luau
