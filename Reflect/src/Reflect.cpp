// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Reflect.h"
#include "Luau/ReflectCommon.h"

LUAU_FASTFLAGVARIABLE(OptLuwuReflectUseAtoms)

namespace Luau
{

static int reflectAllocator(lua_State* L)
{
    pushAstAllocator(L, std::make_shared<AstAllocatorState>());
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

    registerAstAllocator(L);
    registerAstDocument(L);
    registerAstNode(L);
    registerCstNode(L);
    registerAstAux(L);
    registerAstLocal(L);
    registerAstFilter(L);

    lua_setlightuserdataname(L, TagId, "Id");

    // Module table
    lua_createtable(L, 0, 2);
    lua_pushcfunction(L, reflectAllocator, "allocator");
    lua_setfield(L, -2, "allocator");
    lua_pushcfunction(L, reflectFilter, "filter");
    lua_setfield(L, -2, "filter");

    return 1;
}

} // namespace Luau

