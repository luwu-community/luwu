// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Reflect.h"
#include "Luau/ReflectAtoms.gen.h"
#include "Luau/ReflectItems.gen.h"

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

/**
 * Reserved userdata tags for AST/CST reflection objects.
 *
 * SAFETY: Uses the Luwu internal reserved tag range [LUA_UTAG_RESERVED_START..LUA_UTAG_RESERVED_END],
 * keeping public tags [0..LUA_UTAG_RESERVED_START-1] completely free for embedder use.
 */
enum AstUserdataTag : int
{
    TagDocument  = LUA_INTERNAL_UTAG(0),
    TagItem      = LUA_INTERNAL_UTAG(1),
    TagFilter    = LUA_INTERNAL_UTAG(2),
    TagAllocator = LUA_INTERNAL_UTAG(3),
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
};

struct ReflectComment
{
    Luau::Lexeme::Type type = Luau::Lexeme::Type::Comment;
    Luau::AstArray<char> text = {};
    Luau::Location location;
};

struct ClassifiedNodeComments
{
    std::vector<ReflectComment*> leading;
    std::vector<ReflectComment*> trailing;
};

using NodeCommentMap = DenseHashMap2<const Luau::AstNode*, ClassifiedNodeComments>;

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

struct AstTreeState
{
    std::shared_ptr<AstTreeState> forward;

    NodeCommentMap nodeComments;
    NodeCommentMap parsedComments;
    DenseHashMap2<const void*, bool> explicitVisibility;
    DenseHashMap2<const Luau::AstNode*, int> untainted;

    bool tainted = false;
    bool edited = false;
    int activeWalks = 0;
};

inline std::string toString(ConstantNumberParseResult result)
{
    switch (result)
    {
    case ConstantNumberParseResult::Ok:
        return "ok";
    case ConstantNumberParseResult::Imprecise:
        return "imprecise";
    case ConstantNumberParseResult::Malformed:
        return "malformed";
    case ConstantNumberParseResult::BinOverflow:
        return "binoverflow";
    case ConstantNumberParseResult::HexOverflow:
        return "hexoverflow";
    case ConstantNumberParseResult::IntOverflow:
        return "intoverflow";
    }

    return "unknown";
}

template<>
inline std::optional<ConstantNumberParseResult> fromString<ConstantNumberParseResult>(std::string_view s)
{
    for (ConstantNumberParseResult result : {
             ConstantNumberParseResult::Ok,
             ConstantNumberParseResult::Imprecise,
             ConstantNumberParseResult::Malformed,
             ConstantNumberParseResult::BinOverflow,
             ConstantNumberParseResult::HexOverflow,
             ConstantNumberParseResult::IntOverflow,
         })
    {
        if (s == toString(result))
            return result;
    }

    return std::nullopt;
}

inline const AstArgumentName* argumentName(const AstArgumentName& entry)
{
    return entry.first.value ? &entry : nullptr;
}

inline const AstArgumentName* argumentName(const std::optional<AstArgumentName>& entry)
{
    return entry && entry->first.value ? &*entry : nullptr;
}

inline bool hasExplicitVisibility(const AstTreeState& tree, const void* target)
{
    const bool* flag = tree.explicitVisibility.find(target);
    return flag && *flag;
}

inline void copyExplicitVisibility(AstTreeState& tree, const void* from, const void* to)
{
    if (from == to || tree.explicitVisibility.empty())
        return;

    bool value = hasExplicitVisibility(tree, from);
    if (value || tree.explicitVisibility.contains(to))
        tree.explicitVisibility[to] = value;
}

struct AstAllocatorData
{
    std::shared_ptr<AstAllocatorState> state;
};

struct AstDocumentState
{
    std::string source;
    std::shared_ptr<AstAllocatorState> arena;
    std::shared_ptr<AstTreeState> tree = std::make_shared<AstTreeState>();
    Luau::ParseResult parseResult;
    std::vector<size_t> lineOffsets;
    std::vector<ReflectComment*> comments;
    bool commentsExtracted = false;

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

inline AstTreeState& treeOf(AstDocumentState& doc)
{
    while (doc.tree->forward)
        doc.tree = doc.tree->forward;
    return *doc.tree;
}

inline bool isTainted(const AstTreeState& tree, const Luau::AstNode* node)
{
    return tree.tainted && !tree.untainted.contains(node);
}

void joinDocuments(AstDocumentState& doc, AstDocumentState& other);

struct AstDocumentData
{
    std::shared_ptr<AstDocumentState> doc;
};

enum class ItemFamily : uint8_t
{
    Node,
    Cst,
    Plain,
};

struct AstItem;
struct AstValidator;

struct AstItemClass
{
    const char* kind = nullptr;
    const char* category = nullptr;
    ItemFamily family = ItemFamily::Node;
    NodeCategory categoryEnum = NodeCategory::Unknown;
    bool canHoldComments = false;
    bool (*methodHandler)(lua_State* L, AstItem& handle, ReflectAtom atom) = nullptr;
    void (*propCollector)(lua_State* L, AstItem& handle) = nullptr;
    void (*validator)(AstValidator& v, Luau::AstNode* node) = nullptr;
    void (*create)(lua_State* L, const std::shared_ptr<AstDocumentState>& doc) = nullptr;
    void (*label)(lua_State* L, AstItem& handle) = nullptr;
};

struct AstItem
{
    std::shared_ptr<AstDocumentState> doc;
    const AstItemClass* cls = nullptr;
    void* target = nullptr;

