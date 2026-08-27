// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

// Because the embedder may have themselves set useratom, we cannot use Luau's builtin atom system here, instead define the atoms separately using a hashmap + enum
enum AstLocationAtom : uint8_t
{
    Atom_Unknown = 0,
    Atom_BeginLine,
    Atom_BeginColumn,
    Atom_EndLine,
    Atom_EndColumn,
    Atom_StartOffset,
    Atom_EndOffset,
    Atom_Text,
};

static AstLocationAtom getAstLocationAtom(std::string_view key)
{
    static const std::unordered_map<std::string_view, AstLocationAtom> s_atomMap = {
        {"beginLine", Atom_BeginLine},
        {"beginColumn", Atom_BeginColumn},
        {"endLine", Atom_EndLine},
        {"endColumn", Atom_EndColumn},
        {"startOffset", Atom_StartOffset},
        {"endOffset", Atom_EndOffset},
        {"text", Atom_Text},
    };

    if (auto it = s_atomMap.find(key); it != s_atomMap.end())
        return it->second;

    return Atom_Unknown;
}

// Note: computeLineOffsets and locationToOffsets are adapted from lute (https://github.com/luau-lang/lute)
std::vector<size_t> computeLineOffsets(std::string_view content)
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

void pushLocation(lua_State* L, const std::shared_ptr<AstDocumentState>& doc, const Luau::Location& loc)
{
    AstLocationData* data = static_cast<AstLocationData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstLocationData), TagLocation));
    new (data) AstLocationData{doc, loc};
}

AstLocationData& checkAstLocation(lua_State* L, int idx)
{
    if (lua_userdatatag(L, idx) != TagLocation)
        luaL_typeerrorL(L, idx, "AstLocation");
    return *static_cast<AstLocationData*>(lua_touserdata(L, idx));
}

static void astLocationDtor(lua_State* L, void* userdata)
{
    static_cast<AstLocationData*>(userdata)->~AstLocationData();
}

static int astLocationIndex(lua_State* L)
{
    auto& handle = checkAstLocation(L, 1);
    size_t keyLen = 0;
    const char* keyStr = luaL_checklstring(L, 2, &keyLen);
    AstLocationAtom atom = getAstLocationAtom(std::string_view(keyStr, keyLen));
    const auto& loc = handle.location;
    const auto& doc = handle.doc;

    switch (atom)
    {
    case Atom_BeginLine:   { lua_pushinteger(L, loc.begin.line + 1); return 1; }
    case Atom_BeginColumn: { lua_pushinteger(L, loc.begin.column + 1); return 1; }
    case Atom_EndLine:     { lua_pushinteger(L, loc.end.line + 1); return 1; }
    case Atom_EndColumn:   { lua_pushinteger(L, loc.end.column + 1); return 1; }
    case Atom_StartOffset:
    {
        LUAU_ASSERT(doc);
        auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), loc);
        lua_pushinteger(L, int(startOff));
        return 1;
    }
    case Atom_EndOffset:
    {
        LUAU_ASSERT(doc);
        auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), loc);
        lua_pushinteger(L, int(endOff));
        return 1;
    }
    case Atom_Text:
    {
        LUAU_ASSERT(doc);
        auto [startOff, endOff] = locationToOffsets(doc->lineOffsets, doc->source.size(), loc);
        lua_pushlstring(L, doc->source.data() + startOff, endOff - startOff);
        return 1;
    }
    default:
        lua_pushnil(L);
        return 1;
    }
}

static int astLocationToString(lua_State* L)
{
    auto& handle = checkAstLocation(L, 1);
    const auto& loc = handle.location;
    lua_pushfstring(L, "AstLocation(%d:%d - %d:%d)", loc.begin.line + 1, loc.begin.column + 1, loc.end.line + 1, loc.end.column + 1);
    return 1;
}

static int astLocationEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagLocation || lua_userdatatag(L, 2) != TagLocation)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstLocation(L, 1);
    auto& b = checkAstLocation(L, 2);
    lua_pushboolean(L, a.location == b.location && a.doc == b.doc);
    return 1;
}

static void dfgBeginLine(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstLocationData*>(ud);
    lua_userdatadirectfield_setnumber(res, double(data->location.begin.line + 1));
}

static void dfgBeginColumn(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstLocationData*>(ud);
    lua_userdatadirectfield_setnumber(res, double(data->location.begin.column + 1));
}

static void dfgEndLine(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstLocationData*>(ud);
    lua_userdatadirectfield_setnumber(res, double(data->location.end.line + 1));
}

static void dfgEndColumn(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstLocationData*>(ud);
    lua_userdatadirectfield_setnumber(res, double(data->location.end.column + 1));
}

static void dfgStartOffset(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstLocationData*>(ud);
    LUAU_ASSERT(data->doc);
    auto [startOff, endOff] = locationToOffsets(data->doc->lineOffsets, data->doc->source.size(), data->location);
    lua_userdatadirectfield_setnumber(res, double(startOff));
}

static void dfgEndOffset(lua_State* L, void* ud, void* res)
{
    auto* data = static_cast<AstLocationData*>(ud);
    LUAU_ASSERT(data->doc);
    auto [startOff, endOff] = locationToOffsets(data->doc->lineOffsets, data->doc->source.size(), data->location);
    lua_userdatadirectfield_setnumber(res, double(endOff));
}

void registerAstLocation(lua_State* L)
{
    registerUserdataType(L, TagLocation, "AstLocation", astLocationDtor, astLocationIndex, astLocationToString, astLocationEq);
    lua_registeruserdatadirectfieldget(L, TagLocation, "beginLine", dfgBeginLine);
    lua_registeruserdatadirectfieldget(L, TagLocation, "beginColumn", dfgBeginColumn);
    lua_registeruserdatadirectfieldget(L, TagLocation, "endLine", dfgEndLine);
    lua_registeruserdatadirectfieldget(L, TagLocation, "endColumn", dfgEndColumn);
    lua_registeruserdatadirectfieldget(L, TagLocation, "startOffset", dfgStartOffset);
    lua_registeruserdatadirectfieldget(L, TagLocation, "endOffset", dfgEndOffset);
}

} // namespace Luau
