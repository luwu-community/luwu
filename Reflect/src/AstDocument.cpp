// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

void pushAstDocument(lua_State* L, std::shared_ptr<AstDocumentState> doc)
{
    AstDocumentData* data = static_cast<AstDocumentData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstDocumentData), TagDocument));
    new (data) AstDocumentData{std::move(doc)};
}

LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkAstDocument, astDocumentDtor, AstDocumentData, TagDocument, "AstDocument")

static int astDocRoot(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    pushAstNode(L, handle.doc, handle.doc->parseResult.root);
    return 1;
}

static int astDocSource(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    lua_pushlstring(L, handle.doc->source.data(), handle.doc->source.size());
    return 1;
}

static int astDocComments(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    auto& doc = handle.doc;
    const auto& comments = doc->parseResult.commentLocations;
    pushArray(L, comments.size(), [&](size_t i) {
        pushAstAux(L, doc, comments[i]);
    });
    return 1;
}

static int astDocErrors(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    auto& doc = handle.doc;
    const auto& errors = doc->parseResult.errors;
    pushArray(L, errors.size(), [&](size_t i) {
        const auto& err = errors[i];
        lua_createtable(L, 0, 2);
        lua_pushstring(L, err.getMessage().c_str());
        lua_setfield(L, -2, "message");
        pushLocation(L, doc, err.getLocation());
        lua_setfield(L, -2, "location");
    });
    return 1;
}

static int astDocLineOffsets(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    auto& doc = handle.doc;
    pushArray(L, doc->lineOffsets.size(), [&](size_t i) {
        lua_pushinteger(L, int(doc->lineOffsets[i]));
    });
    return 1;
}

static int astDocAllocator(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    pushAstAllocator(L, handle.doc->arena);
    return 1;
}

static int astDocProperties(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    auto& doc = handle.doc;
    lua_createtable(L, 0, 7);

    lua_pushlightuserdatatagged(L, (void*)doc.get(), TagId);
    lua_setfield(L, -2, "id");

    pushAstAllocator(L, doc->arena);
    lua_setfield(L, -2, "allocator");

    pushAstNode(L, doc, doc->parseResult.root);
    lua_setfield(L, -2, "root");

    lua_pushlstring(L, doc->source.data(), doc->source.size());
    lua_setfield(L, -2, "source");

    const auto& comments = doc->parseResult.commentLocations;
    pushArray(L, comments.size(), [&](size_t i) {
        pushAstAux(L, doc, comments[i]);
    });
    lua_setfield(L, -2, "comments");

    const auto& errors = doc->parseResult.errors;
    pushArray(L, errors.size(), [&](size_t i) {
        const auto& err = errors[i];
        lua_createtable(L, 0, 2);
        lua_pushstring(L, err.getMessage().c_str());
        lua_setfield(L, -2, "message");
        pushLocation(L, doc, err.getLocation());
        lua_setfield(L, -2, "location");
    });
    lua_setfield(L, -2, "errors");

    pushArray(L, doc->lineOffsets.size(), [&](size_t i) {
        lua_pushinteger(L, int(doc->lineOffsets[i]));
    });
    lua_setfield(L, -2, "lineOffsets");

    return 1;
}

static int dispatchAstDocMethod(lua_State* L, AstDocumentData& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Allocator:   return astDocAllocator(L);
    case ReflectAtom::Root:        return astDocRoot(L);
    case ReflectAtom::Source:      return astDocSource(L);
    case ReflectAtom::Comments:    return astDocComments(L);
    case ReflectAtom::Errors:      return astDocErrors(L);
    case ReflectAtom::LineOffsets: return astDocLineOffsets(L);
    case ReflectAtom::Properties:  return astDocProperties(L);
    default: break;
    }

    luaL_error(L, "%.*s is not a valid method of AstDocument", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astDocMethodTrampoline, checkAstDocument, dispatchAstDocMethod)
LUAU_REFLECT_NAMECALL(astDocNamecall, checkAstDocument, dispatchAstDocMethod)

static int astDocIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstDocument);

    switch (atom)
    {
    case ReflectAtom::Id:          lua_pushlightuserdatatagged(L, (void*)handle.doc.get(), TagId); return 1;
    default: break;
    }

    if (atom != ReflectAtom::Unknown)
        return pushCachedUserdataMethod(L, TagDocument, keyStr, astDocMethodTrampoline);

    lua_pushnil(L);
    return 1;
}

static int astDocToString(lua_State* L)
{
    lua_pushstring(L, "AstDocument");
    return 1;
}

static int astDocEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagDocument || lua_userdatatag(L, 2) != TagDocument)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstDocument(L, 1);
    auto& b = checkAstDocument(L, 2);
    lua_pushboolean(L, a.doc == b.doc);
    return 1;
}

void registerAstDocument(lua_State* L)
{
    registerUserdataType(L, TagDocument, "AstDocument", astDocumentDtor, astDocIndex, astDocToString, astDocEq, astDocNamecall);
}

} // namespace Luau
