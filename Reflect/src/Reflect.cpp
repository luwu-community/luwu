// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Reflect.h"
#include "Luau/ReflectCommon.h"

LUAU_FASTFLAGVARIABLE(OptLuwuReflectUseAtoms)

namespace Luau
{

static int reflectParse(lua_State* L)
{
    size_t len = 0;
    const char* src = luaL_checklstring(L, 1, &len);
    bool includeCst = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;

    auto doc = std::make_shared<AstDocumentState>();
    doc->source.assign(src, len);
    doc->lineOffsets = computeLineOffsets(doc->source);

    Luau::ParseOptions options;
    options.captureComments = true;
    options.allowDeclarationSyntax = true;
    options.storeCstData = includeCst;

    doc->parseResult = Luau::Parser::parse(doc->source.data(), doc->source.size(), *doc->names, *doc->allocator, options);

    pushAstDocument(L, std::move(doc));
    return 1;
}

static int reflectParseExpr(lua_State* L)
{
    size_t len = 0;
    const char* src = luaL_checklstring(L, 1, &len);
    bool includeCst = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;

    auto doc = std::make_shared<AstDocumentState>();
    doc->source.assign(src, len);
    doc->lineOffsets = computeLineOffsets(doc->source);

    Luau::ParseOptions options;
    options.captureComments = true;
    options.allowDeclarationSyntax = true;
    options.storeCstData = includeCst;

    auto result = Luau::Parser::parseExpr(doc->source.data(), doc->source.size(), *doc->names, *doc->allocator, options);
    doc->parseResult.root = nullptr;
    doc->parseResult.errors = std::move(result.errors);
    if (includeCst)
        doc->parseResult.cstNodeMap = std::move(result.cstNodeMap);

    pushAstNode(L, doc, result.root);
    return 1;
}

int luaopen_reflect(lua_State* L)
{
    if (FFlag::OptLuwuReflectUseAtoms)
    {
        lua_Callbacks* cb = lua_callbacks(L);
        if (!cb->useratom)
        {
            cb->useratom = [](lua_State* L, const char* s, size_t l) -> int16_t {
                return int16_t(resolveGlobalReflectAtom(std::string_view(s, l)));
            };
        }
    }

    registerAstDocument(L);
    registerAstNode(L);
    registerAstLocation(L);
    registerCstNode(L);
    registerAstPosition(L);
    registerAstAux(L);
    registerAstFilter(L);

    lua_setlightuserdataname(L, TagDocument, "Id");
    lua_setlightuserdataname(L, TagNode, "Id");
    lua_setlightuserdataname(L, TagLocation, "Id");
    lua_setlightuserdataname(L, TagCstNode, "Id");
    lua_setlightuserdataname(L, TagPosition, "Id");
    lua_setlightuserdataname(L, TagAux, "Id");
    lua_setlightuserdataname(L, TagFilter, "Id");

    // Module table
    lua_createtable(L, 0, 3);
    lua_pushcfunction(L, reflectParse, "parse");
    lua_setfield(L, -2, "parse");
    lua_pushcfunction(L, reflectParseExpr, "parseExpr");
    lua_setfield(L, -2, "parseExpr");
    lua_pushcfunction(L, reflectFilter, "filter");
    lua_setfield(L, -2, "filter");

    return 1;
}

} // namespace Luau
