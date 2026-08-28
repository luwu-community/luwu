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

static int astDocWalk(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    auto& doc = handle.doc;
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (doc->parseResult.root)
    {
        CallbackVisitor visitor(L, doc, 2);
        doc->parseResult.root->visit(&visitor);
        if (visitor.errorOccurred)
            lua_error(L);
    }
    return 0;
}

static int astDocFind(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    size_t len = 0;
    const char* kindStr = luaL_checklstring(L, 2, &len);
    findNodesByKind(L, handle.doc, handle.doc->parseResult.root, std::string_view(kindStr, len));
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

static int astDocIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstDocument);

    switch (atom)
    {
    case ReflectAtom::Id:          lua_pushlightuserdatatagged(L, (void*)handle.doc.get(), TagDocument); return 1;
    case ReflectAtom::Root:        return pushUserdataMethod(L, TagDocument, "root");
    case ReflectAtom::Source:      return pushUserdataMethod(L, TagDocument, "source");
    case ReflectAtom::Walk:        return pushUserdataMethod(L, TagDocument, "walk");
    case ReflectAtom::Find:        return pushUserdataMethod(L, TagDocument, "find");
    case ReflectAtom::Comments:    return pushUserdataMethod(L, TagDocument, "comments");
    case ReflectAtom::Errors:      return pushUserdataMethod(L, TagDocument, "errors");
    case ReflectAtom::LineOffsets: return pushUserdataMethod(L, TagDocument, "lineOffsets");
    default:
        lua_pushnil(L);
        return 1;
    }
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

static int astDocNamecall(lua_State* L)
{
    LUAU_REFLECT_PREPARE_NAMECALL(checkAstDocument);

    switch (atom)
    {
    case ReflectAtom::Root:        return astDocRoot(L);
    case ReflectAtom::Source:      return astDocSource(L);
    case ReflectAtom::Walk:        return astDocWalk(L);
    case ReflectAtom::Find:        return astDocFind(L);
    case ReflectAtom::Comments:    return astDocComments(L);
    case ReflectAtom::Errors:      return astDocErrors(L);
    case ReflectAtom::LineOffsets: return astDocLineOffsets(L);
    default: break;
    }

    luaL_error(L, "%.*s is not a valid method of AstDocument", int(len), str);
}

void registerAstDocument(lua_State* L)
{
    static const luaL_Reg s_docMethods[] = {
        {"root", astDocRoot},
        {"source", astDocSource},
        {"walk", astDocWalk},
        {"find", astDocFind},
        {"comments", astDocComments},
        {"errors", astDocErrors},
        {"lineOffsets", astDocLineOffsets},
        {nullptr, nullptr},
    };
    registerUserdataType(L, TagDocument, "AstDocument", astDocumentDtor, astDocIndex, astDocToString, astDocEq, s_docMethods, astDocNamecall);
}

} // namespace Luau