    Luau::AstNode* node() const { return static_cast<Luau::AstNode*>(target); }
    const Luau::CstNode* cst() const { return static_cast<const Luau::CstNode*>(target); }
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

inline Luau::AstArray<char> copyToArena(Luau::Allocator& allocator, std::string_view text)
{
    char* copy = static_cast<char*>(allocator.allocate(text.size()));
    std::memcpy(copy, text.data(), text.size());
    return Luau::AstArray<char>{copy, text.size()};
}

inline const std::vector<ReflectComment*>& documentComments(AstDocumentState& doc)
{
    if (!doc.commentsExtracted)
    {
        doc.commentsExtracted = true;
        for (const Luau::Comment& c : doc.parseResult.commentLocations)
        {
            auto [startOff, endOff] = locationToOffsets(doc.lineOffsets, doc.source.size(), c.location);
            std::string_view text = std::string_view(doc.source).substr(startOff, endOff - startOff);
            doc.comments.push_back(doc.allocator()->alloc<ReflectComment>(ReflectComment{c.type, copyToArena(*doc.allocator(), text), c.location}));
        }
    }
    return doc.comments;
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

inline void pushAstItem(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const AstItemClass* cls, void* target)
{
    AstItem* item = static_cast<AstItem*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstItem), TagItem));
    new (item) AstItem{doc, cls, target};
}

const AstItemClass* astNodeClass(Luau::AstNode* node);
const AstItemClass* cstNodeClass(const Luau::CstNode* node);
const AstItemClass* plainItemClass(ReflectAtom atom);

template<typename T>
inline void pushItem(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const T* target)
{
    if (target)
        pushAstItem(L, doc, itemClass<T>(), const_cast<T*>(target));
    else
        lua_pushnil(L);
}

void pushAstFilter(lua_State* L, const AstFilterData& filter);

// Check helpers
AstDocumentData& checkAstDocument(lua_State* L, int idx);
AstFilterData& checkAstFilter(lua_State* L, int idx);
AstFilterData extractAstFilter(lua_State* L, int idx);

inline AstItem& checkAstItem(lua_State* L, int idx, ItemFamily family, const char* typeName)
{
    AstItem* item = static_cast<AstItem*>(lua_touserdatatagged(L, idx, TagItem));
    if (!item || item->cls->family != family)
        luaL_typeerrorL(L, idx, typeName);
    return *item;
}

inline AstItem& checkAstNode(lua_State* L, int idx)
{
    return checkAstItem(L, idx, ItemFamily::Node, "AstNode");
}

inline AstItem& checkCstNode(lua_State* L, int idx)
{
    return checkAstItem(L, idx, ItemFamily::Cst, "CstNode");
}

template<typename T>
inline AstItem& checkItem(lua_State* L, int idx)
{
    AstItem* item = static_cast<AstItem*>(lua_touserdatatagged(L, idx, TagItem));
    if (!item || item->cls != itemClass<T>())
        luaL_typeerrorL(L, idx, itemClass<T>()->kind);
    return *item;
}

Luau::AstNode* createDefaultAstNode(lua_State* L, ReflectAtom atom, Luau::Allocator& alloc);
const Luau::CstNode* createDefaultCstNode(lua_State* L, ReflectAtom atom, Luau::Allocator& alloc);
void attachCommentsToAst(AstDocumentState& doc, Luau::AstNode* rootNode = nullptr);
void recordParsedComments(AstDocumentState& doc, Luau::AstNode* rootNode = nullptr);
void recordExplicitVisibility(AstDocumentState& doc);
std::string reflectPrettyPrint(Luau::AstNode* node, bool asProgram, const AstTreeState& tree, std::string& error);
bool canNodeHoldComments(Luau::AstNode* node);

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

template<typename T>
inline void pushArgumentNames(lua_State* L, const Luau::AstArray<T>& names)
{
    pushArray(L, names.size, [&](size_t i) {
        if (const Luau::AstArgumentName* name = argumentName(names.data[i]))
            lua_pushstring(L, name->first.value);
        else
            lua_pushboolean(L, false);
    });
}

template<typename T>
inline void pushArgumentNameLocations(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::AstArray<T>& names)
{
    pushArray(L, names.size, [&](size_t i) {
        if (const Luau::AstArgumentName* name = argumentName(names.data[i]))
            pushLocation(L, doc, name->second);
        else
            lua_pushboolean(L, false);
    });
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
        pushItem(L, doc, array.data[i]);
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

int16_t reflectUserAtom(lua_State* L, const char* s, size_t l);

inline bool reflectAtomsEnabled(lua_State* L)
{
    return FFlag::OptLuwuReflectUseAtoms && lua_callbacks(L)->useratom == reflectUserAtom;
}

inline ReflectAtom resolveIndexAtom(lua_State* L, const char*& outKeyStr)
{
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, reflectAtomsEnabled(L) ? &atomId : nullptr);
    if (!keyStr)
    {
        outKeyStr = nullptr;
        return ReflectAtom::Unknown;
    }
    outKeyStr = keyStr;
    return resolveReflectAtom(atomId, keyStr, keyLen);
}

