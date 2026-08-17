// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lua.h"
#include "lobject.h"

HeapInteger* luau_newheapinteger(lua_State* L, uint32_t capacity);

HeapInteger* luau_heapint_add(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luau_heapint_sub(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luau_heapint_mul(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luau_heapint_div(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luau_heapint_mod(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luau_heapint_rem(lua_State* L, const HeapInteger* a, const HeapInteger* b);
HeapInteger* luau_heapint_neg(lua_State* L, const HeapInteger* a);
int luau_heapint_cmp(const HeapInteger* a, const HeapInteger* b);
