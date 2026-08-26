// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

// Because the embedder may have themselves set useratom, we cannot use Luau's builtin atom system here, instead define the atoms separately using a hashmap + enum
enum AstDocumentAtom : uint8_t
{
    Atom_Unknown = 0,
    Atom_Root,
    Atom_Source,
    Atom_Walk,
    Atom_Find,
    Atom_Errors,
    Atom_Comments,
    Atom_LineOffsets,
};

static AstDocumentAtom getAstDocumentAtom(std::string_view key)
{
    static const std::unordered_map<std::string_view, AstDocumentAtom> s_atomMap = {
        {"root", Atom_Root},
        {"source", Atom_Source},
        {"walk", Atom_Walk},
        {"find", Atom_Find},
        {"errors", Atom_Errors},
        {"comments", Atom_Comments},
        {"lineOffsets", Atom_LineOffsets},
    };

    if (auto it = s_atomMap.find(key); it != s_atomMap.end())
        return it->second;

    return Atom_Unknown;
}

void pushAstDocument(lua_State* L, std::shared_ptr<AstDocumentState> doc)
{
    AstDocumentData* data = static_cast<AstDocumentData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstDocumentData), TagDocument));
    new (data) AstDocumentData{std::move(doc)};
}

std::shared_ptr<AstDocumentState>& checkAstDocument(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagDocument)
        luaL_typeerrorL(L, idx, "AstDocument");
    return static_cast<AstDocumentData*>(lua_touserdata(L, idx))->doc;
}

static void astDocumentDtor(lua_State* L, void* userdata)
{
    static_cast<AstDocumentData*>(userdata)->~AstDocumentData();
}

static int astDocWalk(lua_State* L)
{
    auto& doc = checkAstDocument(L, 1);
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
    auto& doc = checkAstDocument(L, 1);
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
    auto& doc = checkAstDocument(L, 1);
    size_t keyLen = 0;
    const char* keyStr = luaL_checklstring(L, 2, &keyLen);
    AstDocumentAtom atom = getAstDocumentAtom(std::string_view(keyStr, keyLen));

    switch (atom)
    {
    case Atom_Root:
    {
        pushAstNode(L, doc, doc->parseResult.root);
        return 1;
    }
    case Atom_Source:
    {
        lua_pushlstring(L, doc->source.data(), doc->source.size());
        return 1;
    }
    case Atom_Walk:
    {
        return pushUserdataMethod(L, TagDocument, "walk");
    }
    case Atom_Find:
    {
        return pushUserdataMethod(L, TagDocument, "find");
    }
    case Atom_Comments:
    {
        lua_createtable(L, int(doc->parseResult.commentLocations.size()), 0);
        for (size_t i = 0; i < doc->parseResult.commentLocations.size(); i++)
        {
            pushAstComment(L, doc, doc->parseResult.commentLocations[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
        return 1;
    }
    case Atom_Errors:
    {
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
        return 1;
    }
    case Atom_LineOffsets:
    {
        lua_createtable(L, int(doc->lineOffsets.size()), 0);
        for (size_t i = 0; i < doc->lineOffsets.size(); i++)
        {
            lua_pushinteger(L, int(doc->lineOffsets[i]));
            lua_rawseti(L, -2, int(i + 1));
        }
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
    lua_pushboolean(L, a == b);
    return 1;
}

void registerAstDocument(lua_State* L)
{
    static const luaL_Reg s_docMethods[] = {
        {"walk", astDocWalk},
        {"find", astDocFind},
        {nullptr, nullptr},
    };
    registerUserdataType(L, TagDocument, "AstDocument", astDocumentDtor, astDocIndex, astDocToString, astDocEq, s_docMethods);
}

} // namespace Luau
