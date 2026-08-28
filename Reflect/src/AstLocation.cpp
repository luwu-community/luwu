// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

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

LUAU_REFLECT_DEFINE_VALUE_USERDATA(pushLocation, checkAstLocation, astLocationDtor, AstLocationData, const Luau::Location&, TagLocation, "AstLocation")

static int astLocationIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstLocation);
    const auto& loc = handle.location;

    switch (atom)
    {
    case ReflectAtom::BeginLine:   { lua_pushinteger(L, loc.begin.line + 1); return 1; }
    case ReflectAtom::BeginColumn: { lua_pushinteger(L, loc.begin.column + 1); return 1; }
    case ReflectAtom::EndLine:     { lua_pushinteger(L, loc.end.line + 1); return 1; }
    case ReflectAtom::EndColumn:   { lua_pushinteger(L, loc.end.column + 1); return 1; }
    case ReflectAtom::StartOffset:
    {
        LUAU_ASSERT(doc);
        lua_pushinteger(L, int(positionToOffset(doc->lineOffsets, doc->source.size(), loc.begin)));
        return 1;
    }
    case ReflectAtom::EndOffset:
    {
        LUAU_ASSERT(doc);
        lua_pushinteger(L, int(positionToOffset(doc->lineOffsets, doc->source.size(), loc.end)));
        return 1;
    }
    case ReflectAtom::Text:
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
