// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once
#include "lua.h"

LUA_API const void* lua_getmetatablepointer(lua_State* L, int objindex);
LUA_API const char* lua_gcstatename(int state);
LUA_API int64_t lua_gcallocationrate(lua_State* L);
LUA_API void lua_gcdump(lua_State* L, void* file, const char* (*categoryName)(lua_State* L, uint8_t memcat));
LUA_API int luau_setfflag(const char* name, int value);
LUA_API int luau_getfflag(const char* name);

// Finds a userdata tag for which neither a metatable nor destructor is associated.
// Returns -1 if none were found.
LUA_API int lua_findunuseduserdatatag(lua_State* L);
// Finds a light userdata tag that has not been associated with a name.
// Returns -1 if none were found.
LUA_API int lua_findunusedlightuserdatatag(lua_State* L);
