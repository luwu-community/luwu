// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Reflect.h"
#include "Luau/ReflectCommon.h"

LUAU_FASTFLAGVARIABLE(OptLuwuReflectUseAtoms)
LUAU_FASTFLAG(LuauUdataMetatablePinned)

namespace Luau
{

int16_t reflectUserAtom(lua_State* L, const char* s, size_t l)
{
    return int16_t(resolveGlobalReflectAtom(std::string_view(s, l)));
}

static int reflectAllocator(lua_State* L)
{
    pushAstAllocator(L, std::make_shared<AstAllocatorState>(), std::make_shared<AstTreeState>());
    return 1;
}

int luaopen_reflect(lua_State* L)
{
    if (!FFlag::LuauUdataMetatablePinned)
        luaL_error(L, "reflect requires FFlag LuauUdataMetatablePinned to be enabled");

    lua_getuserdatametatable(L, TagDocument);
    bool registered = !lua_isnil(L, -1);
    lua_pop(L, 1);

    if (!registered)
    {
        if (FFlag::OptLuwuReflectUseAtoms)
        {
            lua_Callbacks* cb = lua_callbacks(L);
            if (!cb->useratom)
                cb->useratom = reflectUserAtom;
        }

        registerAstAllocator(L);
        registerAstDocument(L);
        registerAstItem(L);
        registerAstFilter(L);

        lua_setlightuserdataname(L, TagId, "Id");
    }

    // Module table
    lua_createtable(L, 0, 2);
    lua_pushcfunction(L, reflectAllocator, "allocator");
    lua_setfield(L, -2, "allocator");
    lua_pushcfunction(L, reflectFilter, "filter");
    lua_setfield(L, -2, "filter");

    return 1;
}

} // namespace Luau

