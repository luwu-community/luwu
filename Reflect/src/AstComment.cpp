// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

void pushAstComment(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Comment& comment)
{
    AstCommentData* data = static_cast<AstCommentData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstCommentData), TagComment));
    new (data) AstCommentData{doc, comment};
}

AstCommentData& checkAstComment(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagComment)
        luaL_typeerrorL(L, idx, "AstComment");
    return *static_cast<AstCommentData*>(lua_touserdata(L, idx));
}

static void astCommentDtor(lua_State* L, void* userdata)
{
    static_cast<AstCommentData*>(userdata)->~AstCommentData();
}

static int astCommentIndex(lua_State* L)
{
    auto& handle = checkAstComment(L, 1);
    int atomId = -1;
    size_t keyLen = 0;
    const char* keyStr = lua_tolstringatom(L, 2, &keyLen, FFlag::OptLuwuReflectUseAtoms ? &atomId : nullptr);
    if (!keyStr)
    {
        lua_pushnil(L);
        return 1;
    }
    ReflectAtom atom = resolveReflectAtom(atomId, keyStr, keyLen);
    const auto& comment = handle.comment;
    const auto& doc = handle.doc;

    switch (atom)
    {
    case ReflectAtom::Type:
    {
        switch (comment.type)
        {
        case Luau::Lexeme::Comment:
            lua_pushstring(L, "single");
            break;
        case Luau::Lexeme::BlockComment:
            lua_pushstring(L, "block");
            break;
        case Luau::Lexeme::BrokenComment:
            lua_pushstring(L, "broken");
            break;
        default:
            lua_pushstring(L, "unknown");
            break;
        }
        return 1;
    }
    case ReflectAtom::Location:
    {
        pushLocation(L, doc, comment.location);
        return 1;
    }
    case ReflectAtom::Text:
    {
        LUAU_ASSERT(doc);
        auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), comment.location);
        lua_pushlstring(L, doc->source.data() + startOff, endOff - startOff);
        return 1;
    }
    default:
        lua_pushnil(L);
        return 1;
    }
}

static int astCommentToString(lua_State* L)
{
    auto& handle = checkAstComment(L, 1);
    const auto& loc = handle.comment.location;
    lua_pushfstring(L, "AstComment(%d:%d - %d:%d)", loc.begin.line + 1, loc.begin.column + 1, loc.end.line + 1, loc.end.column + 1);
    return 1;
}

static int astCommentEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagComment || lua_userdatatag(L, 2) != TagComment)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstComment(L, 1);
    auto& b = checkAstComment(L, 2);
    lua_pushboolean(L, a.comment.location == b.comment.location && a.comment.type == b.comment.type && a.doc == b.doc);
    return 1;
}

void registerAstComment(lua_State* L)
{
    registerUserdataType(L, TagComment, "AstComment", astCommentDtor, astCommentIndex, astCommentToString, astCommentEq);
}

} // namespace Luau
