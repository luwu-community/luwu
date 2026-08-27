// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

void pushAstDocument(lua_State* L, std::shared_ptr<AstDocumentState> doc)
{
    AstDocumentData* data = static_cast<AstDocumentData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstDocumentData), TagDocument));
    new (data) AstDocumentData{std::move(doc), Ref{}};
}

AstDocumentData& checkAstDocument(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagDocument)
        luaL_typeerrorL(L, idx, "AstDocument");
    return *static_cast<AstDocumentData*>(lua_touserdata(L, idx));
}

static void astDocumentDtor(lua_State* L, void* userdata)
{
    auto* data = static_cast<AstDocumentData*>(userdata);
    data->cacheRef.release(L);
    data->~AstDocumentData();
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
    auto& doc = handle.doc;
    size_t len = 0;
    const char* kindStr = luaL_checklstring(L, 2, &len);
    std::string_view kind(kindStr, len);

    FindKindVisitor visitor(kind);
    if (doc->parseResult.root)
        doc->parseResult.root->visit(&visitor);

    lua_createtable(L, int(visitor.matches.size()), 0);
    for (size_t i = 0; i < visitor.matches.size(); i++)
    {
        pushAstNode(L, doc, visitor.matches[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

static int astDocIndex(lua_State* L)
{
    auto& handle = checkAstDocument(L, 1);
    auto& doc = handle.doc;
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr);
    if (!keyStr)
    {
        lua_pushnil(L);
        return 1;
    }
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen);

    switch (atom)
    {
    case ReflectAtom::Root:
    {
        pushAstNode(L, doc, doc->parseResult.root);
        return 1;
    }
    case ReflectAtom::Source:
    {
        lua_pushlstring(L, doc->source.data(), doc->source.size());
        return 1;
    }
    case ReflectAtom::Walk:
    {
        return pushUserdataMethod(L, TagDocument, "walk");
    }
    case ReflectAtom::Find:
    {
        return pushUserdataMethod(L, TagDocument, "find");
    }
    case ReflectAtom::Comments:
    {
        pushCachedValue(L, handle.cacheRef, "comments", [&](lua_State* L) {
            lua_createtable(L, int(doc->parseResult.commentLocations.size()), 0);
            for (size_t i = 0; i < doc->parseResult.commentLocations.size(); i++)
            {
                pushAstAux(L, doc, doc->parseResult.commentLocations[i]);
                lua_rawseti(L, -2, int(i + 1));
            }
        });
        return 1;
    }
    case ReflectAtom::Errors:
    {
        pushCachedValue(L, handle.cacheRef, "errors", [&](lua_State* L) {
            lua_createtable(L, int(doc->parseResult.errors.size()), 0);
            for (size_t i = 0; i < doc->parseResult.errors.size(); i++)
            {
                const auto& err = doc->parseResult.errors[i];
                lua_createtable(L, 0, 2);
                lua_pushstring(L, err.getMessage().c_str());
                lua_setfield(L, -2, "message");
                pushLocation(L, doc, err.getLocation());
                lua_setfield(L, -2, "location");

                lua_rawseti(L, -2, int(i + 1));
            }
        });
        return 1;
    }
    case ReflectAtom::LineOffsets:
    {
        pushCachedValue(L, handle.cacheRef, "lineOffsets", [&](lua_State* L) {
            lua_createtable(L, int(doc->lineOffsets.size()), 0);
            for (size_t i = 0; i < doc->lineOffsets.size(); i++)
            {
                lua_pushinteger(L, int(doc->lineOffsets[i]));
                lua_rawseti(L, -2, int(i + 1));
            }
        });
        return 1;
    }
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
    int atomId = -1;
    size_t len = 0;
    if (const char* str = lua_namecallwithlen(L, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr, &len))
    {
        ReflectAtom atom = resolveReflectAtom(atomId, str, len);
        switch (atom)
        {
        case ReflectAtom::Walk:
            return astDocWalk(L);
        case ReflectAtom::Find:
            return astDocFind(L);
        default:
            break;
        }
        luaL_error(L, "%.*s is not a valid method of AstDocument", int(len), str);
    }
    luaL_error(L, "missing method name in namecall");
}

void registerAstDocument(lua_State* L)
{
    static const luaL_Reg s_docMethods[] = {
        {"walk", astDocWalk},
        {"find", astDocFind},
        {nullptr, nullptr},
    };
    registerUserdataType(L, TagDocument, "AstDocument", astDocumentDtor, astDocIndex, astDocToString, astDocEq, s_docMethods, astDocNamecall);
}

} // namespace Luau