inline ReflectAtom resolveNamecallAtom(lua_State* L, const char*& outStr, size_t& outLen)
{
    int atomId = -1;
    size_t len = 0;
    const char* str = lua_namecallwithlen(L, reflectAtomsEnabled(L) ? &atomId : nullptr, &len);
    if (!str)
        luaL_error(L, "missing method name in namecall");
    outStr = str;
    outLen = len;
    return resolveReflectAtom(atomId, str, len);
}

template<typename DataType, auto CheckFn, auto DispatchFn>
inline int reflectMethodTrampoline(lua_State* L)
{
    auto& handle = CheckFn(L, 1);
    size_t len = 0;
    const char* str = lua_tolstring(L, lua_upvalueindex(1), &len);
    ReflectAtom atom = resolveGlobalReflectAtom(std::string_view(str, len));
    return DispatchFn(L, handle, atom, str, len);
}

template<typename DataType, auto CheckFn, auto DispatchFn>
inline int reflectNamecall(lua_State* L)
{
    auto& handle = CheckFn(L, 1);
    const char* str = nullptr;
    size_t len = 0;
    ReflectAtom atom = resolveNamecallAtom(L, str, len);
    return DispatchFn(L, handle, atom, str, len);
}

template<int TagValue, typename DataType>
inline DataType& checkReflectUserdata(lua_State* L, int idx, const char* typeName)
{
    if (lua_userdatatag(L, idx) != TagValue)
        luaL_typeerrorL(L, idx, typeName);
    return *static_cast<DataType*>(lua_touserdata(L, idx));
}

template<typename DataType>
inline void reflectUserdataDtor(lua_State* L, void* userdata)
{
    static_cast<DataType*>(userdata)->~DataType();
}

#define LUAU_REFLECT_RESOLVE_INDEX_ATOM() const char* keyStr = nullptr; ReflectAtom atom = resolveIndexAtom(L, keyStr); if (!keyStr) { lua_pushnil(L); return 1; }
#define LUAU_REFLECT_RESOLVE_NAMECALL_ATOM() const char* str = nullptr; size_t len = 0; ReflectAtom atom = resolveNamecallAtom(L, str, len);

#define LUAU_REFLECT_PREPARE_INDEX(checkFunc) auto& handle = checkFunc(L, 1); LUAU_REFLECT_RESOLVE_INDEX_ATOM()
#define LUAU_REFLECT_PREPARE_NAMECALL(checkFunc) auto& handle = checkFunc(L, 1); LUAU_REFLECT_RESOLVE_NAMECALL_ATOM()

#define LUAU_REFLECT_METHOD_TRAMPOLINE(funcName, checkFunc, dispatchFunc) \
    static int funcName(lua_State* L) { return reflectMethodTrampoline<std::decay_t<decltype(checkFunc(nullptr, 0))>, checkFunc, dispatchFunc>(L); }

#define LUAU_REFLECT_NAMECALL(funcName, checkFunc, dispatchFunc) \
    static int funcName(lua_State* L) { return reflectNamecall<std::decay_t<decltype(checkFunc(nullptr, 0))>, checkFunc, dispatchFunc>(L); }

#define LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkName, dtorName, DataType, TagValue, TypeNameStr) \
    DataType& checkName(lua_State* L, int idx) { return checkReflectUserdata<TagValue, DataType>(L, idx, TypeNameStr); } \
    static void dtorName(lua_State* L, void* userdata) { reflectUserdataDtor<DataType>(L, userdata); }

#define LUAU_REFLECT_DEFINE_TOSTRING(funcName, TypeNameStr) \
    static int funcName(lua_State* L) { lua_pushstring(L, TypeNameStr); return 1; }

#define LUAU_REFLECT_DEFINE_EQ(funcName, TagValue, checkFunc, compareExpr) \
    static int funcName(lua_State* L) \
    { \
        if (lua_userdatatag(L, 1) != TagValue || lua_userdatatag(L, 2) != TagValue) { lua_pushboolean(L, false); return 1; } \
        auto& a = checkFunc(L, 1); auto& b = checkFunc(L, 2); lua_pushboolean(L, (compareExpr)); return 1; \
    }

// Module registration functions
void registerAstAllocator(lua_State* L);
void registerAstDocument(lua_State* L);
void registerAstItem(lua_State* L);
void registerAstFilter(lua_State* L);

void initializeAstNodeClasses();
void initializeCstNodeClasses();

int dispatchAstNodeMethod(lua_State* L, AstItem& handle, ReflectAtom atom, const char* str, size_t len);
int dispatchCstNodeMethod(lua_State* L, AstItem& handle, ReflectAtom atom, const char* str, size_t len);

int reflectFilter(lua_State* L);

} // namespace Luau
