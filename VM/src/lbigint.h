// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lua.h"
#include "lobject.h"

HeapInteger* luaZB_newheapinteger(lua_State* L, uint32_t capacity);

HeapInteger* luaZB_heapinteger_add(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luaZB_heapinteger_sub(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luaZB_heapinteger_mul(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luaZB_heapinteger_div(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luaZB_heapinteger_mod(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luaZB_heapinteger_rem(lua_State* L, const HeapInteger* a, const HeapInteger* b);

void luaZB_heapinteger_pushstring(lua_State* L, const HeapInteger* h);
HeapInteger* luaZB_heapinteger_fromstring(lua_State* L, const char* str, const char** endptr);
HeapInteger* luaZB_heapinteger_neg(lua_State* L, const HeapInteger* a);
int luaZB_heapinteger_cmp(const HeapInteger* a, const HeapInteger* b);
