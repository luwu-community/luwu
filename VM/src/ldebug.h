// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lstate.h"

#define pcRel(pc, p) ((pc) && (pc) != (p)->code ? cast_to(int, (pc) - (p)->code) - 1 : 0)

#define luaG_typeerror(L, o, opname) luaG_typeerrorL(L, o, opname)
#define luaG_forerror(L, o, what) luaG_forerrorL(L, o, what)
#define luaG_runerror(L, fmt, ...) luaG_runerrorL(L, fmt, ##__VA_ARGS__)

#define LUA_MEMERRMSG "not enough memory"

// Luwu structured runtime errors: which code a raised error's position names
enum LuwuErrorAt
{
    // the running function, when it's Luwu code, as luaG_runerror does
    LUWU_ERRORAT_RUNNING,
    // the nearest Luwu code, for a C function that raises on behalf of the code that called it
    LUWU_ERRORAT_NEARESTLUA,
    // the caller, when it's Luwu code, as luaL_error does (luaL_where(L, 1))
    LUWU_ERRORAT_CALLER,
};
#define LUA_ERRERRMSG "error in error handling"

LUAI_FUNC l_noret luaG_typeerrorL(lua_State* L, const TValue* o, const char* opname);
LUAI_FUNC l_noret luaG_forerrorL(lua_State* L, const TValue* o, const char* what);
LUAI_FUNC l_noret luaG_concaterror(lua_State* L, StkId p1, StkId p2);
LUAI_FUNC l_noret luaG_aritherror(lua_State* L, const TValue* p1, const TValue* p2, TMS op);
LUAI_FUNC l_noret luaG_ordererror(lua_State* L, const TValue* p1, const TValue* p2, TMS op);
LUAI_FUNC l_noret luaG_indexerror(lua_State* L, const TValue* p1, const TValue* p2);
LUAI_FUNC l_noret luaG_indexerroratlua(lua_State* L, const TValue* p1, const TValue* p2);
// Luwu Classes (rfcs/classes): raises `msg` at the nearest Lua frame, for an error a C function raises on behalf of the
// Lua code that called it
LUAI_FUNC l_noret luaG_runerroratlua(lua_State* L, LuwuRuntimeKind kind, const char* msg);
LUAI_FUNC l_noret luaG_methoderror(lua_State* L, const TValue* p1, const TString* p2);
LUAI_FUNC l_noret luaG_missingmembererror(lua_State* L, const TValue* p1, const TValue* p2);
// Luwu Classes (rfcs/classes): luaG_missingmembererror for a missing `key` of `classdef`, read on one of its objects
// when `onobject` is set and on the class otherwise
LUAI_FUNC l_noret luaG_classmembererror(lua_State* L, const LuauClass* classdef, bool onobject, const TString* key);
LUAI_FUNC l_noret luaG_instancefieldonclasserror(lua_State* L, const TValue* p1, const TValue* p2);
LUAI_FUNC l_noret luaG_positionalerror(lua_State* L, const LuauClass* classdef, int position);
// Luwu structured runtime errors: `target` is the readonly table, when the raise site has it
LUAI_FUNC l_noret luaG_readonlyerror(lua_State* L, const TValue* target = NULL);
LUAI_FUNC l_noret luaG_privateaccesserror(lua_State* L, const TValue* p2, const TString* className);
LUAI_FUNC l_noret luaG_constassignerror(lua_State* L, const TValue* object, const TValue* key);
LUAI_FUNC l_noret luaG_blockedinitaccesserror(lua_State* L, const TString* className);
LUAI_FUNC l_noret luaG_constassignnotselferror(lua_State* L, const TValue* object, const TValue* key);
// Luwu structured runtime errors: the errors the VM and the base libraries raise as variants of `Error`
LUAI_FUNC l_noret luaG_invalidkeyerror(lua_State* L, const TValue* indexed, const TValue* key);
LUAI_FUNC l_noret luaG_stackoverflowerror(lua_State* L, bool cstack);
LUAI_FUNC l_noret luaG_notyieldableerror(lua_State* L);
LUAI_FUNC l_noret luaG_argumenterror(lua_State* L, int narg, const char* fname, const char* extramsg);
LUAI_FUNC l_noret luaG_unexpectedtypeerror(lua_State* L, int narg, const char* fname, const char* tname);
LUAI_FUNC l_noret luaG_assertionerror(lua_State* L, const TValue* message);
LUAI_FUNC l_noret luaG_privateconstructorerror(lua_State* L, const TString* className);
// Luwu Traits (rfcs/classes/traits.md): a class method (`function f(Self)`) called with something other than its class as `Self`
LUAI_FUNC l_noret luaG_classmethoderror(lua_State* L, const TValue* self, const LuauClass* expected, const TString* methodName);
LUAI_FUNC l_noret luaG_selfclasserror(lua_State* L, const TValue* self, const LuauClass* expected, const TString* methodName, bool selfCall);

LUAI_FUNC LUA_PRINTF_ATTR(2, 3) l_noret luaG_runerrorL(lua_State* L, const char* fmt, ...);

// Luwu structured runtime errors: raises an `Error.Runtime` of `kind` from a C function, positioned like luaL_error
// (luaL_where(L, 1)). `fmt` takes lua_pushvfstring's specifiers.
LUAI_FUNC l_noret luaG_runerrorcallerv(lua_State* L, LuwuRuntimeKind kind, const char* fmt, va_list args);
// Luwu structured runtime errors: raises error variant `variant` with the `count` values at `fields` as its positions and
// `subkind` (LuwuErrorRecord), at the position `at` picks
LUAI_FUNC l_noret luaG_raiseerror(lua_State* L, LuwuBuiltinClass variant, uint8_t subkind, LuwuErrorAt at, const TValue* fields, int count);
// Luwu structured runtime errors: whether `value` is the sentinel thrown for the recorded error
LUAI_FUNC bool luaG_iserrorrecord(lua_State* L, const TValue* value);
// Luwu structured runtime errors: replaces the recorded error's sentinel at `slot`, the top of the stack, with its
// message and clears the record. Returns LUA_ERRRUN, or the status of an error raised while formatting (LUA_ERRMEM or
// LUA_ERRERR), whose message then takes the slot.
LUAI_FUNC int luaG_replaceerrorrecord(lua_State* L, StkId slot);
LUAI_FUNC void luaG_clearerrorrecord(global_State* g);
// Luwu structured runtime errors: adds the calling Luwu code's position (luaL_where(L, 1)) in front of the recorded
// error's message, for coroutine.wrap raising a coroutine's error again
LUAI_FUNC void luaG_prefixerrorrecord(lua_State* L);
LUAI_FUNC void luaG_pusherror(lua_State* L, const char* error);

LUAI_FUNC void luaG_breakpoint(lua_State* L, Proto* p, int line, bool enable);
LUAI_FUNC bool luaG_onbreak(lua_State* L);

LUAI_FUNC int luaG_getline(Proto* p, int pc);
// Luwu try expressions: the trace lua_debugtrace writes, for the top `levels` frames only
LUAI_FUNC TString* luaG_traceback(lua_State* L, int levels);

LUAI_FUNC int luaG_isnative(lua_State* L, int level);
LUAI_FUNC int luaG_hasnative(lua_State* L, int level);
