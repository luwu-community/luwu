// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <stddef.h>

// Luwu object notation: the C API for loading `.luwon` data files.

struct lua_State;

#ifdef __cplusplus
extern "C"
{
#endif

// Pushes the value of a data file's source, evaluated without compiling or running anything. Returns 0 on success.
// On failure, pushes an error message prefixed with the chunk's name and line ("settings.luwon:3: ...") and returns 1.
// Like `luau_load`, `chunkname` may start with '@' or '='.
int luwon_load(struct lua_State* L, const char* chunkname, const char* source, size_t size);

// Opens the `luwon` library as a global: `luwon.decode(text, classes?)` reads a data file's value, whose tables carry
// their comments and locations, and `luwon.encode(value, classes?)` writes one. Call it after luaL_openlibs and before
// luaL_sandbox, like any other standard library.
int luaopen_luwon(struct lua_State* L);

#ifdef __cplusplus
}
#endif
