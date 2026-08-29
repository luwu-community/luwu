// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/ReflectCommon.h"

namespace Luau
{

void pushAstAllocator(lua_State* L, std::shared_ptr<AstAllocatorState> state)
{
    AstAllocatorData* data = static_cast<AstAllocatorData*>(lua_newuserdatataggedwithmetatable(L, sizeof(AstAllocatorData), TagAllocator));
    new (data) AstAllocatorData{std::move(state)};
}

LUAU_REFLECT_DEFINE_USERDATA_BASIC(checkAstAllocator, astAllocatorDtor, AstAllocatorData, TagAllocator, "AstAllocator")

static int astAllocatorParse(lua_State* L)
{
    auto& handle = checkAstAllocator(L, 1);
    size_t len = 0;
    const char* src = luaL_checklstring(L, 2, &len);
    bool includeCst = lua_isboolean(L, 3) ? lua_toboolean(L, 3) : false;

    auto doc = std::make_shared<AstDocumentState>(handle.state);
    doc->source.assign(src, len);
    doc->lineOffsets = computeLineOffsets(doc->source);

    Luau::ParseOptions options;
    options.captureComments = true;
    options.allowDeclarationSyntax = true;
    options.storeCstData = includeCst;

    doc->parseResult = Luau::Parser::parse(doc->source.data(), doc->source.size(), handle.state->names, handle.state->allocator, options);

    pushAstDocument(L, std::move(doc));
    return 1;
}

static int astAllocatorParseExpr(lua_State* L)
{
    auto& handle = checkAstAllocator(L, 1);
    size_t len = 0;
    const char* src = luaL_checklstring(L, 2, &len);
    bool includeCst = lua_isboolean(L, 3) ? lua_toboolean(L, 3) : false;

    auto doc = std::make_shared<AstDocumentState>(handle.state);
    doc->source.assign(src, len);
    doc->lineOffsets = computeLineOffsets(doc->source);

    Luau::ParseOptions options;
    options.captureComments = true;
    options.allowDeclarationSyntax = true;
    options.storeCstData = includeCst;

    auto result = Luau::Parser::parseExpr(doc->source.data(), doc->source.size(), handle.state->names, handle.state->allocator, options);
    doc->parseResult.root = nullptr;
    doc->parseResult.errors = std::move(result.errors);
    if (includeCst)
        doc->parseResult.cstNodeMap = std::move(result.cstNodeMap);

    pushAstNode(L, doc, result.root);
    return 1;
}

static int astAllocatorProperties(lua_State* L)
{
    auto& handle = checkAstAllocator(L, 1);
    lua_createtable(L, 0, 1);
    lua_pushlightuserdatatagged(L, (void*)handle.state.get(), TagId);
    lua_setfield(L, -2, "id");
    return 1;
}

static int dispatchAstAllocatorMethod(lua_State* L, AstAllocatorData& handle, ReflectAtom atom, const char* str, size_t len)
{
    switch (atom)
    {
    case ReflectAtom::Parse:      return astAllocatorParse(L);
    case ReflectAtom::Parseexpr:  return astAllocatorParseExpr(L);
    case ReflectAtom::Properties: return astAllocatorProperties(L);
    default: break;
    }

    luaL_error(L, "%.*s is not a valid method of AstAllocator", int(len), str);
}

LUAU_REFLECT_METHOD_TRAMPOLINE(astAllocatorMethodTrampoline, checkAstAllocator, dispatchAstAllocatorMethod)
LUAU_REFLECT_NAMECALL(astAllocatorNamecall, checkAstAllocator, dispatchAstAllocatorMethod)

static int astAllocatorIndex(lua_State* L)
{
    LUAU_REFLECT_PREPARE_INDEX(checkAstAllocator);

    switch (atom)
    {
    case ReflectAtom::Id:
        lua_pushlightuserdatatagged(L, (void*)handle.state.get(), TagId);
        return 1;
    default:
        break;
    }

    if (atom != ReflectAtom::Unknown)
        return pushCachedUserdataMethod(L, TagAllocator, keyStr, astAllocatorMethodTrampoline);

    lua_pushnil(L);
    return 1;
}

static int astAllocatorToString(lua_State* L)
{
    lua_pushstring(L, "AstAllocator");
    return 1;
}

static int astAllocatorEq(lua_State* L)
{
    if (lua_userdatatag(L, 1) != TagAllocator || lua_userdatatag(L, 2) != TagAllocator)
    {
        lua_pushboolean(L, false);
        return 1;
    }
    auto& a = checkAstAllocator(L, 1);
    auto& b = checkAstAllocator(L, 2);
    lua_pushboolean(L, a.state == b.state);
    return 1;
}

void registerAstAllocator(lua_State* L)
{
    registerUserdataType(L, TagAllocator, "AstAllocator", astAllocatorDtor, astAllocatorIndex, astAllocatorToString, astAllocatorEq, astAllocatorNamecall);
}

} // namespace Luau
