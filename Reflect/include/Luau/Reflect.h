// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lua.h"
#include "lualib.h"

#if LUA_USE_LONGJMP
#error "Reflect cannot be used in longjmp mode"
#endif

namespace Luau
{

int luaopen_reflect(lua_State* L);

} // namespace Luau
